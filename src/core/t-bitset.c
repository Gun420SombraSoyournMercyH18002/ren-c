//
//  File: %t-bitset.c
//  Summary: "bitset datatype"
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


//
//  CT_Bitset: C
//
// !!! Bitset comparison including the NOT is somewhat nebulous.  If you have
// a bitset of 8 bits length as 11111111, is it equal to the negation of
// a bitset of 8 bits length of 00000000 or not?  For the moment, this does
// not attempt to answer any existential questions--as comparisons in R3-Alpha
// need significant review.
//
REBINT CT_Bitset(noquote(Cell(const*)) a, noquote(Cell(const*)) b, bool strict)
{
    DECLARE_LOCAL (atemp);
    DECLARE_LOCAL (btemp);
    Init_Binary(atemp, VAL_BITSET(a));
    Init_Binary(btemp, VAL_BITSET(b));

    if (BITS_NOT(VAL_BITSET(a)) != BITS_NOT(VAL_BITSET(b)))
        return 1;

    return CT_Binary(atemp, btemp, strict);
}


//
//  Make_Bitset: C
//
Binary(*) Make_Bitset(REBLEN num_bits)
{
    REBLEN num_bytes = (num_bits + 7) / 8;
    Binary(*) bin = Make_Binary(num_bytes);
    Clear_Series(bin);
    TERM_BIN_LEN(bin, num_bytes);
    INIT_BITS_NOT(bin, false);
    return bin;
}


//
//  MF_Bitset: C
//
void MF_Bitset(REB_MOLD *mo, noquote(Cell(const*)) v, bool form)
{
    UNUSED(form); // all bitsets are "molded" at this time

    Pre_Mold(mo, v); // #[bitset! or make bitset!

    Binary(const*) s = VAL_BITSET(v);

    if (BITS_NOT(s))
        Append_Ascii(mo->series, "[not bits ");

    DECLARE_LOCAL (binary);
    Init_Binary(binary, s);
    MF_Binary(mo, binary, false); // false = mold, don't form

    if (BITS_NOT(s))
        Append_Codepoint(mo->series, ']');

    End_Mold(mo);
}


//
//  MAKE_Bitset: C
//
Bounce MAKE_Bitset(
    Frame(*) frame_,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_BITSET);
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

    REBINT len = Find_Max_Bit(arg);
    if (len == NOT_FOUND)
        return RAISE(arg);

    Binary(*) bin = Make_Bitset(cast(REBLEN, len));
    Manage_Series(bin);
    Init_Bitset(OUT, bin);

    if (IS_INTEGER(arg))
        return OUT; // allocated at a size, no contents.

    if (IS_BINARY(arg)) {
        Size size;
        const Byte* at = VAL_BINARY_SIZE_AT(&size, arg);
        memcpy(BIN_HEAD(bin), at, (size / 8) + 1);
        return OUT;
    }

    Set_Bits(bin, arg, true);
    return OUT;
}


//
//  TO_Bitset: C
//
Bounce TO_Bitset(Frame(*) frame_, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Bitset(frame_, kind, nullptr, arg);
}


//
//  Find_Max_Bit: C
//
// Return integer number for the maximum bit number defined by
// the value. Used to determine how much space to allocate.
//
REBINT Find_Max_Bit(Cell(const*) val)
{
    REBLEN maxi = 0;

    switch (VAL_TYPE(val)) {

    case REB_INTEGER:
        maxi = Int32s(val, 0);
        break;

    case REB_TEXT:
    case REB_FILE:
    case REB_EMAIL:
    case REB_URL:
    case REB_ISSUE:
    case REB_TAG: {
        REBLEN len;
        Utf8(const*) up = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, val);
        for (; len > 0; --len) {
            Codepoint c;
            up = NEXT_CHR(&c, up);
            if (c > maxi)
                maxi = cast(REBINT, c);
        }
        maxi++;
        break; }

    case REB_BINARY:
        if (VAL_LEN_AT(val) != 0)
            maxi = VAL_LEN_AT(val) * 8 - 1;
        break;

    case REB_BLOCK: {
        Cell(const*) tail;
        Cell(const*) item = VAL_ARRAY_AT(&tail, val);
        for (; item != tail; ++item) {
            REBINT n = Find_Max_Bit(item);
            if (n != NOT_FOUND and cast(REBLEN, n) > maxi)
                maxi = cast(REBLEN, n);
        }
        //maxi++;
        break; }

    case REB_BLANK:
        maxi = 0;
        break;

    default:
        return NOT_FOUND;
    }

    return maxi;
}


