//
//  File: %c-reframer.c
//  Summary: "Function that can transform arbitrary callsite functions"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2021 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the GNU Lesser General Public License (LGPL), Version 3.0.
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.en.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// REFRAMER allows one to define a function that does generalized transforms
// on the input (and output) of other functions.  Unlike ENCLOSE, it does not
// specify an exact function it does surgery on the frame of ahead of time.
// Instead, each invocation of the reframing action interacts with the
// instance that follows it at the callsite.
//
// A simple example is a function which removes quotes from the first
// parameter to a function, and adds them back for the result:
//
//     requote: reframer func [f [frame!]] [
//         p: first parameters of f
//         num-quotes: quotes of f/(p)
//
//         f/(p): noquote f/(p)
//
//         return quote/depth do f num-quotes
//     ]
//
//     >> item: just '''[a b c]
//     == '''[a b c]
//
//     >> requote append item <d>  ; append doesn't accept QUOTED! items
//     == '''[a b c <d>]   ; munging frame and result makes it seem to
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Enfix handling is not yet implemented, e.g. `requote '''1 + 2`
//
// * Because reframers need to know the function they are operating on, they
//   are unable to "see through" a GROUP! to get it, as a group could contain
//   multiple expressions.  So `requote (append item <d>)` cannot work.
//
// * If you "reframe a reframer" at the moment, you will not likely get what
//   you want...as the arguments you want to inspect will be compacted into
//   a frame argument.  It may be possible to make a "compound frame" that
//   captures the user-perceived combination of a reframer and what it's
//   reframing, but that would be technically difficult.
//

#include "sys-core.h"

enum {
    IDX_REFRAMER_SHIM = 1,  // action that can manipulate the reframed frame
    IDX_REFRAMER_PARAM_INDEX,  // index in shim that receives FRAME!
    IDX_REFRAMER_MAX
};


//
//  Make_Pushed_Frame_From_Action_Feed_May_Throw: C
//
// 1. The idea of creating a frame from an evaluative step which includes infix
//    as part of the step would ultimately have to make a composite frame that
//    captured the entire chain of the operation.  That's a heavy concept, but
//    for now we just try to get multiple returns to work which are part of
//    the evaluator and hence can do trickier things.
//
// 2. At the moment, Begin_Prefix_Action() marks the frame as having been
//    invoked...but since it didn't get managed it drops the flag in
//    Drop_Action().
//
//    !!! The flag is new, as a gambit to try and avoid copying frames for
//    DO-ing just in order to expire the old identity.  Under development.
//
// 3. The function did not actually execute, so no SPC(f) was never handed
//    out...the varlist should never have gotten managed.  So this context
//    can theoretically just be put back into the reuse list, or managed
//    and handed out for other purposes.  Caller's choice.
//
Frame(*) Make_Pushed_Frame_From_Action_Feed_May_Throw(
    REBVAL *out,
    Value(*) action,
    Feed(*) feed,
    StackIndex base,
    bool error_on_deferred
){
    Frame(*) f = Make_Frame(
        feed,
        FRAME_MASK_NONE  // FULFILL_ONLY added after Push_Action()
    );
    f->baseline.stack_base = base;  // incorporate refinements
    FRESHEN(out);
    Push_Frame(out, f);

    if (error_on_deferred)  // can't deal with ELSE/THEN, see [1]
        f->flags.bits |= ACTION_EXECUTOR_FLAG_ERROR_ON_DEFERRED_ENFIX;

    Push_Action(f, VAL_ACTION(action), VAL_ACTION_BINDING(action));
    Begin_Prefix_Action(f, VAL_ACTION_LABEL(action));

    Set_Executor_Flag(ACTION, f, FULFILL_ONLY);  // Push_Action() won't allow

    assert(FRM_BINDING(f) == VAL_ACTION_BINDING(action));  // no invocation

    if (Trampoline_With_Top_As_Root_Throws())
        return f;

    assert(Is_None(f->out));  // should only have gathered arguments

    assert(  // !!! new flag, see [2]
        Not_Subclass_Flag(VARLIST, f->varlist, FRAME_HAS_BEEN_INVOKED)
    );

    assert(not (f->flags.bits & ACTION_EXECUTOR_FLAG_FULFILL_ONLY));

    f->u.action.original = VAL_ACTION(action);
    INIT_FRM_PHASE(f, VAL_ACTION(action));  // Drop_Action() cleared, restore
    INIT_FRM_BINDING(f, VAL_ACTION_BINDING(action));

    assert(NOT_SERIES_FLAG(f->varlist, MANAGED));  // shouldn't be, see [3]

    return f;  // may not be at end or thrown, e.g. (x: does+ just y x = 'y)
}


