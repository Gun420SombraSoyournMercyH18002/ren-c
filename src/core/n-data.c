//
//  File: %n-data.c
//  Summary: "native functions for data and context"
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


static bool Check_Char_Range(const REBVAL *val, REBLEN limit)
{
    if (IS_CHAR(val))
        return VAL_CHAR(val) <= limit;

    if (IS_INTEGER(val))
        return VAL_INT64(val) <= cast(REBI64, limit);

    assert(ANY_STRING(val));

    REBLEN len;
    Utf8(const*) up = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, val);

    for (; len > 0; len--) {
        Codepoint c;
        up = NEXT_CHR(&c, up);

        if (c > limit)
            return false;
    }

    return true;
}


//
//  ascii?: native [
//
//  {Returns TRUE if value or string is in ASCII character range (below 128).}
//
//      return: [logic!]
//      value [any-string! char! integer!]
//  ]
//
DECLARE_NATIVE(ascii_q)
{
    INCLUDE_PARAMS_OF_ASCII_Q;

    return Init_Logic(OUT, Check_Char_Range(ARG(value), 0x7f));
}


//
//  latin1?: native [
//
//  {Returns TRUE if value or string is in Latin-1 character range (below 256).}
//
//      return: [logic!]
//      value [any-string! char! integer!]
//  ]
//
DECLARE_NATIVE(latin1_q)
{
    INCLUDE_PARAMS_OF_LATIN1_Q;

    return Init_Logic(OUT, Check_Char_Range(ARG(value), 0xff));
}


//
//  as-pair: native [
//
//  "Combine X and Y values into a pair."
//
//      return: [pair!]
//      x [any-number!]
//      y [any-number!]
//  ]
//
DECLARE_NATIVE(as_pair)
{
    INCLUDE_PARAMS_OF_AS_PAIR;

    return Init_Pair(OUT, ARG(x), ARG(y));
}


//
//  bind: native [
//
//  {Binds words or words in arrays to the specified context}
//
//      return: [action! any-array! any-path! any-word! quoted!]
//      value "Value whose binding is to be set (modified) (returned)"
//          [action! any-array! any-path! any-word! quoted!]
//      target "Target context or a word whose binding should be the target"
//          [any-word! any-context!]
//      /copy "Bind and return a deep copy of a block, don't modify original"
//      /only "Bind only first block (not deep)"
//      /new "Add to context any new words found"
//      /set "Add to context any new set-words found"
//  ]
//
DECLARE_NATIVE(bind)
{
    INCLUDE_PARAMS_OF_BIND;

    REBVAL *v = ARG(value);
    REBVAL *target = ARG(target);

    REBLEN flags = REF(only) ? BIND_0 : BIND_DEEP;

    REBU64 bind_types = TS_WORD;

    REBU64 add_midstream_types;
    if (REF(new)) {
        add_midstream_types = TS_WORD;
    }
    else if (REF(set)) {
        add_midstream_types = FLAGIT_KIND(REB_SET_WORD);
    }
    else
        add_midstream_types = 0;

    Cell(const*) context;

    // !!! For now, force reification before doing any binding.

    if (ANY_CONTEXT(target)) {
        //
        // Get target from an OBJECT!, ERROR!, PORT!, MODULE!, FRAME!
        //
        context = target;
    }
    else {
        assert(ANY_WORD(target));

        if (not Did_Get_Binding_Of(SPARE, target))
            fail (Error_Not_Bound_Raw(target));

        context = SPARE;
    }

    if (ANY_WORDLIKE(v)) {
        //
        // Bind a single word (also works on refinements, `/a` ...or `a.`, etc.

        if (Try_Bind_Word(context, v))
            return COPY(v);

        // not in context, bind/new means add it if it's not.
        //
        if (REF(new) or (IS_SET_WORD(v) and REF(set))) {
            Init_None(Append_Context(VAL_CONTEXT(context), v, nullptr));
            return COPY(v);
        }

        fail (Error_Not_In_Context_Raw(v));
    }

    // Binding an ACTION! to a context means it will obey derived binding
    // relative to that context.  See METHOD for usage.  (Note that the same
    // binding pointer is also used in cases like RETURN to link them to the
    // FRAME! that they intend to return from.)
    //
    if (REB_ACTION == CELL_HEART(v)) {
        INIT_VAL_ACTION_BINDING(v, VAL_CONTEXT(context));
        return COPY(v);
    }

    if (not ANY_ARRAYLIKE(v))  // QUOTED! could have wrapped any type
        fail (Error_Invalid_Arg(frame_, PAR(value)));

    Cell(*) at;
    Cell(const*) tail;
    if (REF(copy)) {
        Array(*) copy = Copy_Array_Core_Managed(
            VAL_ARRAY(v),
            VAL_INDEX(v), // at
            VAL_SPECIFIER(v),
            ARR_LEN(VAL_ARRAY(v)), // tail
            0, // extra
            ARRAY_MASK_HAS_FILE_LINE, // flags
            TS_ARRAY // types to copy deeply
        );
        at = ARR_HEAD(copy);
        tail = ARR_TAIL(copy);
        Init_Array_Cell(OUT, VAL_TYPE(v), copy);
    }
    else {
        ENSURE_MUTABLE(v);  // use IN for virtual binding
        at = VAL_ARRAY_AT_MUTABLE_HACK(&tail, v);  // !!! only *after* index!
        Copy_Cell(OUT, v);
    }

    Bind_Values_Core(
        at,
        tail,
        context,
        bind_types,
        add_midstream_types,
        flags
    );

    return OUT;
}


//
//  in: native [
//
//  "Returns a view of the input bound virtually to the context"
//
//      return: [<opt> any-word! any-array!]
//      context [any-context!]
//      value [<const> <blank> any-word! any-array!]  ; QUOTED! support?
//  ]
//
DECLARE_NATIVE(in)
{
    INCLUDE_PARAMS_OF_IN;

    Context(*) ctx = VAL_CONTEXT(ARG(context));
    REBVAL *v = ARG(value);

    // !!! Note that BIND of a WORD! in historical Rebol/Red would return the
    // input word as-is if the word wasn't in the requested context, while
    // IN would return NONE! on failure.  We carry forward the NULL-failing
    // here in IN, but BIND's behavior on words may need revisiting.
    //
    if (ANY_WORD(v)) {
        Symbol(const*) symbol = VAL_WORD_SYMBOL(v);
        const bool strict = true;
        REBLEN index = Find_Symbol_In_Context(ARG(context), symbol, strict);
        if (index == 0)
            return nullptr;
        return Init_Any_Word_Bound(OUT, VAL_TYPE(v), symbol, ctx, index);
    }

    assert(ANY_ARRAY(v));
    Virtual_Bind_Deep_To_Existing_Context(v, ctx, nullptr, REB_WORD);
    return COPY(v);
}


//
//  without: native [
//
//  "Remove a virtual binding from a value"
//
//      return: [<opt> any-word! any-array!]
//      context "If integer, then removes that number of virtual bindings"
//          [integer! any-context!]
//      value [<const> <blank> any-word! any-array!]  ; QUOTED! support?
//  ]
//
DECLARE_NATIVE(without)
{
    INCLUDE_PARAMS_OF_WITHOUT;

    Context(*) ctx = VAL_CONTEXT(ARG(context));
    REBVAL *v = ARG(value);

    // !!! Note that BIND of a WORD! in historical Rebol/Red would return the
    // input word as-is if the word wasn't in the requested context, while
    // IN would return NONE! on failure.  We carry forward the NULL-failing
    // here in IN, but BIND's behavior on words may need revisiting.
    //
    if (ANY_WORD(v)) {
        Symbol(const*) symbol = VAL_WORD_SYMBOL(v);
        const bool strict = true;
        REBLEN index = Find_Symbol_In_Context(ARG(context), symbol, strict);
        if (index == 0)
            return nullptr;
        return Init_Any_Word_Bound(
            OUT,
            VAL_TYPE(v),
            symbol,  // !!! incoming case...consider impact of strict if false?
            ctx,
            index
        );
    }

    assert(ANY_ARRAY(v));
    Virtual_Bind_Deep_To_Existing_Context(v, ctx, nullptr, REB_WORD);
    return COPY(v);
}

//
//  use: native [
//
//  {Defines words local to a block (See also: LET)}
//
//      return: [<opt> any-value!]
//      vars "Local word(s) to the block"
//          [block! word!]
//      body "Block to evaluate"
//          [block!]
//  ]
//
DECLARE_NATIVE(use)
//
// !!! USE is somewhat deprecated, because LET does something very similar
// without bringing in indentation and an extra block.  The USE word is being
// considered for a more interesting purpose--of being able to import an
// object into a scope, like a WITH statement.
//
// 1. The new context created here winds up being managed.  So if no references
//    exist, GC is ok.  For instance, someone can write `use [x] [print "hi"]`
{
    INCLUDE_PARAMS_OF_USE;

    Value(*) vars = ARG(vars);
    Value(*) body = ARG(body);

    Context(*) context = Virtual_Bind_Deep_To_New_Context(
        body,  // may be replaced with rebound copy, or left the same
        vars  // similar to the "spec" of a loop: WORD!/LIT-WORD!/BLOCK!
    );
    UNUSED(context);  // managed, but see [1]

    if (Do_Any_Array_At_Throws(OUT, body, SPECIFIED))
        return THROWN;

    return OUT;
}


