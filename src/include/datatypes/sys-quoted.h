//
//  File: %sys-quoted.h
//  Summary: {Definitions for QUOTED! Datatype}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2022 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Value cells reserve a byte in their header called the QUOTE_BYTE().  The
// most basic usage is that any value can be "quote" escaped.  The depth is
// the number of apostrophes, e.g. ''''X is a depth of 4.  The operator QUOTE
// can be used to add a quoting level to a value, UNQUOTE to remove one,
// and NOQUOTE to remove all quotes.
//
//     >> quote [a]
//     == '[a]
//
//     >> noquote first ['''''a]
//     == a
//
// But the QUOTE_BYTE() is used to encode other states as well: all datatypes
// (besides QUOTED! itself) have an "isotopic" form as well as a "quasi" form.
// The quasi form will evaluate to the isotopic form, and the isotopic form is
// expressly prohibited from being put in arrays, while also causing errors
// if accessed from plain word fetches.
//
//     >> nice: first [~foo~]
//     == ~foo~
//
//     >> nice
//     == ~foo~
//
//     >> mean: ~foo~
//     == ~foo~  ; isotope
//
//     >> mean
//     ** Script Error: mean is ~foo~ isotope (see ^(...) and GET/ANY)
//
// With the use of the `^xxx` family of types and the `^` operator, it is
// possible to leverage a form of quoting to transition isotopes to normal, and
// normal bad words to quoted:
//
//     >> ^nice
//     == '~foo~
//
//     >> ^mean
//     == ~foo~
//

inline static Count VAL_QUOTED_DEPTH(Cell(const*) v) {
    assert(IS_QUOTED(v));
    return (QUOTE_BYTE(v) - UNQUOTED_1) >> 1;
}

inline static Count VAL_NUM_QUOTES(Cell(const*) v) {
    assert(QUOTE_BYTE(v) != ISOTOPE_0);
    return (QUOTE_BYTE(v) - UNQUOTED_1) >> 1;
}

// Turns X into 'X, or '''[1 + 2] into '''''(1 + 2), etc.
//
inline static Cell(*) Quotify_Core(Cell(*) v, Count depth) {
    if (depth == 0)
        return v;

    if (VAL_NUM_QUOTES(v) + depth >  MAX_QUOTE_DEPTH)
        fail ("Quoting Depth of 126 Exceeded");

    mutable_QUOTE_BYTE(v) += (depth << 1);
    return v;
}

#if (! CPLUSPLUS_11)    // C++ can overload so return type matches input type
    #define Quotify Quotify_Core
#else
    inline static Value(*) Quotify(Value(*) v, Count depth)
        { return cast(REBVAL*, Quotify_Core(v, depth)); }

    inline static Cell(*) Quotify(Cell(*) v, Count depth)
        { return Quotify_Core(v, depth); }
#endif


// Turns 'X into X, or '''''[1 + 2] into '''(1 + 2), etc.
//
inline static Cell(*) Unquotify_Core(Cell(*) v, Count unquotes) {
    if (unquotes == 0) {
        assert(QUOTE_BYTE(v) != ISOTOPE_0);
        return v;
    }

    if (unquotes > VAL_NUM_QUOTES(v))
        fail ("Attempt to set quoting level of value to less than 0");

    mutable_QUOTE_BYTE(v) -= (unquotes << 1);
    return v;
}

#if (! CPLUSPLUS_11)  // C++ can overload so return type matches input type
    #define Unquotify Unquotify_Core
#else
    inline static Value(*) Unquotify(Value(*) v, Count depth)
        { return cast(REBVAL*, Unquotify_Core(v, depth)); }

    inline static Cell(*) Unquotify(Cell(*) v, Count depth)
        { return Unquotify_Core(v, depth); }
#endif


#define VAL_UNESCAPED(v) \
    x_cast(noquote(Cell(const*)), (v))


inline static Count Dequotify(Cell(*) v) {
    Count depth = VAL_NUM_QUOTES(v);
    if (mutable_QUOTE_BYTE(v) & NONQUASI_BIT)
        mutable_QUOTE_BYTE(v) = UNQUOTED_1;
    else
        mutable_QUOTE_BYTE(v) = QUASI_2;
    return depth;
}


//=//// ISOTOPES //////////////////////////////////////////////////////////=//

