//
//  File: %t-quoted.c
//  Summary: "QUOTED! datatype that acts as container for ANY-VALUE!"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2021 Ren-C Open Source Contributors
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
// In historical Rebol, a WORD! and PATH! had variants which were "LIT" types.
// e.g. FOO was a word, while 'FOO was a LIT-WORD!.  The evaluator behavior
// was that the literalness would be removed, leaving a WORD! or PATH! behind,
// making it suitable for comparisons (e.g. `word = 'foo`)
//
// Ren-C has a generic QUOTED! datatype, a container which can be arbitrarily
// deep in escaping.  This faciliated a more succinct way to QUOTE, as well as
// new features.  THE takes the place of the former literalizing operator,
// and JUST will be literalizing but add a quoting level.
//
//    >> quote 1 + 2  ; now evaluative, adds a quoting level
//    == '3
//
//    >> the a  ; acts like Rebol2 QUOTE
//    == a
//
//    >> the 'a
//    == 'a
//
//    >> just a
//    == 'a
//

#include "sys-core.h"

//
//  CT_Quoted: C
//
// !!! Currently, in order to have a GENERIC dispatcher (e.g. REBTYPE())
// then one also must implement a comparison function.  However, compare
// functions specifically take noquote cells, so you can't pass REB_QUOTED to
// them.  The handling for QUOTED! is in the comparison dispatch itself.
//
REBINT CT_Quoted(noquote(Cell(const*)) a, noquote(Cell(const*)) b, bool strict)
{
    UNUSED(a); UNUSED(b); UNUSED(strict);
    assert(!"CT_Quoted should never be called");
    return 0;
}


//
//  MAKE_Quoted: C
//
// !!! This can be done with QUOTE (currently EVAL) which has the ability
// to take a refinement of how deep.  Having a MAKE variant may be good or
// may not be good; if it were to do a level more than 1 it would need to
// take a BLOCK! with an INTEGER! and the value.  :-/
//
Bounce MAKE_Quoted(
    Frame(*) frame_,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_QUOTED);
    if (parent)
        return FAIL(Error_Bad_Make_Parent(kind, unwrap(parent)));

    return Quotify(Copy_Cell(OUT, arg), 1);
}


//
//  TO_Quoted: C
//
// TO is disallowed at the moment, as there is no clear equivalence of things
// "to" a literal.  (to quoted! [[a]] => \\a, for instance?)
//
Bounce TO_Quoted(Frame(*) frame_, enum Reb_Kind kind, const REBVAL *data) {
    return FAIL(Error_Bad_Make(kind, data));
}


//
//  REBTYPE: C
//
// It was for a time considered whether generics should be willing to operate
// on QUOTED!.  e.g. "do whatever the non-quoted version would do, then add
// the quotedness onto the result".
//
//     >> add (the '''1) 2
//     == '''3
//
//     >> first the '[a b c]
//     == a
//
// While a bit outlandish for ADD, it might seem to make more sense for FIND
// and SELECT when you have a QUOTED! block or GROUP!.  However, the solution
// that emerged after trying other options was to make REQUOTE:
//
// https://forum.rebol.info/t/1035
//
// So the number of things supported by QUOTED is limited to COPY at this time.
//
REBTYPE(Quoted)
{
    // Note: SYM_REFLECT is handled directly in the REFLECT native
    //
    switch (ID_OF_SYMBOL(verb)) {
      case SYM_COPY: {  // D_ARG(1) skips RETURN in first arg slot
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(REF(part));
        UNUSED(REF(deep));
        UNUSED(REF(types));

        REBLEN num_quotes = Dequotify(ARG(value));
        bool threw = Run_Generic_Dispatch_Throws(ARG(value), frame_, verb);
        assert(not threw);  // can't throw
        UNUSED(threw);

        return Quotify(OUT, num_quotes); }

      default:
        break;
    }

    fail ("QUOTED! has no GENERIC operations (use NOQUOTE/REQUOTE)");
}