//
//  Did_Get_Binding_Of: C
//
bool Did_Get_Binding_Of(REBVAL *out, const REBVAL *v)
{
    switch (VAL_TYPE(v)) {
    case REB_ACTION: {
        Context(*) binding = VAL_ACTION_BINDING(v); // e.g. METHOD, RETURNs
        if (not binding)
            return false;

        Init_Frame(out, binding, ANONYMOUS);  // !!! Review ANONYMOUS
        break; }

    case REB_WORD:
    case REB_SET_WORD:
    case REB_GET_WORD:
    case REB_META_WORD:
    case REB_THE_WORD: {
        if (IS_WORD_UNBOUND(v))
            return false;

        // Requesting the context of a word that is relatively bound may
        // result in that word having a FRAME! incarnated as a REBSER node (if
        // it was not already reified.)
        //
        Context(*) c = VAL_WORD_CONTEXT(v);

        // If it's a FRAME! we want the phase to match the execution phase at
        // the current moment of execution.
        //
        if (CTX_TYPE(c) == REB_FRAME) {
            Frame(*) f = CTX_FRAME_IF_ON_STACK(c);
            if (f == nullptr)
                Copy_Cell(out, CTX_ARCHETYPE(c));
            else
                Copy_Cell(out, f->rootvar);  // rootvar has phase, binding
        }
        else
            Copy_Cell(out, CTX_ARCHETYPE(c));
        break; }

      default:
        //
        // Will OBJECT!s or FRAME!s have "contexts"?  Or if they are passed
        // in should they be passed trough as "the context"?  For now, keep
        // things clear?
        //
        assert(false);
    }

    // A FRAME! has special properties of ->phase and ->binding which
    // affect the interpretation of which layer of a function composition
    // they correspond to.  If you REDO a FRAME! value it will restart at
    // different points based on these properties.  Assume the time of
    // asking is the layer in the composition the user is interested in.
    //
    // !!! This may not be the correct answer, but it seems to work in
    // practice...keep an eye out for counterexamples.
    //
    if (IS_FRAME(out)) {
        Context(*) c = VAL_CONTEXT(out);
        Frame(*) f = CTX_FRAME_IF_ON_STACK(c);
        if (f) {
            INIT_VAL_FRAME_PHASE(out, FRM_PHASE(f));
            INIT_VAL_FRAME_BINDING(out, FRM_BINDING(f));
        }
        else {
            // !!! Assume the canon FRAME! value in varlist[0] is useful?
            //
            assert(VAL_FRAME_BINDING(out) == UNBOUND); // canon, no binding
        }
    }

    return true;
}


//
//  value?: native [
//
//  "Test if an optional cell contains a value (e.g. `value? null` is FALSE)"
//
//      return: [logic!]
//      optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(value_q)
{
    INCLUDE_PARAMS_OF_VALUE_Q;

    return Init_Logic(OUT, ANY_VALUE(ARG(optional)));
}


//
//  any-inert?: native [
//
//  "Test if a value type is inert"
//
//      return: [logic!]
//      optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(any_inert_q)
//
// This could be done via a typeset bit the way ANY-BLOCK! and other tests are
// done.  However, the types are organized to make this particular test fast.
{
    INCLUDE_PARAMS_OF_ANY_INERT_Q;

    REBVAL *v = ARG(optional);

    return Init_Logic(OUT, not Is_Nulled(v) and ANY_INERT(v));
}


//
//  unbind: native [
//
//  "Unbinds words from context."
//
//      return: [block! any-word!]
//      word [block! any-word!]
//          "A word or block (modified) (returned)"
//      /deep
//          "Process nested blocks"
//  ]
//
DECLARE_NATIVE(unbind)
{
    INCLUDE_PARAMS_OF_UNBIND;

    REBVAL *word = ARG(word);

    if (ANY_WORD(word))
        Unbind_Any_Word(word);
    else {
        assert(IS_BLOCK(word));

        Cell(const*) tail;
        Cell(*) at = VAL_ARRAY_AT_ENSURE_MUTABLE(&tail, word);
        option(Context(*)) context = nullptr;
        Unbind_Values_Core(at, tail, context, did REF(deep));
    }

    return COPY(word);
}


//
//  collect-words: native [
//
//  {Collect unique words used in a block (used for context construction)}
//
//      return: [block!]
//      block [block!]
//      /deep "Include nested blocks"
//      /set "Only include set-words"
//      /ignore "Ignore prior words"
//          [any-context! block!]
//  ]
//
DECLARE_NATIVE(collect_words)
{
    INCLUDE_PARAMS_OF_COLLECT_WORDS;

    Flags flags;
    if (REF(set))
        flags = COLLECT_ONLY_SET_WORDS;
    else
        flags = COLLECT_ANY_WORD;

    if (REF(deep))
        flags |= COLLECT_DEEP;

    Cell(const*) tail;
    Cell(const*) at = VAL_ARRAY_AT(&tail, ARG(block));
    return Init_Block(
        OUT,
        Collect_Unique_Words_Managed(at, tail, flags, ARG(ignore))
    );
}


//
//  Get_Var_Push_Refinements_Throws: C
//
bool Get_Var_Push_Refinements_Throws(
    REBVAL *out,
    option(REBVAL*) steps_out,  // if NULL, then GROUP!s not legal
    Cell(const*) var,
    REBSPC *var_specifier
){
    assert(var != out);
    assert(steps_out != out);  // Legal for SET, not for GET

    if (ANY_GROUP(var)) {  // !!! GET-GROUP! makes sense, but SET-GROUP!?
        if (not steps_out)
            fail (Error_Bad_Get_Group_Raw(var));

        if (steps_out != GROUPS_OK)
            fail ("GET-VAR on GROUP! with steps doesn't have answer ATM");

        if (Do_Any_Array_At_Throws(out, var, var_specifier))
            return true;

        return false;
    }

    if (IS_BLANK(var)) {
        Init_Nulled(out);  // "blank in, null out" get variable convention
        if (steps_out and steps_out != GROUPS_OK)
            Init_Blank(unwrap(steps_out));
        return false;
    }

    if (ANY_WORD(var)) {

      get_source:  // Note: source may be `out`, due to GROUP fetch above!

        if (steps_out and steps_out != GROUPS_OK) {
            //
            // set the steps out *first* before overwriting out
            //
            Derelativize(unwrap(steps_out), var, var_specifier);
            mutable_HEART_BYTE(unwrap(steps_out)) = REB_WORD;
        }

        Copy_Cell(out, Lookup_Word_May_Fail(var, var_specifier));

        Decay_If_Isotope(out);  // !!! should not be possible, review
        return false;
    }

    if (ANY_PATH(var)) {  // !!! SET-PATH! too?
        DECLARE_LOCAL (safe);
        PUSH_GC_GUARD(safe);
        DECLARE_LOCAL (result);
        PUSH_GC_GUARD(result);

        bool threw = Get_Path_Push_Refinements_Throws(
            result, safe, var, var_specifier  // var may be in `out`
        );
        DROP_GC_GUARD(result);
        DROP_GC_GUARD(safe);

        if (steps_out and steps_out != GROUPS_OK)
            Init_None(unwrap(steps_out));  // !!! What to return?

        Move_Cell(out, result);
        return threw;
    }

    StackIndex base = TOP_INDEX;

    if (ANY_SEQUENCE(var)) {
        if (Not_Cell_Flag(var, SEQUENCE_HAS_NODE))  // byte compressed
            fail (var);

        const Node* node1 = VAL_NODE1(var);
        if (NODE_BYTE(node1) & NODE_BYTEMASK_0x01_CELL) { // pair compressed
            assert(false);  // these don't exist yet
            fail (var);
        }

        switch (SER_FLAVOR(SER(node1))) {
          case FLAVOR_SYMBOL:
            if (Get_Cell_Flag(var, REFINEMENT_LIKE))  // `/a` or `.a`
                goto get_source;

            // `a/` or `a.`
            //
            // !!! If this is a PATH!, it should error if it's not an action...
            // and if it's a TUPLE! it should error if it is an action.  Review.
            //
            goto get_source;

          case FLAVOR_ARRAY:
            break;

          default:
            panic (var);
        }

        Cell(const*) tail;
        Cell(const*) head = VAL_ARRAY_AT(&tail, var);
        Cell(const*) at;
        REBSPC *at_specifier = Derive_Specifier(var_specifier, var);
        for (at = head; at != tail; ++at) {
            if (IS_GROUP(at)) {
                if (not steps_out)
                    fail (Error_Bad_Get_Group_Raw(var));

                if (Do_Any_Array_At_Throws(out, at, at_specifier)) {
                    Drop_Data_Stack_To(base);
                    return true;
                }
                Move_Cell(PUSH(), out);

                // By convention, picker steps quote the first item if it was a
                // GROUP!.  It has to be somehow different because `('a).b` is
                // trying to pick B out of the WORD! a...not out of what is
                // fetched from A.  So if the convention is that the first item
                // of a "steps" block needs to be "fetched" we quote it.
                //
                if (at == head)
                    Quotify(TOP, 1);
            }
            else
                Derelativize(PUSH(), at, at_specifier);
        }
    }
    else if (IS_THE_BLOCK(var)) {
        REBSPC *at_specifier = Derive_Specifier(var_specifier, var);
        Cell(const*) tail;
        Cell(const*) head = VAL_ARRAY_AT(&tail, var);
        Cell(const*) at;
        for (at = head; at != tail; ++at)
            Derelativize(PUSH(), at, at_specifier);
    }
    else
        fail (var);

    StackIndex stackindex = base + 1;

  blockscope {
    StackValue(*) at = Data_Stack_At(stackindex);
    if (IS_QUOTED(at)) {
        Copy_Cell(out, at);
        Unquotify(out, 1);
    }
    else if (IS_WORD(at)) {
        Copy_Cell(
            out,
            Lookup_Word_May_Fail(at, SPECIFIED)
        );
        if (Is_Isotope(out))
            fail (Error_Bad_Word_Get(at, out));
    }
    else
        fail (Copy_Cell(out, at));
  }
    ++stackindex;

    DECLARE_LOCAL (temp);
    PUSH_GC_GUARD(temp);

    while (stackindex != TOP_INDEX + 1) {
        Move_Cell(temp, out);
        Quotify(temp, 1);
        const void *ins = rebQ(cast(REBVAL*, Data_Stack_At(stackindex)));
        if (rebRunThrows(
            out,  // <-- output cell
            Lib(PICK_P), temp, ins
        )){
            Drop_Data_Stack_To(base);
            DROP_GC_GUARD(temp);
            fail (Error_No_Catch_For_Throw(TOP_FRAME));
        }
        ++stackindex;
    }

    DROP_GC_GUARD(temp);

    if (steps_out and steps_out != GROUPS_OK) {
        Array(*) a = Pop_Stack_Values(base);
        Init_Array_Cell(unwrap(steps_out), REB_THE_BLOCK, a);
    }
    else
        Drop_Data_Stack_To(base);

    Decay_If_Isotope(out);  // !!! should not be possible, review
    return false;
}


