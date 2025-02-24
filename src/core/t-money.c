//
//  File: %t-money.c
//  Summary: "extended precision datatype"
//  Section: datatypes
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

#include "datatypes/sys-money.h"

//
//  Scan_Money: C
//
// Scan and convert money.  Return zero if error.
//
const Byte* Scan_Money(
    Cell(*) out,
    const Byte* cp,
    REBLEN len
){
    const Byte* end;

    if (*cp == '$') {
        ++cp;
        --len;
    }
    if (len == 0)
        return nullptr;

    Init_Money(out, string_to_deci(cp, &end));
    if (end != cp + len)
        return nullptr;

    return end;
}


//
//  CT_Money: C
//
REBINT CT_Money(noquote(Cell(const*)) a, noquote(Cell(const*)) b, bool strict)
{
    UNUSED(strict);

    bool e = deci_is_equal(VAL_MONEY_AMOUNT(a), VAL_MONEY_AMOUNT(b));
    if (e)
        return 0;

    bool g = deci_is_lesser_or_equal(
        VAL_MONEY_AMOUNT(b), VAL_MONEY_AMOUNT(a)
    );
    return g ? 1 : -1;
}


//
//  MAKE_Money: C
//
Bounce MAKE_Money(
    Frame(*) frame_,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_MONEY);
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

    if (IS_LOGIC(arg)) {
        return Init_Money(OUT, int_to_deci(VAL_LOGIC(arg) ? 1 : 0));
    }
    else switch (VAL_TYPE(arg)) {
      case REB_INTEGER:
        return Init_Money(OUT, int_to_deci(VAL_INT64(arg)));

      case REB_DECIMAL:
      case REB_PERCENT:
        return Init_Money(OUT, decimal_to_deci(VAL_DECIMAL(arg)));

      case REB_MONEY:
        return Copy_Cell(OUT, arg);

      case REB_TEXT: {
        const Byte* bp = Analyze_String_For_Scan(
            nullptr,
            arg,
            MAX_SCAN_MONEY
        );

        const Byte* end;
        Init_Money(OUT, string_to_deci(bp, &end));
        if (end == bp or *end != '\0')
            goto bad_make;
        return OUT; }

//      case REB_ISSUE:
      case REB_BINARY:
        Bin_To_Money_May_Fail(OUT, arg);
        return OUT;

      default:
        break;
    }

  bad_make:

    return RAISE(Error_Bad_Make(REB_MONEY, arg));
}


//
//  TO_Money: C
//
Bounce TO_Money(Frame(*) frame_, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Money(frame_, kind, nullptr, arg);
}


//
//  MF_Money: C
//
void MF_Money(REB_MOLD *mo, noquote(Cell(const*)) v, bool form)
{
    UNUSED(form);

    if (mo->opts & MOLD_FLAG_LIMIT) {
        // !!! In theory, emits should pay attention to the mold options,
        // at least the limit.
    }

    Byte buf[60];
    REBINT len = deci_to_string(buf, VAL_MONEY_AMOUNT(v), '$', '.');
    Append_Ascii_Len(mo->series, s_cast(buf), len);
}


//
//  Bin_To_Money_May_Fail: C
//
// Will successfully convert or fail (longjmp) with an error.
//
void Bin_To_Money_May_Fail(REBVAL *result, const REBVAL *val)
{
    if (not IS_BINARY(val))
        fail (val);

    Size size;
    const Byte* at = VAL_BINARY_SIZE_AT(&size, val);
    if (size > 12)
        size = 12;

    Byte buf[MAX_HEX_LEN+4] = {0}; // binary to convert
    memcpy(buf, at, size);
    memcpy(buf + 12 - size, buf, size); // shift to right side
    memset(buf, 0, 12 - size);
    Init_Money(result, binary_to_deci(buf));
}