//
//  the: native [
//
//  "Returns value passed in without evaluation"
//
//      return: "Input value, verbatim--unless /SOFT and soft quoted type"
//          [<opt> any-value!]
//      'value [any-value!]
//      /soft "Evaluate if a GET-GROUP!, GET-WORD!, or GET-PATH!"
//  ]
//
DECLARE_NATIVE(the)
//
// Note: THE is not a perfect synonym for the action assigned to @.  See notes
// on THE* for why.
{
    INCLUDE_PARAMS_OF_THE;

    REBVAL *v = ARG(value);

    if (REF(soft) and ANY_ESCAPABLE_GET(v)) {
        if (Eval_Value_Throws(OUT, v, SPECIFIED))
            return THROWN;
        return OUT;  // Don't set UNEVALUATED flag
    }

    Copy_Cell(OUT, v);

    Set_Cell_Flag(OUT, UNEVALUATED);
    return OUT;
}


//
//  the*: native [
//
//  "Returns value passed in without evaluation, BUT ~null~ becomes pure NULL"
//
//      return: "Input value, verbatim--unless /SOFT and soft quoted type"
//          [<opt> any-value!]
//      'value "Does not allow BAD-WORD! arguments except for ~null~"
//          [any-value!]
//  ]
//
DECLARE_NATIVE(the_p)
//
// THE* is the variant assigned to @.  It does not let you use it with
// BAD-WORD!, except for ~null~, which is transitioned to true NULL.
//
//     >> @ ~null~
//     ; null
//
// This is done as a convenience for the API so people can write:
//
//     rebElide("append block try @", value_might_be_null);
//
// ...instead of:
//
//     rebElide("append block try", rebQ(value_might_be_null));
//
// Because the API machinery will put a plain `~null~` into the stream as
// a surrogate for a NULL instead of asserting/erroring.  If you know that
// what you are dealing with might be a BAD-WORD!, then you should use
// rebQ() instead...
{
    INCLUDE_PARAMS_OF_THE_P;

    REBVAL *v = ARG(value);

    if (IS_BAD_WORD(v)) {
        if (VAL_BAD_WORD_ID(v) == SYM_NULL)
            Init_Nulled(OUT);
        else
            fail ("@ and THE* only accept BAD-WORD! of ~NULL~ to make NULL");
    }
    else
        Copy_Cell(OUT, v);

    Set_Cell_Flag(OUT, UNEVALUATED);
    return OUT;
}


//
//  just*: native [  ; deprecate temporarily due to isotopic block methodoloy
//
//  "Returns quoted eversion of value passed in without evaluation"
//
//      return: "Input value, verbatim--unless /SOFT and soft quoted type"
//          [<opt> any-value!]
//      'value [any-value!]
//      /soft "Evaluate if a GET-GROUP!, GET-WORD!, or GET-PATH!"
//  ]
//
DECLARE_NATIVE(just_p)
//
// Note: This could be defined as `chain [:the | :quote]`.  However, it can be
// needed early in the boot (before REDESCRIBE is available), and it is also
// something that needs to perform well due to common use.  Having it be its
// own native is probably worthwhile.
{
    INCLUDE_PARAMS_OF_JUST_P;

    REBVAL *v = ARG(value);

    if (REF(soft) and ANY_ESCAPABLE_GET(v)) {
        if (Eval_Value_Throws(OUT, v, SPECIFIED))
            return THROWN;
        return Quotify(OUT, 1);  // Don't set UNEVALUATED flag
    }

    Copy_Cell(OUT, v);
    Set_Cell_Flag(OUT, UNEVALUATED);  // !!! should this bit be set?
    return Quotify(OUT, 1);
}


//
//  quote: native [
//
//  {Constructs a quoted form of the evaluated argument}
//
//      return: "Quoted value (if depth = 0, may not be quoted)"
//          [<opt> any-value!]
//      optional [<opt> any-value!]
//      /depth "Number of quoting levels to apply (default 1)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(quote)
{
    INCLUDE_PARAMS_OF_QUOTE;

    REBINT depth = REF(depth) ? VAL_INT32(ARG(depth)) : 1;

    if (depth == 0)
        return COPY(ARG(optional));

    if (depth < 0)
        fail (PAR(depth));

    Copy_Cell(OUT, ARG(optional));
    return Quotify(OUT, depth);
}


