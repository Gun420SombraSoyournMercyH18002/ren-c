//
//  File: %n-io.c
//  Summary: "native functions for input and output"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  form: native [
//
//  "Converts a value to a human-readable string."
//
//      return: "Returns null if input is void"
//          [<opt> text!]
//      value "The value to form (currently errors on isotopes)"
//          [<maybe> element?]
//  ]
//
DECLARE_NATIVE(form)
{
    INCLUDE_PARAMS_OF_FORM;

    REBVAL *v = ARG(value);

    return Init_Text(OUT, Copy_Form_Value(v, 0));
}


//
//  mold: native [
//
//  "Converts value to a REBOL-readable string"
//
//      return: "Returns null if input is void"
//          [<opt> text!]
//      @truncated "Whether the mold was truncated"
//          [logic!]
//      value [<maybe> element?]
//      /only "For a block value, mold only its contents, no outer []"
//      /all "Use construction syntax"
//      /flat "No indentation"
//      /limit "Limit to a certain length"
//          [integer!]
//  ]
//
DECLARE_NATIVE(mold)
{
    INCLUDE_PARAMS_OF_MOLD;

    REBVAL *v = ARG(value);

    DECLARE_MOLD (mo);
    if (REF(all))
        SET_MOLD_FLAG(mo, MOLD_FLAG_ALL);
    if (REF(flat))
        SET_MOLD_FLAG(mo, MOLD_FLAG_INDENT);
    if (REF(limit)) {
        SET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT);
        mo->limit = Int32(ARG(limit));
    }

    Push_Mold(mo);

    if (REF(only) and IS_BLOCK(v))
        SET_MOLD_FLAG(mo, MOLD_FLAG_ONLY);

    Mold_Value(mo, v);

    String(*) popped = Pop_Molded_String(mo);  // sets MOLD_FLAG_TRUNCATED

    Init_Logic(ARG(truncated), did (mo->opts & MOLD_FLAG_WAS_TRUNCATED));

    Init_Text(OUT, popped);
    return Proxy_Multi_Returns(frame_);
}


//
//  write-stdout: native [
//
//  "Boot-only implementation of WRITE-STDOUT (HIJACK'd by STDIO module)"
//
//      return: <none>
//      value [<maybe> text! char! binary!]
//          "Text to write, if a STRING! or CHAR! is converted to OS format"
//  ]
//
DECLARE_NATIVE(write_stdout)
//
// This code isn't supposed to run during normal bootup.  But for debugging
// we don't want a parallel set of PRINT operations and specializations just
// on the off chance something goes wrong in boot.  So this stub is present
// to do debug I/O.
{
    INCLUDE_PARAMS_OF_WRITE_STDOUT;

    REBVAL *v = ARG(value);

  #if defined(NDEBUG)
    UNUSED(v);
    fail ("Boot WRITE-STDOUT needs to be a debug build or loaded I/O module");
  #else
    if (IS_TEXT(v)) {
        printf("WRITE-STDOUT: %s\n", cast(const char*, STR_HEAD(VAL_STRING(v))));
        fflush(stdout);
    }
    else if (IS_CHAR(v)) {
        printf("WRITE-STDOUT: char %lu\n", cast(unsigned long, VAL_CHAR(v)));
    }
    else {
        assert(IS_BINARY(v));
      #if DEBUG_HAS_PROBE
        PROBE(v);
      #else
        fail ("Boot WRITE-STDOUT received BINARY!, needs DEBUG_HAS_PROBE");
      #endif
    }
    return NONE;
  #endif
}