//
//  Check_Bit: C
//
// Check bit indicated. Returns true if set.
// If uncased is true, try to match either upper or lower case.
//
bool Check_Bit(Binary(const*) bset, REBLEN c, bool uncased)
{
    REBLEN i, n = c;
    REBLEN tail = BIN_LEN(bset);
    bool flag = false;

    if (uncased) {
        if (n >= UNICODE_CASES)
            uncased = false; // no need to check
        else
            n = LO_CASE(c);
    }

    // Check lowercase char:
retry:
    i = n >> 3;
    if (i < tail)
        flag = did (BIN_HEAD(bset)[i] & (1 << (7 - (n & 7))));

    // Check uppercase if needed:
    if (uncased && !flag) {
        n = UP_CASE(c);
        uncased = false;
        goto retry;
    }

    if (BITS_NOT(bset))
        return not flag;

    return flag;
}


//
//  Set_Bit: C
//
// Set/clear a single bit. Expand if needed.
//
void Set_Bit(Binary(*) bset, REBLEN n, bool set)
{
    REBLEN i = n >> 3;
    REBLEN tail = BIN_LEN(bset);
    Byte bit;

    // Expand if not enough room:
    if (i >= tail) {
        if (!set) return; // no need to expand
        Expand_Series(bset, tail, (i - tail) + 1);
        memset(BIN_AT(bset, tail), 0, (i - tail) + 1);
        TERM_SERIES_IF_NECESSARY(bset);
    }

    bit = 1 << (7 - ((n) & 7));
    if (set)
        BIN_HEAD(bset)[i] |= bit;
    else
        BIN_HEAD(bset)[i] &= ~bit;
}


