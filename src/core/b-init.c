//
//  File: %b-init.c
//  Summary: "initialization functions"
//  Section: bootstrap
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
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
// The primary routine for starting up Rebol is Startup_Core().  It runs the
// bootstrap in phases, based on processing various portions of the data in
// %tmp-boot-block.r (which is the aggregated code from the %mezz/*.r files,
// packed into one file as part of the build preparation).
//
// As part of an effort to lock down the memory usage, Ren-C added a parallel
// Shutdown_Core() routine which would gracefully exit Rebol, with assurances
// that all accounting was done correctly.  This includes being sure that the
// number used to track memory usage for triggering garbage collections would
// balance back out to exactly zero.
//
// (Release builds can instead close only vital resources like files, and
// trust the OS exit() to reclaim memory more quickly.  However Ren-C's goal
// is to be usable as a library that may be initialized and shutdown within
// a process that's not exiting, so the ability to clean up is important.)
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * The core language startup process does not include any command-line
//   processing.  That is left up to the API client and whether such processing
//   is relevant.  If it is, then tools like PARSE are available to use.  So
//   if any switches are needed to affect the boot process itself, those are
//   currently done with environment variables.
//
// * In order to make sure startup and shutdown can balance, during shutdown
//   the libRebol API will call shutdown, then startup, then shutdown again.
//   So if you're seeing slow performance on shutdown, check the debug flag.
//

#include "sys-core.h"

#define EVAL_DOSE 10000


//
//  Check_Basics: C
//
// Initially these checks were in the debug build only.  However, they are so
// foundational that it's probably worth getting a coherent crash in any build
// where these tests don't work.
//
static void Check_Basics(void)
{
    //=//// CHECK REBVAL SIZE ////////////////////////////////////////////=//

    // The system is designed with the intent that REBVAL is 4x(32-bit) on
    // 32-bit platforms and 4x(64-bit) on 64-bit platforms.  It's a crtical
    // performance point.  For the moment we consider it to be essential
    // enough that the system that it refuses to run if not true.
    //
    // But if someone is in an odd situation with a larger sized cell--and
    // it's an even multiple of ALIGN_SIZE--it may still work.  For instance:
    // the DEBUG_TRACK_EXTEND_CELLS mode doubles the cell size to carry the
    // file, line, and tick of their initialization (or last TOUCH_CELL()).
    // Define UNUSUAL_REBVAL_SIZE to bypass this check.

    size_t sizeof_REBVAL = sizeof(REBVAL);  // in variable avoids warning
  #if defined(UNUSUAL_REBVAL_SIZE)
    if (sizeof_REBVAL % ALIGN_SIZE != 0)
        panic ("size of REBVAL does not evenly divide by ALIGN_SIZE");
  #else
    if (sizeof_REBVAL != sizeof(void*) * 4)
        panic ("size of REBVAL is not sizeof(void*) * 4");

    #if defined(DEBUG_SERIES_ORIGINS) || defined(DEBUG_COUNT_TICKS)
        assert(sizeof(REBSER) == sizeof(REBVAL) * 2 + sizeof(void*) * 2);
    #else
        assert(sizeof(REBSER) == sizeof(REBVAL) * 2);
    #endif
  #endif

    //=//// CHECK REBSER INFO PLACEMENT ///////////////////////////////////=//

    // REBSER places the `info` bits exactly after a REBVAL so they can do
    // double-duty as terminator for that REBVAL when enumerated as an ARRAY.

  blockscope {
    size_t offset = offsetof(REBSER, info.flags);  // variable avoids warning
    if (offset - offsetof(REBSER, content) != sizeof(REBVAL))
        panic ("bad structure alignment for internal array termination"); }

    //=//// CHECK BYTE-ORDERING SENSITIVE FLAGS //////////////////////////=//

    // See the %sys-node.h file for an explanation of what these are, and
    // why having them work is fundamental to the API.

    REBFLGS flags
        = FLAG_LEFT_BIT(5) | FLAG_SECOND_BYTE(21) | FLAG_SECOND_UINT16(1975);

    REBYTE m = FIRST_BYTE(flags);  // 6th bit from left set (0b00000100 is 4)
    REBYTE d = SECOND_BYTE(flags);
    uint16_t y = SECOND_UINT16(flags);
    if (m != 4 or d != 21 or y != 1975) {
      #if !defined(NDEBUG)
        printf("m = %u, d = %u, y = %u\n", m, d, y);
      #endif
        panic ("Bad composed integer assignment for byte-ordering macro.");
    }
}


#if !defined(OS_STACK_GROWS_UP) && !defined(OS_STACK_GROWS_DOWN)
    //
    // This is a naive guess with no guarantees.  If there *is* a "real"
    // answer, it would be fairly nuts:
    //
    // http://stackoverflow.com/a/33222085/211160
    //
    // Prefer using a build configuration #define, if possible (although
    // emscripten doesn't necessarily guarantee up or down):
    //
    // https://github.com/kripken/emscripten/issues/5410
    //
    bool Guess_If_Stack_Grows_Up(int *p) {
        int i;
        if (not p)
            return Guess_If_Stack_Grows_Up(&i);  // RECURSION: avoids inlining
        if (p < &i)  // !!! this comparison is undefined behavior
            return true;  // upward
        return false;  // downward
    }
#endif