//
//  Get_Var_Core_Throws: C
//
bool Get_Var_Core_Throws(
    REBVAL *out,
    option(REBVAL*) steps_out,  // if NULL, then GROUP!s not legal
    Cell(const*) var,
    REBSPC *var_specifier
){
    StackIndex base = TOP_INDEX;
    bool threw = Get_Var_Push_Refinements_Throws(
        out, steps_out, var, var_specifier
    );
    if (TOP_INDEX != base) {
        assert(IS_ACTION(out) and not threw);
        //
        // !!! Note: passing EMPTY_BLOCK here for the def causes problems;
        // that needs to be looked into.
        //
        DECLARE_LOCAL (action);
        Move_Cell(action, out);
        return Specialize_Action_Throws(out, action, nullptr, base);
    }
    return threw;
}


//
//  Get_Var_May_Fail: C
//
// Simple interface, does not process GROUP!s (lone or in TUPLE!s)
//
void Get_Var_May_Fail(
    REBVAL *out,
    Cell(const*) source,
    REBSPC *specifier,
    bool any
){
    REBVAL *steps_out = nullptr;

    if (Get_Var_Core_Throws(out, steps_out, source, specifier))
        fail (Error_No_Catch_For_Throw(TOP_FRAME));

    if (not any)
        if (Is_Isotope(out))
            fail (Error_Bad_Word_Get(source, out));
}


//
//  Get_Path_Push_Refinements_Throws: C
//
// This form of Get_Path() is low-level, and may return a non-ACTION! value
// if the path is inert (e.g. `/abc` or `.a.b/c/d`).
//
// It is also able to return a non-ACTION! value if REDBOL-PATHS compatibility
// is enabled.
//
bool Get_Path_Push_Refinements_Throws(
    REBVAL *out,
    REBVAL *safe,
    Cell(const*) path,
    REBSPC *path_specifier
){
    if (Not_Cell_Flag(path, SEQUENCE_HAS_NODE)) {  // byte compressed, inert
        Derelativize(out, path, path_specifier);  // inert
        return false;
    }

    const Node* node1 = VAL_NODE1(path);
    if (NODE_BYTE(node1) & NODE_BYTEMASK_0x01_CELL) {
        assert(false);  // none of these exist yet
        return false;
    }

    switch (SER_FLAVOR(SER(node1))) {
      case FLAVOR_SYMBOL : {
        if (Get_Cell_Flag(path, REFINEMENT_LIKE)) {  // `/a` - should these GET?
            Get_Word_May_Fail(out, path, path_specifier);
            return false;
        }

        // !!! `a/` should error if it's not an action...
        //
        Get_Word_May_Fail(out, path, path_specifier);
        if (not IS_ACTION(out))
            fail (Error_Inert_With_Slashed_Raw());
        return false; }

      case FLAVOR_ARRAY : {}
        break;

      default :
        panic (path);
    }

    Cell(const*) tail;
    Cell(const*) head = VAL_ARRAY_AT(&tail, path);
    while (IS_BLANK(head)) {
        ++head;
        if (head == tail)
            fail ("Empty PATH!");
    }

    if (ANY_INERT(head)) {
        Derelativize(out, path, path_specifier);
        return false;
    }

    if (IS_GROUP(head)) {
        //
        // Note: Historical Rebol did not allow GROUP! at the head of path.
        // We can thus restrict head-of-path evaluations to ACTION!.
        //
        REBSPC *derived = Derive_Specifier(path_specifier, path);
        if (Eval_Value_Throws(out, head, derived))
            return true;
        if (not IS_ACTION(out))
            fail ("Head of PATH! did not evaluate to an ACTION!");
    }
    else if (IS_TUPLE(head)) {
        //
        // Note: Historical Rebol didn't have WORD!-bearing TUPLE!s at all.
        // We can thus restrict head-of-path evaluations to ACTION!, or
        // this exemption...where blank-headed tuples can carry over the
        // inert evaluative behavior.  For instance:
        //
        //    >> .a.b/c/d
        //    == .a.b/c/d
        //
        if (IS_BLANK(VAL_SEQUENCE_AT(safe, head, 0))) {
            Derelativize(out, path, path_specifier);
            return false;
        }

        REBSPC *derived = Derive_Specifier(path_specifier, path);

        DECLARE_LOCAL (steps);
        if (Get_Var_Core_Throws(out, steps, head, derived))
            return true;

        if (not IS_ACTION(out))
            fail ("TUPLE! must resolve to an action if head of PATH!");
    }
    else if (IS_WORD(head)) {
        REBSPC *derived = Derive_Specifier(path_specifier, path);
        const REBVAL *lookup = Lookup_Word_May_Fail(
            head,
            derived
        );

        if (Is_Isotope(lookup))
            fail (Error_Bad_Word_Get(head, lookup));

        // Under the new thinking, PATH! is only used to invoke actions.
        //
        if (IS_ACTION(lookup)) {
            Copy_Cell(out, lookup);
            goto action_in_out;
        }

        Derelativize(safe, path, path_specifier);
        mutable_HEART_BYTE(safe) = REB_TUPLE;

        // ...but historical Rebol used PATH! for everything.  For Redbol
        // compatibility, we flip over to a TUPLE!.  We must be sure that
        // we are running in a mode where tuple allows the getting of
        // actions (though it's slower because it does specialization)
        //
        REBVAL *redbol = Get_System(SYS_OPTIONS, OPTIONS_REDBOL_PATHS);
        if (not IS_LOGIC(redbol) or VAL_LOGIC(redbol) == false) {
            Derelativize(out, path, path_specifier);
            rebElide(
                "echo [The PATH!", out, "doesn't evaluate to",
                    "an ACTION! in the first slot.]",
                "echo [SYSTEM.OPTIONS.REDBOL-PATHS is FALSE so this",
                    "is not allowed by default.]",
                "echo [For now, we'll enable it automatically...but it",
                    "will slow down the system!]",
                "echo [Please use TUPLE! instead, like", safe, "]",

                "system.options.redbol-paths: true",
                "wait 3"
            );
        }

        DECLARE_LOCAL (steps);
        if (Get_Var_Core_Throws(out, steps, safe, SPECIFIED))
            return true;

        if (Is_Isotope(out))
            fail (Error_Bad_Word_Get(path, out));

        return false;  // refinements pushed by Redbol-adjusted Get_Var()
    }
    else
        fail (head);  // what else could it have been?

  action_in_out:

    assert(IS_ACTION(out));

    // We push the remainder of the path in *reverse order* as words to act
    // as refinements to the function.  The action execution machinery will
    // decide if they are valid or not.
    //
    REBLEN len = VAL_SEQUENCE_LEN(path) - 1;
    for (; len != 0; --len) {
        Cell(const*) at = VAL_SEQUENCE_AT(safe, path, len);
        if (IS_GROUP(at)) {
            DECLARE_LOCAL (temp);
            REBSPC *derived = Derive_Specifier(
                path_specifier,
                at
            );
            if (Eval_Value_Throws(temp, at, derived))
                return true;

            Move_Cell(safe, temp);
            Decay_If_Isotope(safe);
            if (Is_Isotope(safe))
                fail (Error_Bad_Isotope(safe));

            at = safe;
        }
        if (Is_Nulled(at) or IS_BLANK(at)) {
            // just skip it
        }
        else if (IS_WORD(at))
            Init_Pushed_Refinement(PUSH(), VAL_WORD_SYMBOL(at));
        else if (IS_PATH(at) and IS_REFINEMENT(at))
            Init_Pushed_Refinement(PUSH(), VAL_REFINEMENT_SYMBOL(at));
        else
            fail (at);
    }

    return false;
}


