//
//  File: %n-function.c
//  Summary: "Generator for an ACTION! whose body is a block of user code"
//  Section: natives
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// FUNC is a common means for creating an action from a BLOCK! of code, with
// another block serving as the "spec" for parameters and HELP:
//
//     >> print-sum-twice: func [
//            {Prints the sum of two integers, and return the sum}
//            return: "The sum" [integer!]
//            x "First Value" [integer!]
//            y "Second Value" [integer!]
//            <local> sum
//        ][
//            sum: x + y
//            repeat 2 [print ["The sum is" sum]]
//            return sum
//        ]
//
//     >> print-sum-twice 10 20
//     The sum is 30
//     The sum is 30
//
// Ren-C brings new abilities not present in historical Rebol:
//
// * Return-type checking via `return: [...]` in the spec
//
// * Definitional RETURN, so that each FUNC has a local definition of its
//   own version of return specially bound to its invocation.
//
// * Specific binding of arguments, so that each instance of a recursion
//   can discern WORD!s from each recursion.  (In R3-Alpha, this was only
//   possible using CLOSURE which made a costly deep copy of the function's
//   body on every invocation.  Ren-C's method does not require a copy.)
//
// * Invisible functions (return: <void>) that vanish completely,
//   leaving whatever result was in the evaluation previous to the function
//   call as-is.
//
// * Refinements-as-their-own-arguments--which streamlines the evaluator,
//   saves memory, simplifies naming, and simplifies the FRAME! mechanics.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * R3-Alpha defined FUNC in terms of MAKE ACTION! on a block.  There was
//   no particular advantage to having an entry point to making functions
//   from a spec and body that put them both in the same block, so FUNC
//   serves as a more logical native entry point for that functionality.
//
// * While FUNC is intended to be an optimized native due to its commonality,
//   the belief is still that it should be possible to build an equivalent
//   (albeit slower) version in usermode out of other primitives.  The current
//   plan is that those primitives would be MAKE ACTION! from a FRAME!, and
//   being able to ADAPT a block of code into that frame.  This makes ADAPT
//   the more foundational operation for fusing interfaces with block bodies.
//

#include "sys-core.h"