//
//  Set_Stack_Limit: C
//
// See C_STACK_OVERFLOWING for remarks on this **non-standard** technique of
// stack overflow detection.  Note that each thread would have its own stack
// address limits, so this has to be updated for threading.
//
// Currently, this is called every time PUSH_TRAP() is called when Saved_State
// is NULL, and hopefully only one instance of it per thread will be in effect
// (otherwise, the bounds would add and be useless).
//
void Set_Stack_Limit(void *base, uintptr_t bounds) {
  #if defined(OS_STACK_GROWS_UP)
    TG_Stack_Limit = cast(uintptr_t, base) + bounds;
  #elif defined(OS_STACK_GROWS_DOWN)
    TG_Stack_Limit = cast(uintptr_t, base) - bounds;
  #else
    TG_Stack_Grows_Up = Guess_If_Stack_Grows_Up(NULL);
    if (TG_Stack_Grows_Up)
        TG_Stack_Limit = cast(uintptr_t, base) + bounds;
    else
        TG_Stack_Limit = cast(uintptr_t, base) - bounds;
  #endif
}


//
//  Startup_Lib: C
//
// Since no good literal form exists, the %sysobj.r file uses the words.  They
// have to be defined before the point that it runs (along with the natives).
//
static void Startup_Lib(void)
{
    REBCTX *lib = Alloc_Context_Core(REB_MODULE, 1, NODE_FLAG_MANAGED);
    Lib_Context_Value = Alloc_Value();
    Init_Any_Context(Lib_Context_Value, REB_MODULE, lib);
    Lib_Context = VAL_CONTEXT(Lib_Context_Value);

  //=//// INITIALIZE LIB PATCHES ///////////////////////////////////////////=//

    for (REBLEN i = 1; i < LIB_SYMS_MAX; ++i) {
        REBARR *patch = &PG_Lib_Patches[i];
        patch->leader.bits = NODE_FLAG_NODE
            | FLAG_FLAVOR(PATCH)  // checked when setting LINK(PatchContext)
            | PATCH_FLAG_LET
            | NODE_FLAG_MANAGED
            | SERIES_FLAG_LINK_NODE_NEEDS_MARK
            | SERIES_FLAG_INFO_NODE_NEEDS_MARK;

        mutable_LINK(PatchContext, patch) = nullptr;  // signals unused
        Prep_Cell(ARR_SINGLE(ARR(patch)));  // should overwrite
    }

  //=//// INITIALIZE EARLY BOOT USED VALUES ////////////////////////////////=//

    // These have various applications, such as BLANK! is used during scanning
    // to build a path like `/a/b` out of an array with [_ a b] in it.  Since
    // the scanner is also what would load code like `blank: _`, we need to
    // seed the values to get the ball rolling.

    Init_Nulled(force_Lib(NULL));
    assert(IS_FALSEY(Lib(NULL)) and IS_NULLED(Lib(NULL)));

    Init_Blank(force_Lib(BLANK));
    assert(IS_FALSEY(Lib(BLANK)) and IS_BLANK(Lib(BLANK)));

    // !!! Rebol is firm on TRUE and FALSE being WORD!s, as opposed to the
    // literal forms of logical true and false.  Not only does this frequently
    // lead to confusion, but there's not consensus on what a good literal form
    // would be. R3-Alpha used #[true] and #[false] (but often molded them as
    // looking like the words true and false anyway).  $true and $false have
    // been proposed, but would not be backward compatible in files read
    // by bootstrap.
    //
    Init_True(force_Lib(TRUE));
    Init_False(force_Lib(FALSE));
    assert(IS_TRUTHY(Lib(TRUE)) and VAL_LOGIC(Lib(TRUE)) == true);
    assert(IS_FALSEY(Lib(FALSE)) and VAL_LOGIC(Lib(FALSE)) == false);

    // !!! Other constants are just initialized as part of Startup_Base().
}


//
//  Shutdown_Lib: C
//
static void Shutdown_Lib(void)
{
    // !!! Since the PG_Lib_Patches are REBSER that live outside the pools,
    // the Shutdown_GC() will not kill them off.  We want to make sure the
    // variables are RESET() and that the patches look empty in case the
    // Startup() gets called again.
    //
    for (REBLEN i = 1; i < LIB_SYMS_MAX; ++i) {
        REBARR *patch = &PG_Lib_Patches[i];

        if (LINK(PatchContext, patch) == nullptr)
            continue;  // was never initialized !!! should it not be in lib?

        RESET(ARR_SINGLE(patch));
        Decay_Series(patch);

        // !!! Typically nodes aren't zeroed out when they are freed.  Since
        // this one is a global, it is set to nullptr just to indicate that
        // the freeing process happened.  Should all nodes be zeroed?
        //
        mutable_LINK(PatchContext, patch) = nullptr;
    }

    rebRelease(Lib_Context_Value);
    Lib_Context_Value = nullptr;
    Lib_Context = nullptr;
}


static REBVAL *Make_Locked_Tag(const char *utf8) { // helper
    REBVAL *t = rebText(utf8);
    mutable_KIND3Q_BYTE(t) = REB_TAG;
    mutable_HEART_BYTE(t) = REB_TAG;

    Force_Value_Frozen_Deep(t);
    return t;
}

//
//  Init_Action_Spec_Tags: C
//
// FUNC and PROC search for these tags, like <opt> and <local>.  They are
// natives and run during bootstrap, so these string comparisons are
// needed.
//
static void Init_Action_Spec_Tags(void)
{
    Root_None_Tag = Make_Locked_Tag("none");
    Root_With_Tag = Make_Locked_Tag("with");
    Root_Variadic_Tag = Make_Locked_Tag("variadic");
    Root_Opt_Tag = Make_Locked_Tag("opt");
    Root_End_Tag = Make_Locked_Tag("end");
    Root_Blank_Tag = Make_Locked_Tag("blank");
    Root_Local_Tag = Make_Locked_Tag("local");
    Root_Skip_Tag = Make_Locked_Tag("skip");
    Root_Const_Tag = Make_Locked_Tag("const");
    Root_Invisible_Tag = Make_Locked_Tag("invisible");
    Root_Void_Tag = Make_Locked_Tag("void");

    // !!! Needed for bootstrap, as `@arg` won't LOAD in old r3
    //
    Root_Meta_Tag = Make_Locked_Tag("meta");

    // Used by SPECIALIZE as a unique identity for telling what's been
    // specialized and what hasn't.
    //
    Root_Unspecialized_Tag = Make_Locked_Tag("unspecialized");
}