//
//  meta: native [
//
//  {Turns BAD-WORD! isotopes into plain BAD-WORD!, ignores NULL, quotes rest}
//
//      return: "Will be invisible if input is purely invisible (see META*)"
//          [<void> <opt> quoted! bad-word! error! block!]
//      ^optional [<void> <opt> any-value!]
//  ]
//
DECLARE_NATIVE(meta)
{
    INCLUDE_PARAMS_OF_META;

    REBVAL *v = ARG(optional);

    if (Is_Meta_Of_Void(v))
        return VOID;  // see META* for non-passthru of ~void~ isotope

    return COPY(v);  // argument was already ^META, no need to Meta_Quotify()
}


//
//  meta*: native [
//
//  {Behavior of ^^ symbol, gives ~void~ BAD-WORD! vs. passing through voids}
//
//      return: [<opt> quoted! bad-word! the-word! error! block!]
//      ^optional [<opt> <void> <fail> any-value!]
//  ]
//
DECLARE_NATIVE(meta_p)
{
    INCLUDE_PARAMS_OF_META_P;

    REBVAL *v = ARG(optional);

    return COPY(v);  // argument was ^META, so no need to Meta_Quotify()
}


//
//  unquote: native [
//
//  {Remove quoting levels from the evaluated argument}
//
//      return: "Value with quotes removed"
//          [<opt> any-value!]
//      value "Any value allowed in case /DEPTH is 0"
//          [<opt> any-value!]
//      /depth "Number of quoting levels to remove (default 1)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(unquote)
{
    INCLUDE_PARAMS_OF_UNQUOTE;

    REBVAL *v = ARG(value);

    REBINT depth = (REF(depth) ? VAL_INT32(ARG(depth)) : 1);

    if (depth < 0)
        fail (PAR(depth));

    if (cast(REBLEN, depth) > VAL_NUM_QUOTES(v))
        fail ("Value not quoted enough for unquote depth requested");

    Unquotify(Copy_Cell(OUT, v), depth);
    return OUT;
}


//
//  unmeta: native [
//
//  {Variant of UNQUOTE that also accepts BAD-WORD! to make isotopes}
//
//      return: [<opt> <void> any-value!]
//      ^value "Taken as ^META for passthru tolerance of pure and isotope void"
//          [<opt> <void> quoted! the-word! bad-word! error! block!]
//  ]
//
DECLARE_NATIVE(unmeta)
//
// Note: It is weird to accept isotopes as input to an UNMETA operation, as it
// is not possible to produce them with a META operation.  But the asymmetric
// choice to accept meta states representing ~void~ isotopes is pragmatic.
// (This errors on other isotopes.)
//
// Consider what FOR-BOTH would need to do in order to please UNMETA here:
//
//      for-both: lambda ['var blk1 blk2 body] [
//          unmeta all [
//              '~void~  ; <-- this is the nuisance we want to avoid
//              meta for-each (var) blk1 body
//              meta for-each (var) blk2 body
//          ]
//      ]
//
// The loop has converted values into the ^META domain so that they can be used
// with the ALL.  The components can opt out if neither loop runs a body,
// which effectively would render it to act like `all []`.  In this case we
// are seeking to generate a ~void~ isotope result...but the ALL will itself
// yield a non-META ~void~ isotope in the all-opt-out scenario.  A "pure"
// version of UNMETA would not take a ^META argument and error in this case.
//
// Having to work around it by slipping a quoted ~void~ BAD-WORD! into the mix
// is busywork, when UNMETA can simply return the ~void~ isotope the empty ALL
// gave it instead of erroring for the sake of "purity".  (There is a similar
// compromise in META, which is what allows it to take the MAYBE result of pure
// invisibility and pass it through vs. returning BLANK! like `^` does.)
{
    INCLUDE_PARAMS_OF_UNMETA;

    REBVAL *v = ARG(value);

    if (Is_Meta_Of_Void(v))
        return VOID;  // ^-- see explanation

    if (Is_Nulled(v))
        return nullptr;  // ^(null) => null, so the reverse must be true

    if (IS_BAD_WORD(v)) {
        Isotopify(v);
        fail (Error_Bad_Isotope(v));  // no other isotopes valid for the trick
    }

    assert(IS_QUOTED(v));  // handling the invisibility detour is done now...
    Unquotify(v, 1);  // drop quote level caused by ^META parameter convention

    // Now remove the level of meta the user was asking for.
    //
    return UNMETA(v);
}


