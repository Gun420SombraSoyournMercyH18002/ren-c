//
//  File: %sys-logic.h
//  Summary: "LOGIC! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// LOGIC! is a simple boolean value type which can be either true or false.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * A good source notation for logic literals was never chosen, so #[true]
//   and #[false] have been used.  Rebol2, Red, and R3-Alpha accept this
//   notation...but render them ambiguously as the words `true` and `false`.
//

inline static REBVAL *Init_Logic_Core(Cell(*) out, bool flag) {
    Reset_Cell_Header_Untracked(out, CELL_MASK_LOGIC);
    PAYLOAD(Logic, out).flag = flag;
  #ifdef ZERO_UNUSED_CELL_FIELDS
    EXTRA(Any, out).trash = ZEROTRASH;
  #endif
    return cast(REBVAL*, out);
}

#define Init_Logic(out,flag) \
    Init_Logic_Core(TRACK(out), (flag))

#define Init_True(out)      Init_Logic((out), true)
#define Init_False(out)     Init_Logic((out), false)

inline static bool VAL_LOGIC(noquote(Cell(const*)) v) {
    assert(CELL_HEART(v) == REB_LOGIC);
    return PAYLOAD(Logic, v).flag;
}


//=//// "TRUTHINESS" AND "FALSEYNESS" //////////////////////////////////////=//
//
// Like most languages, more things are "truthy" than logic #[true] and more
// things are "falsey" than logic #[false].  NULLs and BLANK!s are also falsey,
// and most other values are considered truthy.  Any value type is truthy when
// quoted, and BAD-WORD!s are also truthy; specifically for patterns like this:
//
//     for-both: func ['var blk1 blk2 body] [
//         return unmeta all [
//             meta for-each :var blk1 body  ; isotope results become BAD-WORD!
//             meta for-each :var blk2 body  ; only NULL is falsey for BREAK
//         ]
//     ]
//
// Despite Rebol's C heritage, the INTEGER! 0 is purposefully not "falsey".

inline static bool Is_Truthy(Cell(const*) v) {
    assert(QUOTE_BYTE(v) != ISOTOPE_255);  // should never be passed isotopes!
    if (VAL_TYPE(v) > REB_LOGIC)
        return true;  // includes QUOTED! `if first ['_] [-- "this is truthy"]`
    if (IS_LOGIC(v))
        return VAL_LOGIC(v);
    assert(IS_BLANK(v) or Is_Nulled(v));
    return false;
}

#define Is_Falsey(v) \
    (not Is_Truthy(v))

// Although a BLOCK! value is true, some constructs are safer by not allowing
// literal blocks.  e.g. `if [x] [print "this is not safe"]`.  The evaluated
// bit can let these instances be distinguished.  Note that making *all*
// evaluations safe would be limiting, e.g. `foo: any [false-thing []]`...
// So ANY and ALL use Is_Truthy() directly
//
inline static bool Is_Conditional_True(const REBVAL *v) {
    if (Is_Falsey(v))
        return false;
    if (IS_BLOCK(v))
        if (Get_Cell_Flag(v, UNEVALUATED))
            fail (Error_Block_Conditional_Raw(v));  // !!! Unintended_Literal?
    return true;
}

#define Is_Conditional_False(v) \
    (not Is_Conditional_True(v))