static void Shutdown_Action_Spec_Tags(void)
{
    rebRelease(Root_None_Tag);
    rebRelease(Root_With_Tag);
    rebRelease(Root_Variadic_Tag);
    rebRelease(Root_Opt_Tag);
    rebRelease(Root_End_Tag);
    rebRelease(Root_Blank_Tag);
    rebRelease(Root_Local_Tag);
    rebRelease(Root_Skip_Tag);
    rebRelease(Root_Const_Tag);
    rebRelease(Root_Invisible_Tag);
    rebRelease(Root_Void_Tag);

    rebRelease(Root_Meta_Tag);  // !!! only needed for bootstrap with old r3

    rebRelease(Root_Unspecialized_Tag);
}


//
//  Startup_End_Node: C
//
// We can't actually put an end value in the middle of a block, so we poke
// this one into a program global.  It is not legal to bit-copy an END (you
// always use SET_END), so we can make it unwritable.
//
static void Startup_End_Node(void)
{
    SET_END(Prep_Cell(&PG_End_Cell));
    assert(IS_END(END_CELL));  // sanity check
}


//
//  Startup_Empty_Arrays: C
//
// Generic read-only empty array, which will be put into EMPTY_BLOCK when
// Alloc_Value() is available.  Note it's too early for ARRAY_HAS_FILE_LINE.
//
// Warning: GC must not run before Init_Root_Vars() puts it in an API node!
//
static void Startup_Empty_Arrays(void)
{
    PG_Empty_Array = Make_Array_Core(0, NODE_FLAG_MANAGED);
    Freeze_Array_Deep(PG_Empty_Array);

    // "Empty" PATH!s that look like `/` are actually a WORD! cell format
    // under the hood.  This allows them to have bindings and do double-duty
    // for actions like division or other custom purposes.  But when they
    // are accessed as an array, they give two blanks `[_ _]`.
    //
  blockscope {
    REBARR *a = Make_Array_Core(2, NODE_FLAG_MANAGED);
    Init_Blank(ARR_AT(a, 0));
    Init_Blank(ARR_AT(a, 1));
    SET_SERIES_LEN(a, 2);
    Freeze_Array_Deep(a);
    PG_2_Blanks_Array = a;
  }

    // !!! See comments in Make_Expired_Frame_Ctx_Managed(); it makes a
    // varlist for a particular action, but it seems this is working without?
    //
  blockscope {
    REBARR *varlist = Alloc_Singular(
        FLAG_FLAVOR(VARLIST) | NODE_FLAG_MANAGED  // !!! not dynamic
    );
    varlist->leader.bits |= SERIES_MASK_VARLIST;  // !!! adds dynamic
    CLEAR_SERIES_FLAG(varlist, DYNAMIC);  // !!! removes (review cleaner way)
    SET_SERIES_FLAG(varlist, INACCESSIBLE);

    assert(PG_Inaccessible_Varlist == nullptr);
    PG_Inaccessible_Varlist = varlist;
  }
}

static void Shutdown_Empty_Arrays(void) {
    PG_Empty_Array = nullptr;
    PG_2_Blanks_Array = nullptr;
    PG_Inaccessible_Varlist = nullptr;
}


//
//  Init_Root_Vars: C
//
// Create some global variables that are useful, and need to be safe from
// garbage collection.  This relies on the mechanic from the API, where
// handles are kept around until they are rebRelease()'d.
//
// This is called early, so there are some special concerns to building the
// values that would not apply later in boot.
//
static void Init_Root_Vars(void)
{
    // Simple isolated values, not available via lib, e.g. not Lib(TRUE) or
    // Lib(BLANK)...
    //
    // They should only be accessed by macros which retrieve their values
    // as `const`, to avoid the risk of accidentally changing them.  (This
    // rule is broken by some special system code which `m_cast`s them for
    // the purpose of using them as directly recognizable pointers which
    // also look like values.)
    //
    // It is presumed that these types will never need to have GC behavior,
    // and thus can be stored safely in program globals without mention in
    // the root set.  Should that change, they could be explicitly added
    // to the GC's root set.

    RESET_CELL(Prep_Cell(&PG_Meta_Value), REB_META, CELL_MASK_NONE);
    RESET_CELL(Prep_Cell(&PG_The_Value), REB_THE, CELL_MASK_NONE);

    Init_Return_Signal(Prep_Cell(&PG_R_Thrown), C_THROWN);
    Init_Return_Signal(Prep_Cell(&PG_R_Invisible), C_INVISIBLE);
    Init_Return_Signal(Prep_Cell(&PG_R_Redo_Unchecked), C_REDO_UNCHECKED);
    Init_Return_Signal(Prep_Cell(&PG_R_Redo_Checked), C_REDO_CHECKED);
    Init_Return_Signal(Prep_Cell(&PG_R_Unhandled), C_UNHANDLED);

    Root_Empty_Block = Init_Block(Alloc_Value(), PG_Empty_Array);
    Force_Value_Frozen_Deep(Root_Empty_Block);

    // Note: has to be a BLOCK!, 2-element blank paths use SYM__SLASH_1_
    //
    Root_2_Blanks_Block = Init_Block(Alloc_Value(), PG_2_Blanks_Array);
    Force_Value_Frozen_Deep(Root_2_Blanks_Block);

    // Note: rebText() can't run yet, review.
    //
    REBSTR *nulled_uni = Make_String(1);

  #if !defined(NDEBUG)
    REBUNI test_nul;
    NEXT_CHR(&test_nul, STR_AT(nulled_uni, 0));
    assert(test_nul == '\0');
    assert(STR_LEN(nulled_uni) == 0);
  #endif

    Root_Empty_Text = Init_Text(Alloc_Value(), nulled_uni);
    Force_Value_Frozen_Deep(Root_Empty_Text);

    Root_Empty_Binary = Init_Binary(Alloc_Value(), Make_Binary(0));
    Force_Value_Frozen_Deep(Root_Empty_Binary);
}