//
//  unget: native [
//
//  {Interim tool for emulating future GET-WORD!/GET-TUPLE! semantics}
//
//      return: [<opt> <void> any-value!]
//      'var "Quoted for convenience"
//          [word! tuple!]
//  ]
//
DECLARE_NATIVE(unget)
{
    INCLUDE_PARAMS_OF_UNGET;

    if (Get_Var_Core_Throws(SPARE, GROUPS_OK, ARG(var), SPECIFIED))
        return THROWN;

    return UNMETA(SPARE);
}


//
//  spread: native [
//
//  {Make block arguments splice}
//
//      return: "Isotope of BLOCK! or unquoted value"
//          [<opt> any-value!]
//      array [<opt> quoted! any-array!]
//  ]
//
DECLARE_NATIVE(spread)
//
// !!! The name SPREAD is being chosen because it is more uncommon than splice,
// and there is no particular contention for its design.  SPLICE may be a more
// complex operation.
{
    INCLUDE_PARAMS_OF_SPREAD;

    REBVAL *v = ARG(array);

    if (Is_Nulled(v))
        return nullptr;  // Put TRY on the APPEND or whatever, not SPREAD

    if (IS_QUOTED(v))
        return Unquotify(Copy_Cell(OUT, v), 1);

    return Splicify(Copy_Cell(OUT, v));
}


//
//  maybe: native [
//
//  {If argument is null or none, make it void (also pass through voids)}
//
//      return: "Value (if it's anything other than the states being checked)"
//          [<opt> <void> any-value!]
//      ^optional [<opt> <void> <fail> any-value!]
//  ]
//
DECLARE_NATIVE(maybe)
{
    INCLUDE_PARAMS_OF_MAYBE;

    REBVAL *v = ARG(optional);

    if (
        Is_Meta_Of_Void(v)
        or Is_Nulled(v) or Is_Meta_Of_Null_Isotope(v)
        or Is_Meta_Of_None(v)
    ){
        return VOID;
    }

    if (IS_ERROR(v)) {  // fold in TRY behavior as well
        ERROR_VARS *vars = ERR_VARS(VAL_CONTEXT(v));
        if (
            IS_WORD(&vars->id)
            and VAL_WORD_ID(&vars->id) == SYM_TRY_IF_NULL_MEANT
        ){
            return VOID;
        }
        return FAIL(VAL_CONTEXT(v));
    }

    Move_Cell(OUT, v);
    Meta_Unquotify(OUT);
    Decay_If_Isotope(OUT);
    return OUT;
}


//
//  maybe+: native [
//
//  {Special Test: Potential future of a MAYBE Intrinsic}
//
//      return: "Value (if it's anything other than void)"
//          [<opt> <void> any-value!]
//  ]
//
DECLARE_NATIVE(maybe_a)
//
// !!! One aspect of the implementation of translucency is that functions like
// IF do not actually overwrite the output cell when they don't run their
// branch (nor WHILE if they don't run their body, etc.)
//
// Something interesting about that is that we could implement MAYBE as
// writing its argument directly onto its output, and looking for if it was
// stale or not.  This implements that experiment.
{
    INCLUDE_PARAMS_OF_MAYBE_A;

    if (Eval_Step_In_Subframe_Throws(
        OUT,
        frame_,
        FRAME_FLAG_MAYBE_STALE
            | EVAL_EXECUTOR_FLAG_SINGLE_STEP
    )){
        return THROWN;
    }

    if (Is_Stale(OUT))
        return VOID;

    return OUT;
}


//
//  quoted?: native [
//
//  {Tells you if the argument is QUOTED! or not}
//
//      return: [logic!]
//      optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(quoted_q)
{
    INCLUDE_PARAMS_OF_QUOTED_Q;

    return Init_Logic(OUT, VAL_TYPE(ARG(optional)) == REB_QUOTED);
}


//
//  noquote: native [
//
//  {Removes all levels of quoting from a quoted value}
//
//      return: [<opt> any-value!]
//      optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(noquote)
{
    INCLUDE_PARAMS_OF_NOQUOTE;

    REBVAL *v = ARG(optional);
    Unquotify(v, VAL_NUM_QUOTES(v));
    return COPY(v);
}