//
//  new-line: native [
//
//  {Sets or clears the new-line marker within a block or group.}
//
//      return: [block!]
//      position "Position to change marker (modified)"
//          [block! group!]
//      mark "Set TRUE for newline"
//          [logic!]
//      /all "Set/clear marker to end of series"
//      /skip "Set/clear marker periodically to the end of the series"
//          [integer!]
//  ]
//
DECLARE_NATIVE(new_line)
{
    INCLUDE_PARAMS_OF_NEW_LINE;

    bool mark = VAL_LOGIC(ARG(mark));

    REBVAL *pos = ARG(position);
    Cell(const*) tail;
    Cell(*) item = VAL_ARRAY_AT_ENSURE_MUTABLE(&tail, pos);
    Array(*) a = VAL_ARRAY_KNOWN_MUTABLE(pos);  // need if setting flag at tail

    REBINT skip;
    if (REF(all))
        skip = 1;
    else if (REF(skip)) {
        skip = Int32s(ARG(skip), 1);
        if (skip < 1)
            skip = 1;
    }
    else
        skip = 0;

    REBLEN n;
    for (n = 0; true; ++n, ++item) {
        if (item == tail) {  // no cell at tail; use flag on array
            if (mark)
                Set_Subclass_Flag(ARRAY, a, NEWLINE_AT_TAIL);
            else
                Clear_Subclass_Flag(ARRAY, a, NEWLINE_AT_TAIL);
            break;
        }

        if (skip != 0 and (n % skip != 0))
            continue;

        if (mark)
            Set_Cell_Flag(item, NEWLINE_BEFORE);
        else
            Clear_Cell_Flag(item, NEWLINE_BEFORE);

        if (skip == 0)
            break;
    }

    return COPY(pos);
}


//
//  new-line?: native [
//
//  {Returns the state of the new-line marker within a block or group.}
//
//      return: [logic?]
//      position "Position to check marker"
//          [block! group! varargs!]
//  ]
//
DECLARE_NATIVE(new_line_q)
{
    INCLUDE_PARAMS_OF_NEW_LINE_Q;

    REBVAL *pos = ARG(position);

    Array(const*) arr;
    Cell(const*) item;
    Cell(const*) tail;

    if (IS_VARARGS(pos)) {
        Frame(*) f;
        REBVAL *shared;
        if (Is_Frame_Style_Varargs_May_Fail(&f, pos)) {
            if (FRM_IS_VARIADIC(f)) {
                //
                // C va_args input to frame, as from the API, but not in the
                // process of using string components which *might* have
                // newlines.  Review edge cases, like:
                //
                //    REBVAL *new_line_q = rebValue(":new-line?");
                //    bool case_one = rebUnboxLogic("new-line?", "[\n]");
                //    bool case_two = rebUnboxLogic(new_line_q, "[\n]");
                //
                return Init_Logic(OUT, false);
            }

            arr = f_array;
            if (Is_Frame_At_End(f)) {
                item = nullptr;
                tail = nullptr;
            }
            else {
                item = At_Feed(f->feed);
                tail = At_Feed(f->feed) + 1;  // !!! Review
            }
        }
        else if (Is_Block_Style_Varargs(&shared, pos)) {
            arr = VAL_ARRAY(shared);
            item = VAL_ARRAY_AT(&tail, shared);
        }
        else
            panic ("Bad VARARGS!");
    }
    else {
        assert(IS_GROUP(pos) or IS_BLOCK(pos));
        arr = VAL_ARRAY(pos);
        item = VAL_ARRAY_AT(&tail, pos);
    }

    if (item != tail)
        return Init_Logic(OUT, Get_Cell_Flag(item, NEWLINE_BEFORE));

    return Init_Logic(OUT, Get_Subclass_Flag(ARRAY, arr, NEWLINE_AT_TAIL));
}


//
//  Milliseconds_From_Value: C
//
// Note that this routine is used by the SLEEP extension, as well as by WAIT.
//
REBLEN Milliseconds_From_Value(Cell(const*) v) {
    REBINT msec;

    switch (VAL_TYPE(v)) {
    case REB_INTEGER:
        msec = 1000 * Int32(v);
        break;

    case REB_DECIMAL:
        msec = cast(REBINT, 1000 * VAL_DECIMAL(v));
        break;

    case REB_TIME:
        msec = cast(REBINT, VAL_NANO(v) / (SEC_SEC / 1000));
        break;

    default:
        panic (NULL); // avoid uninitialized msec warning
    }

    if (msec < 0)
        fail (Error_Out_Of_Range(v));

    return cast(REBLEN, msec);
}