static void Shutdown_Root_Vars(void)
{
    RESET(&PG_End_Cell);
    RESET(&PG_Meta_Value);
    RESET(&PG_The_Value);
    RESET(&PG_Unset_Value);
    RESET(&PG_Void_Value);

    RESET(&PG_R_Thrown);
    RESET(&PG_R_Invisible);
    RESET(&PG_R_Redo_Unchecked);
    RESET(&PG_R_Redo_Checked);
    RESET(&PG_R_Unhandled);

    rebRelease(Root_Empty_Text);
    Root_Empty_Text = nullptr;
    rebRelease(Root_Empty_Block);
    Root_Empty_Block = nullptr;
    rebRelease(Root_2_Blanks_Block);
    Root_2_Blanks_Block = nullptr;
    rebRelease(Root_Empty_Binary);
    Root_Empty_Binary = nullptr;
}


//
//  Init_System_Object: C
//
// Evaluate the system object and create the global SYSTEM word.  We do not
// BIND_ALL here to keep the internal system words out of the global context.
// (See also N_context() which creates the subobjects of the system object.)
//
static void Init_System_Object(
    const REBVAL *boot_sysobj_spec,
    REBARR *datatypes_catalog,
    REBARR *natives_catalog,
    REBARR *generics_catalog,
    REBCTX *errors_catalog
) {
    assert(VAL_INDEX(boot_sysobj_spec) == 0);
    const RELVAL *spec_tail;
    RELVAL *spec_head
        = VAL_ARRAY_KNOWN_MUTABLE_AT(&spec_tail, boot_sysobj_spec);

    // Create the system object from the sysobj block (defined in %sysobj.r)
    //
    REBCTX *system = Make_Context_Detect_Managed(
        REB_OBJECT, // type
        spec_head, // scan for toplevel set-words
        spec_tail,
        nullptr  // parent
    );

    // Create a global value for it in the Lib context, so we can say things
    // like `system/contexts` (also protects newly made context from GC).
    //
    Init_Object(force_Lib(SYSTEM), system);

    Bind_Values_Deep(spec_head, spec_tail, Lib_Context_Value);

    // Bind it so CONTEXT native will work (only used at topmost depth)
    //
    Bind_Values_Shallow(spec_head, spec_tail, CTX_ARCHETYPE(system));

    // Evaluate the block (will eval CONTEXTs within).  Expects void result.
    //
    DECLARE_LOCAL (result);
    if (Do_Any_Array_At_Throws(result, boot_sysobj_spec, SPECIFIED))
        panic (result);
    if (not IS_BLANK(result))
        panic (result);

    // Init_Action_Meta_Shim() made Root_Action_Meta as a bootstrap hack
    // since it needed to make function meta information for natives before
    // %sysobj.r's code could run using those natives.  But make sure what it
    // made is actually identical to the definition in %sysobj.r.
    //
    assert(
        0 == CT_Context(
            Get_System(SYS_STANDARD, STD_ACTION_META),
            Root_Action_Meta,
            true  // "strict equality"
        )
    );

    // Create system/catalog/* for datatypes, natives, generics, errors
    //
    Init_Block(Get_System(SYS_CATALOG, CAT_DATATYPES), datatypes_catalog);
    Init_Block(Get_System(SYS_CATALOG, CAT_NATIVES), natives_catalog);
    Init_Block(Get_System(SYS_CATALOG, CAT_ACTIONS), generics_catalog);
    Init_Object(Get_System(SYS_CATALOG, CAT_ERRORS), errors_catalog);

    // Create system/codecs object
    //
    Init_Object(
        Get_System(SYS_CODECS, 0),
        Alloc_Context_Core(REB_OBJECT, 10, NODE_FLAG_MANAGED)
    );

    // The "standard error" template was created as an OBJECT!, because the
    // `make error!` functionality is not ready when %sysobj.r runs.  Fix
    // up its archetype so that it is an actual ERROR!.
    //
  blockscope {
    REBVAL *std_error = Get_System(SYS_STANDARD, STD_ERROR);
    REBCTX *c = VAL_CONTEXT(std_error);
    mutable_KIND3Q_BYTE(std_error) = REB_ERROR;
    mutable_HEART_BYTE(std_error) = REB_ERROR;

    REBVAL *rootvar = CTX_ROOTVAR(c);
  #if !defined(NDEBUG)
    assert(rootvar->header.bits & CELL_FLAG_PROTECTED);
    rootvar->header.bits &= ~CELL_FLAG_PROTECTED;
  #endif
    mutable_KIND3Q_BYTE(rootvar) = REB_ERROR;
    mutable_HEART_BYTE(rootvar) = REB_ERROR;
  #if !defined(NDEBUG)
    rootvar->header.bits |= CELL_FLAG_PROTECTED;
  #endif
  }
}