static REBVAL *Math_Arg_For_Money(REBVAL *store, REBVAL *arg, Symbol(const*) verb)
{
    if (IS_MONEY(arg))
        return arg;

    if (IS_INTEGER(arg)) {
        Init_Money(store, int_to_deci(VAL_INT64(arg)));
        return store;
    }

    if (IS_DECIMAL(arg) or IS_PERCENT(arg)) {
        Init_Money(store, decimal_to_deci(VAL_DECIMAL(arg)));
        return store;
    }

    fail (Error_Math_Args(REB_MONEY, verb));
}


//
//  REBTYPE: C
//
REBTYPE(Money)
{
    REBVAL *v = D_ARG(1);

    switch (ID_OF_SYMBOL(verb)) {
      case SYM_ADD: {
        REBVAL *arg = Math_Arg_For_Money(SPARE, D_ARG(2), verb);
        return Init_Money(
            OUT,
            deci_add(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_SUBTRACT: {
        REBVAL *arg = Math_Arg_For_Money(SPARE, D_ARG(2), verb);
        return Init_Money(
            OUT,
            deci_subtract(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_MULTIPLY: {
        REBVAL *arg = Math_Arg_For_Money(SPARE, D_ARG(2), verb);
        return Init_Money(
            OUT,
            deci_multiply(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_DIVIDE: {
        REBVAL *arg = Math_Arg_For_Money(SPARE, D_ARG(2), verb);
        return Init_Money(
            OUT,
            deci_divide(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_REMAINDER: {
        REBVAL *arg = Math_Arg_For_Money(SPARE, D_ARG(2), verb);
        return Init_Money(
            OUT,
            deci_mod(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_NEGATE: // sign bit is the 32nd bit, highest one used
        PAYLOAD(Any, v).second.u ^= (cast(uintptr_t, 1) << 31);
        return COPY(v);

      case SYM_ABSOLUTE:
        PAYLOAD(Any, v).second.u &= ~(cast(uintptr_t, 1) << 31);
        return COPY(v);

      case SYM_ROUND: {
        INCLUDE_PARAMS_OF_ROUND;
        USED(ARG(value));  // aliased as v, others are passed via frame_
        USED(ARG(even)); USED(ARG(down)); USED(ARG(half_down));
        USED(ARG(floor)); USED(ARG(ceiling)); USED(ARG(half_ceiling));

        REBVAL *to = ARG(to);

        DECLARE_LOCAL (temp);
        if (REF(to)) {
            if (IS_INTEGER(to))
                Init_Money(temp, int_to_deci(VAL_INT64(to)));
            else if (IS_DECIMAL(to) or IS_PERCENT(to))
                Init_Money(temp, decimal_to_deci(VAL_DECIMAL(to)));
            else if (IS_MONEY(to))
                Copy_Cell(temp, to);
            else
                fail (PARAM(to));
        }
        else
            Init_Money(temp, int_to_deci(0));

        Init_Money(
            OUT,
            Round_Deci(VAL_MONEY_AMOUNT(v), frame_, VAL_MONEY_AMOUNT(temp))
        );

        if (REF(to)) {
            if (IS_DECIMAL(to) or IS_PERCENT(to)) {
                REBDEC dec = deci_to_decimal(VAL_MONEY_AMOUNT(OUT));
                Reset_Unquoted_Header_Untracked(
                    TRACK(OUT),
                    FLAG_HEART_BYTE(VAL_TYPE(to)) | CELL_MASK_NO_NODES
                );
                VAL_DECIMAL(OUT) = dec;
                return OUT;
            }
            if (IS_INTEGER(to)) {
                REBI64 i64 = deci_to_int(VAL_MONEY_AMOUNT(OUT));
                return Init_Integer(OUT, i64);
            }
        }
        mutable_HEART_BYTE(OUT) = REB_MONEY;
        return OUT; }

      case SYM_EVEN_Q:
      case SYM_ODD_Q: {
        REBINT result = 1 & cast(REBINT, deci_to_int(VAL_MONEY_AMOUNT(v)));
        if (ID_OF_SYMBOL(verb) == SYM_EVEN_Q)
            result = not result;
        return Init_Logic(OUT, result != 0); }

      case SYM_COPY:
        return COPY(v);

      default:
        break;
    }

    fail (UNHANDLED);
}
