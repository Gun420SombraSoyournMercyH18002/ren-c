//
//  File: %c-eval.c
//  Summary: "Central Interpreter Evaluator"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2022 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// This file contains code for the `Evaluator_Executor()`.  It is responsible
// for the typical interpretation of BLOCK! or GROUP!, in terms of giving
// sequences like `x: 1 + 2` a meaning for how SET-WORD! or INTEGER! behaves.
//
// By design the evaluator is not recursive at the C level--it is "stackless".
// At points where a sub-expression must be evaluated in a new frame, it will
// heap-allocate that frame and then do a C `return` of BOUNCE_CONTINUE.
// Processing then goes through the "Trampoline" (see %c-trampoline.c), which
// later re-enters the suspended frame's executor with the result.  Setting
// the frame's STATE byte prior to suspension is a common way of letting a
// frame know where to pick up from when it left off.
//
// When it encounters something that needs to be handled as a function
// application, it defers to %c-action.c for the Action_Executor().  The
// action gets its own frame.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Evaluator_Executor() is LONG.  That's largely on purpose.  Breaking it
//   into functions would add overhead (in the debug build if not also release
//   builds) and prevent interesting tricks and optimizations.  It is
//   separated into sections, and the invariants in each section are made
//   clear with comments and asserts.
//
// * See %d-eval.c for more detailed assertions of the preconditions,
//   postconditions, and state...which are broken out to help keep this file
//   a more manageable length.
//
// * The evaluator only moves forward, and operates on a strict window of
//   visibility of two elements at a time (current position and "lookback").
//   See `Reb_Feed` for the code that provides this abstraction over Ren-C
//   arrays as well as C va_list.
//

#include "sys-core.h"


// The frame contains a "feed" whose ->value typically represents a "current"
// step in the feed.  But the evaluator is organized in a way that the
// notion of what is "current" can get out of sync with the feed.  An example
// would be when a SET-WORD! evaluates its right hand side, causing the feed
// to advance an arbitrary amount.
//
// So the frame has its own frame state for tracking the "current" position,
// and maintains the optional cache of what the fetched value of that is.
// These macros help make the code less ambiguous.
//
#undef At_Frame
#undef f_gotten
#define f_next              cast(const Reb_Cell*, f->feed->p)
#define f_next_gotten       f->feed->gotten
#define f_current           f->u.eval.current
#define f_current_gotten    f->u.eval.current_gotten

// In debug builds, the KIND_BYTE() calls enforce cell validity...but slow
// things down a little.  So we only use the checked version in the main
// switch statement.  This abbreviation is also shorter and more legible.
//
#define kind_current VAL_TYPE_UNCHECKED(f_current)


#define frame_ f  // for OUT, SPARE, STATE macros

// We make the macro for getting specifier a bit more complex here, to
// account for reevaluation.
//
// https://forum.rebol.info/t/should-reevaluate-apply-let-bindings/1521
//
#undef f_specifier
#define f_specifier \
    (STATE == ST_EVALUATOR_REEVALUATING ? SPECIFIED : FEED_SPECIFIER(f->feed))


// In the early development of FRAME!, the Frame(*) for evaluating across a
// block was reused for each ACTION! call.  Since no more than one action was
// running at a time, this seemed to work.  However, that didn't allow for
// a separate "reified" entry for users to point at.  While giving each
// action its own Frame(*) has performance downsides, it makes the objects
// correspond to what they are...and may be better for cohering the "executor"
// pattern by making it possible to use a constant executor per frame.
//
// !!! Evil Macro, repeats parent!
//
STATIC_ASSERT(
    EVAL_EXECUTOR_FLAG_FULFILLING_ARG
    == ACTION_EXECUTOR_FLAG_FULFILLING_ARG
);

STATIC_ASSERT(
    EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_TUPLE
    == ACTION_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_TUPLE
);

#define Make_Action_Subframe(parent) \
    Make_Frame((parent)->feed, \
        FRAME_FLAG_MAYBE_STALE | FRAME_FLAG_FAILURE_RESULT_OK \
        | ((parent)->flags.bits \
            & (EVAL_EXECUTOR_FLAG_FULFILLING_ARG \
                | EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_TUPLE)))


#if DEBUG_EXPIRED_LOOKBACK
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (f->feed->stress != nullptr)
#else
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (f_current == &f->feed->lookback)
#endif


//
// SET-WORD! and SET-TUPLE! want to do roughly the same thing as the first step
// of their evaluation.  They evaluate the right hand side into f->out.
//
// What makes this slightly complicated is that the current value may be in
// a place that doing a Fetch_Next_In_Frame() might corrupt it.  This could
// be accounted for by pushing the value to some other stack--e.g. the data
// stack.  That would mean `x: y: z: ...` would only accrue one cell of
// space for each level instead of one whole frame.
//
// But for the moment, a new frame is used each time.
//
//////////////////////////////////////////////////////////////////////////////
//
// 1. Note that any enfix quoting operators that would quote backwards to see
//    the `x:` would have intercepted it during a lookahead...pre-empting any
//    of this code.
//
// 2. Using a SET-XXX! means you always have at least two elements; it's like
//    an arity-1 function.  `1 + x: whatever ...`.  This overrides the no
//    lookahead behavior flag right up front.
//
// 3. If the evaluation step doesn't produce any output, we want the variable
//    to be set to NULL, but the voidness to propagate:
//
//        >> 1 + 2 x: comment "hi"
//        == 3
//
//        >> x
//        == null
//
//    Originally this would unset x, and propagate a none.  But once isotope
//    assignments were disallowed, that ruined `x: y: if condition [...]`
//    where we want `x = y` after that.  Cases like `return [# result]: ...`
//    presented a challenge for how not naming a variable would want to
//    preserve the value, so turning voids to nones didn't feel good there.
//    The general `x: case [...]` being able to leave X as NULL after no
//    match was also a persuasion point, along with the tight link between
//    NULL and VOID by triggering ELSE and linked by ^META state.  In the
//    balance it is simply too good to worry about the potentially misleading
//    state of X after the assignment.  So it was changed.
//
// 4. If current is pointing into the lookback buffer or the fetched value,
//    it will not work to hold onto this pointer while evaluating the right
//    hand side.  The old stackless build wrote current into the spare and
//    restored it in the state switch().  Did this ever happen?
//
inline static Frame(*) Maybe_Rightward_Continuation_Needed(Frame(*) f)
{
    if (Get_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT))  {  // e.g. `10 -> x:`
        Clear_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT);
        Clear_Cell_Flag(f->out, UNEVALUATED);  // this helper counts as eval
        return nullptr;
    }

    if (Is_Feed_At_End(f->feed))  // `do [x:]`, `do [o.x:]`, etc. are illegal
        fail (Error_Need_Non_End(f_current));

    Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);  // always >= 2 elements, see [2]

    assert(Is_Stale(OUT));  // SET-XXX! may need to be invisible, see [3]

    Flags flags =
        EVAL_EXECUTOR_FLAG_SINGLE_STEP  // v-- if f was fulfilling, we are
        | (f->flags.bits & EVAL_EXECUTOR_FLAG_FULFILLING_ARG)
        | FRAME_FLAG_MAYBE_STALE
        | FRAME_FLAG_FAILURE_RESULT_OK;  // trap [e: transcode "1&aa"] works

    if (Did_Init_Inert_Optimize_Complete(OUT, f->feed, &flags))
        return nullptr;  // If eval not hooked, ANY-INERT! may not need a frame

    Frame(*) subframe = Make_Frame(
        f->feed,
        flags  // inert optimize adjusted the flags to jump in mid-eval
    );
    Push_Frame(OUT, subframe);

    assert(f_current != &f->feed->lookback);  // are these possible?  see [4]
    assert(f_current != &f->feed->fetched);

    return subframe;
}