//
//  Init_Invokable_From_Feed_Throws: C
//
// This builds a frame from a feed *as if* it were going to be used to call
// an action, but doesn't actually make the call.  Instead it leaves the
// varlist available for other purposes.
//
// If the next item in the feed is not a WORD! or PATH! that look up to an
// action (nor an ACTION! literally) then the output will be set to a QUOTED!
// version of what would be evaluated to.  So in the case of NULL, it will be
// a single quote of nothing.
//
bool Init_Invokable_From_Feed_Throws(
    REBVAL *out,
    option(Cell(const*)) first,  // override first value, vs. At_Feed(feed)
    Feed(*) feed,
    bool error_on_deferred  // if not planning to keep running, can't ELSE/THEN
){
    Cell(const*) v = first ? unwrap(first) : Try_At_Feed(feed);

    // !!! The case of `([x]: @)` wants to make something which when it
    // evaluates becomes invisible.  There's no QUOTED! value that can do
    // that, so if the feature is to be supported it needs to be VOID.
    //
    // Not all callers necessarily want to tolerate an end condition, so this
    // needs review.
    //
    if (v == nullptr) {  // no first, and feed was at end
        FRESHEN(out);
        return false;
    }

    // Unfortunately, this means that `[x y]: ^(do f)` and `[x y]: ^ do f`
    // can't work.  The problem is that you don't know how many expressions
    // will be involved in these cases, and the multi-return is a syntax trick
    // that can only work when interacting with one function, and even plain
    // groups break that guarantee.  Do meta values with e.g. `[^x y]: do f`.
    //
    if (ANY_GROUP(v))  // `requote (append [a b c] #d, <can't-work>)`
        fail ("Actions made with REFRAMER cannot work with GROUP!s");

    StackIndex base = TOP_INDEX;

    if (IS_WORD(v) or IS_TUPLE(v) or IS_PATH(v)) {
        DECLARE_LOCAL (steps);
        if (Get_Var_Push_Refinements_Throws(
            out,
            steps,
            v,
            FEED_SPECIFIER(feed)
        )){
            return true;
        }
    }
    else
        Derelativize(out, v, FEED_SPECIFIER(feed));

    if (not first)  // nothing passed in, so we used a feed value
        Fetch_Next_In_Feed(feed);  // we've seen it now

    if (not Is_Activation(out)) {
        Quotify(out, 1);
        return false;
    }

    // !!! Process_Action_Throws() calls Drop_Action() and loses the phase.
    // It probably shouldn't, but since it does we need the action afterward
    // to put the phase back.
    //
    DECLARE_LOCAL (action);
    Move_Cell(action, out);
    PUSH_GC_GUARD(action);

    option(String(const*)) label = VAL_ACTION_LABEL(action);

    Frame(*) f = Make_Pushed_Frame_From_Action_Feed_May_Throw(
        out,
        action,
        feed,
        base,
        error_on_deferred
    );

    if (Is_Throwing(f)) {  // signals threw
        Drop_Frame(f);
        DROP_GC_GUARD(action);
        return true;
    }

    // The exemplar may or may not be managed as of yet.  We want it
    // managed, but Push_Action() does not use ordinary series creation to
    // make its nodes, so manual ones don't wind up in the tracking list.
    //
    Action(*) act = VAL_ACTION(action);
    assert(FRM_BINDING(f) == VAL_ACTION_BINDING(action));

    assert(NOT_SERIES_FLAG(f->varlist, MANAGED));

    Array(*) varlist = f->varlist;
    f->varlist = nullptr;  // don't let Drop_Frame() free varlist (we want it)
    INIT_BONUS_KEYSOURCE(varlist, ACT_KEYLIST(act));  // disconnect from f
    Drop_Frame(f);
    DROP_GC_GUARD(action);

    SET_SERIES_FLAG(varlist, MANAGED); // can't use Manage_Series

    Init_Frame(out, CTX(varlist), label);
    return false;  // didn't throw
}