//
//  Set_Bits: C
//
// Set/clear bits indicated by strings and chars and ranges.
//
bool Set_Bits(Binary(*) bset, Cell(const*) val, bool set)
{
    if (IS_INTEGER(val)) {
        REBLEN n = Int32s(val, 0);
        if (n > MAX_BITSET)
            return false;
        Set_Bit(bset, n, set);
        return true;
    }

    if (IS_BINARY(val)) {
        REBLEN i = VAL_INDEX(val);

        const Byte* bp = BIN_HEAD(VAL_BINARY(val));
        for (; i != VAL_LEN_HEAD(val); i++)
            Set_Bit(bset, bp[i], set);

        return true;
    }

    if (IS_ISSUE(val) or ANY_STRING(val)) {
        REBLEN len;
        Utf8(const*) up = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, val);
        for (; len > 0; --len) {
            Codepoint c;
            up = NEXT_CHR(&c, up);
            Set_Bit(bset, c, set);
        }

        return true;
    }

    if (!ANY_ARRAY(val))
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    Cell(const*) tail;
    Cell(const*) item = VAL_ARRAY_AT(&tail, val);

    if (
        item != tail
        && IS_WORD(item)
        && VAL_WORD_ID(item) == SYM_NOT_1  // see TO-C-NAME
    ){
        INIT_BITS_NOT(bset, true);
        item++;
    }

    // Loop through block of bit specs:

    for (; item != tail; item++) {

        switch (VAL_TYPE(item)) {
        case REB_ISSUE: {
            if (not IS_CHAR(item)) {  // no special handling for hyphen
                Set_Bits(bset, SPECIFIC(item), set);
                break;
            }
            Codepoint c = VAL_CHAR(item);
            if (
                item + 1 != tail
                && IS_WORD(item + 1)
                && VAL_WORD_SYMBOL(item + 1) == Canon(HYPHEN_1)
            ){
                item += 2;
                if (IS_CHAR(item)) {
                    REBLEN n = VAL_CHAR(item);
                    if (n < c)
                        fail (Error_Index_Out_Of_Range_Raw());
                    do {
                        Set_Bit(bset, c, set);
                    } while (c++ < n); // post-increment: test before overflow
                }
                else
                    fail (Error_Bad_Value(item));
            }
            else
                Set_Bit(bset, c, set);
            break; }

        case REB_INTEGER: {
            REBLEN n = Int32s(SPECIFIC(item), 0);
            if (n > MAX_BITSET)
                return false;
            if (
                item + 1 != tail
                && IS_WORD(item + 1)
                && VAL_WORD_SYMBOL(item + 1) == Canon(HYPHEN_1)
            ){
                Codepoint c = n;
                item += 2;
                if (IS_INTEGER(item)) {
                    n = Int32s(SPECIFIC(item), 0);
                    if (n < c)
                        fail (Error_Index_Out_Of_Range_Raw());
                    for (; c <= n; c++)
                        Set_Bit(bset, c, set);
                }
                else
                    fail (Error_Bad_Value(item));
            }
            else
                Set_Bit(bset, n, set);
            break; }

        case REB_BINARY:
        case REB_TEXT:
        case REB_FILE:
        case REB_EMAIL:
        case REB_URL:
        case REB_TAG:
            Set_Bits(bset, SPECIFIC(item), set);
            break;

        case REB_WORD: {
            // Special: BITS #{000...}
            if (not IS_WORD(item) or VAL_WORD_ID(item) != SYM_BITS)
                return false;
            item++;
            if (not IS_BINARY(item))
                return false;

            Size n;
            const Byte* at = VAL_BINARY_SIZE_AT(&n, item);

            Codepoint c = BIN_LEN(bset);
            if (n >= c) {
                Expand_Series(bset, c, (n - c));
                memset(BIN_AT(bset, c), 0, (n - c));
            }
            memcpy(BIN_HEAD(bset), at, n);
            break; }

        default:
            return false;
        }
    }

    return true;
}


//
//  Check_Bits: C
//
// Check bits indicated by strings and chars and ranges.
// If uncased is true, try to match either upper or lower case.
//
bool Check_Bits(Binary(const*) bset, Cell(const*) val, bool uncased)
{
    if (IS_CHAR(val))
        return Check_Bit(bset, VAL_CHAR(val), uncased);

    if (IS_INTEGER(val))
        return Check_Bit(bset, Int32s(val, 0), uncased);

    if (IS_BINARY(val)) {
        REBLEN i = VAL_INDEX(val);
        const Byte* bp = BIN_HEAD(VAL_BINARY(val));
        for (; i != VAL_LEN_HEAD(val); ++i)
            if (Check_Bit(bset, bp[i], uncased))
                return true;
        return false;
    }

    if (ANY_STRING(val)) {
        REBLEN len;
        Utf8(const*) up = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, val);
        for (; len > 0; --len) {
            Codepoint c;
            up = NEXT_CHR(&c, up);
            if (Check_Bit(bset, c, uncased))
                return true;
        }

        return false;
    }

    if (!ANY_ARRAY(val))
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    // Loop through block of bit specs

    Cell(const*) tail;
    Cell(const*) item = VAL_ARRAY_AT(&tail, val);
    for (; item != tail; item++) {

        switch (VAL_TYPE(item)) {

        case REB_ISSUE: {
            if (not IS_CHAR(item)) {
                if (Check_Bits(bset, SPECIFIC(item), uncased))
                    return true;
            }
            Codepoint c = VAL_CHAR(item);
            if (
                IS_WORD(item + 1)
                && VAL_WORD_SYMBOL(item + 1) == Canon(HYPHEN_1)
            ){
                item += 2;
                if (IS_CHAR(item)) {
                    REBLEN n = VAL_CHAR(item);
                    if (n < c)
                        fail (Error_Index_Out_Of_Range_Raw());
                    for (; c <= n; c++)
                        if (Check_Bit(bset, c, uncased))
                            return true;
                }
                else
                    fail (Error_Bad_Value(item));
            }
            else
                if (Check_Bit(bset, c, uncased))
                    return true;
            break; }

        case REB_INTEGER: {
            REBLEN n = Int32s(SPECIFIC(item), 0);
            if (n > 0xffff)
                return false;
            if (
                IS_WORD(item + 1)
                && VAL_WORD_SYMBOL(item + 1) == Canon(HYPHEN_1)
            ){
                Codepoint c = n;
                item += 2;
                if (IS_INTEGER(item)) {
                    n = Int32s(SPECIFIC(item), 0);
                    if (n < c)
                        fail (Error_Index_Out_Of_Range_Raw());
                    for (; c <= n; c++)
                        if (Check_Bit(bset, c, uncased))
                            return true;
                }
                else
                    fail (Error_Bad_Value(item));
            }
            else
                if (Check_Bit(bset, n, uncased))
                    return true;
            break; }

        case REB_BINARY:
        case REB_TEXT:
        case REB_FILE:
        case REB_EMAIL:
        case REB_URL:
        case REB_TAG:
//      case REB_ISSUE:
            if (Check_Bits(bset, SPECIFIC(item), uncased))
                return true;
            break;

        default:
            fail (Error_Invalid_Type(VAL_TYPE(item)));
        }
    }
    return false;
}