//
//  Init_Contexts_Object: C
//
// This sets up the system/contexts object.
//
// !!! One of the critical areas in R3-Alpha that was not hammered out
// completely was the question of how the binding process gets started, and
// how contexts might inherit or relate.
//
// However, the basic model for bootstrap is that the "user context" is the
// default area for new code evaluation.  It starts out as a copy of an
// initial state set up in the lib context.  When native routines or other
// content gets overwritten in the user context, it can be borrowed back
// from `system/contexts/lib` (typically aliased as "lib" in the user context).
//
static void Init_Contexts_Object(void)
{
    Copy_Cell(Get_System(SYS_CONTEXTS, CTX_SYS), Sys_Context_Value);

    Copy_Cell(Get_System(SYS_CONTEXTS, CTX_LIB), Lib_Context_Value);

    // We don't initialize the USER context...yet.  Make it more obvious what
    // is wrong if it's used during boot.
    //
    const char *label = "startup-mezz-not-finished-yet";
    Init_Bad_Word(
        Get_System(SYS_CONTEXTS, CTX_USER),
        Intern_UTF8_Managed(cb_cast(label), strsize(label))
    );
}


//
//  Startup_Signals: C
//
// Creating series calls SET_SIGNAL(), and that is done in %m-pools.c right
// now.  That needs the Eval_XXX variables to be initialized at present.
//
void Startup_Signals(void)
{
    Trace_Level = 0;
    TG_Jump_List = nullptr;

  #if !defined(NDEBUG)
    Total_Eval_Cycles_Doublecheck = 0;
  #endif

    Total_Eval_Cycles = 0;
    Eval_Dose = EVAL_DOSE;
    Eval_Countdown = Eval_Dose;
    Eval_Signals = 0;
    Eval_Sigmask = ALL_BITS;
    Eval_Limit = 0;

    TG_Ballast = MEM_BALLAST; // or overwritten by debug build below...
    TG_Max_Ballast = MEM_BALLAST;

  #ifndef NDEBUG
    const char *env_recycle_torture = getenv("R3_RECYCLE_TORTURE");
    if (env_recycle_torture and atoi(env_recycle_torture) != 0)
        TG_Ballast = 0;

    if (TG_Ballast == 0) {
        printf(
            "**\n" \
            "** R3_RECYCLE_TORTURE is nonzero in environment variable!\n" \
            "** (or TG_Ballast is set to 0 manually in the init code)\n" \
            "** Recycling on EVERY evaluator step, *EXTREMELY* SLOW!...\n" \
            "** Useful in finding bugs before you can run RECYCLE/TORTURE\n" \
            "** But you might only want to do this with -O2 debug builds.\n"
            "**\n"
        );
        fflush(stdout);
     }
  #endif

    // The thrown arg is not intended to ever be around long enough to be
    // seen by the GC.
    //
    Prep_Cell(&TG_Thrown_Arg);
  #if !defined(NDEBUG)
    SET_END(&TG_Thrown_Arg);

    Prep_Cell(&TG_Thrown_Label_Debug);
    SET_END(&TG_Thrown_Label_Debug); // see notes, only used "SPORADICALLY()"
  #endif
}


#if !defined(NDEBUG)
    //
    // The C language initializes global variables to zero:
    //
    // https://stackoverflow.com/q/2091499
    //
    // For some values this may risk them being consulted and interpreted as
    // the 0 carrying information, as opposed to them not being ready yet.
    // Any variables that should be trashed up front should do so here.
    //
    static void Startup_Trash_Debug(void) {
        assert(not TG_Top_Frame);
        TRASH_POINTER_IF_DEBUG(TG_Top_Frame);
        assert(not TG_Bottom_Frame);
        TRASH_POINTER_IF_DEBUG(TG_Bottom_Frame);

        // ...add more on a case-by-case basis if the case seems helpful...
    }
#endif