//
//  Init_Frame_From_Feed_Throws: C
//
// Making an invokable from a feed might return a QUOTED!, because that is
// more efficient (and truthful) than creating a FRAME! for the identity
// function.  However, MAKE FRAME! of a VARARGS! was an experimental feature
// that has to follow the rules of MAKE FRAME!...e.g. returning a frame.
// This converts QUOTED!s into frames for the identity function.
//
bool Init_Frame_From_Feed_Throws(
    REBVAL *out,
    Cell(const*) first,
    Feed(*) feed,
    bool error_on_deferred
){
    if (Init_Invokable_From_Feed_Throws(out, first, feed, error_on_deferred))
        return true;

    if (IS_FRAME(out))
        return false;

    assert(IS_QUOTED(out));
    Context(*) exemplar = Make_Context_For_Action(
        Lib(IDENTITY),
        TOP_INDEX,
        nullptr
    );

    Unquotify(Copy_Cell(CTX_VAR(exemplar, 2), out), 1);

    // Should we save the WORD! from a variable access to use as the name of
    // the identity alias?
    //
    option(Symbol(const*)) label = nullptr;
    Init_Frame(out, exemplar, label);
    return false;
}


// The REFRAMER native specializes out the FRAME! argument of the function
// being modified when it builds the interface.
//
// So the next thing to do is to fulfill the next function's frame without
// running it, in order to build a frame to put into that specialized slot.
// Then we run the reframer.
//
// !!! As a first cut we build on top of specialize, and look for the
// parameter by means of a particular labeled void.
//
Bounce Reframer_Dispatcher(Frame(*) f)
{
    Frame(*) frame_ = f;  // for RETURN macros

    Array(*) details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_REFRAMER_MAX);

    REBVAL* shim = DETAILS_AT(details, IDX_REFRAMER_SHIM);
    assert(IS_ACTION(shim));

    REBVAL* param_index = DETAILS_AT(details, IDX_REFRAMER_PARAM_INDEX);
    assert(IS_INTEGER(param_index));

    // First run ahead and make the frame we want from the feed.
    //
    // Note: We can't write the value directly into the arg (as this frame
    // may have been built by a higher level ADAPT or other function that
    // still holds references, and those references could be reachable by
    // code that runs to fulfill parameters...which could see partially
    // filled values).  And we don't want to overwrite f->out in case of
    // invisibility.  So the frame's spare cell is used.
    //
    bool error_on_deferred = true;
    if (Init_Invokable_From_Feed_Throws(
        SPARE,
        nullptr,
        f->feed,
        error_on_deferred
    )){
        return THROWN;
    }

    REBVAL *arg = FRM_ARG(f, VAL_INT32(param_index));
    Move_Cell(arg, SPARE);

    INIT_FRM_PHASE(f, VAL_ACTION(shim));
    INIT_FRM_BINDING(f, VAL_ACTION_BINDING(shim));

    return BOUNCE_REDO_CHECKED;  // the redo will use the updated phase & binding
}