//
//  Func_Dispatcher: C
//
// Puts a definitional return ACTION! in the RETURN slot of the frame, and
// runs the body block associated with this function.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// 1. FUNC(TION) does its evaluation into the SPARE cell, because the idea is
//    that arbitrary code may want to return void--even if the body itself
//    produces evaluative products.  This voidness can come from either a
//    `return: <void>` annotation in the spec, or code like `(return void)`.
//    A RETURN that uses a void trampoline UNWIND to pass by this dispatcher
//    will be able to be invisible because OUT will not have been corrupted.
//
// 2. A design point here is that Func_Dispatcher() does no typechecking, and
//    does not even request to catch THROWN values.  This makes it easier to
//    write usermode parallels to FUNC because there is no need for any special
//    CATCH logic--the usermode RETURN can simply use UNWIND and the trampoline
//    will catch the result.  Hence the full typechecking burden is on RETURN.
//
// 3. There is an exception made for tolerating the lack of a RETURN call for
//    the cases of `return: <none>` and `return: <void>`.  This has a little
//    bit of a negative side in that if someone is to hook the RETURN function,
//    it will not be called in these "fallout" cases.  It's deemed too ugly
//    to slip in a "hidden" call to RETURN for these cases, and too much of
//    a hassle to force people to put RETURN NONE or RETURN at the end.  So
//    this is the compromise chosen.
//
Bounce Func_Dispatcher(Frame(*) f)
{
    Frame(*) frame_ = f;  // so we can use OUT

    enum {
        ST_FUNC_INITIAL_ENTRY = STATE_0,
        ST_FUNC_BODY_EXECUTING
    };

    switch (STATE) {
      case ST_FUNC_INITIAL_ENTRY: goto initial_entry;
      case ST_FUNC_BODY_EXECUTING: goto body_finished_without_returning;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Action(*) phase = FRM_PHASE(f);
    Array(*) details = ACT_DETAILS(phase);
    Cell(*) body = ARR_AT(details, IDX_DETAILS_1);  // code to run
    assert(IS_BLOCK(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    assert(ACT_HAS_RETURN(phase));  // all FUNC have RETURN
    assert(KEY_SYM(ACT_KEYS_HEAD(phase)) == SYM_RETURN);

    REBVAL *cell = FRM_ARG(f, 1);
    assert(Is_None(cell));
    Init_Activation(
        cell,
        VAL_ACTION(Lib(DEFINITIONAL_RETURN)),
        Canon(RETURN),  // relabel (the RETURN in lib is a dummy action)
        CTX(f->varlist)  // bind this return to know where to return from
    );

    STATE = ST_FUNC_BODY_EXECUTING;

    assert(Is_Fresh(SPARE));
    return CONTINUE_CORE(
        SPARE,  // body evaluative result discarded, see [1]
        FRAME_MASK_NONE,  // no DISPATCHER_CATCHES, so RETURN skips, see [2]
        SPC(f->varlist), body
    );

} body_finished_without_returning: {  ////////////////////////////////////////

    const REBPAR *param = ACT_PARAMS_HEAD(FRM_PHASE(f));

    if (NOT_PARAM_FLAG(param, RETURN_TYPECHECKED)) {
        Init_None(OUT);  // none falls out of FUNC by default
        return Proxy_Multi_Returns(f);
    }

    if (GET_PARAM_FLAG(param, RETURN_VOID)) {
        // void, regardless of body result, see [3]
        Init_Void(OUT);
        return Proxy_Multi_Returns(f);
    }

    if (GET_PARAM_FLAG(param, RETURN_NONE)) {
        Init_None(OUT);  // none, regardless of body result, see [3]
        return Proxy_Multi_Returns(f);
    }

    fail ("Functions with RETURN: in spec must use RETURN to typecheck");
}}


//
//  Make_Interpreted_Action_May_Fail: C
//
// This digests the spec block into a `paramlist` for parameter descriptions,
// along with an associated `keylist` of the names of the parameters and
// various locals.  A separate object that uses the same keylist is made
// which maps the parameters to any descriptions that were in the spec.
//
// Due to the fact that the typesets in paramlists are "lossy" of information
// in the source, another object is currently created as well that maps the
// parameters to the BLOCK! of type information as it appears in the source.
// Attempts are being made to close the gap between that and the paramlist, so
// that separate arrays aren't needed for this closely related information:
//
// https://forum.rebol.info/t/1459
//
// The C function dispatcher that is used for the resulting ACTION! varies.
// For instance, if the body is empty then it picks a dispatcher that does
// not bother running the code.  And if there's no return type specified,
// a dispatcher that doesn't check the type is used.
//
// There is also a "definitional return" MKF_RETURN option used by FUNC, so
// the body will introduce a RETURN specific to each action invocation, thus
// acting more like:
//
//     return: make action! [
//         [{Returns a value from a function.} value [<opt> any-value!]]
//         [unwind/with (binding of 'return) :value]
//     ]
//     (body goes here)
//
// This pattern addresses "Definitional Return" in a way that does not need to
// build in RETURN as a language keyword in any specific form (in the sense
// that MAKE ACTION! does not itself require it).
//
// FUNC optimizes by not internally building or executing the equivalent body,
// but giving it back from BODY-OF.  This gives FUNC the edge to pretend to
// add containing code and simulate its effects, while really only holding
// onto the body the caller provided.
//
// While plain MAKE ACTION! has no RETURN, UNWIND can be used to exit frames
// but must be explicit about what frame is being exited.  This can be used
// by usermode generators that want to create something return-like.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// 1. At one time there were many optimized dispatchers for cases like
//    `func [...] []` which would not bother running empty blocks, and which
//    did not write into a temporary cell and then copy over the result in
//    a later phase.  The introduction of LAMBDA as an alternative generator
//    made these optimizations give diminishing returns, so they were all
//    eliminated (though they set useful precedent for varying dispatchers).
//
Action(*) Make_Interpreted_Action_May_Fail(
    const REBVAL *spec,
    const REBVAL *body,
    Flags mkf_flags,  // MKF_RETURN, etc.
    Dispatcher* dispatcher,
    REBLEN details_capacity
){
    assert(IS_BLOCK(spec) and IS_BLOCK(body));
    assert(details_capacity >= 1);  // relativized body put in details[0]

    Context(*) meta;
    Array(*) paramlist = Make_Paramlist_Managed_May_Fail(
        &meta,
        spec,
        &mkf_flags
    );

    Action(*) a = Make_Action(
        paramlist,
        nullptr,  // no partials
        dispatcher,
        details_capacity  // we fill in details[0], caller fills any extra
    );

    assert(ACT_META(a) == nullptr);
    mutable_ACT_META(a) = meta;

    Array(*) copy = Copy_And_Bind_Relative_Deep_Managed(
        body,  // new copy has locals bound relatively to the new action
        a,
        VAR_VISIBILITY_ALL // we created exemplar, see all!
    );

    // Favor the spec first, then the body, for file and line information.
    //
    if (Get_Subclass_Flag(ARRAY, VAL_ARRAY(spec), HAS_FILE_LINE_UNMASKED)) {
        mutable_LINK(Filename, copy) = LINK(Filename, VAL_ARRAY(spec));
        copy->misc.line = VAL_ARRAY(spec)->misc.line;
        Set_Subclass_Flag(ARRAY, copy, HAS_FILE_LINE_UNMASKED);
    }
    else if (
        Get_Subclass_Flag(ARRAY, VAL_ARRAY(body), HAS_FILE_LINE_UNMASKED)
    ){
        mutable_LINK(Filename, copy) = LINK(Filename, VAL_ARRAY(body));
        copy->misc.line = VAL_ARRAY(body)->misc.line;
        Set_Subclass_Flag(ARRAY, copy, HAS_FILE_LINE_UNMASKED);
    }
    else {
        // Ideally all source series should have a file and line numbering
        // At the moment, if a function is created in the body of another
        // function it doesn't work...trying to fix that.
    }

    // Save the relativized body in the action's details block.  Since it is
    // a Cell(*) and not a REBVAL*, the dispatcher must combine it with a
    // running frame instance (the Frame(*) received by the dispatcher) before
    // executing the interpreted code.
    //
    Array(*) details = ACT_DETAILS(a);
    Cell(*) rebound = Init_Relative_Block(
        ARR_AT(details, IDX_NATIVE_BODY),
        a,
        copy
    );

    // Capture the mutability flag that was in effect when this action was
    // created.  This allows the following to work:
    //
    //    >> do mutable [f: function [] [b: [1 2 3] clear b]]
    //    >> f
    //    == []
    //
    // So even though the invocation is outside the mutable section, we have
    // a memory that it was created under those rules.  (It's better to do
    // this based on the frame in effect than by looking at the CONST flag of
    // the incoming body block, because otherwise ordinary Ren-C functions
    // whose bodies were created from dynamic code would have mutable bodies
    // by default--which is not a desirable consequence from merely building
    // the body dynamically.)
    //
    // Note: besides the general concerns about mutability-by-default, when
    // functions are allowed to modify their bodies with words relative to
    // their frame, the words would refer to that specific recursion...and not
    // get picked up by other recursions that see the common structure.  This
    // means compatibility would be with the behavior of R3-Alpha CLOSURE,
    // not with R3-Alpha FUNCTION.
    //
    if (Get_Cell_Flag(body, CONST))
        Set_Cell_Flag(rebound, CONST);  // Inherit_Const() would need REBVAL*

    return a;
}


//
//  func*: native [
//
//  "Defines an ACTION! with given spec and body"
//
//      return: [activation?]
//      spec "Help string (opt) followed by arg words (and opt type + string)"
//          [block!]
//      body "Code implementing the function--use RETURN to yield a result"
//          [block!]
//  ]
//
DECLARE_NATIVE(func_p)
{
    INCLUDE_PARAMS_OF_FUNC_P;

    REBVAL *spec = ARG(spec);
    REBVAL *body = ARG(body);

    Action(*) func = Make_Interpreted_Action_May_Fail(
        spec,
        body,
        MKF_RETURN | MKF_KEYWORDS,
        &Func_Dispatcher,
        1 + IDX_DETAILS_1  // archetype and one array slot (will be filled)
    );

    return Init_Activation(OUT, func, ANONYMOUS, UNBOUND);
}


//
//  endable?: native [
//
//  {Tell whether a parameter is registered as <end> or not}
//
//      return: [logic?]
//      parameter [word!]
//  ]
//
DECLARE_NATIVE(endable_q)
//
// !!! The general mechanics by which parameter properties are extracted have
// not been designed.  This extraction feature was added to support making
// semi-"variadic" combinators in UPARSE, but better is needed.
{
    INCLUDE_PARAMS_OF_ENDABLE_Q;

    REBVAL *v = ARG(parameter);

    if (not Did_Get_Binding_Of(SPARE, v))
        fail (PARAM(parameter));

    if (not IS_FRAME(SPARE))
        fail ("ENDABLE? requires a WORD! bound into a FRAME! at present");

    Context(*) ctx = VAL_CONTEXT(SPARE);
    Action(*) act = CTX_FRAME_ACTION(ctx);

    REBPAR *param = ACT_PARAM(act, VAL_WORD_INDEX(v));
    bool endable = GET_PARAM_FLAG(param, ENDABLE);

    return Init_Logic(OUT, endable);
}


//
//  skippable?: native [
//
//  {Tell whether a parameter is registered as <skip> or not}
//
//      return: [logic?]
//      parameter [word!]
//  ]
//
DECLARE_NATIVE(skippable_q)
//
// !!! The general mechanics by which parameter properties are extracted have
// not been designed.  This extraction feature was added to support making
// combinators that could <skip> arguments in UPARSE, but better is needed.
{
    INCLUDE_PARAMS_OF_SKIPPABLE_Q;

    REBVAL *v = ARG(parameter);

    if (not Did_Get_Binding_Of(SPARE, v))
        fail (PARAM(parameter));

    if (not IS_FRAME(SPARE))
        fail ("SKIPPABLE? requires a WORD! bound into a FRAME! at present");

    Context(*) ctx = VAL_CONTEXT(SPARE);
    Action(*) act = CTX_FRAME_ACTION(ctx);

    REBPAR *param = ACT_PARAM(act, VAL_WORD_INDEX(v));
    bool skippable = GET_PARAM_FLAG(param, SKIPPABLE);

    return Init_Logic(OUT, skippable);
}


//
//  Init_Thrown_Unwind_Value: C
//
// This routine generates a thrown signal that can be used to indicate a
// desire to jump to a particular level in the stack with a return value.
// It is used in the implementation of the UNWIND native.
//
// See notes is %sys-frame.h about how there is no actual REB_THROWN type.
//
Bounce Init_Thrown_Unwind_Value(
    Frame(*) frame_,
    const REBVAL *level, // FRAME!, ACTION! (or INTEGER! relative to frame)
    const REBVAL *value,
    Frame(*) target // required if level is INTEGER! or ACTION!
) {
    DECLARE_LOCAL (label);
    Copy_Cell(label, Lib(UNWIND));

    if (IS_FRAME(level)) {
        TG_Unwind_Frame = CTX_FRAME_IF_ON_STACK(VAL_CONTEXT(level));
    }
    else if (IS_INTEGER(level)) {
        REBLEN count = VAL_INT32(level);
        if (count <= 0)
            fail (Error_Invalid_Exit_Raw());

        Frame(*) f = target->prior;
        for (; true; f = f->prior) {
            if (f == BOTTOM_FRAME)
                fail (Error_Invalid_Exit_Raw());

            if (not Is_Action_Frame(f))
                continue; // only exit functions

            if (Is_Action_Frame_Fulfilling(f))
                continue; // not ready to exit

            --count;
            if (count == 0) {
                TG_Unwind_Frame = f;
                break;
            }
        }
    }
    else {
        assert(IS_ACTION(level));

        Frame(*) f = target->prior;
        for (; true; f = f->prior) {
            if (f == BOTTOM_FRAME)
                fail (Error_Invalid_Exit_Raw());

            if (not Is_Action_Frame(f))
                continue; // only exit functions

            if (Is_Action_Frame_Fulfilling(f))
                continue; // not ready to exit

            if (VAL_ACTION(level) == f->u.action.original) {
                TG_Unwind_Frame = f;
                break;
            }
        }
    }

    return Init_Thrown_With_Label(frame_, value, label);
}


//
//  unwind: native [
//
//  {Jump up the stack to return from a specific frame or call.}
//
//      return: []  ; !!! notation for divergent functions?
//      level "Frame, action, or index to exit from"
//          [frame! action! integer!]
//      ^result "Result for enclosing state"
//          [<opt> <end> <fail> <pack> any-value!]
//  ]
//
DECLARE_NATIVE(unwind)
//
// UNWIND is implemented via a throw that bubbles through the stack.  Using
// UNWIND's action REBVAL with a target `binding` field is the protocol
// understood by Eval_Core to catch a throw itself.
//
// !!! Allowing to pass an INTEGER! to jump from a function based on its
// BACKTRACE number is a bit low-level, and perhaps should be restricted to
// a debugging mode (though it is a useful tool in "code golf").
//
// !!! This might be a little more natural if the label of the throw was a
// FRAME! value.  But that also would mean throws named by frames couldn't be
// taken advantage by the user for other features, while this only takes one
// function away.  (Or, perhaps Isotope frames could be used?)
{
    INCLUDE_PARAMS_OF_UNWIND;

    REBVAL *level = ARG(level);
    REBVAL *v = ARG(result);

    Meta_Unquotify(v);

    return Init_Thrown_Unwind_Value(FRAME, level, v, frame_);
}


//
//  definitional-return: native [
//
//  {RETURN, giving a result to the caller}
//
//      return: []  ; !!! notation for "divergent?"
//      ^value [<opt> <void> <fail> <pack> any-value!]
//      /only "Do not do proxying of output variables, just return argument"
//  ]
//
DECLARE_NATIVE(definitional_return)
//
// Returns in Ren-C are functions that are aware of the function they return
// to.  So the dispatchers for functions that provide return e.g. FUNC will
// actually use an instance of this native, and poke a binding into it to
// identify the action.
//
// This means the RETURN that is in LIB is actually just a dummy function
// which you will bind to and run if there is no definitional return in effect.
//
// (The cached name of the value for this native is set to RETURN by the
// dispatchers that use it, which might be a bit confusing.)
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_RETURN;

    REBVAL *v = ARG(value);
    Frame(*) f = frame_;  // frame of this RETURN call (implicit DECLARE_NATIVE arg)

    // Each ACTION! cell for RETURN has a piece of information in it that can
    // can be unique (the binding).  When invoked, that binding is held in the
    // Frame(*).  This generic RETURN dispatcher interprets that binding as the
    // FRAME! which this instance is specifically intended to return from.
    //
    Context(*) f_binding = FRM_BINDING(f);
    if (not f_binding)
        fail (Error_Return_Archetype_Raw());  // must have binding to jump to

    Frame(*) target_frame = CTX_FRAME_MAY_FAIL(f_binding);

    // !!! We only have a Frame(*) via the binding.  We don't have distinct
    // knowledge about exactly which "phase" the original RETURN was
    // connected to.  As a practical matter, it can only return from the
    // current phase (what other option would it have, any other phase is
    // either not running yet or has already finished!).  But this means the
    // `target_frame->phase` may be somewhat incidental to which phase the
    // RETURN originated from...and if phases were allowed different return
    // typesets, then that means the typechecking could be somewhat random.
    //
    // Without creating a unique tracking entity for which phase was
    // intended for the return, it's not known which phase the return is
    // for.  So the return type checking is done on the basis of the
    // underlying function.  So compositions that share frames cannot expand
    // the return type set.  The unfortunate upshot of this is--for instance--
    // that an ENCLOSE'd function can't return any types the original function
    // could not.  :-(
    //
    Action(*) target_fun = target_frame->u.action.original;

    // Defininitional returns are "locals"--there's no argument type check.
    // So TYPESET! bits in the RETURN param are used for legal return types.
    //
    const REBPAR *param = ACT_PARAMS_HEAD(target_fun);
    assert(KEY_SYM(ACT_KEYS_HEAD(target_fun)) == SYM_RETURN);

    if (Is_Meta_Of_Raised(v)) {
        Unquasify(v);
        Raisify(v);  // Meta_Unquotify won't do this, it fail()'s
        goto skip_type_check;
    }

    if (Is_Meta_Of_Nihil(v)) {  // RETURN NIHIL
        if (GET_PARAM_FLAG(param, VANISHABLE)) {
            Init_Nihil(v);
            goto skip_type_check;
        }

        // !!! Treating a return of NIHIL as a return of NONE helps some
        // scenarios, for instance piping UPARSE combinators which do not
        // want to propagate pure invisibility.  The idea should be reviewed
        // to see if VOID makes more sense...but start with a more "ornery"
        // value to see how it shapes up.
        //
        Init_None(v);
    }
    else {
        // Safe to unquotify for type checking
        Meta_Unquotify(v);
    }

    // Check type NOW instead of waiting and letting Eval_Core()
    // check it.  Reasoning is that the error can indicate the callsite,
    // e.g. the point where `return badly-typed-value` happened.
    //
    // !!! In the userspace formulation of this abstraction, it indicates
    // it's not RETURN's type signature that is constrained, as if it were
    // then RETURN would be implicated in the error.  Instead, RETURN must
    // take [<opt> any-value!] as its argument, and then report the error
    // itself...implicating the frame (in a way parallel to this native).
    //
    if (GET_PARAM_FLAG(param, RETURN_NONE) and not Is_None(v))
        fail ("If RETURN: <none> is in a function spec, RETURN NONE only");

    if (not TYPE_CHECK(param, v)) {
        Decay_If_Unstable(v);
        if (not TYPE_CHECK(param, v))
            fail (Error_Bad_Return_Type(target_frame, v));
    }

  skip_type_check: {  ////////////////////////////////////////////////////////

    DECLARE_LOCAL (label);
    Copy_Cell(label, Lib(UNWIND)); // see Make_Thrown_Unwind_Value
    TG_Unwind_Frame = target_frame;

    if (not Is_Raised(v) and not REF(only))
        Proxy_Multi_Returns_Core(target_frame, v);

    return Init_Thrown_With_Label(FRAME, v, label);
  }
}


//
//  inherit-meta: native [
//
//  {Copy help information from the original function to the derived function}
//
//      return: "Same as derived (assists in efficient chaining)"
//          [activation!]
//      derived [<unrun> action!]
//      original [<unrun> action!]
//      /augment "Additional spec information to scan"
//          [block!]
//  ]
//
DECLARE_NATIVE(inherit_meta)
{
    INCLUDE_PARAMS_OF_INHERIT_META;

    REBVAL *derived = ARG(derived);
    mutable_QUOTE_BYTE(derived) = ISOTOPE_0;  // ensure return is isotope

    const REBVAL *original = ARG(original);

    UNUSED(ARG(augment));  // !!! not yet implemented

    Context(*) m1 = ACT_META(VAL_ACTION(original));
    if (not m1)  // nothing to copy
        return COPY(ARG(derived));

    // Often the derived function won't have its own meta information yet.  But
    // if it was created via an AUGMENT, it will have some...only the notes
    // and types for the added parameters, the others will be NULL.
    //
    Context(*) m2 = ACT_META(VAL_ACTION(derived));
    if (not m2) {  // doesn't have its own information
        m2 = Copy_Context_Shallow_Managed(VAL_CONTEXT(Root_Action_Meta));
        mutable_ACT_META(VAL_ACTION(derived)) = m2;
    }

    // By default, inherit description (though ideally, they should have
    // changed it to explain why it's different).
    //
    option(Value(*)) description2 = Select_Symbol_In_Context(
        CTX_ARCHETYPE(m2),
        Canon(DESCRIPTION)
    );
    if (description2 and Is_Nulled(unwrap(description2))) {
        option(Value(*)) description1 = Select_Symbol_In_Context(
            CTX_ARCHETYPE(m1),
            Canon(DESCRIPTION)
        );
        if (description1)
            Copy_Cell(unwrap(description2), unwrap(description1));
    }

    REBLEN which = 0;
    option(SymId) syms[] = {SYM_PARAMETER_NOTES, SYM_PARAMETER_TYPES, SYM_0};

    for (; syms[which] != 0; ++which) {
        Value(*) val1 = try_unwrap(Select_Symbol_In_Context(
            CTX_ARCHETYPE(m1),
            Canon_Symbol(unwrap(syms[which]))
        ));
        if (not val1 or Is_Nulled(val1) or Is_Void(val1))
            continue;  // nothing to inherit from
        if (not ANY_CONTEXT(val1))
            fail ("Expected context in original meta information");

        Context(*) ctx1 = VAL_CONTEXT(val1);

        Value(*) val2 = try_unwrap(Select_Symbol_In_Context(
            CTX_ARCHETYPE(m2),
            Canon_Symbol(unwrap(syms[which]))
        ));
        if (not val2)
            continue;

        Context(*) ctx2;
        if (Is_Nulled(val2) or Is_Void(val2)) {
            ctx2 = Make_Context_For_Action(
                derived,  // the action
                TOP_INDEX,  // would weave in refinements pushed (none apply)
                nullptr  // !!! review, use fast map from names to indices
            );
            Init_Frame(val2, ctx2, ANONYMOUS);
        }
        else if (ANY_CONTEXT(val2)) {  // already had context (e.g. augment)
            ctx2 = VAL_CONTEXT(val2);
        }
        else
            fail ("Expected context in derived meta information");

        EVARS e;
        Init_Evars(&e, val2);

        while (Did_Advance_Evars(&e)) {
            if (not Is_Void(e.var) and not Is_Nulled(e.var))
                continue;  // already set to something

            option(Value(*)) slot = Select_Symbol_In_Context(
                CTX_ARCHETYPE(ctx1),
                KEY_SYMBOL(e.key)
            );
            if (slot)
                Copy_Cell(e.var, unwrap(slot));
            else
                Init_Nulled(e.var);  // don't want to leave as `~` isotope
        }

        Shutdown_Evars(&e);
    }

    return COPY(ARG(derived));
}