// By this point, the Lib_Context contains basic definitions for things
// like true, false, the natives, and the generics.
//
// It's also possible to trap failures and exit in a graceful fashion.  This is
// the routine protected by rebRescue() so initialization can handle exceptions.
//
static REBVAL *Startup_Mezzanine(BOOT_BLK *boot)
{
  //=//// BASE STARTUP /////////////////////////////////////////////////////=//

    // The code in "base" is the lowest level of initialization written as
    // Rebol code.  This is where things like `+` being an infix form of ADD is
    // set up, or FIRST being a specialization of PICK.  It also has wrappers
    // for more basic natives like FUNC* or SPECIALIZE*, that handle aspects
    // that are easier to write in usermode than in C (like inheriting HELP
    // information).

    rebElide(
        //
        // Code is already interned to Lib_Context by TRANSCODE.  Create
        // actual variables for top-level SET-WORD!s only, and run.
        //
        "bind/only/set", &boot->base, Lib_Context_Value,
        "do", &boot->base  // ENSURE not available yet (but returns blank)
    );


  //=//// SYS STARTUP //////////////////////////////////////////////////////=//

    // The SYS context contains supporting Rebol code for implementing "system"
    // features.  It is lower-level than the LIB context, but has natives,
    // generics, and the definitions from Startup_Base() available.
    //
    // See the helper Sys() for a quick way of getting at the functions by
    // their symbol.
    //
    // (Note: The SYS context should not be confused with "the system object",
    // which is a different thing.)

    rebElide(
        //
        // The scan of the boot block interned everything to Lib_Context, but
        // we want to overwrite that with the Sys_Context here.
        //
        "intern*", Sys_Context_Value, &boot->sys,

        "bind/only/set", &boot->sys, Sys_Context_Value,
        "ensure blank! do", &boot->sys,

        // SYS contains the implementation of the module machinery itself, so
        // we don't have MODULE or EXPORT available.  Do the exports manually,
        // and then import the results to lib.
        //
        "set-meta", Sys_Context_Value, "make object! [",
            "Name: 'System",  // this is MAKE OBJECT!, not MODULE, must quote
            "Exports: [module load load-value decode encode encoding-of]",
        "]",
        "sys.import*", Lib_Context_Value, Sys_Context_Value
    );

    // !!! It was a stated goal at one point that it should be possible to
    // protect the entire system object and still run the interpreter.  That
    // was commented out in R3-Alpha
    //
    //    comment [if :lib/secure [protect-system-object]]


  //=//// MEZZ STARTUP /////////////////////////////////////////////////////=//

    rebElide(
        //
        // The code is already bound non-specifically to the Lib_Context during
        // scanning.
        //
        // (It's not necessarily the greatest idea to have LIB be this
        // flexible.  But as it's not hardened from mutations altogether then
        // prohibiting it doesn't offer any real security...and only causes
        // headaches when trying to do something weird.)

        // Create actual variables for top-level SET-WORD!s only, and run.
        //
        "bind/only/set", SPECIFIC(&boot->mezz), Lib_Context_Value,
        "do", SPECIFIC(&boot->mezz)
    );


  //=//// MAKE USER CONTEXT ////////////////////////////////////////////////=//

    // None of the above code should have needed the "user" context, which is
    // purely application-space.  We probably shouldn't even create it during
    // boot at all.  But at the moment, code like JS-NATIVE or TCC natives
    // need to bind the code they run somewhere.  It's also where API called
    // code runs if called from something like an int main() after boot.
    //
    // Doing this as a proper module creation gives us IMPORT and INTERN (as
    // well as EXPORT...?  When do you export from the user context?)
    //
    assert(User_Context == nullptr);  // shouldn't have existed up to now
    rebElide("system.contexts.user: module [Name: User] []");
    User_Context_Value = Copy_Cell(
        Alloc_Value(),
        Get_System(SYS_CONTEXTS, CTX_USER)
    );
    rebUnmanage(User_Context_Value);
    User_Context = VAL_CONTEXT(User_Context_Value);

    return nullptr;
}