//
//  get: native [
//
//  {Gets the value of a word or path, or block of words/paths}
//
//      return: [<opt> any-value!]
//      steps: "Allow GROUP! evals, returns block of reusable PICK/POKE steps"
//          [the-block! the-word! blank!]
//
//      source "Word or path to get, or block of PICK steps"
//          [<blank> any-word! any-sequence! any-group! the-block!]
//      /any "Do not error on BAD-WORD! isotopes"
//  ]
//
DECLARE_NATIVE(get)
//
// 1. Plain PICK can't throw (e.g. from a GROUP!) because it won't evaluate
//    them.  However, we can get errors.  Confirm we only are raising errors
//    unless steps_out were passed.
{
    INCLUDE_PARAMS_OF_GET;

    REBVAL *steps = ARG(steps);
    REBVAL *source = ARG(source);

    if (WANTED(steps)) {
        // see note in SET, we can't tell if GROUPS_OK would be sufficient
    }
    else
        steps = nullptr;  // no GROUP! evals

    if (Get_Var_Core_Throws(OUT, steps, source, SPECIFIED)) {
        assert(steps or IS_ERROR(VAL_THROWN_LABEL(frame_)));  // see [1]
        return THROWN;
    }

    if (not REF(any))
        if (Is_Isotope(OUT))
            fail (Error_Bad_Word_Get(source, OUT));

    Proxy_Multi_Returns(frame_);
    return OUT;
}


//
//  Set_Var_Core_Updater_Throws: C
//
// This is centralized code for setting variables.  If it returns `true`, the
// out cell will contain the thrown value.  If it returns `false`, the out
// cell will have steps with any GROUP!s evaluated.
//
// It tries to improve efficiency by handling cases that don't need methodized
// calling of POKE* up front.  If a frame is needed, then it leverages that a
// frame with pushed cells is available to avoid needing more temporaries.
//
// **Almost all parts of the system should go through this code for assignment,
// even when they know they have just a WORD! in their hand and don't need path
// dispatch.**  It handles other details like isotope decay.  Only a few places
// bypass this code for reasons of optimization, but they must do so carefully.
//
// The evaluator cases for SET_TUPLE and SET_GROUP use this routine, while the
// SET_WORD is (currently) its own optimized case.  When they run:
//
//    `out` is the frame's spare (f_spare)
//    `steps_out` is also frame spare
//    `target` is the currently processed value (v)
//    `target_specifier` is the feed's specifier (v_specifier)
//    `setval` is the value held in the output (f->out)
//
// It is legal to have `target == out`.  It means the target may be overwritten
// in the course of the assignment.
//
bool Set_Var_Core_Updater_Throws(
    REBVAL *out,  // GC-safe cell to write steps to, or put thrown value
    option(REBVAL*) steps_out,  // no GROUP!s if nulled
    Cell(const*) var,  // e.g. v
    REBSPC *var_specifier,  // e.g. v_specifier
    const REBVAL *setval,  // e.g. f->out (in the evaluator, right hand side)
    const REBVAL *updater
){
    // Note: `steps_out` can be equal to `out` can be equal to `target`

    DECLARE_LOCAL (temp);  // target might be same as out (e.g. spare)

    if (ANY_GROUP(var)) {  // !!! SET-GROUP! makes sense, but GET-GROUP!?
        if (not steps_out)
            fail (Error_Bad_Get_Group_Raw(var));

        if (Do_Any_Array_At_Throws(temp, var, var_specifier))
            return true;

        Move_Cell(out, temp);  // if spare was var, we are replacing it
        var = out;
        var_specifier = SPECIFIED;
    }

    if (Is_Blackhole(var)) {
        if (steps_out and steps_out != GROUPS_OK)
            Init_Blackhole(unwrap(steps_out));
        return false;
    }

    // !!! Review decaying strategy--it seems that we should assert the caller
    // has already decayed it to avoid doing it twice, or paying for it when
    // the caller knows they don't have a decaying isotope.
    //
    const REBVAL *decayed = Pointer_To_Decayed(setval);

    if (ANY_WORD(var)) {

      set_target:

        if (VAL_ACTION(updater) == VAL_ACTION(Lib(POKE_P))) {
            //
            // Shortcut past POKE for WORD! (though this subverts hijacking,
            // review that case.)
            //
            Copy_Cell(Sink_Word_May_Fail(var, var_specifier), decayed);
        }
        else {
            // !!! This is a hack to try and get things working for PROTECT*.
            // Things are in roughly the right place, but very shaky.  Revisit
            // as BINDING OF is reviewed in terms of answers for LET.
            //
            Derelativize(temp, var, var_specifier);
            Quotify(temp, 1);
            PUSH_GC_GUARD(temp);
            if (rebRunThrows(
                out,  // <-- output cell
                updater, "binding of", temp, temp, rebQ(decayed)
            )){
                DROP_GC_GUARD(temp);
                fail (Error_No_Catch_For_Throw(TOP_FRAME));
            }
            DROP_GC_GUARD(temp);
        }

        if (steps_out and steps_out != GROUPS_OK) {
            if (steps_out != var)  // could be true if GROUP eval
                Derelativize(unwrap(steps_out), var, var_specifier);

            // If the variable is a compressed path form like `a.` then turn
            // it into a plain word.
            //
            mutable_HEART_BYTE(unwrap(steps_out)) = REB_WORD;
        }
        return false;  // did not throw
    }

    StackIndex base = TOP_INDEX;

    // If we have a sequence, then GROUP!s must be evaluated.  (If we're given
    // a steps array as input, then a GROUP! is literally meant as a
    // GROUP! by value).  These evaluations should only be allowed if the
    // caller has asked us to return steps.

    if (ANY_SEQUENCE(var)) {
        if (Not_Cell_Flag(var, SEQUENCE_HAS_NODE))  // compressed byte form
            fail (var);

        const Node* node1 = VAL_NODE1(var);
        if (Is_Node_A_Cell(node1)) {  // pair optimization
            assert(false);  // these don't exist yet
            fail (var);
        }

        switch (SER_FLAVOR(SER(node1))) {
          case FLAVOR_SYMBOL: {
            if (Get_Cell_Flag(var, REFINEMENT_LIKE))  // `/a` or `.a`
               goto set_target;

            // `a/` or `a.`
            //
            // !!! If this is a PATH!, it should error if it's not an action...
            // and if it's a TUPLE! it should error if it is an action.  Review.
            //
            goto set_target; }

          case FLAVOR_ARRAY:
            break;  // fall through

          default:
            panic (var);
        }

        Cell(const*) tail;
        Cell(const*) head = VAL_ARRAY_AT(&tail, var);
        Cell(const*) at;
        REBSPC *at_specifier = Derive_Specifier(var_specifier, var);
        for (at = head; at != tail; ++at) {
            if (IS_GROUP(at)) {
                if (not steps_out)
                    fail (Error_Bad_Get_Group_Raw(var));

                if (Do_Any_Array_At_Throws(temp, at, at_specifier)) {
                    Drop_Data_Stack_To(base);
                    return true;
                }
                Move_Cell(PUSH(), temp);

                // By convention, picker steps quote the first item if it was a
                // GROUP!.  It has to be somehow different because `('a).b` is
                // trying to pick B out of the WORD! a...not out of what is
                // fetched from A.  So if the convention is that the first item
                // of a "steps" block needs to be "fetched" we quote it.
                //
                if (at == head)
                    Quotify(TOP, 1);
            }
            else
                Derelativize(PUSH(), at, at_specifier);
        }
    }
    else if (IS_THE_BLOCK(var)) {
        Cell(const*) tail;
        Cell(const*) head = VAL_ARRAY_AT(&tail, var);
        Cell(const*) at;
        REBSPC *at_specifier = Derive_Specifier(var_specifier, var);
        for (at = head; at != tail; ++at)
            Derelativize(PUSH(), at, at_specifier);
    }
    else
        fail (var);

    DECLARE_LOCAL (writeback);
    PUSH_GC_GUARD(writeback);
    Init_None(writeback);  // needs to be GC safe

    PUSH_GC_GUARD(temp);

    StackIndex stackindex_top = TOP_INDEX;

  poke_again:
  blockscope {
    StackIndex stackindex = base + 1;

  blockscope {
    StackValue(*) at = Data_Stack_At(stackindex);
    if (IS_QUOTED(at)) {
        Copy_Cell(out, at);
        Unquotify(out, 1);
    }
    else if (IS_WORD(at)) {
        Copy_Cell(
            out,
            Lookup_Word_May_Fail(at, SPECIFIED)
        );
        if (Is_Isotope(out))
            fail (Error_Bad_Word_Get(at, out));
    }
    else
        fail (Copy_Cell(out, at));
  }

    ++stackindex;

    // Keep PICK-ing until you come to the last step.

    while (stackindex != stackindex_top) {
        Move_Cell(temp, out);
        Quotify(temp, 1);
        const void *ins = rebQ(cast(REBVAL*, Data_Stack_At(stackindex)));
        if (rebRunThrows(
            out,  // <-- output cell
            Lib(PICK_P), temp, ins
        )){
            DROP_GC_GUARD(temp);
            DROP_GC_GUARD(writeback);
            fail (Error_No_Catch_For_Throw(TOP_FRAME));  // don't let PICKs throw
        }
        ++stackindex;
    }

    // Now do a the final step, an update (often a poke)

    Move_Cell(temp, out);
    Quotify(temp, 1);
    const void *ins = rebQ(cast(REBVAL*, Data_Stack_At(stackindex)));
    if (rebRunThrows(
        out,  // <-- output cell
        updater, temp, ins, rebQ(decayed)
    )){
        DROP_GC_GUARD(temp);
        DROP_GC_GUARD(writeback);
        fail (Error_No_Catch_For_Throw(TOP_FRAME));  // don't let POKEs throw
    }

    // Subsequent updates become pokes, regardless of initial updater function

    updater = Lib(POKE_P);

    if (not Is_Nulled(out)) {
        Move_Cell(writeback, out);
        decayed = writeback;  // e.g. decayed setval

        --stackindex_top;

        if (stackindex_top != base + 1)
            goto poke_again;

        // can't use POKE, need to use SET
        if (not IS_WORD(Data_Stack_At(base + 1)))
            fail ("Can't POKE back immediate value unless it's to a WORD!");

        Copy_Cell(
            Sink_Word_May_Fail(Data_Stack_At(base + 1), SPECIFIED),
            decayed
        );
    }
  }

    DROP_GC_GUARD(temp);
    DROP_GC_GUARD(writeback);

    if (steps_out and steps_out != GROUPS_OK)
        Init_Block(unwrap(steps_out), Pop_Stack_Values(base));
    else
        Drop_Data_Stack_To(base);

    return false;
}