//
//  reframer*: native [
//
//  {Make a function that manipulates an invocation at the callsite}
//
//      return: [activation!]
//      shim "The action that has a FRAME! (or QUOTED!) argument to supply"
//          [<unrun> action!]
//      /parameter "Shim parameter receiving the frame--defaults to last"
//          [word!]
//  ]
//
DECLARE_NATIVE(reframer_p)
{
    INCLUDE_PARAMS_OF_REFRAMER_P;

    mutable_QUOTE_BYTE(ARG(shim)) = UNQUOTED_1;  // remove isotope if present

    Action(*) shim = VAL_ACTION(ARG(shim));
    option(Symbol(const*)) label = VAL_ACTION_LABEL(ARG(shim));

    StackIndex base = TOP_INDEX;

    struct Reb_Binder binder;
    INIT_BINDER(&binder);
    Context(*) exemplar = Make_Context_For_Action_Push_Partials(
        ARG(shim),
        base,
        &binder,
        NONE_CELL
    );

    option(Context(*)) error = nullptr;  // can't fail() with binder in effect

    REBLEN param_index = 0;

    if (TOP_INDEX != base) {
        error = Error_User("REFRAMER can't use partial specializions ATM");
        goto cleanup_binder;
    }

  blockscope {
    const REBKEY *key;
    const REBPAR *param;

    if (REF(parameter)) {
        Symbol(const*) symbol = VAL_WORD_SYMBOL(ARG(parameter));
        param_index = Get_Binder_Index_Else_0(&binder, symbol);
        if (param_index == 0) {
            error = Error_No_Arg(label, symbol);
            goto cleanup_binder;
        }
        key = CTX_KEY(exemplar, param_index);
        param = cast_PAR(CTX_VAR(exemplar, param_index));
    }
    else {
        param = Last_Unspecialized_Param(&key, shim);
        param_index = param - ACT_PARAMS_HEAD(shim) + 1;
    }

    // Make sure the parameter is able to accept FRAME! arguments (the type
    // checking will ultimately use the same slot we overwrite here!)
    //
/*    if (not TYPE_CHECK(param, REB_FRAME)) {
        DECLARE_LOCAL (label_word);
        if (label)
            Init_Word(label_word, unwrap(label));
        else
            Init_Blank(label_word);

        DECLARE_LOCAL (param_word);
        Init_Word(param_word, KEY_SYMBOL(key));

        error = Error_Expect_Arg_Raw(
            label_word,
            Datatype_From_Kind(REB_FRAME),
            param_word
        );
        goto cleanup_binder;
    } */
  }

  cleanup_binder: {
    const REBKEY *tail;
    const REBKEY *key = ACT_KEYS(&tail, shim);
    const REBPAR *param = ACT_PARAMS_HEAD(shim);
    for (; key != tail; ++key, ++param) {
        if (Is_Specialized(param))
            continue;

        Symbol(const*) symbol = KEY_SYMBOL(key);
        REBLEN index = Remove_Binder_Index_Else_0(&binder, symbol);
        assert(index != 0);
        UNUSED(index);
    }

    SHUTDOWN_BINDER(&binder);

    if (error)  // once binder is cleaned up, safe to raise errors
        fail (unwrap(error));
  }

    // We need the dispatcher to be willing to start the reframing step even
    // though the frame to be processed isn't ready yet.  So we have to
    // specialize the argument with something that type checks.  It wants a
    // FRAME!, so temporarily fill it with the exemplar frame itself.
    //
    // !!! An expired frame would be better, or tweaking the argument so it
    // takes a void and giving it ~pending~; would make bugs more obvious.
    //
    REBVAL *var = CTX_VAR(exemplar, param_index);
    assert(Is_None(var));
    Copy_Cell(var, CTX_ARCHETYPE(exemplar));

    // Make action with enough space to store the implementation phase and
    // which parameter to fill with the *real* frame instance.
    //
    Manage_Series(CTX_VARLIST(exemplar));
    Action(*) reframer = Alloc_Action_From_Exemplar(
        exemplar,  // shim minus the frame argument
        &Reframer_Dispatcher,
        IDX_REFRAMER_MAX  // details array capacity => [shim, param_index]
    );

    Array(*) details = ACT_DETAILS(reframer);
    Copy_Cell(ARR_AT(details, IDX_REFRAMER_SHIM), ARG(shim));
    Init_Integer(ARR_AT(details, IDX_REFRAMER_PARAM_INDEX), param_index);

    return Init_Activation(OUT, reframer, label, UNBOUND);
}