//
//  Startup_Core: C
//
// Initialize the interpreter core.
//
// !!! This will either succeed or "panic".  Panic currently triggers an exit
// to the OS.  The code is not currently written to be able to cleanly shut
// down from a partial initialization.  (It should be.)
//
// The phases of initialization are tracked by PG_Boot_Phase.  Some system
// functions are unavailable at certain phases.
//
// Though most of the initialization is run as C code, some portions are run
// in Rebol.  For instance, GENERIC is a function registered very early on in
// the boot process, which is run from within a block to register more
// functions.
//
// At the tail of the initialization, `finish-init-core` is run.  This Rebol
// function lives in %sys-start.r.   It should be "host agnostic" and not
// assume things about command-line switches (or even that there is a command
// line!)  Converting the code that made such assumptions ongoing.
//
void Startup_Core(void)
{
  #if defined(TO_WINDOWS) && defined(DEBUG_SERIES_ORIGINS)
    Startup_Winstack();  // Do first so shutdown crashes have stack traces
  #endif

  #if !defined(NDEBUG)
    Startup_Trash_Debug();
  #endif

//=//// INITIALIZE TICK COUNT /////////////////////////////////////////////=//

    // The timer tick starts at 1, not 0.  This is because the debug build
    // uses signed timer ticks to double as an extra bit of information in
    // REB_BLANK cells to indicate they are "unreadable".
    //
  #if defined(DEBUG_COUNT_TICKS)
    TG_Tick = 1;
  #endif

//=//// INITIALIZE STACK MARKER METRICS ///////////////////////////////////=//

    // !!! See notes on Set_Stack_Limit() about the dodginess of this
    // approach.  Note also that even with a single evaluator used on multiple
    // threads, you have to trap errors to make sure an attempt is not made
    // to longjmp the state to an address from another thread--hence every
    // thread switch must also be a site of trapping all errors.  (Or the
    // limit must be saved in thread local storage.)

    int dummy;  // variable whose address acts as base of stack for below code
    Set_Stack_Limit(&dummy, DEFAULT_STACK_BOUNDS);

//=//// INITIALIZE BASIC DIAGNOSTICS //////////////////////////////////////=//

  #if defined(TEST_EARLY_BOOT_PANIC)
    panic ("early panic test"); // should crash
  #elif defined(TEST_EARLY_BOOT_FAIL)
    fail (Error_No_Value_Raw(Lib(BLANK))); // same as panic (crash)
  #endif

  #ifdef DEBUG_ENABLE_ALWAYS_MALLOC
    PG_Always_Malloc = false;
  #endif

  #ifdef DEBUG_HAS_PROBE
    PG_Probe_Failures = false;
  #endif

    // Globals
    PG_Boot_Phase = BOOT_START;
    PG_Boot_Level = BOOT_LEVEL_FULL;
    PG_Mem_Usage = 0;
    PG_Mem_Limit = 0;
    Reb_Opts = TRY_ALLOC(REB_OPTS);
    memset(Reb_Opts, 0, sizeof(REB_OPTS));
    TG_Jump_List = nullptr;

    Check_Basics();

//=//// INITIALIZE MEMORY AND ALLOCATORS //////////////////////////////////=//

    Startup_Signals();

    Startup_Pools(0);  // performs allocation, calls SET_SIGNAL()
    Startup_GC();

    Startup_Raw_Print();
    Startup_Scanner();
    Startup_String();

//=//// INITIALIZE API ////////////////////////////////////////////////////=//

    // The API is one means by which variables can be made whose lifetime is
    // indefinite until program shutdown.  In R3-Alpha this was done with
    // boot code that laid out some fixed structure arrays, but it's more
    // general to do it this way.

    Init_Char_Cases();
    Startup_CRC();             // For word hashing
    Set_Random(0);
    Startup_Interning();

    Startup_End_Node();
    Startup_Empty_Arrays();

    Startup_Collector();
    Startup_Mold(MIN_COMMON / 4);

    Startup_Data_Stack(STACK_MIN / 4);
    Startup_Frame_Stack(); // uses Canon() in FRM_FILE() currently

    Startup_Api();

    Startup_Symbols();

//=//// CREATE GLOBAL OBJECTS /////////////////////////////////////////////=//

    Init_Root_Vars();    // Special REBOL values per program

    Init_Action_Spec_Tags(); // Note: uses MOLD_BUF, not available until here

//=//// CREATE SYSTEM MODULES //////////////////////////////////////////////=//

    Startup_Lib();  // establishes Lib_Context and Lib_Context_Value

  #if !defined(NDEBUG)
    Assert_Pointer_Detection_Working();  // uses root series/values to test
  #endif

    REBCTX *sys = Alloc_Context_Core(REB_MODULE, 1, NODE_FLAG_MANAGED);
    Sys_Context_Value = Alloc_Value();
    Init_Any_Context(Sys_Context_Value, REB_MODULE, sys);
    Sys_Context = VAL_CONTEXT(Sys_Context_Value);

//=//// LOAD BOOT BLOCK ///////////////////////////////////////////////////=//

    // The %make-boot.r process takes all the various definitions and
    // mezzanine code and packs it into one compressed string in
    // %tmp-boot-block.c which gets embedded into the executable.  This
    // includes the type list, word list, error message templates, system
    // object, mezzanines, etc.

    size_t utf8_size;
    const int max = -1;  // trust size in gzip data
    REBYTE *utf8 = Decompress_Alloc_Core(
        &utf8_size,
        Boot_Block_Compressed,
        Boot_Block_Compressed_Size,
        max,
        SYM_GZIP
    );

    // The boot code contains portions that are supposed to be interned to the
    // SYS context instead of the LIB context.  But the Base and Mezzanine
    // are interned to the Lib, so go ahead and take advantage of that.
    //
    // (We could separate the text of the SYS portion out, and scan that
    // separately to avoid the extra work.  Not a high priority.)
    //
    REBARR *boot_array = Scan_UTF8_Managed(
        Intern_Unsized_Managed("-tmp-boot-"),
        utf8,
        utf8_size,
        Lib_Context  // used by Base + Mezzanine, overruled in SYS
    );
    PUSH_GC_GUARD(boot_array); // managed, so must be guarded

    rebFree(utf8); // don't need decompressed text after it's scanned

    BOOT_BLK *boot =
        cast(BOOT_BLK*, ARR_HEAD(VAL_ARRAY_KNOWN_MUTABLE(ARR_HEAD(boot_array))));

    // Initialize UNSET_VALUE and VOID_VALUE (must be after symbols loaded)
    //
    Init_Unset(Prep_Cell(&PG_Unset_Value));  // symbol not GC'd
    Init_Void(Prep_Cell(&PG_Void_Value));  // symbol not GC'd

    // ID_OF_SYMBOL(), VAL_WORD_ID() and Canon(XXX) now available

    PG_Boot_Phase = BOOT_LOADED;

//=//// CREATE BASIC VALUES ///////////////////////////////////////////////=//

    // Before any code can start running (even simple bootstrap code), some
    // basic words need to be defined.  For instance: You can't run %sysobj.r
    // unless `true` and `false` have been added to the Lib_Context--they'd be
    // undefined.  And while analyzing the function specs during the
    // definition of natives, things like the <opt> tag are needed as a basis
    // for comparison to see if a usage matches that.

    REBARR *datatypes_catalog = Startup_Datatypes(
        VAL_ARRAY_KNOWN_MUTABLE(&boot->types),
        VAL_ARRAY_KNOWN_MUTABLE(&boot->typespecs)
    );
    Manage_Series(datatypes_catalog);
    PUSH_GC_GUARD(datatypes_catalog);

    // !!! REVIEW: Startup_Typesets() uses symbols, data stack, and
    // adds words to lib--not available untilthis point in time.
    //
    Startup_Typesets();

//=//// RUN CODE BEFORE ERROR HANDLING INITIALIZED ////////////////////////=//

    // boot->natives is from the automatically gathered list of natives found
    // by scanning comments in the C sources for `native: ...` declarations.
    //
    REBARR *natives_catalog = Startup_Natives(SPECIFIC(&boot->natives));
    Manage_Series(natives_catalog);
    PUSH_GC_GUARD(natives_catalog);

    // boot->generics is the list in %generics.r
    //
    REBARR *generics_catalog = Startup_Generics(SPECIFIC(&boot->generics));
    Manage_Series(generics_catalog);
    PUSH_GC_GUARD(generics_catalog);

    // boot->errors is the error definition list from %errors.r
    //
    REBCTX *errors_catalog = Startup_Errors(SPECIFIC(&boot->errors));
    PUSH_GC_GUARD(errors_catalog);

    Init_System_Object(
        SPECIFIC(&boot->sysobj),
        datatypes_catalog,
        natives_catalog,
        generics_catalog,
        errors_catalog
    );

    DROP_GC_GUARD(errors_catalog);
    DROP_GC_GUARD(generics_catalog);
    DROP_GC_GUARD(natives_catalog);
    DROP_GC_GUARD(datatypes_catalog);

    Init_Contexts_Object();

    PG_Boot_Phase = BOOT_ERRORS;

  #if defined(TEST_MID_BOOT_PANIC)
    panic (EMPTY_ARRAY); // panics should be able to give some details by now
  #elif defined(TEST_MID_BOOT_FAIL)
    fail (Error_No_Value_Raw(Lib(BLANK))); // DEBUG->assert, RELEASE->panic
  #endif

    // Pre-make the stack overflow error (so it doesn't need to be made
    // during a stack overflow).  Error creation machinery depends heavily
    // on the system object being initialized, so this can't be done until
    // now.
    //
    Startup_Stackoverflow();

//=//// RUN MEZZANINE CODE NOW THAT ERROR HANDLING IS INITIALIZED /////////=//

    PG_Boot_Phase = BOOT_MEZZ;

    assert(DSP == 0 and FS_TOP == FS_BOTTOM);

    REBVAL *error = rebRescue(cast(REBDNG*, &Startup_Mezzanine), boot);
    if (error) {
        //
        // There is theoretically some level of error recovery that could
        // be done here.  e.g. the evaluator works, it just doesn't have
        // many functions you would expect.  How bad it is depends on
        // whether base and sys ran, so perhaps only errors running "mezz"
        // should be returned.
        //
        // For now, assume any failure to declare the functions in those
        // sections is a critical one.  It may be desirable to tell the
        // caller that the user halted (quitting may not be appropriate if
        // the app is more than just the interpreter)
        //
        // !!! If halt cannot be handled cleanly, it should be set up so
        // that the user isn't even *able* to request a halt at this boot
        // phase.
        //
        panic (error);
    }

    assert(DSP == 0 and FS_TOP == FS_BOTTOM);

    DROP_GC_GUARD(boot_array);

    PG_Boot_Phase = BOOT_DONE;

  #if !defined(NDEBUG)
    Check_Memory_Debug(); // old R3-Alpha check, call here to keep it working
  #endif

    // We don't actually load any extensions during the core startup.  The
    // builtin extensions can be selectively loaded in whatever order the
    // API client wants (they may not want to load all extensions that are
    // built in that were available all the time).
    //
    Startup_Extension_Loader();

    Recycle(); // necessary?
}