//
//  Set_Var_Core_Throws: C
//
bool Set_Var_Core_Throws(
    REBVAL *out,  // GC-safe cell to write steps to, or put thrown value
    option(REBVAL*) steps_out,  // no GROUP!s if nulled
    Cell(const*) var,  // e.g. v
    REBSPC *var_specifier,  // e.g. v_specifier
    const REBVAL *setval  // e.g. f->out (in the evaluator, right hand side)
){
    return Set_Var_Core_Updater_Throws(
        out,
        steps_out,
        var,
        var_specifier,
        setval,
        Lib(POKE_P)
    );
}


//
//  Set_Var_May_Fail: C
//
// Simpler function, where GROUP! is not ok...and there's no interest in
// preserving the "steps" to reuse in multiple assignments.
//
void Set_Var_May_Fail(
    Cell(const*) target,
    REBSPC *target_specifier,
    const REBVAL *setval
){
    option(REBVAL*) steps_out = nullptr;

    DECLARE_LOCAL (dummy);
    if (Set_Var_Core_Throws(dummy, steps_out, target, target_specifier, setval))
        fail (Error_No_Catch_For_Throw(TOP_FRAME));
}


//
//  set: native [
//
//  {Sets a word or path to specified value (see also: UNPACK)}
//
//      return: "Same value as input"
//          [<opt> <void> any-value!]
//      steps: "Allow GROUP! evals, returns block of reusable PICK/POKE steps"
//          [the-block! the-word! blackhole!]
//
//      target "Word or path (# means ignore assignment, just return value)"
//          [blackhole! any-word! any-sequence! any-group! any-block!]
//      ^value [<opt> <void> any-value!]
//  ]
//
DECLARE_NATIVE(set)
//
// 1. Internally we have an optimization for allowing you to say evaluations
//    with groups are okay, but you don't actually plan to use the steps.
//    But we aren't told if something is ~wanted~ vs. ~wanted-in-a-variable~
//    so we can't special-case with GROUPS_OK.  Review revealing this nuance
//    via a distinct state.
//
// 2. While the written value would decay if an isotope, the overall return
//    result is the same as was passed in:
//
//        >> set 'x match logic! false
//        == ~false~  ; isotope
//
//    The exception is ~void~ isotopes, which have no reified form, and are
//    returned as none (~) isotopes.
//
// 3. Plain POKE can't throw (e.g. from a GROUP!) because it won't evaluate
//    them.  However, we can get errors.  Confirm we only are raising errors
//    unless steps_out were passed.
{
    INCLUDE_PARAMS_OF_SET;

    REBVAL *steps = ARG(steps);
    REBVAL *target = ARG(target);
    REBVAL *v = ARG(value);

    if (WANTED(steps)) {
        // can't tell if GROUPS_OK would be sufficient, see [1]
    }
    else
        steps = nullptr;  // no GROUP! evals

    if (Is_Meta_Of_Void(v))
        Init_None(v);  // can't store the void--no reified form, see [2]
    else
        Meta_Unquotify(v);

    if (Set_Var_Core_Throws(OUT, steps, target, SPECIFIED, v)) {
        assert(steps or IS_ERROR(VAL_THROWN_LABEL(frame_)));  // see [3]
        return THROWN;
    }

    Proxy_Multi_Returns(frame_);
    return COPY(v);  // result does not decay unless void, see [2]
}


//
//  try: native [
//
//  {Turn null parameter failures and pure void to NULL}
//
//      return: [<opt> any-value!]
//      ^optional [<opt> <void> <fail> any-value!]
//  ]
//
DECLARE_NATIVE(try)
{
    INCLUDE_PARAMS_OF_TRY;

    REBVAL *v = ARG(optional);

    if (IS_ERROR(v)) {
        ERROR_VARS *vars = ERR_VARS(VAL_CONTEXT(v));
        if (
            IS_WORD(&vars->id)
            and VAL_WORD_ID(&vars->id) == SYM_TRY_IF_NULL_MEANT
        ){
            return COPY(SPECIFIC(&vars->line + 1));
        }
        return FAIL(VAL_CONTEXT(v));
    }

    if (Is_Nulled(v) or Is_Meta_Of_Void(v))
        return Init_Nulled(OUT);

    Meta_Unquotify(v);
    Decay_If_Isotope(v);  // !!! this decays isotopes, should it?

    return COPY(v);  // !!! also tolerates other isotopes, should it?
}


//
//  opt: native [
//
//  {Convert blanks to nulls, pass through most other values (See Also: TRY)}
//
//      return: "null on blank, ~null~ if input was NULL, or original value"
//          [<opt> any-value!]
//      ^optional [<opt> <void> any-value!]
//  ]
//
DECLARE_NATIVE(opt)
{
    INCLUDE_PARAMS_OF_OPT;

    REBVAL *v = ARG(optional);

    if (Is_Meta_Of_Void(v))
        return nullptr;  // so you can say `x: opt if condition [...]`

    if (Is_Meta_Of_None(v))
        return nullptr;  // handy for `opt switch x [1 [if false [<a>]]` cases

    Meta_Unquotify(v);
    Decay_If_Isotope(v);

    if (IS_BLANK(v))
        return nullptr;

    if (Is_Isotope(v))
        fail (Error_Bad_Isotope(v));

    // !!! Experimental: opting a null gives you an isotope.  You generally
    // don't put OPT on expressions you believe can be null, so this permits
    // creating a likely error in those cases.  To get around it, OPT TRY
    //
    if (Is_Nulled(v))
        return Init_Null_Isotope(OUT);

    return COPY(v);
}


//
//  resolve: native [
//
//  {Copy context by setting values in the target from those in the source.}
//
//      return: "Same as the target module"
//          [module!]
//      where [<blank> module!] "(modified)"
//      source [<blank> module!]
//      exports "Which words to export from the source"
//          [<blank> block!]
//  ]
//
DECLARE_NATIVE(resolve)
{
    INCLUDE_PARAMS_OF_RESOLVE;

    Context(*) where = VAL_CONTEXT(ARG(where));
    Context(*) source = VAL_CONTEXT(ARG(source));

    Cell(const*) tail;
    Cell(const*) v = VAL_ARRAY_AT(&tail, ARG(exports));
    for (; v != tail; ++v) {
        if (not IS_WORD(v))
            fail (ARG(exports));

        Symbol(const*) symbol = VAL_WORD_SYMBOL(v);

        bool strict = true;

        const REBVAL *src = MOD_VAR(source, symbol, strict);
        if (src == nullptr)
            fail (v);  // fail if unset value, also?

        REBVAL *dest = MOD_VAR(where, symbol, strict);
        if (dest != nullptr) {
            // Fail if found?
            RESET(dest);
        }
        else {
            dest = Append_Context(where, nullptr, symbol);
        }

        Copy_Cell(dest, src);
    }

    return COPY(ARG(where));
}


//
//  enfixed?: native [
//
//  {TRUE if looks up to a function and gets first argument before the call}
//
//      return: [logic!]
//      action [action!]
//  ]
//
DECLARE_NATIVE(enfixed_q)
{
    INCLUDE_PARAMS_OF_ENFIXED_Q;

    return Init_Logic(
        OUT,
        Get_Action_Flag(VAL_ACTION(ARG(action)), ENFIXED)
    );
}


//
//  enfix: native [
//
//  {For making enfix functions, e.g `+: enfixed :add` (copies)}
//
//      return: [action!]
//      action [action!]
//  ]
//
DECLARE_NATIVE(enfix)
{
    INCLUDE_PARAMS_OF_ENFIX;

    REBVAL *action = ARG(action);

    if (Get_Action_Flag(VAL_ACTION(action), ENFIXED))
        fail (
            "ACTION! is already enfixed (review callsite, enfix changed"
            " https://forum.rebol.info/t/1156"
        );

    Set_Action_Flag(VAL_ACTION(action), ENFIXED);

    return COPY(action);
}