// Isotopes are foundational in covering edge cases in representation which
// plague Rebol2 and Red.  They enable shifting into a "non-literal" domain,
// where whatever "weird" condition the isotope was attempting to capture can
// be handled without worrying about conflating with more literal usages.
// A good example is addressing the splicing intent for blocks:
//
//     >> append [a b c] [d e]
//     == [a b c [d e]]
//
//     >> ~(d e)~
//     == ~(d e)~  ; isotope (this connotes a "splice")
//
//     >> append [a b c] ~(d e)~
//     == [a b c d e]
//
//     >> append [a b c] '~(d e)~
//     == [a b c ~(d e)~]
//
// As demonstrated, the reified QUASI! form and the "ghostly" isotopic form
// work in concert to solve the problem.
//
// * Besides the word isotopes of ~true~, ~false~ and ~null~, isotope forms
//   are neither true nor false...they must be decayed or handled in some other
//   way, for instance DID/DIDN'T or THEN/ELSE.
//
// * A special parameter convention must be used to receive isotopes.  Code
//   that isn't expecting such strange circumstances can error if they ever
//   happen, while more sensitive code can be adapted to cleanly handle the
//   intents that they care about.
//
// Unstable isotopes like packs (block isotopes), error isotopes, and object
// isotopes aren't just not allowed in blocks, they can't be in variables.
//

#define Is_Isotope(v) \
    (QUOTE_BYTE(v) == ISOTOPE_0)

inline static bool Is_Isotope_Unstable(Cell(const*) v) {
    assert(Is_Isotope(v));
    return (
        HEART_BYTE(v) == REB_BLOCK  // Is_Pack()
        or HEART_BYTE(v) == REB_ERROR  // Is_Raised()
        or HEART_BYTE(v) == REB_OBJECT  // Is_Lazy()
    );
}

#define Is_Isotope_Stable(v) \
    (not Is_Isotope_Unstable(v))

inline static bool Is_Stable(Cell(const*) v)
  { return not Is_Isotope(v) or Is_Isotope_Stable(v); }


//=//// QUASI! FORMS //////////////////////////////////////////////////////=//

// * QUASI! states are truthy.  There's a reason for this, because it
//   allows operations in the ^META domain to easily use functions like ALL
//   and ANY on the meta values.  (See the FOR-BOTH example.)

inline static Value(*) Unquasify(Value(*) v) {
    assert(QUOTE_BYTE(v) == QUASI_2);
    mutable_QUOTE_BYTE(v) = UNQUOTED_1;
    return v;
}

inline static Value(*) Quasify(Value(*) v) {
    assert(QUOTE_BYTE(v) == UNQUOTED_1);  // e.g. can't quote void
    mutable_QUOTE_BYTE(v) = QUASI_2;
    return v;
}

inline static Value(*) Quasify_Isotope(Value(*) v) {
    assert(Is_Isotope(v));
    mutable_QUOTE_BYTE(v) = QUASI_2;
    return v;
}

inline static Value(*) Reify(Value(*) v) {
    assert(not Is_Void(v));
    if (QUOTE_BYTE(v) == ISOTOPE_0)
        mutable_QUOTE_BYTE(v) = QUASI_2;
    return v;
}

inline static Value(*) Degrade(Value(*) v) {
    if (QUOTE_BYTE(v) == QUASI_2)
        mutable_QUOTE_BYTE(v) = ISOTOPE_0;
    return v;
}

inline static Value(*) Concretize(Value(*) v) {
    assert(not Is_Void(v));
    assert(not Is_None(v));
    if (QUOTE_BYTE(v) == ISOTOPE_0)
        mutable_QUOTE_BYTE(v) = UNQUOTED_1;
    return v;
}


//=//// META QUOTING ///////////////////////////////////////////////////////=//

// Meta quoting is a superset of plain quoting.  It has the twist that it can
// quote isotopes to produce QUASI! values.  This is done by META (alias ^)
// and the REB_META_XXX family of values (like ^WORD, ^TU.P.LE...)
//
// It's hard to summarize in one place all the various applications of this
// feature!  But it's critical to accomplishing composability by which a
// usermode function can accomplish what the system is able to do internally
// with C.  See FOR-BOTH for at least one good example.
//
//  https://forum.rebol.info/t/1833
//

inline static Value(*) Meta_Quotify(Value(*) v) {
    if (QUOTE_BYTE(v) == ISOTOPE_0) {
        mutable_QUOTE_BYTE(v) = QUASI_2;
        return v;
    }
    return Quotify(v, 1);  // a non-isotope winds up quoted
}

inline static Value(*) Meta_Unquotify(Value(*) v) {
    if (QUOTE_BYTE(v) == QUASI_2)
        mutable_QUOTE_BYTE(v) = ISOTOPE_0;
    else
        Unquotify_Core(v, 1);  // will assert the input is quoted
    return v;
}