//
//  Trim_Tail_Zeros: C
//
// Remove extra zero bytes from end of byte string.
//
void Trim_Tail_Zeros(Binary(*) ser)
{
    REBLEN len = BIN_LEN(ser);
    Byte* bp = BIN_HEAD(ser);

    while (len > 0 && bp[len] == 0)
        len--;

    if (bp[len] != 0)
        len++;

    SET_SERIES_LEN(ser, len);
}


//
//  REBTYPE: C
//
REBTYPE(Bitset)
{
    REBVAL *v = D_ARG(1);

    option(SymId) sym = ID_OF_SYMBOL(verb);
    switch (sym) {

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        Cell(const*) picker = ARG(picker);
        bool bit = Check_Bits(VAL_BITSET(v), picker, false);

        return bit ? Init_True(OUT) : Init_Nulled(OUT); }

    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE_P: {
        INCLUDE_PARAMS_OF_POKE_P;
        UNUSED(ARG(location));

        Cell(const*) picker = ARG(picker);

        REBVAL *setval = ARG(value);

        Binary(*) bset = BIN(VAL_BITSET_ENSURE_MUTABLE(v));
        if (not Set_Bits(
            bset,
            picker,
            BITS_NOT(bset) ? Is_Falsey(setval) : Is_Truthy(setval)
        )){
            fail (PARAM(picker));
        }
        return nullptr; }


      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // covered by `v`

        option(SymId) property = VAL_WORD_ID(ARG(property));
        switch (property) {
          case SYM_LENGTH:
            return Init_Integer(v, BIN_LEN(VAL_BITSET(v)) * 8);

          case SYM_TAIL_Q:
            // Necessary to make EMPTY? work:
            return Init_Logic(OUT, BIN_LEN(VAL_BITSET(v)) == 0);

          default:
            break;
        }

        break; }

    // Add AND, OR, XOR

      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_SELECT;
        if (Is_Isotope(ARG(value)))
            fail (ARG(value));

        UNUSED(PARAM(series));  // covered by `v`
        UNUSED(PARAM(tail));  // no feature for tail output

        if (REF(part) or REF(skip) or REF(match))
            fail (Error_Bad_Refines_Raw());

        if (not Check_Bits(VAL_BITSET(v), ARG(value), REF(case)))
            return nullptr;
        return Init_True(OUT); }

      case SYM_COMPLEMENT: {
        Binary(*) copy = BIN(Copy_Series_Core(VAL_BITSET(v), NODE_FLAG_MANAGED));
        INIT_BITS_NOT(copy, not BITS_NOT(VAL_BITSET(v)));
        return Init_Bitset(OUT, copy); }

      case SYM_APPEND:  // Accepts: #"a" "abc" [1 - 10] [#"a" - #"z"] etc.
      case SYM_INSERT: {
        REBVAL *arg = D_ARG(2);
        if (Is_Void(arg))
            return COPY(v);  // don't fail on read only if it would be a no-op

        if (Is_Isotope(arg))
            fail (arg);

        Binary(*) bin = VAL_BITSET_ENSURE_MUTABLE(v);

        bool diff;
        if (BITS_NOT(VAL_BITSET(v)))
            diff = false;
        else
            diff = true;

        if (not Set_Bits(bin, arg, diff))
            fail (arg);
        return COPY(v); }

      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;
        UNUSED(PARAM(series));  // covered by `v`

        Binary(*) bin = VAL_BITSET_ENSURE_MUTABLE(v);

        if (not REF(part))
            fail (Error_Missing_Arg_Raw());

        if (not Set_Bits(bin, ARG(part), false))
            fail (PARAM(part));

        return COPY(v); }

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PARAM(value));

        if (REF(part) or REF(deep))
            fail (Error_Bad_Refines_Raw());

        Binary(*) copy = BIN(Copy_Series_Core(VAL_BITSET(v), NODE_FLAG_MANAGED));
        INIT_BITS_NOT(copy, BITS_NOT(VAL_BITSET(v)));
        return Init_Bitset(OUT, copy); }

      case SYM_CLEAR: {
        Binary(*) bin = VAL_BITSET_ENSURE_MUTABLE(v);
        INIT_BITS_NOT(bin, false);
        Clear_Series(bin);
        return COPY(v); }

      case SYM_INTERSECT:
      case SYM_UNION:
      case SYM_DIFFERENCE:
      case SYM_EXCLUDE: {
        REBVAL *arg = D_ARG(2);
        if (IS_BITSET(arg)) {
            if (BITS_NOT(VAL_BITSET(arg))) {  // !!! see #2365
                fail ("Bitset negation not handled by set operations");
            }
            Binary(const*) bin = VAL_BITSET(arg);
            Init_Binary(arg, bin);
        }
        else if (not IS_BINARY(arg))
            fail (Error_Math_Args(VAL_TYPE(arg), verb));

        bool negated_result = false;

        if (BITS_NOT(VAL_BITSET(v))) {  // !!! see #2365
            //
            // !!! Narrowly handle the case of exclusion from a negated bitset
            // as simply unioning, because %pdf-maker.r uses this.  General
            // answer is on the Roaring Bitsets branch--this R3 stuff is junk.
            //
            if (sym == SYM_EXCLUDE) {
                negated_result = true;
                sym = SYM_UNION;
            }
            else
                fail ("Bitset negation not handled by (most) set operations");
        }

        Binary(const*) bin = VAL_BITSET(v);
        Init_Binary(v, bin);

        // !!! Until the replacement implementation with Roaring Bitmaps, the
        // bitset is based on a BINARY!.  Reuse the code on the generated
        // proxy values.
        //
        REBVAL *action;
        switch (sym) {
          case SYM_INTERSECT:
            action = rebValue("unrun :bitwise-and");
            break;

          case SYM_UNION:
            action = rebValue("unrun :bitwise-or");
            break;

          case SYM_DIFFERENCE:
            action = rebValue("unrun :bitwise-xor");
            break;

          case SYM_EXCLUDE:
            action = rebValue("unrun :bitwise-and-not");
            break;

          default:
            panic (nullptr);
        }

        REBVAL *processed = rebValue(rebR(action), rebQ(v), rebQ(arg));

        Binary(*) bits = VAL_BINARY_KNOWN_MUTABLE(processed);
        rebRelease(processed);

        INIT_BITS_NOT(bits, negated_result);
        Trim_Tail_Zeros(bits);
        return Init_Bitset(OUT, bits); }

      default:
        break;
    }

    fail (UNHANDLED);
}