//
//  semiquoted?: native [
//
//  {Discern if a function parameter came from an "active" evaluation.}
//
//      return: [logic!]
//      parameter [word!]
//  ]
//
DECLARE_NATIVE(semiquoted_q)
//
// This operation is somewhat dodgy.  So even though the flag is carried by
// all values, and could be generalized in the system somehow to query on
// anything--we don't.  It's strictly for function parameters, and
// even then it should be restricted to functions that have labeled
// themselves as absolutely needing to do this for ergonomic reasons.
{
    INCLUDE_PARAMS_OF_SEMIQUOTED_Q;

    // !!! TBD: Enforce this is a function parameter (specific binding branch
    // makes the test different, and easier)

    const REBVAL *var = Lookup_Word_May_Fail(ARG(parameter), SPECIFIED);

    return Init_Logic(OUT, Get_Cell_Flag(var, UNEVALUATED));
}


//
//  identity: native [
//
//  {Returns input value (https://en.wikipedia.org/wiki/Identity_function)}
//
//      return: [<opt> <void> any-value!]
//      ^value [<opt> <void> any-value!]
//  ]
//
DECLARE_NATIVE(identity) // sample uses: https://stackoverflow.com/q/3136338
{
    INCLUDE_PARAMS_OF_IDENTITY;

    REBVAL *v = ARG(value);

    if (Is_Meta_Of_Void(v))
        return VOID;

    return COPY(Meta_Unquotify(v));
}


//
//  free: native [
//
//  {Releases the underlying data of a value so it can no longer be accessed}
//
//      return: <none>
//      memory [<blank> any-series! any-context! handle!]
//  ]
//
DECLARE_NATIVE(free)
{
    INCLUDE_PARAMS_OF_FREE;

    REBVAL *v = ARG(memory);

    if (ANY_CONTEXT(v) or IS_HANDLE(v))
        fail ("FREE only implemented for ANY-SERIES! at the moment");

    REBSER *s = VAL_SERIES_ENSURE_MUTABLE(v);
    if (GET_SERIES_FLAG(s, INACCESSIBLE))
        fail ("Cannot FREE already freed series");

    Decay_Series(s);
    return NONE; // !!! Could return freed value
}


//
//  free?: native [
//
//  {Tells if data has been released with FREE}
//
//      return: "Returns false if value wouldn't be FREEable (e.g. LOGIC!)"
//          [logic!]
//      value [any-value!]
//  ]
//
DECLARE_NATIVE(free_q)
{
    INCLUDE_PARAMS_OF_FREE_Q;

    REBVAL *v = ARG(value);

    // All freeable values put their freeable series in the payload's "first".
    //
    if (Not_Cell_Flag(v, FIRST_IS_NODE))
        return Init_False(OUT);

    Node* n = VAL_NODE1(v);

    // If the node is not a series (e.g. a pairing), it cannot be freed (as
    // a freed version of a pairing is the same size as the pairing).
    //
    // !!! Technically speaking a PAIR! could be freed as an array could, it
    // would mean converting the node.  Review.
    //
    if (n == nullptr or Is_Node_A_Cell(n))
        return Init_False(OUT);

    return Init_Logic(OUT, GET_SERIES_FLAG(SER(n), INACCESSIBLE));
}


//
//  As_String_May_Fail: C
//
// Shared code from the refinement-bearing AS-TEXT and AS TEXT!.
//
bool Try_As_String(
    REBVAL *out,
    enum Reb_Kind new_kind,
    const REBVAL *v,
    REBLEN quotes,
    enum Reb_Strmode strmode
){
    assert(strmode == STRMODE_ALL_CODEPOINTS or strmode == STRMODE_NO_CR);

    if (ANY_WORD(v)) {  // ANY-WORD! can alias as a read only ANY-STRING!
        Init_Any_String(out, new_kind, VAL_WORD_SYMBOL(v));
        Inherit_Const(Quotify(out, quotes), v);
    }
    else if (IS_BINARY(v)) {  // If valid UTF-8, BINARY! aliases as ANY-STRING!
        Binary(const*) bin = VAL_BINARY(v);
        Size byteoffset = VAL_INDEX(v);

        // The position in the binary must correspond to an actual
        // codepoint boundary.  UTF-8 continuation byte is any byte where
        // top two bits are 10.
        //
        // !!! Should this be checked before or after the valid UTF-8?
        // Checking before keeps from constraining input on errors, but
        // may be misleading by suggesting a valid "codepoint" was seen.
        //
        const Byte* at_ptr = BIN_AT(bin, byteoffset);
        if (Is_Continuation_Byte_If_Utf8(*at_ptr))
            fail ("Index at codepoint to convert binary to ANY-STRING!");

        String(const*) str;
        REBLEN index;
        if (
            not IS_SER_UTF8(bin)
            or strmode != STRMODE_ALL_CODEPOINTS
        ){
            // If the binary wasn't created as a view on string data to
            // start with, there's no assurance that it's actually valid
            // UTF-8.  So we check it and cache the length if so.  We
            // can do this if it's locked, but not if it's just const...
            // because we may not have the right to.
            //
            // Regardless of aliasing, not using STRMODE_ALL_CODEPOINTS means
            // a valid UTF-8 string may have been edited to include CRs.
            //
            if (not Is_Series_Frozen(bin))
                if (Get_Cell_Flag(v, CONST))
                    fail (Error_Alias_Constrains_Raw());

            bool all_ascii = true;
            REBLEN num_codepoints = 0;

            index = 0;

            Size bytes_left = BIN_LEN(bin);
            const Byte* bp = BIN_HEAD(bin);
            for (; bytes_left > 0; --bytes_left, ++bp) {
                if (bp < at_ptr)
                    ++index;

                Codepoint c = *bp;
                if (c < 0x80)
                    Validate_Ascii_Byte(bp, strmode, BIN_HEAD(bin));
                else {
                    bp = Back_Scan_UTF8_Char(&c, bp, &bytes_left);
                    if (bp == NULL)  // !!! Should Back_Scan() fail?
                        fail (Error_Bad_Utf8_Raw());

                    all_ascii = false;
                }

                ++num_codepoints;
            }
            mutable_SER_FLAVOR(m_cast(Binary(*), bin)) = FLAVOR_STRING;
            str = STR(bin);

            TERM_STR_LEN_SIZE(
                m_cast(Raw_String*, str),  // legal for tweaking cached data
                num_codepoints,
                BIN_LEN(bin)
            );
            mutable_LINK(Bookmarks, m_cast(Binary(*), bin)) = nullptr;

            // !!! TBD: cache index/offset

            UNUSED(all_ascii);  // TBD: maintain cache
        }
        else {
            // !!! It's a string series, but or mapping acceleration is
            // from index to offset... not offset to index.  Recalculate
            // the slow way for now.

            str = STR(bin);
            index = 0;

            Utf8(const*) cp = STR_HEAD(str);
            REBLEN len = STR_LEN(str);
            while (index < len and cp != at_ptr) {
                ++index;
                cp = NEXT_STR(cp);
            }
        }

        Init_Any_String_At(out, new_kind, str, index);
        Inherit_Const(Quotify(out, quotes), v);
    }
    else if (IS_ISSUE(v)) {
        if (Get_Cell_Flag(v, ISSUE_HAS_NODE)) {
            assert(Is_Series_Frozen(VAL_STRING(v)));
            goto any_string;  // ISSUE! series must be immutable
        }

        // If payload of an ISSUE! lives in the cell itself, a read-only
        // series must be created for the data...because otherwise there isn't
        // room for an index (which ANY-STRING! needs).  For behavior parity
        // with if the payload *was* in the series, this alias must be frozen.

        REBLEN len;
        Size size;
        Utf8(const*) utf8 = VAL_UTF8_LEN_SIZE_AT(&len, &size, v);
        assert(size + 1 <= sizeof(PAYLOAD(Bytes, v).at_least_8));  // must fit

        String(*) str = Make_String_Core(size, SERIES_FLAGS_NONE);
        memcpy(SER_DATA(str), utf8, size + 1);  // +1 to include '\0'
        TERM_STR_LEN_SIZE(str, len, size);  // !!! SET_STR asserts size, review
        Freeze_Series(str);
        Init_Any_String(out, new_kind, str);
    }
    else if (ANY_STRING(v) or IS_URL(v)) {
      any_string:
        Copy_Cell(out, v);
        mutable_HEART_BYTE(out) = new_kind;
        Trust_Const(Quotify(out, quotes));
    }
    else
        return false;

    return true;
}