//
//  Shutdown_Core: C
//
// The goal of Shutdown_Core() is to release all memory and resources that the
// interpreter has accrued since Startup_Core().  This is a good "sanity check"
// that there aren't unaccounted-for leaks (or semantic errors which such
// leaks may indicate).
//
// Also, being able to clean up is important for a library...which might be
// initialized and shut down multiple times in the same program run.  But
// clients wishing a speedy exit may force an exit to the OS instead of doing
// a clean shut down.  (Note: There still might be some system resources
// that need to be waited on, such as asynchronous writes.)
//
// While some leaks are detected by the debug build during shutdown, even more
// can be found with a tool like Valgrind or Address Sanitizer.
//
void Shutdown_Core(bool clean)
{
  #if !defined(NDEBUG)
    Check_Memory_Debug(); // old R3-Alpha check, call here to keep it working
  #endif

    assert(TG_Jump_List == nullptr);

    // Shutting down extensions is currently considered semantically mandatory,
    // as it may flush writes to files (filesystem extension) or do other
    // work.  If you really want to do a true "unclean shutdown" you can always
    // call exit().
    //
    Shutdown_Extension_Loader();

    if (not clean)
        return;

    // !!! Currently the molding logic uses a test of the Boot_Phase to know
    // if it's safe to check the system object for how many digits to mold.
    // This isn't ideal, but if we are to be able to use PROBE() or other
    // molding-based routines during shutdown, we have to signal not to look
    // for that setting in the system object.
    //
    PG_Boot_Phase = BOOT_START;

    Shutdown_Data_Stack();

    Shutdown_Stackoverflow();
    Shutdown_Typesets();

    Shutdown_Natives();
    Shutdown_Action_Spec_Tags();
    Shutdown_Root_Vars();

    Shutdown_Datatypes();

    Shutdown_Lib();

    rebRelease(Sys_Context_Value);
    Sys_Context_Value = nullptr;
    Sys_Context = nullptr;

    rebRelease(User_Context_Value);
    User_Context_Value = nullptr;
    User_Context = nullptr;

    Shutdown_Frame_Stack();  // all API calls (e.g. rebRelease()) before this
    Shutdown_Api();

//=//// ALL MANAGED SERIES MUST HAVE THE KEEPALIVE REFERENCES GONE NOW ////=//

    const bool shutdown = true; // go ahead and free all managed series
    Recycle_Core(shutdown, NULL);

    Shutdown_Mold();
    Shutdown_Collector();
    Shutdown_Raw_Print();
    Shutdown_CRC();
    Shutdown_String();
    Shutdown_Scanner();
    Shutdown_Char_Cases();

    Shutdown_Symbols();
    Shutdown_Interning();

    Shutdown_GC();

    Shutdown_Empty_Arrays();  // should have been freed.

    FREE(REB_OPTS, Reb_Opts);

    // Shutting down the memory manager must be done after all the Free_Mem
    // calls have been made to balance their Alloc_Mem calls.
    //
    Shutdown_Pools();

  #if defined(TO_WINDOWS) && defined(DEBUG_SERIES_ORIGINS)
    Shutdown_Winstack();  // Do last so shutdown crashes have stack traces
  #endif
}