//
//  Evaluator_Executor: C
//
// Expression execution can be thought of as having four distinct states:
//
//    * new_expression
//    * evaluate
//    * lookahead
//    * finished -or- threw
//
// It is possible to preload states and start an evaluator at any of these.
//
Bounce Evaluator_Executor(Frame(*) f)
{
    if (THROWING)
        return THROWN;  // no state to clean up

    assert(TOP_INDEX >= BASELINE->stack_base);  // e.g. REDUCE accrues
    assert(OUT != SPARE);  // overwritten by temporary calculations

    if (Get_Executor_Flag(EVAL, f, NO_EVALUATIONS)) {  // see flag for rationale
        if (Is_Feed_At_End(f->feed))
            return OUT;
        Derelativize(OUT, At_Feed(f->feed), FEED_SPECIFIER(f->feed));
        Set_Cell_Flag(OUT, UNEVALUATED);
        Fetch_Next_Forget_Lookback(f);
        return OUT;
    }

    // A barrier shouldn't cause an error in evaluation if code would be
    // willing to accept an <end>.  So we allow argument gathering to try to
    // run, but it may error if that's not acceptable.
    //
    if (Get_Feed_Flag(f->feed, BARRIER_HIT)) {
        if (Get_Executor_Flag(EVAL, f, FULFILLING_ARG)) {
            if (Get_Frame_Flag(f, MAYBE_STALE))
                Mark_Eval_Out_Stale(OUT);
            else
                assert(Is_Void(OUT));
            return OUT;
        }
        Clear_Feed_Flag(f->feed, BARRIER_HIT);  // not an argument, clear flag
    }

    // Given how the evaluator is written, it's inevitable that there will
    // have to be a test for points to `goto` before running normal eval.
    // This cost is paid on every entry to Eval_Core().
    //
    switch (STATE) {
      case ST_EVALUATOR_INITIAL_ENTRY:
        Sync_Feed_At_Cell_Or_End_May_Fail(f->feed);
        TRASH_POINTER_IF_DEBUG(f_current);
        /*TRASH_POINTER_IF_DEBUG(f_current_gotten);*/  // trash option() ptrs?
        break;

      case ST_EVALUATOR_STEPPING_AGAIN:  // when not single stepping
        goto new_expression;

      case ST_EVALUATOR_LOOKING_AHEAD:
        goto lookahead;

      case ST_EVALUATOR_REEVALUATING: {  // v-- IMPORTANT: Keep STATE
        //
        // It's important to leave STATE as ST_EVALUATOR_REEVALUATING
        // during the switch state, because that's how the evaluator knows
        // not to redundantly apply LET bindings.  See `f_specifier` above.

        // The re-evaluate functionality may not want to heed the enfix state
        // in the action itself.  See DECLARE_NATIVE(shove)'s /ENFIX for instance.
        // So we go by the state of a flag on entry.
        //
        if (f->u.eval.enfix_reevaluate == 'N') {
            // either not enfix or not an action
        }
        else {
            assert(f->u.eval.enfix_reevaluate == 'Y');

            Frame(*) subframe = Make_Action_Subframe(f);
            Push_Frame(OUT, subframe);
            Push_Action(
                subframe,
                VAL_ACTION(f_current),
                VAL_ACTION_BINDING(f_current)
            );
            Begin_Enfix_Action(subframe, VAL_ACTION_LABEL(f_current));
                // ^-- invisibles cache NO_LOOKAHEAD

            Set_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT);

            assert(Is_Void(SPARE));
            goto process_action;
        }

        if (Not_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT))
            Mark_Eval_Out_Stale(OUT);

        f_current_gotten = nullptr;  // !!! allow/require to be passe in?
        goto evaluate; }

      case ST_EVALUATOR_RUNNING_GROUP : goto group_result_in_spare;

      case ST_EVALUATOR_RUNNING_META_GROUP :
        STATE = ST_EVALUATOR_INITIAL_ENTRY;
        goto lookahead;

      case ST_EVALUATOR_RUNNING_SET_GROUP : goto set_group_result_in_spare;

      case ST_EVALUATOR_SET_WORD_RIGHTSIDE : goto set_word_rightside_in_out;

      case ST_EVALUATOR_SET_TUPLE_RIGHTSIDE : goto set_tuple_rightside_in_out;

      case ST_EVALUATOR_RUNNING_ACTION :
        STATE = ST_EVALUATOR_INITIAL_ENTRY;
        goto lookahead;

      case ST_EVALUATOR_SET_BLOCK_RIGHTSIDE:
        STATE = ST_EVALUATOR_INITIAL_ENTRY;
        goto set_block_rightside_result_in_out;

      case ST_EVALUATOR_SET_BLOCK_LOOKAHEAD:
        STATE = ST_EVALUATOR_INITIAL_ENTRY;
        goto set_block_lookahead_result_in_out;

      default:
        assert(false);
    }

  #if !defined(NDEBUG)
    Evaluator_Expression_Checks_Debug(f);
  #endif

  new_expression:

  //=//// START NEW EXPRESSION ////////////////////////////////////////////=//

    assert(Not_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT));

    // OUT might be erased, e.g. the header is all 0 bits (CELL_MASK_0).
    // This is considered FRESH() but not WRITABLE(), so the Set_Cell_Flag()
    // routines will reject it.  While we are already doing a flag masking
    // operation to add CELL_FLAG_STALE, ensure the cell carries the NODE and
    // CELL flags (we already checked that it was FRESH()).  This promotes
    // erased cells to a stale void state.
    //
    // Note that adding CELL_FLAG_STALE means the out cell won't act as the
    // input to an enfix operation.
    //
    OUT->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL | CELL_FLAG_STALE
    );

    UPDATE_EXPRESSION_START(f);  // !!! See FRM_INDEX() for caveats

    // If asked to evaluate `[]` then we have now done all the work the
    // evaluator needs to do--including marking the output stale.
    //
    if (Is_Frame_At_End(f))
        goto finished;

    f_current = Lookback_While_Fetching_Next(f);
    f_current_gotten = f_next_gotten;
    f_next_gotten = nullptr;

  evaluate: ;  // meaningful semicolon--subsequent macro may declare things

    // ^-- doesn't advance expression index: `reeval x` starts with `reeval`

  //=//// LOOKAHEAD FOR ENFIXED FUNCTIONS THAT QUOTE THEIR LEFT ARG ///////=//

    if (Is_Frame_At_End(f))
        goto give_up_backward_quote_priority;

    if (VAL_TYPE_UNCHECKED(f_next) != REB_WORD)  // right's kind
        goto give_up_backward_quote_priority;

    assert(not f_next_gotten);  // Fetch_Next_In_Frame() cleared it
    f_next_gotten = Lookup_Word(f_next, FEED_SPECIFIER(f->feed));

    if (
        not f_next_gotten
        or REB_ACTION != VAL_TYPE_UNCHECKED(unwrap(f_next_gotten))
    ){
        goto give_up_backward_quote_priority;  // note only ACTION! is ENFIXED
    }

    if (Get_Action_Flag(VAL_ACTION(unwrap(f_next_gotten)), IS_BARRIER)) {
        //
        // In a situation like `foo |`, we want FOO to be able to run...it
        // may take 0 args or it may be able to tolerate END.  But we should
        // not be required to run the barrier in the same evaluative step
        // as the left hand side.  (It can be enfix, or it can not be.)
        //
        Set_Feed_Flag(f->feed, BARRIER_HIT);
        goto give_up_backward_quote_priority;
    }

    if (Not_Action_Flag(VAL_ACTION(unwrap(f_next_gotten)), ENFIXED))
        goto give_up_backward_quote_priority;

  blockscope {
    Action(*) enfixed = VAL_ACTION(unwrap(f_next_gotten));

    if (Not_Action_Flag(enfixed, QUOTES_FIRST))
        goto give_up_backward_quote_priority;

    // If the action soft quotes its left, that means it's aware that its
    // "quoted" argument may be evaluated sometimes.  If there's evaluative
    // material on the left, treat it like it's in a group.
    //
    if (
        Get_Action_Flag(enfixed, POSTPONES_ENTIRELY)
        or (
            Get_Feed_Flag(f->feed, NO_LOOKAHEAD)
            and not ANY_SET_KIND(kind_current)  // not SET-WORD!, SET-PATH!...
        )
    ){
        // !!! cache this test?
        //
        const REBPAR *first = First_Unspecialized_Param(nullptr, enfixed);
        if (
            VAL_PARAM_CLASS(first) == PARAM_CLASS_SOFT
            or VAL_PARAM_CLASS(first) == PARAM_CLASS_META
        ){
            goto give_up_backward_quote_priority;  // yield as an exemption
        }
    }

    // Let the <skip> flag allow the right hand side to gracefully decline
    // interest in the left hand side due to type.  This is how DEFAULT works,
    // such that `case [condition [...] default [...]]` does not interfere
    // with the BLOCK! on the left, but `x: default [...]` gets the SET-WORD!
    //
    if (Get_Action_Flag(enfixed, SKIPPABLE_FIRST)) {
        const REBPAR *first = First_Unspecialized_Param(nullptr, enfixed);
        if (not TYPE_CHECK(first, kind_current))  // left's kind
            goto give_up_backward_quote_priority;
    }

    // Lookback args are fetched from OUT, then copied into an arg slot.
    // Put the backwards quoted value into OUT.
    //
    Derelativize(OUT, f_current, f_specifier);  // for NEXT_ARG_FROM_OUT
    Set_Cell_Flag(OUT, UNEVALUATED);  // so lookback knows it was quoted

    // We skip over the word that invoked the action (e.g. ->-, OF, =>).
    // v will then hold a pointer to that word (possibly now resident in the
    // frame spare).  (OUT holds what was the left)
    //
    f_current_gotten = f_next_gotten;
    f_current = Lookback_While_Fetching_Next(f);

    if (
        Is_Feed_At_End(f->feed)  // v-- out is what used to be on left
        and (
            VAL_TYPE_UNCHECKED(OUT) == REB_WORD
            or VAL_TYPE_UNCHECKED(OUT) == REB_TUPLE
        )
    ){
        // We make a special exemption for left-stealing arguments, when
        // they have nothing to their right.  They lose their priority
        // and we run the left hand side with them as a priority instead.
        // This lets us do e.g. `(just =>)` or `help of`
        //
        // Swap it around so that what we had put in OUT goes back to being in
        // the lookback cell and can be used as current.  Then put what was
        // current into OUT so it can be consumed as the first parameter of
        // whatever that was.

        Move_Cell(&f->feed->lookback, OUT);
        Derelativize(OUT, f_current, f_specifier);
        Set_Cell_Flag(OUT, UNEVALUATED);

        // leave *next at END
        f_current = &f->feed->lookback;
        f_current_gotten = nullptr;

        Set_Executor_Flag(EVAL, f, DIDNT_LEFT_QUOTE_TUPLE);  // better error msg
        Set_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT);  // literal right op is arg

        goto give_up_backward_quote_priority;  // run PATH!/WORD! normal
    }
  }

    // Wasn't the at-end exception, so run normal enfix with right winning.
    //
  blockscope {
    Frame(*) subframe = Make_Action_Subframe(f);
    Push_Frame(OUT, subframe);
    Push_Action(
        subframe,
        VAL_ACTION(unwrap(f_current_gotten)),
        VAL_ACTION_BINDING(unwrap(f_current_gotten))
    );
    Begin_Enfix_Action(subframe, VAL_WORD_SYMBOL(f_current));

    Set_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT);

    goto process_action; }

  give_up_backward_quote_priority:

  //=//// BEGIN MAIN SWITCH STATEMENT /////////////////////////////////////=//

    // This switch is done with a case for all REB_XXX values, in order to
    // facilitate use of a "jump table optimization":
    //
    // http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables
    //
    // Subverting the jump table optimization with specialized branches for
    // fast tests like ANY_INERT() and IS_NULLED_OR_VOID_OR_END() has shown
    // to reduce performance in practice.  The compiler does the right thing.

    switch (QUOTE_BYTE_UNCHECKED(f_current)) {

      //=//// QUASI!  and QUOTED! //////////////////////////////////////////=//
      //
      // QUASI! forms will produce an isotope when evaluated of whatever it is
      // containing:
      //
      //     >> bar: ~whatever~
      //     == ~whatever~  ; isotope
      //
      //     >> bar
      //     ** Error: bar is a ~whatever~ isotope
      //
      // To bypass the error, use GET/ANY.
      //
      // QUOTED! forms simply evaluate to remove one level of quoting.

    case QUASI_2:
        if (Is_Meta_Of_Void(f_current))
            break;  // the `~` just vaporizes, leaves output as-is
        Derelativize(OUT, f_current, f_specifier);
        mutable_QUOTE_BYTE(OUT) = ISOTOPE_0;
        Set_Cell_Flag(OUT, SCANT_EVALUATED_ISOTOPE);  // see flag comments
        break;

    default:  // e.g. QUOTED!
        Derelativize(OUT, f_current, f_specifier);
        Unquotify(OUT, 1);  // asserts it is not an isotope
        break;

    case UNQUOTED_1: switch (CELL_HEART_UNCHECKED(f_current)) {

    //=//// NULL //////////////////////////////////////////////////////////=//
    //
    // Since nulled cells can't be in BLOCK!s, the evaluator shouldn't usually
    // see them.  It is technically possible to see one using REEVAL, such as
    // with `reeval first []`.
    //
    // Note: It seems tempting to let NULL evaluate to NULL as a convenience
    // for such cases.  But this breaks the system in subtle ways--like
    // making it impossible to "reify" the instruction stream as a BLOCK!
    // for the debugger.  Mechanically speaking, this is best left an error.
    //
    // Note: The API can't splice null values in the instruction stream:
    //
    //     REBVAL *v = nullptr;
    //     bool is_null = rebUnboxLogic("null?", v);  // should be rebQ(v)
    //
    // But what it does as a compromise is it will make the spliced values
    // into ~null~ BAD-WORD!s.  This will sometimes work out due to decay
    // (though not the above case, as NULL? will error when passed isotopes).
    // Yet a convenience is supplied by making the @ operator turn ~null~
    // BAD-WORD!s into proper nulls:
    //
    //     bool is_null = rebUnboxLogic("null?", rebQ(v));
    //     bool is_null = rebUnboxLogic("null? @", v);  // equivalent, shorter

      case REB_NULL:
        fail (Error_Evaluate_Null_Raw());


    //=//// COMMA! ////////////////////////////////////////////////////////=//
    //
    // A comma is a lightweight looking expression barrier.

       case REB_COMMA:
        if (Get_Executor_Flag(EVAL, f, FULFILLING_ARG)) {
            Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);
            Set_Feed_Flag(f->feed, BARRIER_HIT);
            goto finished;
        }
        break;


    //=//// ACTION! ///////////////////////////////////////////////////////=//
    //
    // If an action makes it to the SWITCH statement, that means it is either
    // literally an action value in the array (`do compose [1 (:+) 2]`) or is
    // being retriggered via REEVAL.
    //
    // Most action evaluations are triggered from a WORD! or PATH! case.

      case REB_ACTION: {
        Frame(*) subframe = Make_Action_Subframe(f);
        Push_Frame(OUT, subframe);
        Push_Action(
            subframe,
            VAL_ACTION(f_current),
            VAL_ACTION_BINDING(f_current)
        );
        Begin_Prefix_Action(subframe, VAL_ACTION_LABEL(f_current));

        // We'd like `10 >- = 5 + 5` to work, and to do so it reevaluates in
        // a new frame, but has to run the `=` as "getting its next arg from
        // the output slot, but not being run in an enfix mode".
        //
        if (Not_Feed_Flag(subframe->feed, NEXT_ARG_FROM_OUT))
            Mark_Eval_Out_Stale(subframe->out);

        goto process_action; }

    //=//// ACTION! ARGUMENT FULFILLMENT AND/OR TYPE CHECKING PROCESS /////=//

        // This one processing loop is able to handle ordinary action
        // invocation, specialization, and type checking of an already filled
        // action frame.  It walks through both the formal parameters (in
        // the spec) and the actual arguments (in the call frame) using
        // pointer incrementation.
        //
        // Based on the parameter type, it may be necessary to "consume" an
        // expression from values that come after the invocation point.  But
        // not all parameters will consume arguments for all calls.

      process_action: {
        //
        // Gather args and execute function (the arg gathering makes nested
        // eval calls that lookahead, but no lookahead after the action runs)
        //
        STATE = ST_EVALUATOR_RUNNING_ACTION;
        return CATCH_CONTINUE_SUBFRAME(TOP_FRAME); }


    //=//// WORD! //////////////////////////////////////////////////////////=//
    //
    // A plain word tries to fetch its value through its binding.  It fails
    // if the word is unbound (or if the binding is to a variable which is
    // set, but to a non-isotope form of bad-word!).  Should the word look up
    // to an action, then that action will be invoked.
    //
    // NOTE: The usual dispatch of enfix functions is *not* via a REB_WORD in
    // this switch, it's by some code at the `lookahead:` label.  You only see
    // enfix here when there was nothing to the left, so cases like `(+ 1 2)`
    // or in "stale" left hand situations like `10 comment "hi" + 20`.

      case REB_WORD:
        if (not f_current_gotten)
            f_current_gotten = Lookup_Word_May_Fail(f_current, f_specifier);

        if (VAL_TYPE_UNCHECKED(unwrap(f_current_gotten)) == REB_ACTION) {
            Action(*) action = VAL_ACTION(unwrap(f_current_gotten));

            if (Get_Action_Flag(action, ENFIXED)) {
                if (
                    Get_Action_Flag(action, POSTPONES_ENTIRELY)
                    or Get_Action_Flag(action, DEFERS_LOOKBACK)
                ){
                    if (Get_Executor_Flag(EVAL, f, FULFILLING_ARG)) {
                        Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);
                        Set_Feed_Flag(f->feed, DEFERRING_ENFIX);
                        RESET(OUT);
                        goto finished;
                    }
                }
            }

            Context(*) binding = VAL_ACTION_BINDING(unwrap(f_current_gotten));
            Symbol(const*) label = VAL_WORD_SYMBOL(f_current);  // use WORD!
            bool enfixed = Get_Action_Flag(action, ENFIXED);

            Frame(*) subframe = Make_Action_Subframe(f);
            Push_Frame(OUT, subframe);
            Push_Action(subframe, action, binding);
            Begin_Action_Core(subframe, label, enfixed);

            if (enfixed)
                Set_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT);

            goto process_action;
        }

        if (Is_Isotope(unwrap(f_current_gotten)))  // checked second
            fail (Error_Bad_Word_Get(f_current, unwrap(f_current_gotten)));

        Copy_Cell(OUT, unwrap(f_current_gotten));  // no CELL_FLAG_UNEVALUATED
        break;


    //=//// SET-WORD! /////////////////////////////////////////////////////=//
    //
    // Right side is evaluated into `out`, and then copied to the variable.
    //
    // Null and void assigns are allowed: https://forum.rebol.info/t/895/4
    //
    //////////////////////////////////////////////////////////////////////////
    //
    // 1. Void unsets the variable, and propagates a none signal, instead of
    //    a void.  This maintains `y: x: (...)` where y = x afterward:
    //
    //        >> x: comment "hi"
    //        == ~  ; isotope
    //
    //        >> get/any 'x
    //        == ~  ; isotope
    //
    // 2. Running functions flushes the f_next_gotten cache.  But a plain
    //    assignment can cause trouble too:
    //
    //        >> x: <before> x: 1 x
    //                            ^-- x value was cached in infix lookahead
    //
    //    It used to not be a problem, when variables didn't just pop into
    //    existence or have their caching states changed.  But INDEX_ATTACHED
    //    and the various complexities involved with that means we have to
    //    flush here if the symbols match.

    set_blank_in_spare: ///////////////////////////////////////////////////////

    set_word_in_spare: ///////////////////////////////////////////////////////

        f_current = SPARE;
        goto set_word_common;

    set_word_common: /////////////////////////////////////////////////////////

      case REB_SET_WORD: {
        Frame(*) subframe = Maybe_Rightward_Continuation_Needed(f);
        if (not subframe)
            goto set_word_rightside_in_out;

        STATE = ST_EVALUATOR_SET_WORD_RIGHTSIDE;
        return CATCH_CONTINUE_SUBFRAME(subframe);

      } set_word_rightside_in_out: {  ////////////////////////////////////////

        if (IS_BLANK(f_current)) {
            // happens with `(void): ...`
        }
        else if (Is_Stale(OUT)) {
            RESET(Sink_Word_May_Fail(f_current, f_specifier));
        }
        else if (Is_Raised(OUT)) {
            // Don't assign, but let (trap [a: transcode "1&aa"]) work
        }
        else if (
            Is_Isotope(OUT)
            and Not_Cell_Flag(OUT, SCANT_EVALUATED_ISOTOPE)  // from QUASI!
            and Pointer_To_Decayed(OUT) == OUT  // don't corrupt OUT
        ){
            fail (Error_Bad_Isotope(OUT));
        }
        else {
            if (IS_ACTION(OUT))  // !!! Review: When to update labels?
                INIT_VAL_ACTION_LABEL(OUT, VAL_WORD_SYMBOL(f_current));

            Copy_Cell(
                Sink_Word_May_Fail(f_current, f_specifier),
                Pointer_To_Decayed(OUT)
            );

            if (f_next_gotten)  // cache can tamper with lookahead, see [2]
                if (VAL_WORD_SYMBOL(f_next) == VAL_WORD_SYMBOL(f_current))
                    f_next_gotten = nullptr;
        }

        STATE = ST_EVALUATOR_INITIAL_ENTRY;
        break; }


    //=//// GET-WORD! /////////////////////////////////////////////////////=//
    //
    // A GET-WORD! does no dispatch on functions.  It will fetch other values
    // as normal, but will error on unfriendly BAD-WORD!.
    //
    // This handling matches Rebol2 behavior, choosing to break with R3-Alpha
    // and Red which will give back "UNSET!" if you use a GET-WORD!.  The
    // mechanics of @word are a completely new approach.
    //
    // https://forum.rebol.info/t/1301

      case REB_META_WORD:
        STATE = ST_EVALUATOR_META_WORD;
        goto process_get_word;

      case REB_GET_WORD:
        STATE = ST_EVALUATOR_GET_WORD;
        goto process_get_word;

      process_get_word:
        assert(
            STATE == ST_EVALUATOR_META_WORD
            or STATE == ST_EVALUATOR_GET_WORD
        );

        if (not f_current_gotten)
            f_current_gotten = Lookup_Word_May_Fail(f_current, f_specifier);

        Copy_Cell(OUT, unwrap(f_current_gotten));
        assert(Not_Cell_Flag(OUT, UNEVALUATED));

        // !!! All isotopic decay should have already happened.
        // Lookup_Word() should be asserting this!

        if (STATE == ST_EVALUATOR_META_WORD)
            Meta_Quotify(OUT);
        else {
            if (Is_Isotope(OUT))
                fail (Error_Bad_Word_Get(f_current, OUT));
        }

        STATE = ST_EVALUATOR_INITIAL_ENTRY;
        break;


    //=//// GROUP! and GET-GROUP! /////////////////////////////////////////=//
    //
    // Groups simply evaluate their contents, and will evaluate to void if
    // the contents completely disappear.
    //
    // GET-GROUP! currently acts as a synonym for group, see [1].
    //
    //////////////////////////////////////////////////////////////////////////
    //
    // 1. It was initially theorized that `:(x)` would act a shorthand for the
    //   expression `get x`.  But that's already pretty short--and arguably a
    //   cleaner way of saying the same thing.  Making it a synonym for GROUP!
    //   seems wasteful on the surface, but it means dialects can be free to
    //   use it to make a distinction--like escaping soft-quoted slots.
    //
    // 2. A group can vanish, leaving what was to the left of it stale:
    //
    //        >> 1 + 2 (comment "hi") * 3
    //        ** Error: The 3 is stale
    //
    //    But some evaluation clients (like ALL) need to know the difference
    //    between when a result wasn't overwritten, or when it was overwritten
    //    with a stale value, e.g.
    //
    //        all [10 (comment "hi")] => 10
    //        all [10 (20 comment "hi")] => 20
    //
    //    If we evaluated the GROUP! into the OUT cell overlapping with the
    //    previous result, the stale bit alone wouldn't tell us which situation
    //    we had.  So we evaluate into the SPARE cell to discern.  (We could
    //    also mark the out cell with some other bit, if one were available?)

      case REB_GET_GROUP:  // synonym for GROUP!, see [1]
      case REB_GROUP: {
        f_next_gotten = nullptr;  // arbitrary code changes fetched variables

        Frame(*) subframe = Make_Frame_At_Core(
            f_current,
            f_specifier,
            FRAME_FLAG_FAILURE_RESULT_OK
        );
        Push_Frame(RESET(SPARE), subframe);

        STATE = ST_EVALUATOR_RUNNING_GROUP;  // must target spare, see [2]
        return CATCH_CONTINUE_SUBFRAME(subframe);

      } group_result_in_spare: {  ////////////////////////////////////////////

        if (Is_Void(SPARE))
            Mark_Eval_Out_Voided(OUT);  // asserts it's stale
        else
            Move_Cell(OUT, SPARE);

        STATE = ST_EVALUATOR_INITIAL_ENTRY;
        break; }


    //=//// META-GROUP! ///////////////////////////////////////////////////=//
    //
    // A META-GROUP! simply gives the meta form of its evaluation.  Unlike the
    // GROUP! and GET-GROUP!, it doesn't need to be concerned about staleness
    // as it always overwrites the result.

      case REB_META_GROUP: {
        f_next_gotten = nullptr;  // arbitrary code changes fetched variables

        Frame(*) subframe = Make_Frame_At_Core(
            f_current,
            f_specifier,
            FRAME_FLAG_META_RESULT | FRAME_FLAG_FAILURE_RESULT_OK
        );
        Push_Frame(OUT, subframe);

        STATE = ST_EVALUATOR_RUNNING_META_GROUP;
        return CATCH_CONTINUE_SUBFRAME(subframe); }


    //=//// TUPLE! /////////////////////////////////////////////////////////=//
    //
    // TUPLE! runs through an extensible mechanism based on PICK* and POKE*.
    // Hence `a.b.c` is kind of like a shorthand for `pick (pick a 'b) 'c`.
    //
    // In actuality, the mechanism is more sophisticated than that...because
    // some picking does "sub-value" addressing.  For more details, see the
    // explanation in %sys-pick.h
    //
    // For now, we defer to what GET does.
    //
    // Tuples looking up to BAD-WORD! isotopes are handled consistently with
    // WORD! and GET-WORD!, and will error...directing you use GET/ANY if
    // fetching isotopes is what you actually intended.

      case REB_TUPLE: {
        Cell(const*) head = VAL_SEQUENCE_AT(SPARE, f_current, 0);
        if (IS_BLANK(head) or ANY_INERT(head)) {
            Derelativize(OUT, f_current, f_specifier);
            break;
        }

        if (Get_Var_Core_Throws(SPARE, GROUPS_OK, f_current, f_specifier))
            goto return_thrown;

        if (VAL_TYPE_UNCHECKED(SPARE) == REB_ACTION) {  // uncheck for isotopes
            Action(*) act = VAL_ACTION(SPARE);

            // PATH! dispatch is costly and can error in more ways than WORD!:
            //
            //     e: trap [do make block! ":a"] e.id = 'not-bound
            //                                   ^-- not ready @ lookahead
            //
            // Plus with GROUP!s in a path, their evaluations can't be undone.
            //
            if (Get_Action_Flag(act, ENFIXED))
                fail ("Use `>-` to shove left enfix operands into PATH!s");

            Frame(*) subframe = Make_Action_Subframe(f);
            Push_Frame(OUT, subframe);
            Push_Action(
                subframe,
                VAL_ACTION(SPARE),
                VAL_ACTION_BINDING(SPARE)
            );
            Begin_Prefix_Action(subframe, VAL_ACTION_LABEL(SPARE));
            goto process_action;
        }

        if (Is_Isotope(SPARE))  // we test *after* action (faster common case)
            fail (Error_Bad_Word_Get(f_current, SPARE));

        Move_Cell(OUT, SPARE);  // won't move CELL_FLAG_UNEVALUATED
        break; }


    //=//// PATH! //////////////////////////////////////////////////////////=//
    //
    // Ren-C has moved to member-access model of "dots instead of slashes".
    // So by default, PATH! should only be used for picking refinements on
    // functions.  TUPLE! should be used for picking members out of structures.
    // This has benefits because function dispatch is more complex than the
    // usual PICK process, and being able to say that "slashing" is not
    // methodized the way "dotting" is gives some hope of optimizing it.
    //
    // *BUT* for compatibility with Redbol, there has to be a mode where the
    // paths are effectively turned into TUPLE!s.  This is implemented and
    // controlled by the flag SYSTEM.OPTIONS.REDBOL-PATHS - but it is slow
    // because the method has to actually synthesize a specialized function,
    // instead of pushing refinements to the stack.  (Going through a method
    // call would not allow accrual of data stack elements.)
    //
    // PATH!s starting with inert values do not evaluate.  `/foo/bar` has a
    // blank at its head, and it evaluates to itself.

      case REB_PATH: {
        Cell(const*) temp = VAL_SEQUENCE_AT(SPARE, f_current, 0);
        if (IS_BLANK(temp) or ANY_INERT(temp)) {
            Derelativize(OUT, f_current, f_specifier);
            break;
        }

        temp = VAL_SEQUENCE_AT(
            SPARE,
            f_current,
            VAL_SEQUENCE_LEN(f_current) - 1
        );
        bool applying = IS_BLANK(temp);  // terminal slash is APPLY

        // The frame captures the stack pointer, and since refinements are
        // pushed we want to capture it before that point (so it knows the
        // refinements are for it).
        //
        Frame(*) subframe = Make_Action_Subframe(f);
        Push_Frame(OUT, subframe);

        if (Get_Path_Push_Refinements_Throws(
            SPARE,
            OUT,
            f_current,
            f_specifier
        )){
            Drop_Frame(subframe);
            goto return_thrown;
        }

        if (not IS_ACTION(SPARE)) {
            //
            // !!! This is legacy support, which will be done another way in
            // the future.  You aren't supposed to use PATH! to get field
            // access...just action execution.
            //
            Drop_Frame(subframe);
            Move_Cell(OUT, SPARE);
            break;
        }

        // PATH! dispatch is costly and can error in more ways than WORD!:
        //
        //     e: trap [do make block! ":a"] e.id = 'not-bound
        //                                   ^-- not ready @ lookahead
        //
        // Plus with GROUP!s in a path, their evaluations can't be undone.
        //
        if (Get_Action_Flag(VAL_ACTION(SPARE), ENFIXED))
            fail ("Use `>-` to shove left enfix operands into PATH!s");

        if (not applying) {
            Push_Action(subframe, VAL_ACTION(SPARE), VAL_ACTION_BINDING(SPARE));
            Begin_Prefix_Action(subframe, VAL_ACTION_LABEL(SPARE));
            goto process_action;
        }

        if (Is_Frame_At_End(f))
            fail ("Terminal-Slash Action Invocation Needs APPLY argument");

        STATE = ST_EVALUATOR_RUNNING_ACTION;  // bounces back to do lookahead
        rebPushContinuation(
            OUT,
            FRAME_FLAG_MAYBE_STALE,
            Lib(APPLY), rebQ(SPARE), rebDERELATIVIZE(f_next, f_specifier)
        );
        Fetch_Next_Forget_Lookback(f);
        return BOUNCE_CONTINUE; }


    //=//// SET-PATH! /////////////////////////////////////////////////////=//
    //
    // See notes on PATH! for why Ren-C aligns itself with the general idea of
    // "dots instead of slashes" for member selection.  For now, those who
    // try to use SET-PATH! will receive a warning once...then it will set
    // a switch to where it runs the SET-TUPLE! code instead.
    //
    // Future uses of SET-PATH! could do things like verify that the thing
    // being assigned is an ACTION!:
    //
    //     >> foo/: 10
    //     ** Expected an ACTION! but got an INTEGER!
    //
    // Or it might be a way of installing a "getter/setter" function:
    //
    //     >> obj.field/: func [/value] [
    //            either value [print ["Assigning" value]] [print "Getting"]]
    //        ]
    //
    //     >> obj.field: 10
    //     Assigning 10
    //
    // But for the moment, it is just used in Redbol emulation.

      case REB_SET_PATH: {
        REBVAL *redbol = Get_System(SYS_OPTIONS, OPTIONS_REDBOL_PATHS);
        if (not IS_LOGIC(redbol) or VAL_LOGIC(redbol) == false) {
            Derelativize(OUT, f_current, f_specifier);
            mutable_HEART_BYTE(OUT) = REB_SET_TUPLE;

            Derelativize(SPARE, f_current, f_specifier);
            rebElide(
                "echo [The SET-PATH!", SPARE, "is no longer the preferred",
                    "way to do member assignments.]",
                "echo [SYSTEM.OPTIONS.REDBOL-PATHS is FALSE, so SET-PATH!",
                    "is not allowed by default.]",
                "echo [For now, we'll enable it automatically...but it",
                    "will slow down the system!]",
                "echo [Please use TUPLE! instead, like", OUT, "]",

                "system.options.redbol-paths: true",
                "wait 3"
            );
        }
        goto generic_set_common; }


    //=//// SET-TUPLE! /////////////////////////////////////////////////////=//
    //
    // !!! The evaluation ordering is dictated by the fact that there isn't a
    // separate "evaluate path to target location" and "set target' step.
    // This is because some targets of assignments (e.g. gob.size.x:) do not
    // correspond to a cell that can be returned; the path operation "encodes
    // as it goes" and requires the value to set as a parameter.  Yet it is
    // counterintuitive given the "left-to-right" nature of the language:
    //
    //     >> foo: make object! [[bar][bar: 10]]
    //
    //     >> foo.(print "left" 'bar): (print "right" 20)
    //     right
    //     left
    //     == 20
    //
    // Isotope and NULL assigns are allowed: https://forum.rebol.info/t/895/4

    set_tuple_in_spare: //////////////////////////////////////////////////////

        f_current = SPARE;
        goto generic_set_common;

    generic_set_common: //////////////////////////////////////////////////////

      case REB_SET_TUPLE: {
        Frame(*) subframe = Maybe_Rightward_Continuation_Needed(f);
        if (not subframe)
            goto set_tuple_rightside_in_out;

        STATE = ST_EVALUATOR_SET_TUPLE_RIGHTSIDE;
        return CATCH_CONTINUE_SUBFRAME(subframe);

      } set_tuple_rightside_in_out: {  ///////////////////////////////////////

        /*  // !!! Should we figure out how to cache a label in the cell?
        if (IS_ACTION(OUT))
            INIT_VAL_ACTION_LABEL(OUT, VAL_WORD_SYMBOL(v));
        */

        if (Is_Stale(OUT)) {
            if (Set_Var_Core_Throws(
                SPARE,
                GROUPS_OK,
                f_current,
                f_specifier,
                VOID_CELL
            )){
                goto return_thrown;
            }
        }
        else if (Is_Raised(OUT)) {
            // Don't assign, but let (trap [a.b: transcode "1&aa"]) work
        }
        else if (
            Is_Isotope(OUT)
            and Not_Cell_Flag(OUT, UNEVALUATED)  // see QUASI! handling
            and Pointer_To_Decayed(OUT) == OUT  // don't corrupt out
        ){
            fail (Error_Bad_Isotope(OUT));
        }
        else {
            if (Set_Var_Core_Throws(
                SPARE,
                GROUPS_OK,
                f_current,
                f_specifier,
                Pointer_To_Decayed(OUT)
            )){
                goto return_thrown;
            }
        }

        STATE = ST_EVALUATOR_INITIAL_ENTRY;
        break; }


    //=//// SET-GROUP! /////////////////////////////////////////////////////=//
    //
    // A SET-GROUP! will act as a SET-WORD!, SET-TUPLE!, or SET-BLOCK! based
    // on what the group evaluates to.

      case REB_SET_GROUP: {
        f_next_gotten = nullptr;  // arbitrary code changes fetched variables

        Frame(*) subframe = Make_Frame_At_Core(
            f_current,
            f_specifier,
            FRAME_MASK_NONE
        );
        Push_Frame(RESET(SPARE), subframe);

        STATE = ST_EVALUATOR_RUNNING_SET_GROUP;
        return CATCH_CONTINUE_SUBFRAME(subframe);

      } set_group_result_in_spare: {  ////////////////////////////////////////

        STATE = ST_EVALUATOR_INITIAL_ENTRY;

        f_current = SPARE;

        if (Is_Void(SPARE)) {
            Init_Blank(SPARE);
            goto set_blank_in_spare;
        }

        if (Is_Isotope(SPARE))
            fail (Error_Bad_Isotope(SPARE));

        switch (VAL_TYPE(SPARE)) {
          case REB_BLOCK:
            goto set_block_in_spare;

          case REB_WORD:
            goto set_word_in_spare;

          case REB_TUPLE:
            goto set_tuple_in_spare;

          default:
            fail ("Unknown type for use in SET-GROUP!");
        }
      break; }


    //=//// GET-PATH! and GET-TUPLE! //////////////////////////////////////=//
    //
    // Note that the GET native on a PATH! won't allow GROUP! execution:
    //
    //    foo: [X]
    //    path: 'foo/(print "side effect!" 1)
    //    get path  ; not allowed, due to surprising side effects
    //
    // However a source-level GET-PATH! allows them, since they are at the
    // callsite and you are assumed to know what you are doing:
    //
    //    :foo/(print "side effect" 1)  ; this is allowed
    //
    // Consistent with GET-WORD!, a GET-PATH! won't allow BAD-WORD! access on
    // the plain (unfriendly) forms.

      case REB_META_PATH:
      case REB_META_TUPLE:
        STATE = ST_EVALUATOR_META_PATH_OR_META_TUPLE;
        goto eval_path_or_tuple;

      case REB_GET_PATH:
      case REB_GET_TUPLE:
        STATE = ST_EVALUATOR_PATH_OR_TUPLE;
        goto eval_path_or_tuple;

      eval_path_or_tuple:

        assert(
            STATE == ST_EVALUATOR_PATH_OR_TUPLE
            or STATE == ST_EVALUATOR_META_PATH_OR_META_TUPLE
        );

        RESET(OUT);  // !!! Not needed, should there be debug only TRASH()
        if (Get_Var_Core_Throws(OUT, GROUPS_OK, f_current, f_specifier))
            goto return_thrown;

        // !!! This didn't appear to be true for `-- "hi" "hi"`, processing
        // GET-PATH! of a variadic.  Review if it should be true.
        //
        /* assert(Not_Cell_Flag(OUT, CELL_FLAG_UNEVALUATED)); */
        Clear_Cell_Flag(OUT, UNEVALUATED);

        if (STATE == ST_EVALUATOR_META_PATH_OR_META_TUPLE)
            Meta_Quotify(OUT);
        else {
            if (Is_Isotope(OUT))
                fail (Error_Bad_Word_Get(f_current, OUT));
        }

        STATE = ST_EVALUATOR_INITIAL_ENTRY;
        break;


    //=//// GET-BLOCK! ////////////////////////////////////////////////////=//
    //
    // The most useful evaluative operation for GET-BLOCK! was deemed to be
    // a REDUCE.  This does not correspond to what one would think of as an
    // "itemwise get" of a block as GET of BLOCK! acted in historical Rebol.
    // But most usages of that have been superseded by the UNPACK operation.
    //
    // The existence of GET-BLOCK! means that operations like "REPEND" are not
    // necessary, as it's very easy for users to ask for blocks to be reduced.

      case REB_GET_BLOCK: {
        Derelativize(SPARE, f_current, f_specifier);
        mutable_HEART_BYTE(SPARE) = REB_BLOCK;
        if (rebRunThrows(
            OUT,  // <-- output cell
            Canon(REDUCE), SPARE
        )){
            goto return_thrown;
        }
        break; }


    //=//// SET-BLOCK! ////////////////////////////////////////////////////=//
    //
    // The evaluator treats SET-BLOCK! specially as a means for implementing
    // multiple return values.  The trick is that it does so by pre-loading
    // arguments in the frame with variables to update, in a way that could've
    // historically been achieved with passing WORD! or PATH! to a refinement.
    // So if there was a function that updates a variable you pass in by name:
    //
    //     result: updating-function/update arg1 arg2 'var
    //
    // The /UPDATE parameter is marked as being effectively a "return value",
    // so that equivalent behavior can be achieved with:
    //
    //     [result var]: updating-function arg1 arg2
    //
    // It supports `_` in slots whose results you don't want to ask for, `#`
    // in slots you want to ask for (but don't want to name), will evaluate
    // GROUP!s, and also allows THE-WORD! to `@circle` which result you want
    // to be the overall result of the expression (defaults to the normal
    // main return value).

    set_block_in_spare: //////////////////////////////////////////////////////

        f_current = SPARE;
        goto set_block_common;

    set_block_common: ////////////////////////////////////////////////////////

      case REB_SET_BLOCK: {
        assert(Not_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT));

        // As with the other SET-XXX! variations, we may need to continue
        // what is to the left of the assignment if the right hand side
        // winds up vanishing.
        //
        //     >> (10 [x]: comment "we don't want this to be 10")
        //     == 10
        //
        //     >> x
        //     ; null
        //
        assert(Is_Stale(OUT));

        // We pre-process the SET-BLOCK! first, because we are going to
        // advance the feed in order to build a frame for the following code.
        // It makes more sense for any GROUP!s to be evaluated on the left
        // before on the right, so push the results to the stack.
        //
        // !!! Should the block be locked while the advancement happens?  It
        // wouldn't need to be since everything is on the stack before code
        // is run on the right...but it might reduce confusion.

        if (VAL_LEN_AT(f_current) == 0)
            fail ("SET-BLOCK! must not be empty for now.");

        StackIndex stackindex_circled = 0;  // which pushed is main return

      blockscope {
        Cell(const*) tail;
        Cell(const*) check = VAL_ARRAY_AT(&tail, f_current);
        REBSPC *check_specifier = Derive_Specifier(f_specifier, f_current);

        TRASH_POINTER_IF_DEBUG(f_current);  // might be SPARE, we use it now

        for (; tail != check; ++check) {
            //
            // THE-XXX! types are used to mark which result should be the
            // overall return of the expression.  But a GROUP! can't resolve
            // to that and make the decision, so handle it up front.
            //
            if (IS_THE(check)) {
                if (stackindex_circled != 0) {
                  too_many_circled:
                    fail ("Can't circle more than one multi-return result");
                }
                REBVAL *let = rebValue("let temp");  // have to fabricate var
                Move_Cell(PUSH(), let);
                rebRelease(let);
                stackindex_circled = TOP_INDEX;
                continue;
            }
            if (
                IS_THE_WORD(check)
                or IS_THE_PATH(check)
                or IS_THE_TUPLE(check)
            ){
                if (stackindex_circled != 0)
                    goto too_many_circled;
                Derelativize(PUSH(), check, check_specifier);
                Plainify(TOP);
                stackindex_circled = TOP_INDEX;
                continue;
            }

            // Carets indicate a desire to get a "meta" result.
            //
            // !!! The multi-return mechanism doesn't allow an arbitrary number
            // of meta steps, just one.  Should you be able to say ^(^(x)) or
            // something like that to add more?  :-/
            //
            // !!! Should both @(^) and ^(@) be allowed?
            //
            if (IS_META(check)) {
                if (stackindex_circled != 0)
                    goto too_many_circled;
                REBVAL *let = rebValue("let temp, '^temp");  // have to fabricate var
                Move_Cell(PUSH(), let);
                rebRelease(let);
                stackindex_circled = TOP_INDEX;
                continue;
            }
            if (
                IS_META_WORD(check)
                or IS_META_PATH(check)
                or IS_META_TUPLE(check)
            ){
                Derelativize(PUSH(), check, check_specifier);
                continue;
            }

            Cell(const*) item;
            REBSPC *item_specifier;
            if (
                IS_GROUP(check)
                or IS_THE_GROUP(check)
                or IS_META_GROUP(check)
            ){
                if (Do_Any_Array_At_Throws(SPARE, check, check_specifier)) {
                    Drop_Data_Stack_To(BASELINE->stack_base);
                    goto return_thrown;
                }
                item = SPARE;
                item_specifier = SPECIFIED;
            }
            else {
                item = check;
                item_specifier = check_specifier;
            }
            if (IS_BLANK(item)) {
                Init_Blank(PUSH());
            }
            else if (Is_Blackhole(item)) {
                //
                // !!! If someone writes `[... @(#) ...]: ...`, then there is
                // a problem if it's not the first slot; as the function needs
                // a variable location to write the result to.  For now, just
                // fabricate a LET variable.
                //
                if (TOP_INDEX == BASELINE->stack_base or not IS_THE_GROUP(check))
                    Init_Blackhole(PUSH());
                else {
                    REBVAL *let = rebValue("let temp");
                    assert(IS_WORD(let));
                    Move_Cell(PUSH(), let);
                    rebRelease(let);
                }
            }
            else if (
                IS_WORD(item)
                or IS_PATH(item)
                or IS_TUPLE(item)
            ){
                Derelativize(PUSH(), item, item_specifier);
            }
            else
                fail ("SET-BLOCK! elements are WORD/PATH/TUPLE/BLANK/ISSUE");

            if (IS_THE_GROUP(check))
                stackindex_circled = TOP_INDEX;
            else if (IS_META_GROUP(check))
                Metafy(TOP);
        }

        // By default, the ordinary return result will be returned.  Indicate
        // this with dsp_circled = 0, as if no circling were active.  (We had
        // to set it to nonzero in the case of `[@ ...]: ...` to give an error
        // if more than one return were circled.)
        //
        if (stackindex_circled == BASELINE->stack_base + 1)
            stackindex_circled = 0;
     }

        StackIndex base = TOP_INDEX;

        // Build a frame for the function call by fulfilling its arguments.
        // The function will be in a state that it can be called, but not
        // invoked yet.
        //
        STATE = ST_EVALUATOR_SET_BLOCK_RIGHTSIDE;  // reeval messes f_specifier
        DECLARE_LOCAL (action);
        if (Get_Var_Push_Refinements_Throws(
            action,
            GROUPS_OK,
            f_next,
            f_specifier
        )){
            Drop_Data_Stack_To(BASELINE->stack_base);
            goto return_thrown;
        }

        // We haven't ruled out other things on the right of SET-BLOCK!, but
        // nothing yet...
        //
        if (not IS_ACTION(action))
            fail ("SET-BLOCK! is only allowed to have ACTION! on right ATM.");

        Fetch_Next_Forget_Lookback(f);

        bool error_on_deferred = false;
        Frame(*) sub = Make_Pushed_Frame_From_Action_Feed_May_Throw(
            OUT,
            action,
            f->feed,
            base,
            error_on_deferred
        );

        Set_Frame_Flag(sub, FAILURE_RESULT_OK);  // may pass to except

        if (Is_Throwing(sub)) {
            Drop_Frame(sub);
            Drop_Data_Stack_To(BASELINE->stack_base);
            goto return_thrown;
        }

        // Now we want to enumerate through the outputs, and fill them with
        // words/paths/_/# from the data stack.  Note the first slot is set
        // from the "primary" output so it doesn't go in a slot.
        //
        StackIndex stackindex_output = BASELINE->stack_base + 2;

      blockscope {
        sub->executor = &Action_Executor;

        Begin_Prefix_Action(sub, VAL_ACTION_LABEL(action));

        const REBKEY *key_tail = sub->u.action.key_tail;
        const REBKEY *key = sub->u.action.key;
        Value(*) arg = sub->u.action.arg;
        const REBPAR *param = sub->u.action.param;

        for (; key != key_tail; ++key, ++arg, ++param) {
            if (stackindex_output == TOP_INDEX + 1)
                break;  // no more outputs requested
            if (Is_Specialized(param))
                continue;
            if (VAL_PARAM_CLASS(param) != PARAM_CLASS_OUTPUT)
                continue;

            Value(*) var = arg + 1;

            StackValue(*) at = Data_Stack_At(stackindex_output);
            ++stackindex_output;

            assert(Is_Void(arg) or Is_Nulled(arg));

            RESET(arg);  // !!! Can we stop nulls, to avoid RESET()?

            Copy_Cell(var, at);
            ++key, ++arg, ++param;
        }
      }

        // Now run the frame...no need to preserve OUT (always overwritten on
        // an assignment)
        //
        assert(STATE == ST_EVALUATOR_SET_BLOCK_RIGHTSIDE);
        frame_->u.eval.stackindex_circled = stackindex_circled;

        FRM_STATE_BYTE(sub) = ST_ACTION_TYPECHECKING;
        return CATCH_CONTINUE_SUBFRAME(sub);

    } set_block_rightside_result_in_out: {  //////////////////////////////////

        // We called arbitrary code, so we have to toss the cache (in case
        // e.g. ELSE comes next and it got redefined to 3 or something)
        //
        f_next_gotten = nullptr;

        // Now we have to look ahead in case there is enfix code afterward.
        // We want parity, for instance:
        //
        //    >> x: find "abc" 'b then [10]
        //    == 10
        //
        //    >> x
        //    == 10
        //
        //    >> [y]: find "abc" 'b then [10]
        //    == 10
        //
        //    >> y
        //    == 10
        //
        // But at this point we've only run the FIND part, so we'd just have
        // "bc" in the output.  We used `error_on_deferred` as false, so the
        // feed will be in a waiting state for enfix that we can continue by
        // jumping into the evaluator at the ST_EVALUATOR_LOOKING_AHEAD state.
        //
        if (Not_Frame_At_End(f) and VAL_TYPE(f_next) == REB_WORD) {
            Flags flags =
                EVAL_EXECUTOR_FLAG_SINGLE_STEP
                | FLAG_STATE_BYTE(ST_EVALUATOR_LOOKING_AHEAD)
                | EVAL_EXECUTOR_FLAG_INERT_OPTIMIZATION  // tolerate enfix late
                | FRAME_FLAG_MAYBE_STALE  // won't be, but avoids RESET()
                | FRAME_FLAG_FAILURE_RESULT_OK;  // might pass OUT to EXCEPT

            Frame(*) subframe = Make_Frame(f->feed, flags);
            Push_Frame(OUT, subframe);  // offer potential enfix previous OUT

            STATE = ST_EVALUATOR_SET_BLOCK_LOOKAHEAD;
            return CATCH_CONTINUE_SUBFRAME(subframe);
        }

    } set_block_lookahead_result_in_out: {  //////////////////////////////////

        StackIndex stackindex_circled = frame_->u.eval.stackindex_circled;

        // Take care of the SET for the main result.  For the moment, if you
        // asked to opt out of the main result this will give you a ~none~
        // isotope...but there is not currently any way for the invoked
        // routine to be aware that the caller opted out of the return.
        //
        // !!! Could there be a way of a function indicating it was willing
        // to accept RETURN being not requested, somehow?  It would be unsafe
        // having it be NULL as `return` would be a no-op, but if it were an
        // isotope like `~unrequested~` then that could be tested for by
        // a particular kind of routine...
        //
        // !!! Move the main result set part off the stack and into the spare
        // in case there are GROUP! evaluations in the assignment; though that
        // needs more thinking (e.g. what if they throw?)
        //
        Copy_Cell(SPARE, Data_Stack_At(BASELINE->stack_base + 1));
        if (IS_BLANK(SPARE)) {
            //
            // Had to allow failure for ([_]: fail "a" except [...]), but if
            // there was no except to handle it then a desire to suppress the
            // result means it should fail.
            //
            // !!! Review if it should actually force a raised error.
            //
            if (not Is_Raised(OUT))
                Init_None(OUT);  // can't be void (we overwrote the cell)
        }
        else if (Is_Blackhole(SPARE)) {
            // pass through everything (even failures), e.g. no assignment
        }
        else if (ANY_META_KIND(VAL_TYPE(SPARE))) {
            if (Is_Stale(OUT))
                Init_Meta_Of_Void(OUT);
            else
                Meta_Quotify(OUT);

            Set_Var_May_Fail(SPARE, SPECIFIED, OUT);
        }
        else if (Is_Stale(OUT)) {
            Set_Var_May_Fail(
                SPARE, SPECIFIED,
                VOID_CELL
            );
        }
        else if (Is_Raised(OUT)) {
            // Don't assign, but let (trap [[a b]: transcode "1&aa"]) work
        }
        else if (
            Is_Isotope(OUT)
            and Not_Cell_Flag(OUT, SCANT_EVALUATED_ISOTOPE)  // from QUASI!
            and Pointer_To_Decayed(OUT) == OUT  // don't corrupt OUT
        ){
            fail (Error_Bad_Isotope(OUT));
        }
        else {  // regular assignment
            Set_Var_May_Fail(
                SPARE, SPECIFIED,
                Pointer_To_Decayed(OUT)
            );
        }

        // Function machinery did the proxying.  If a return element was
        // "circled" then it becomes the overall return.  (This will be put
        // in as part of the function machinery behavior also, soon.)
        //
        if (not Is_Raised(OUT) and stackindex_circled != 0) {
            Copy_Cell(SPARE, Data_Stack_At(stackindex_circled));  // stable
            Get_Var_May_Fail(  // (PICK* can be methodized, hence SPARE stable)
                OUT,
                SPARE,
                SPECIFIED,
                true  // any
            );
        }

        Drop_Data_Stack_To(BASELINE->stack_base);

        // We've just changed the values of variables, and these variables
        // might be coming up next.  Consider:
        //
        //     304 = [a]: test 1020
        //     a = 304
        //
        // The `a` was fetched and found to not be enfix, and in the process
        // its value was known.  But then we assigned that a with a new value
        // in the implementation of SET-BLOCK! here, so, it's incorrect.
        //
        f_next_gotten = nullptr;

        break; }


    //=//// META-BLOCK! ////////////////////////////////////////////////////=//
    //
    // Just produces a quoted version of the block it is given:
    //
    //    >> ^[a b c]
    //    == '[a b c]
    //
    // (It's hard to think of another meaning that would be sensible.)

      case REB_META_BLOCK:
        Inertly_Derelativize_Inheriting_Const(OUT, f_current, f->feed);
        mutable_HEART_BYTE(OUT) = REB_BLOCK;
        Quotify(OUT, 1);
        break;


    //=////////////////////////////////////////////////////////////////////=//
    //
    // Treat all the other Is_Bindable() types as inert
    //
    //=////////////////////////////////////////////////////////////////////=//

      case REB_THE_BLOCK:
      case REB_THE_WORD:
      case REB_THE_PATH:
      case REB_THE_TUPLE:
      case REB_THE_GROUP:
        //
      case REB_BLOCK:
        //
      case REB_BINARY:
        //
      case REB_TEXT:
      case REB_FILE:
      case REB_EMAIL:
      case REB_URL:
      case REB_TAG:
      case REB_ISSUE:
        //
      case REB_BITSET:
        //
      case REB_MAP:
        //
      case REB_VARARGS:
        //
      case REB_OBJECT:
      case REB_FRAME:
      case REB_MODULE:
      case REB_ERROR:
      case REB_PORT:
        goto inert;


    //=///////////////////////////////////////////////////////////////////=//
    //
    // Treat all the other NOT Is_Bindable() types as inert
    //
    //=///////////////////////////////////////////////////////////////////=//

      case REB_BLANK:  // new behavior, evaluate to NULL
        Init_Nulled(OUT);
        break;

    inert:
      case REB_LOGIC:
      case REB_INTEGER:
      case REB_DECIMAL:
      case REB_PERCENT:
      case REB_MONEY:
      case REB_PAIR:
      case REB_TIME:
      case REB_DATE:
        //
      case REB_DATATYPE:
      case REB_TYPESET:
        //
      case REB_EVENT:
      case REB_HANDLE:
        //
      case REB_CUSTOM:  // custom types (IMAGE!, VECTOR!) are all inert
        Inertly_Derelativize_Inheriting_Const(OUT, f_current, f->feed);
        break;


    //=//// GARBAGE (pseudotypes or otherwise //////////////////////////////=//

      default:
        panic (f_current);
    }}

  //=//// END MAIN SWITCH STATEMENT ///////////////////////////////////////=//

    // The UNEVALUATED flag is one of the bits that doesn't get copied by
    // Copy_Cell() or Derelativize().  Hence it can be overkill to clear it
    // off if one knows a value came from doing those things.  This test at
    // the end checks to make sure that the right thing happened.
    //
    // !!! This check requires caching the kind of `v` at the start of switch.
    // Is it worth it to do so?
    //
    /*if (ANY_INERT_KIND(kind_current)) {  // if() to check which part failed
        assert(Get_Cell_Flag(OUT, UNEVALUATED));
    }
    else if (Get_Cell_Flag(OUT, UNEVALUATED)) {
        //
        // !!! Should ONLY happen if we processed a WORD! that looked up to
        // an invisible function, and left something behind that was not
        // previously evaluative.  To track this accurately, we would have
        // to use an FRAME_FLAG_DEBUG_INVISIBLE_UNEVALUATIVE here, because we
        // don't have the word anymore to look up (and even if we did, what
        // it looks up to may have changed).
        //
        assert(kind_current == REB_WORD or ANY_INERT(OUT));
    }*/

    // We're sitting at what "looks like the end" of an evaluation step.
    // But we still have to consider enfix.  e.g.
    //
    //    [pos val]: evaluate [1 + 2 * 3]
    //
    // We want that to give a position of [] and `val = 9`.  The evaluator
    // cannot just dispatch on REB_INTEGER in the switch() above, give you 1,
    // and consider its job done.  It has to notice that the word `+` looks up
    // to an ACTION! that was assigned with SET/ENFIX, and keep going.
    //
    // Next, there's a subtlety with FEED_FLAG_NO_LOOKAHEAD which explains why
    // processing of the 2 argument doesn't greedily continue to advance, but
    // waits for `1 + 2` to finish.  This is because the right hand argument
    // of math operations tend to be declared #tight.
    //
    // Note that invisible functions have to be considered in the lookahead
    // also.  Consider this case:
    //
    //    [pos val]: evaluate [1 + 2 * comment ["hi"] 3 4 / 5]
    //
    // We want `val = 9`, with `pos = [4 / 5]`.  To do this, we
    // can't consider an evaluation finished until all the "invisibles" have
    // been processed.
    //
    // If that's not enough to consider :-) it can even be the case that
    // subsequent enfix gets "deferred".  Then, possibly later the evaluated
    // value gets re-fed back in, and we jump right to this post-switch point
    // to give it a "second chance" to take the enfix.  (See 'deferred'.)
    //
    // So this post-switch step is where all of it happens, and it's tricky!

  lookahead:

    // If something was run with the expectation it should take the next arg
    // from the output cell, and an evaluation cycle ran that wasn't an
    // ACTION! (or that was an arity-0 action), that's not what was meant.
    // But it can happen, e.g. `x: 10 | x ->-`, where ->- doesn't get an
    // opportunity to quote left because it has no argument...and instead
    // retriggers and lets x run.

    if (Get_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT)) {
        if (Get_Executor_Flag(EVAL, f, DIDNT_LEFT_QUOTE_TUPLE))
            fail (Error_Literal_Left_Tuple_Raw());

        assert(!"Unexpected lack of use of NEXT_ARG_FROM_OUT");
    }

  //=//// IF NOT A WORD!, IT DEFINITELY STARTS A NEW EXPRESSION ///////////=//

    // For long-pondered technical reasons, only WORD! is able to dispatch
    // enfix.  If it's necessary to dispatch an enfix function via path, then
    // a word is used to do it, like `>-` in `x: >- lib.method [...] [...]`.

    if (Is_Feed_At_End(f->feed)) {
        Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);
        goto finished;  // hitting end is common, avoid do_next's switch()
    }

    switch (VAL_TYPE_UNCHECKED(f_next)) {
      case REB_WORD:
        break;  // need to check for lookahead

      default:
        Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);
        goto finished;
    }

  //=//// FETCH WORD! TO PERFORM SPECIAL HANDLING FOR ENFIX/INVISIBLES ////=//

    // First things first, we fetch the WORD! (if not previously fetched) so
    // we can see if it looks up to any kind of ACTION! at all.

    if (not f_next_gotten)
        f_next_gotten = Lookup_Word(f_next, FEED_SPECIFIER(f->feed));
    else
        assert(f_next_gotten == Lookup_Word(f_next, FEED_SPECIFIER(f->feed)));

  //=//// NEW EXPRESSION IF UNBOUND, NON-FUNCTION, OR NON-ENFIX ///////////=//

    // These cases represent finding the start of a new expression.
    //
    // Fall back on word-like "dispatch" even if ->gotten is null (unset or
    // unbound word).  It'll be an error, but that code path raises it for us.

    if (
        not f_next_gotten
        or REB_ACTION != VAL_TYPE_UNCHECKED(unwrap(f_next_gotten))
        or Not_Action_Flag(VAL_ACTION(unwrap(f_next_gotten)), ENFIXED)
    ){
      lookback_quote_too_late: // run as if starting new expression

        Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);
        Clear_Executor_Flag(EVAL, f, INERT_OPTIMIZATION);

        // Since it's a new expression, EVALUATE doesn't want to run it
        // even if invisible, as it's not completely invisible (enfixed)
        //
        goto finished;
    }

  //=//// IS WORD ENFIXEDLY TIED TO A FUNCTION (MAY BE "INVISIBLE") ///////=//

  blockscope {
    Action(*) enfixed = VAL_ACTION(unwrap(f_next_gotten));

    if (Get_Action_Flag(enfixed, QUOTES_FIRST)) {
        //
        // Left-quoting by enfix needs to be done in the lookahead before an
        // evaluation, not this one that's after.  This happens in cases like:
        //
        //     left-the: enfix func [:value] [:value]
        //     the <something> left-the
        //
        // But due to the existence of <end>-able and <skip>-able parameters,
        // the left quoting function might be okay with seeing nothing on the
        // left.  Start a new expression and let it error if that's not ok.
        //
        assert(Not_Executor_Flag(EVAL, f, DIDNT_LEFT_QUOTE_TUPLE));
        if (Get_Executor_Flag(EVAL, f, DIDNT_LEFT_QUOTE_TUPLE))
            fail (Error_Literal_Left_Tuple_Raw());

        const REBPAR *first = First_Unspecialized_Param(nullptr, enfixed);
        if (VAL_PARAM_CLASS(first) == PARAM_CLASS_SOFT) {
            if (Get_Feed_Flag(f->feed, NO_LOOKAHEAD)) {
                Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);
                Clear_Executor_Flag(EVAL, f, INERT_OPTIMIZATION);
                goto finished;
            }
        }
        else if (Not_Executor_Flag(EVAL, f, INERT_OPTIMIZATION))
            goto lookback_quote_too_late;
    }

    Clear_Executor_Flag(EVAL, f, INERT_OPTIMIZATION);  // served purpose if set

    if (
        Get_Executor_Flag(EVAL, f, FULFILLING_ARG)
        and not (Get_Action_Flag(enfixed, DEFERS_LOOKBACK)
                                       // ^-- `1 + if false [2] else [3]` => 4
        )
    ){
        if (Get_Feed_Flag(f->feed, NO_LOOKAHEAD)) {
            // Don't do enfix lookahead if asked *not* to look.

            Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);

            assert(Not_Feed_Flag(f->feed, DEFERRING_ENFIX));
            Set_Feed_Flag(f->feed, DEFERRING_ENFIX);

            goto finished;
        }

        Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);
    }

    // A deferral occurs, e.g. with:
    //
    //     return if condition [...] else [...]
    //
    // The first time the ELSE is seen, IF is fulfilling its branch argument
    // and doesn't know if its done or not.  So this code senses that and
    // runs, returning the output without running ELSE, but setting a flag
    // to know not to do the deferral more than once.
    //
    if (
        Get_Executor_Flag(EVAL, f, FULFILLING_ARG)
        and (
            Get_Action_Flag(enfixed, POSTPONES_ENTIRELY)
            or (
                Get_Action_Flag(enfixed, DEFERS_LOOKBACK)
                and Not_Feed_Flag(f->feed, DEFERRING_ENFIX)
            )
        )
    ){
        if (
            Is_Action_Frame(f->prior)
            and Get_Executor_Flag(ACTION, f->prior, ERROR_ON_DEFERRED_ENFIX)
        ){
            // Operations that inline functions by proxy (such as MATCH and
            // ENSURE) cannot directly interoperate with THEN or ELSE...they
            // are building a frame with PG_Dummy_Action as the function, so
            // running a deferred operation in the same step is not an option.
            // The expression to the left must be in a GROUP!.
            //
            fail (Error_Ambiguous_Infix_Raw());
        }

        Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);

        if (
            Is_Action_Frame(f->prior)
            //
            // ^-- !!! Before stackless it was always the case when we got
            // here that a function frame was fulfilling, because SET-WORD!
            // would reuse frames while fulfilling arguments...but stackless
            // changed this and has SET-WORD! start new frames.  Review.
            //
            and not Is_Action_Frame_Fulfilling(f->prior)
        ){
            // This should mean it's a variadic frame, e.g. when we have
            // the 2 in the output slot and are at the THEN in:
            //
            //     variadic2 1 2 then (t => [print ["t is" t] <then>])
            //
            // We want to treat this like a barrier.
            //
            Set_Feed_Flag(f->feed, BARRIER_HIT);
            goto finished;
        }

        Set_Feed_Flag(f->feed, DEFERRING_ENFIX);

        // Leave enfix operator pending in the frame.  It's up to the parent
        // frame to decide whether to ST_EVALUATOR_LOOKING_AHEAD to jump
        // back in and finish fulfilling this arg or not.  If it does resume
        // and we get to this check again, f->prior->deferred can't be null,
        // otherwise it would be an infinite loop.
        //
        goto finished;
    }

    Clear_Feed_Flag(f->feed, DEFERRING_ENFIX);

    // An evaluative lookback argument we don't want to defer, e.g. a normal
    // argument or a deferable one which is not being requested in the context
    // of parameter fulfillment.  We want to reuse the OUT value and get it
    // into the new function's frame.

    Frame(*) subframe = Make_Action_Subframe(f);
    Push_Frame(OUT, subframe);
    Push_Action(subframe, enfixed, VAL_ACTION_BINDING(unwrap(f_next_gotten)));
    Begin_Enfix_Action(subframe, VAL_WORD_SYMBOL(f_next));

    Fetch_Next_Forget_Lookback(f);  // advances next

    Set_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT);

    goto process_action; }

  finished:

    // Want to keep this flag between an operation and an ensuing enfix in
    // the same frame, so can't clear in Drop_Action(), e.g. due to:
    //
    //     left-the: enfix :the
    //     o: make object! [f: does [1]]
    //     o.f left-the  ; want error suggesting >- here, need flag for that
    //
    Clear_Executor_Flag(EVAL, f, DIDNT_LEFT_QUOTE_TUPLE);
    assert(Not_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT));  // must be consumed

  #if !defined(NDEBUG)
    Evaluator_Exit_Checks_Debug(f);
  #endif

    if (
        Not_Frame_At_End(f)
        and Not_Executor_Flag(EVAL, f, SINGLE_STEP)
    ){
        if (Is_Raised(OUT))
            fail (VAL_CONTEXT(OUT));

        STATE = ST_EVALUATOR_STEPPING_AGAIN;
        return BOUNCE_CONTINUE;  // go through trampoline, for debug hooking
    }

    return OUT;

  return_thrown:

  #if !defined(NDEBUG)
    Evaluator_Exit_Checks_Debug(f);
  #endif

    return BOUNCE_THROWN;
}