//
//  as: native [
//
//  {Aliases underlying data of one value to act as another of same class}
//
//      return: [
//          <opt> integer!
//          issue! url!
//          any-sequence! any-series! any-word!
//          frame! action!
//      ]
//      type [datatype!]
//      value [
//          <blank>
//          integer!
//          issue! url!
//          any-sequence! any-series! any-word! frame! action!
//      ]
//  ]
//
DECLARE_NATIVE(as)
{
    INCLUDE_PARAMS_OF_AS;

    REBVAL *v = ARG(value);

    REBVAL *t = ARG(type);
    enum Reb_Kind new_kind = VAL_TYPE_KIND(t);
    if (new_kind == VAL_TYPE(v))
        return COPY(v);

    switch (new_kind) {
      case REB_INTEGER: {
        if (not IS_CHAR(v))
            fail ("AS INTEGER! only supports what-were-CHAR! issues ATM");
        return Init_Integer(OUT, VAL_CHAR(v)); }

      case REB_BLOCK:
      case REB_GROUP:
        if (ANY_SEQUENCE(v)) {  // internals vary based on optimization
            if (Not_Cell_Flag(v, SEQUENCE_HAS_NODE))
                fail ("Array Conversions of byte-oriented sequences TBD");

            const Node* node1 = VAL_NODE1(v);
            assert(not (NODE_BYTE(node1) & NODE_BYTEMASK_0x01_CELL));

            switch (SER_FLAVOR(SER(node1))) {
              case FLAVOR_SYMBOL: {
                Array(*) a = Make_Array_Core(2, NODE_FLAG_MANAGED);
                if (Get_Cell_Flag(v, REFINEMENT_LIKE)) {
                    Init_Blank(ARR_AT(a, 0));
                    Copy_Cell(ARR_AT(a, 1), v);
                    mutable_HEART_BYTE(ARR_AT(a, 1)) = REB_WORD;
                }
                else {
                    Copy_Cell(ARR_AT(a, 0), v);
                    mutable_HEART_BYTE(ARR_AT(a, 0)) = REB_WORD;
                    Init_Blank(ARR_AT(a, 1));
                }
                SET_SERIES_LEN(a, 2);
                Init_Block(v, a);
                break; }

              case FLAVOR_ARRAY:
                assert(Is_Array_Frozen_Shallow(VAL_ARRAY(v)));
                assert(VAL_INDEX(v) == 0);
                mutable_HEART_BYTE(v) = REB_BLOCK;
                break;

              default:
                assert(false);
            }
        }
        else if (not ANY_ARRAY(v))
            goto bad_cast;

        goto adjust_v_kind;

      case REB_TUPLE:
      case REB_GET_TUPLE:
      case REB_SET_TUPLE:
      case REB_META_TUPLE:
      case REB_THE_TUPLE:
      case REB_PATH:
      case REB_GET_PATH:
      case REB_SET_PATH:
      case REB_META_PATH:
      case REB_THE_PATH:
        if (ANY_ARRAY(v)) {
            //
            // Even if we optimize the array, we don't want to give the
            // impression that we would not have frozen it.
            //
            if (not Is_Array_Frozen_Shallow(VAL_ARRAY(v)))
                Freeze_Array_Shallow(VAL_ARRAY_ENSURE_MUTABLE(v));

            if (Try_Init_Any_Sequence_At_Arraylike_Core(
                OUT,  // if failure, nulled if too short...else bad element
                new_kind,
                VAL_ARRAY(v),
                VAL_SPECIFIER(v),
                VAL_INDEX(v)
            )){
                return OUT;
            }

            fail (Error_Bad_Sequence_Init(OUT));
        }

        if (ANY_SEQUENCE(v)) {
            Copy_Cell(OUT, v);
            mutable_HEART_BYTE(OUT)
                = new_kind;
            return Trust_Const(OUT);
        }

        goto bad_cast;

      case REB_ISSUE: {
        if (IS_INTEGER(v)) {
            Context(*) error = Maybe_Init_Char(OUT, VAL_UINT32(v));
            if (error)
                return FAIL(error);
            return OUT;
        }

        if (ANY_STRING(v)) {
            REBLEN len;
            Size utf8_size = VAL_SIZE_LIMIT_AT(&len, v, UNLIMITED);

            if (utf8_size + 1 <= sizeof(PAYLOAD(Bytes, v).at_least_8)) {
                //
                // Payload can fit in a single issue cell.
                //
                Reset_Cell_Header_Untracked(
                    TRACK(OUT),
                    REB_ISSUE,
                    CELL_MASK_NONE
                );
                memcpy(
                    PAYLOAD(Bytes, OUT).at_least_8,
                    VAL_STRING_AT(v),
                    utf8_size + 1  // copy the '\0' terminator
                );
                EXTRA(Bytes, OUT).exactly_4[IDX_EXTRA_USED] = utf8_size;
                EXTRA(Bytes, OUT).exactly_4[IDX_EXTRA_LEN] = len;
            }
            else {
                if (not Try_As_String(
                    OUT,
                    REB_TEXT,
                    v,
                    0,  // no quotes
                    STRMODE_ALL_CODEPOINTS  // See AS-TEXT/STRICT for stricter
                )){
                    goto bad_cast;
                }
                Freeze_Series(VAL_SERIES(OUT));  // must be frozen
            }
            mutable_HEART_BYTE(OUT) = REB_ISSUE;
            return OUT;
        }

        goto bad_cast; }

      case REB_TEXT:
      case REB_TAG:
      case REB_FILE:
      case REB_URL:
      case REB_EMAIL:
        if (not Try_As_String(
            OUT,
            new_kind,
            v,
            0,  // no quotes
            STRMODE_ALL_CODEPOINTS  // See AS-TEXT/STRICT for stricter
        )){
            goto bad_cast;
        }
        return OUT;

      case REB_WORD:
      case REB_GET_WORD:
      case REB_SET_WORD:
      case REB_META_WORD:
      case REB_THE_WORD: {
        if (IS_ISSUE(v)) {
            if (Get_Cell_Flag(v, ISSUE_HAS_NODE)) {
                //
                // Handle the same way we'd handle any other read-only text
                // with a series allocation...e.g. reuse it if it's already
                // been validated as a WORD!, or mark it word-valid if it's
                // frozen and hasn't been marked yet.
                //
                // Note: We may jump back up to use the intern_utf8 branch if
                // that falls through.
                //
                goto any_string;
            }

            // Data that's just living in the payload needs to be handled
            // and validated as a WORD!.

          intern_utf8: {
            //
            // !!! This uses the same path as Scan_Word() to try and run
            // through the same validation.  Review efficiency.
            //
            Size size;
            Utf8(const*) utf8 = VAL_UTF8_SIZE_AT(&size, v);
            if (nullptr == Scan_Any_Word(OUT, new_kind, utf8, size))
                fail (Error_Bad_Char_Raw(v));

            return Inherit_Const(OUT, v);
          }
        }

        if (ANY_STRING(v)) {  // aliasing data as an ANY-WORD! freezes data
          any_string: {
            String(const*) s = VAL_STRING(v);

            if (not Is_Series_Frozen(s)) {
                //
                // We always force strings used with AS to frozen, so that the
                // effect of freezing doesn't appear to mystically happen just
                // in those cases where the efficient reuse works out.

                if (Get_Cell_Flag(v, CONST))
                    fail (Error_Alias_Constrains_Raw());

                Freeze_Series(VAL_SERIES(v));
            }

            if (VAL_INDEX(v) != 0)  // can't reuse non-head series AS WORD!
                goto intern_utf8;

            if (IS_SYMBOL(s)) {
                //
                // This string's content was already frozen and checked, e.g.
                // the string came from something like `as text! 'some-word`
            }
            else {
                // !!! If this spelling is already interned we'd like to
                // reuse the existing series, and if not we'd like to promote
                // this series to be the interned one.  This efficiency has
                // not yet been implemented, so we just intern it.
                //
                goto intern_utf8;
            }

            Init_Any_Word(OUT, new_kind, SYM(s));
            return Inherit_Const(OUT, v);
          }
        }

        if (IS_BINARY(v)) {
            if (VAL_INDEX(v) != 0)  // ANY-WORD! stores binding, not position
                fail ("Cannot convert BINARY! to WORD! unless at the head");

            // We have to permanently freeze the underlying series from any
            // mutation to use it in a WORD! (and also, may add STRING flag);
            //
            Binary(const*) bin = VAL_BINARY(v);
            if (not Is_Series_Frozen(bin))
                if (Get_Cell_Flag(v, CONST))  // can't freeze or add IS_STRING
                    fail (Error_Alias_Constrains_Raw());

            String(const*) str;
            if (IS_SYMBOL(bin))
                str = STR(bin);
            else {
                // !!! There isn't yet a mechanic for interning an existing
                // string series.  That requires refactoring.  It would need
                // to still check for invalid patterns for words (e.g.
                // invalid UTF-8 or even just internal spaces/etc.).
                //
                // We do a new interning for now.  But we do that interning
                // *before* freezing the old string, so that if there's an
                // error converting we don't add any constraints to the input.
                //
                Size size;
                const Byte* data = VAL_BINARY_SIZE_AT(&size, v);
                str = Intern_UTF8_Managed(data, size);

                // Constrain the input in the way it would be if we were doing
                // the more efficient reuse.
                //
                mutable_SER_FLAVOR(m_cast(Binary(*), bin)) = FLAVOR_STRING;
                Freeze_Series(bin);
            }

            return Inherit_Const(Init_Any_Word(OUT, new_kind, SYM(str)), v);
        }

        if (not ANY_WORD(v))
            goto bad_cast;
        goto adjust_v_kind; }

      case REB_BINARY: {
        if (IS_ISSUE(v)) {
            if (Get_Cell_Flag(v, ISSUE_HAS_NODE))
                goto any_string_as_binary;  // had a series allocation

            // Data lives in payload--make new frozen series for BINARY!

            Size size;
            Utf8(const*) utf8 = VAL_UTF8_SIZE_AT(&size, v);
            Binary(*) bin = Make_Binary_Core(size, NODE_FLAG_MANAGED);
            memcpy(BIN_HEAD(bin), utf8, size + 1);
            SET_SERIES_USED(bin, size);
            Freeze_Series(bin);
            Init_Binary(OUT, bin);
            return Inherit_Const(OUT, v);
        }

        if (ANY_WORD(v) or ANY_STRING(v)) {
          any_string_as_binary:
            Init_Binary_At(
                OUT,
                VAL_STRING(v),
                ANY_WORD(v) ? 0 : VAL_BYTEOFFSET(v)
            );
            return Inherit_Const(OUT, v);
        }

        fail (v); }

    case REB_ACTION: {
      if (IS_FRAME(v)) {
        //
        // We want AS ACTION! AS FRAME! of an action to be basically a no-op.
        // So that means that it uses the dispatcher and details it encoded
        // in the phase.  This means COPY of a FRAME! needs to create a new
        // action identity at that moment.  There is no Make_Action() here,
        // because all frame references to this frame are the same action.
        //
        assert(ACT_EXEMPLAR(VAL_FRAME_PHASE(v)) == VAL_CONTEXT(v));
        Freeze_Array_Shallow(CTX_VARLIST(VAL_CONTEXT(v)));
        return Init_Action(
            OUT,
            VAL_FRAME_PHASE(v),
            ANONYMOUS,  // see note, we might have stored this in varlist slot
            VAL_FRAME_BINDING(v)
        );
      }

      fail (v); }

      default:  // all applicable types should be handled above
        break;
    }

  bad_cast:
    fail (Error_Bad_Cast_Raw(v, ARG(type)));

  adjust_v_kind:
    //
    // Fallthrough for cases where changing the type byte and potentially
    // updating the quotes is enough.
    //
    Copy_Cell(OUT, v);
    mutable_HEART_BYTE(OUT) = new_kind;
    return Trust_Const(OUT);
}


//
//  as-text: native [
//      {AS TEXT! variant that may disallow CR LF sequences in BINARY! alias}
//
//      return: [<opt> text!]
//      value [<blank> any-value!]
//      /strict "Don't allow CR LF sequences in the alias"
//  ]
//
DECLARE_NATIVE(as_text)
{
    INCLUDE_PARAMS_OF_AS_TEXT;

    REBVAL *v = ARG(value);
    Dequotify(v);  // number of incoming quotes not relevant
    if (not ANY_SERIES(v) and not ANY_WORD(v) and not ANY_PATH(v))
        fail (PAR(value));

    const REBLEN quotes = 0;  // constant folding (see AS behavior)

    enum Reb_Kind new_kind = REB_TEXT;
    if (new_kind == VAL_TYPE(v) and not REF(strict))
        return COPY(Quotify(v, quotes));  // just may change quotes

    if (not Try_As_String(
        OUT,
        REB_TEXT,
        v,
        quotes,
        REF(strict) ? STRMODE_NO_CR : STRMODE_ALL_CODEPOINTS
    )){
        fail (Error_Bad_Cast_Raw(v, Datatype_From_Kind(REB_TEXT)));
    }

    return OUT;
}


//
//  aliases?: native [
//
//  {Return whether or not the underlying data of one value aliases another}
//
//      return: [logic!]
//      value1 [any-series!]
//      value2 [any-series!]
//  ]
//
DECLARE_NATIVE(aliases_q)
{
    INCLUDE_PARAMS_OF_ALIASES_Q;

    return Init_Logic(OUT, VAL_SERIES(ARG(value1)) == VAL_SERIES(ARG(value2)));
}


//
//  null?: native [
//
//  "Tells you if the argument is not a value"
//
//      return: [logic!]
//      optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(null_q)
{
    INCLUDE_PARAMS_OF_NULL_Q;

    return Init_Logic(OUT, Is_Nulled(ARG(optional)));
}


//
//  none?: native [
//
//  "Tells you if argument is a ~ isotope (not a ~ BAD-WORD!)"
//
//      return: [logic!]
//      ^optional [<opt> <void> any-value!]
//  ]
//
DECLARE_NATIVE(none_q)
{
    INCLUDE_PARAMS_OF_NONE_Q;

    return Init_Logic(OUT, Is_Meta_Of_None(ARG(optional)));
}


//
//  wanted?: native [
//
//  "Tells you if a multi-return output parameter is wanted by the caller"
//
//      return: [logic!]
//      output [word! tuple!]
//  ]
//
DECLARE_NATIVE(wanted_q)
{
    INCLUDE_PARAMS_OF_WANTED_Q;

    Value(*) output = ARG(output);

    goto initial_entry;

  initial_entry: {  //////////////////////////////////////////////////////////

    if (not Did_Get_Binding_Of(SPARE, output) or not IS_FRAME(SPARE))
        goto not_output_parameter;

    Action(*) phase = VAL_FRAME_PHASE(SPARE);

    Index index = VAL_WORD_INDEX(output);
    REBPAR* param = ACT_PARAM(phase, index);
    if (VAL_PARAM_CLASS(param) != PARAM_CLASS_OUTPUT)
        goto not_output_parameter;

    Value(*) var = CTX_VAR(VAL_CONTEXT(SPARE), index + 1);
    if (Is_None(var) or IS_BLANK(var))
        return Init_False(OUT);

    assert(IS_WORD(var) or IS_TUPLE(var) or Is_Blackhole(var));
    return Init_True(OUT);

} not_output_parameter: {  ///////////////////////////////////////////////////

    fail ("WANTED? can only be used on output parameters");
}}


//
//  void: native [
//
//  "0-arity comment (use ~void~ to make translucent isotopes)"
//
//      return: [<void>]
//  ]
//
DECLARE_NATIVE(void)
{
    INCLUDE_PARAMS_OF_VOID;

    return VOID;
}


//
//  void?: native [
//
//  "Tells you if argument is void"
//
//      return: [logic!]
//      ^optional [<opt> <void> any-value!]
//  ]
//
DECLARE_NATIVE(void_q)
{
    INCLUDE_PARAMS_OF_VOID_Q;

    return Init_Logic(OUT, Is_Meta_Of_Void(ARG(optional)));
}


//
//  blackhole?: native [
//
//  "Tells you if argument is a blackhole (#) or ~blackhole~ isotope"
//
//      return: [logic!]
//      ^optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(blackhole_q)
{
    INCLUDE_PARAMS_OF_BLACKHOLE_Q;

    REBVAL *v = ARG(optional);
    Meta_Unquotify(v);

    if (Is_Isotope(v)) {
        if (VAL_ISOTOPE_ID(v) == SYM_BLACKHOLE)
            return Init_True(OUT);
        return Init_False(OUT);
    }

    return Init_Logic(OUT, Is_Blackhole(v));
}

//
//  heavy: native [
//
//  {Make the heavy form of NULL (passes through all other values)}
//
//      return: [<void> <opt> any-value!]
//      ^optional [<opt> <void> any-value!]
//  ]
//
DECLARE_NATIVE(heavy) {
    INCLUDE_PARAMS_OF_HEAVY;

    REBVAL *v = ARG(optional);

    if (Is_Meta_Of_Void(v))
        return VOID;

    if (Is_Nulled(v))
        return Init_Null_Isotope(OUT);

    return COPY(Meta_Unquotify(v));
}


//
//  light: native [
//
//  {Make the light form of NULL (passes through all other values)}
//
//      return: [<opt> any-value!]
//      ^optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(light) {
    INCLUDE_PARAMS_OF_LIGHT;

    Move_Cell(OUT, Meta_Unquotify(ARG(optional)));
    Decay_If_Isotope(OUT);

    return OUT;
}


//
//  none: native [
//
//  {Make the "unfriendly" `~` isotope (variables holding it are called UNSET?)}
//
//      return: []
//  ]
//
DECLARE_NATIVE(none) {
    INCLUDE_PARAMS_OF_NONE;

    return NONE;
}


//
//  decay: native [
//
//  "Turn ~null~, ~blank~ and ~false~ isotopes into their corresponding values"
//
//      return: [<opt> any-value!]
//      ^optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(decay)
{
    INCLUDE_PARAMS_OF_DECAY;

    Move_Cell(OUT, Meta_Unquotify(ARG(optional)));
    Decay_If_Isotope(OUT);

    return OUT;
}


//
//  isotopify-if-falsey: native [
//
//  "Turn NULL, BLANK! and #[false] into their corresponding isotopes"
//
//      return: [<opt> <void> any-value!]
//      ^optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(isotopify_if_falsey)
{
    INCLUDE_PARAMS_OF_ISOTOPIFY_IF_FALSEY;

    REBVAL *v = ARG(optional);

    if (Is_Meta_Of_Void(v))
        return VOID;

    Meta_Unquotify(v);
    Isotopify_If_Falsey(v);

    return Move_Cell(OUT, v);
}

//
//  reify: native [
//
//  "Turn NULL and isotopes into plain BAD-WORD!s, pass through other values"
//
//      return: [any-value!]
//      ^optional [<opt> <void> any-value!]
//  ]
//
DECLARE_NATIVE(reify)
{
    INCLUDE_PARAMS_OF_REIFY;

    REBVAL *v = ARG(optional);

    if (Is_Nulled(v))
        return Init_Bad_Word(OUT, Canon(NULL));

    if (Is_Meta_Of_Void(v))
        return Init_Bad_Word(OUT, Canon(VOID));

    if (IS_BAD_WORD(v))  // e.g. the input was an isotope form
        return COPY(v);

    assert(IS_QUOTED(v));
    return COPY(Unquotify(v, 1));
}


//
//  something?: native [
//
//  "Returns FALSE if a argument isn't BLANK! or NULL"
//
//      return: [logic!]
//      value [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(something_q)
//
// !!! See remarks on `nothing?` regarding `~` and isotopes.
{
    INCLUDE_PARAMS_OF_SOMETHING_Q;

    return Init_Logic(OUT, not IS_NULLED_OR_BLANK(ARG(value)));
}
