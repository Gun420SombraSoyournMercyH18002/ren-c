//
//  File: %sys-feed.h
//  Summary: {Accessors and Argument Pushers/Poppers for Function Call Frames}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2019 Ren-C Open Source Contributors
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
// A "Feed" represents an abstract source of Rebol values, which only offers
// a guarantee of being able to have two sequential values in the feed as
// having valid pointers at one time.  The main pointer is the feed's value
// (feed->value), and to be able to have another pointer to the previous
// value one must request a "lookback" at the time of advancing the feed.
//
// One reason for the feed's strict nature is that it offers an interface not
// just to Rebol BLOCK!s and other arrays, but also to variadic lists such
// as C's va_list...in a system which also allows the mixure of portions of
// UTF-8 string source text.  C's va_list does not retain a memory of the
// past, so once va_arg() is called it forgets the previous value...and
// since values may also be fabricated from text it can get complicated.
//
// Another reason for the strictness is to help rein in the evaluator design
// to keep it within a certain boundary of complexity.


#define FEED_SINGULAR(feed)     ARR(&(feed)->singular)
#define FEED_SINGLE(feed)       mutable_SER_CELL(&(feed)->singular)

#define LINK_Splice_TYPE        Array(*)
#define LINK_Splice_CAST        ARR
#define HAS_LINK_Splice         FLAVOR_FEED

#define MISC_Pending_TYPE       Cell(const*)
#define MISC_Pending_CAST       (Cell(const*))
#define HAS_MISC_Pending        FLAVOR_FEED


#define FEED_SPLICE(feed) \
    LINK(Splice, &(feed)->singular)

// This contains an Is_End() marker if the next fetch should be an attempt
// to consult the va_list (if any).  That end marker may be resident in
// an array, or if it's a plain va_list source it may be the global END.
//
#define FEED_PENDING(feed) \
    MISC(Pending, &(feed)->singular)

#define FEED_IS_VARIADIC(feed)  IS_COMMA(FEED_SINGLE(feed))

#define FEED_VAPTR_POINTER(feed)    PAYLOAD(Comma, FEED_SINGLE(feed)).vaptr
#define FEED_PACKED(feed)           PAYLOAD(Comma, FEED_SINGLE(feed)).packed

inline static option(va_list*) FEED_VAPTR(Feed(*) feed)
  { return FEED_VAPTR_POINTER(feed); }



// For performance, we always get the specifier from the same location, even
// if we're not using an array.  So for the moment, that means using a
// COMMA! (which for technical reasons has a nullptr binding and is thus
// always SPECIFIED).  However, VAL_SPECIFIER() only runs on arrays, so
// we sneak past that by accessing the node directly.
//
#define FEED_SPECIFIER(feed) \
    ARR(BINDING(FEED_SINGLE(feed)))

#define FEED_ARRAY(feed) \
    VAL_ARRAY(FEED_SINGLE(feed))

#define FEED_INDEX(feed) \
    VAL_INDEX_UNBOUNDED(FEED_SINGLE(feed))


// Ordinary Rebol internals deal with REBVAL* that are resident in arrays.
// But a va_list can contain UTF-8 string components or special instructions
// that are other Detect_Rebol_Pointer() types.  Anyone who wants to set or
// preload a frame's state for a va_list has to do this detection, so this
// code has to be factored out to just take a void* (because a C va_list
// cannot have its first parameter in the variadic, va_list* is insufficient)
//
inline static void Detect_Feed_Pointer_Maybe_Fetch(
    Feed(*) feed,
    const void *p
){
    assert(FEED_PENDING(feed) == nullptr);

  detect_again:;

    // !!! On stack overflow errors, the system (theoretically) will go through
    // all the frames and make sure variadic feeds are ended.  If we put
    // trash in this value (e.g. 0xDECAFBAD) that code crashes.  For now, use
    // END so that if something below causes a stack overflow before the
    // operation finishes, those crashes don't happen.
    //
    feed->value = END;  // should be assigned below

    if (not p) {  // libRebol's null/<opt> (Is_Nulled prohibited in CELL case)

        // This is the compromise of convenience, where ~null~ is put in
        // to the feed.  If it's converted into an array we've told a
        // small lie (~null~ is a BAD-WORD! and a thing, so not the same
        // as the NULL non-thing).  It will evaluate to a ~null~ isotope
        // which *usually* acts like NULL, but not with ELSE/THEN directly.
        //
        // We must use something legal to put in arrays, so non-isotope.
        //
        Init_Bad_Word(&feed->fetched, Canon(NULL));

        assert(FEED_SPECIFIER(feed) == SPECIFIED);  // !!! why assert this?
        feed->value = &feed->fetched;

    } else switch (Detect_Rebol_Pointer(p)) {

      case DETECTED_AS_UTF8: {
        //
        // Note that the context is only used on loaded text from C string
        // data.  The scanner leaves all spliced values with whatever bindings
        // they have (even if that is none).
        //
        // !!! Some kind of "binding instruction" might allow other uses?
        //
        // !!! We really should be able to free this array without managing it
        // when we're done with it, though that can get a bit complicated if
        // there's an error or need to reify into a value.  For now, do the
        // inefficient thing and manage it.
        //
        // !!! Scans that produce only one value (which are likely very
        // common) can go into feed->fetched and not make an array at all.
        //
        Array(*) reified = unwrap(Try_Scan_Utf8_For_Detect_Feed_Pointer_Managed(
            cast(const Byte*, p),
            feed,
            Get_Context_From_Stack()
        ));

        if (not reified) {
            //
            // This happens when somone says rebValue(..., "", ...) or similar,
            // and gets an empty array from a string scan.  It's not legal
            // to put an END in f->value, and it's unknown if the variadic
            // feed is actually over so as to put null... so get another
            // value out of the va_list and keep going.
            //
            if (FEED_VAPTR(feed))
                p = va_arg(*unwrap(FEED_VAPTR(feed)), const void*);
            else
                p = *FEED_PACKED(feed)++;
            goto detect_again;
        }

        // !!! for now, assume scan went to the end; ultimately it would need
        // to pass the feed in as a parameter for partial scans
        //
        assert(not FEED_IS_VARIADIC(feed));

        feed->value = ARR_HEAD(reified);
        Init_Array_Cell_At(FEED_SINGLE(feed), REB_BLOCK, reified, 1);
        break; }

      case DETECTED_AS_SERIES: {  // e.g. rebQ, rebU, or a rebR() handle
        Array(*) inst1 = ARR(m_cast(void*, p));

        // As we feed forward, we're supposed to be freeing this--it is not
        // managed -and- it's not manuals tracked, it is only held alive by
        // the va_list()'s plan to visit it.  A fail() here won't auto free
        // it *because it is this traversal code which is supposed to free*.
        //
        // !!! Actually, THIS CODE CAN'T FAIL.  :-/  It is part of the
        // implementation of fail's cleanup itself.
        //
        switch (SER_FLAVOR(inst1)) {
          case FLAVOR_INSTRUCTION_SPLICE: {
            REBVAL *single = SPECIFIC(ARR_SINGLE(inst1));
            if (IS_BLANK(single)) {
                GC_Kill_Series(inst1);
                goto detect_again;
            }

            if (IS_BLOCK(single)) {
                feed->value = nullptr;  // will become FEED_PENDING(), ignored
                Splice_Block_Into_Feed(feed, single);
            }
            else {
                assert(IS_QUOTED(single));
                Unquotify(Copy_Cell(&feed->fetched, single), 1);
                feed->value = &feed->fetched;
            }
            GC_Kill_Series(inst1);
            break; }

          case FLAVOR_API: {
            //
            // We usually get the API *cells* passed to us, not the singular
            // array holding them.  But the rebR() function will actually
            // flip the "release" flag and then return the existing API handle
            // back, now behaving as an instruction.
            //
            assert(Get_Subclass_Flag(API, inst1, RELEASE));

            // !!! Originally this asserted it was a managed handle, but the
            // needs of API-TRANSIENT are such that a handle which outlives
            // the frame is returned as a SINGULAR_API_RELEASE.  Review.
            //
            /*assert(GET_SERIES_FLAG(inst1, MANAGED));*/

            // See notes above (duplicate code, fix!) about how we might like
            // to use the as-is value and wait to free until the next cycle
            // vs. putting it in fetched/MARKED_TEMPORARY...but that makes
            // this more convoluted.  Review.

            REBVAL *single = SPECIFIC(ARR_SINGLE(inst1));
            Copy_Cell(&feed->fetched, single);
            feed->value = &feed->fetched;
            rebRelease(single);  // *is* the instruction
            break; }

          default:
            //
            // Besides instructions, other series types aren't currenlty
            // supported...though it was considered that you could use
            // Context(*) or Action(*) directly instead of their archtypes.  This
            // was considered when thinking about ditching value archetypes
            // altogether (e.g. no usable cell pattern guaranteed at the head)
            // but it's important in several APIs to emphasize a value gives
            // phase information, while archetypes do not.
            //
            panic (inst1);
        }
        break; }

      case DETECTED_AS_CELL: {
        const REBVAL *cell = cast(const REBVAL*, p);
        assert(not IS_RELATIVE(cast(Cell(const*), cell)));

        assert(FEED_SPECIFIER(feed) == SPECIFIED);

        if (Is_Nulled(cell))  // API enforces use of C's nullptr (0) for NULL
            assert(!"NULLED cell API leak, see NULLIFY_NULLED() in C source");

        feed->value = cell;  // cell can be used as-is
        break; }

      case DETECTED_AS_END: {  // end of variadic input, so that's it for this
        feed->value = END;

        // The va_end() is taken care of here, or if there is a throw/fail it
        // is taken care of by Abort_Frame_Core()
        //
        if (FEED_VAPTR(feed))
            va_end(*unwrap(FEED_VAPTR(feed)));
        else
            assert(FEED_PACKED(feed));

        // !!! Error reporting expects there to be an array.  The whole story
        // of errors when there's a va_list is not told very well, and what
        // will have to likely happen is that in debug modes, all va_list
        // are reified from the beginning, else there's not going to be
        // a way to present errors in context.  Fake an empty array for now.
        //
        Init_Block(FEED_SINGLE(feed), EMPTY_ARRAY);
        break; }

      default:
        panic (p);
    }
}


//
// Fetch_Next_In_Feed()
//
// Once a va_list is "fetched", it cannot be "un-fetched".  Hence only one
// unit of fetch is done at a time, into f->value.
//
inline static void Fetch_Next_In_Feed(Feed(*) feed) {
    //
    // !!! This used to assert that feed->value wasn't "Is_End()".  Things have
    // gotten more complex, because feed->fetched may have been Move_Cell()'d
    // from, which triggers a RESET() and that's indistinguishable from END.
    // To the extent the original assert provided safety, revisit it.

    // The NEXT_ARG_FROM_OUT flag is a trick used by frames, which must be
    // careful about the management of the trick.  It's put on the feed
    // and not the frame in order to catch cases where it slips by, so this
    // assert is important.
    //
    if (Get_Feed_Flag(feed, NEXT_ARG_FROM_OUT))
        assert(!"Fetch_Next_In_Feed() called but NEXT_ARG_FROM_OUT set");

    // We are changing ->value, and thus by definition any ->gotten value
    // will be invalid.  It might be "wasteful" to always set this to null,
    // especially if it's going to be overwritten with the real fetch...but
    // at a source level, having every call to Fetch_Next_In_Frame have to
    // explicitly set ->gotten to null is overkill.  Could be split into
    // a version that just trashes ->gotten in the debug build vs. null.
    //
    feed->gotten = nullptr;

  retry_splice:
    if (FEED_PENDING(feed)) {
        assert(Not_End(FEED_PENDING(feed)));

        feed->value = FEED_PENDING(feed);
        mutable_MISC(Pending, &feed->singular) = nullptr;
    }
    else if (FEED_IS_VARIADIC(feed)) {
        //
        // A variadic can source arbitrary pointers, which can be detected
        // and handled in different ways.  Notably, a UTF-8 string can be
        // differentiated and loaded.
        //
        if (FEED_VAPTR(feed)) {
            const void *p = va_arg(*unwrap(FEED_VAPTR(feed)), const void*);
            Detect_Feed_Pointer_Maybe_Fetch(feed, p);
        }
        else {
            //
            // C++ variadics use an ordinary packed array of pointers, because
            // they do more ambitious things with the arguments and there is
            // no (standard) way to construct a C va_list programmatically.
            //
            const void *p = *FEED_PACKED(feed)++;
            Detect_Feed_Pointer_Maybe_Fetch(feed, p);
        }
    }
    else {
        if (FEED_INDEX(feed) != cast(REBINT, ARR_LEN(FEED_ARRAY(feed)))) {
            feed->value = ARR_AT(FEED_ARRAY(feed), FEED_INDEX(feed));
            ++FEED_INDEX(feed);
        }
        else {
            feed->value = END;

            // !!! At first this dropped the hold here; but that created
            // problems if you write `do code: [clear code]`, because END
            // is reached when CODE is fulfilled as an argument to CLEAR but
            // before CLEAR runs.  This subverted the series hold mechanic.
            // Instead we do the drop in Free_Feed(), though drops on splices
            // happen here.  It's not perfect, but holds need systemic review.

            if (FEED_SPLICE(feed)) {  // one or more additional splices to go
                if (Get_Feed_Flag(feed, TOOK_HOLD)) {  // see note above
                    assert(GET_SERIES_INFO(FEED_ARRAY(feed), HOLD));
                    CLEAR_SERIES_INFO(m_cast(Array(*), FEED_ARRAY(feed)), HOLD);
                    Clear_Feed_Flag(feed, TOOK_HOLD);
                }

                Raw_Array* splice = FEED_SPLICE(feed);
                memcpy(
                    FEED_SINGULAR(feed),
                    FEED_SPLICE(feed),
                    sizeof(Raw_Array)
                );
                GC_Kill_Series(splice);  // Array(*) would hold reference
                goto retry_splice;
            }
        }
    }
}


// Most calls to Fetch_Next_In_Frame() are no longer interested in the
// cell backing the pointer that used to be in f->value (this is enforced
// by a rigorous test in DEBUG_EXPIRED_LOOKBACK).  Special care must be
// taken when one is interested in that data, because it may have to be
// moved.  So current can be returned from Fetch_Next_In_Frame_Core().

inline static Cell(const*) Lookback_While_Fetching_Next(Frame(*) f) {
  #if DEBUG_EXPIRED_LOOKBACK
    if (feed->stress) {
        RESET(feed->stress);
        free(feed->stress);
        feed->stress = nullptr;
    }
  #endif

    assert(READABLE(f->feed->value));  // ensure cell

    // f->value may be synthesized, in which case its bits are in the
    // `f->feed->fetched` cell.  That synthesized value would be overwritten
    // by another fetch, which would mess up lookback...so we cache those
    // bits in the lookback cell in that case.
    //
    // The reason we do this conditionally isn't just to avoid moving 4
    // platform pointers worth of data.  It's also to keep from reifying
    // array cells unconditionally with Derelativize().  (How beneficial
    // this is currently kind of an unknown, but in the scheme of things it
    // seems like it must be something favorable to optimization.)
    //
    Cell(const*) lookback;
    if (f->feed->value == &f->feed->fetched) {
        Move_Cell_Core(
            &f->feed->lookback,
            SPECIFIC(&f->feed->fetched),
            CELL_MASK_ALL
        );
        lookback = &f->feed->lookback;
    }
    else
        lookback = f->feed->value;

    Fetch_Next_In_Feed(f->feed);

  #if DEBUG_EXPIRED_LOOKBACK
    if (preserve) {
        f->stress = cast(Cell(*), malloc(sizeof(Cell)));
        memcpy(f->stress, *opt_lookback, sizeof(Cell));
        lookback = f->stress;
    }
  #endif

    return lookback;
}

#define Fetch_Next_Forget_Lookback(f) \
    Fetch_Next_In_Feed(f->feed)


// This code is shared by Literal_Next_In_Feed(), and used without a feed
// advancement in the inert branch of the evaluator.  So for something like
// `repeat 2 [append [] 10]`, the steps are:
//
//    1. REPEAT defines its body parameter as <const>
//    2. When REPEAT runs Do_Any_Array_At_Throws() on the const ARG(body), the
//       frame gets FEED_FLAG_CONST due to the CELL_FLAG_CONST.
//    3. The argument to append is handled by the inert processing branch
//       which moves the value here.  If the block wasn't made explicitly
//       mutable (e.g. with MUTABLE) it takes the flag from the feed.
//
inline static void Inertly_Derelativize_Inheriting_Const(
    REBVAL *out,
    Cell(const*) v,
    Feed(*) feed
){
    assert(not Is_Isotope(v));  // Source should not have isotopes

    Derelativize(out, v, FEED_SPECIFIER(feed));
    Set_Cell_Flag(out, UNEVALUATED);

    if (Not_Cell_Flag(v, EXPLICITLY_MUTABLE))
        out->header.bits |= (feed->flags.bits & FEED_FLAG_CONST);
}

inline static void Literal_Next_In_Feed(REBVAL *out, Feed(*) feed) {
    Inertly_Derelativize_Inheriting_Const(out, feed->value, feed);
    Fetch_Next_In_Feed(feed);
}


#define Alloc_Feed() \
    Alloc_Pooled(FED_POOL)

inline static void Free_Feed(Feed(*) feed) {
    //
    // Aborting valist frames is done by just feeding all the values
    // through until the end.  This is assumed to do any work, such
    // as SINGULAR_FLAG_API_RELEASE, which might be needed on an item.  It
    // also ensures that va_end() is called, which happens when the frame
    // manages to feed to the end.
    //
    // Note: While on many platforms va_end() is a no-op, the C standard
    // is clear it must be called...it's undefined behavior to skip it:
    //
    // http://stackoverflow.com/a/32259710/211160

    // !!! Since we're not actually fetching things to run them, this is
    // overkill.  A lighter sweep of the va_list pointers that did just
    // enough work to handle rebR() releases, and va_end()ing the list
    // would be enough.  But for the moment, it's more important to keep
    // all the logic in one place than to make variadic interrupts
    // any faster...they're usually reified into an array anyway, so
    // the frame processing the array will take the other branch.

    while (Not_End(feed->value))
        Fetch_Next_In_Feed(feed);

    assert(Is_End(feed->value));
    assert(FEED_PENDING(feed) == nullptr);

    // !!! See notes in Fetch_Next regarding the somewhat imperfect way in
    // which splices release their holds.  (We wait until Free_Feed() so that
    // `do code: [clear code]` doesn't drop the hold until the block frame
    // is actually fully dropped.)
    //
    if (Get_Feed_Flag(feed, TOOK_HOLD)) {
        assert(GET_SERIES_INFO(FEED_ARRAY(feed), HOLD));
        CLEAR_SERIES_INFO(m_cast(Array(*), FEED_ARRAY(feed)), HOLD);
        Clear_Feed_Flag(feed, TOOK_HOLD);
    }

    Free_Pooled(FED_POOL, feed);
}

inline static Feed(*) Prep_Feed_Common(void* preallocated, Flags flags) {
   Feed(*) feed = cast(Reb_Feed*, preallocated);

  #if DEBUG_COUNT_TICKS
    feed->tick = TG_Tick;
  #endif

    Init_Trash(Prep_Cell(&feed->fetched));
    Init_Trash(Prep_Cell(&feed->lookback));

    Stub* s = Prep_Stub(
        &feed->singular,  // preallocated
        NODE_FLAG_NODE | FLAG_FLAVOR(FEED)
    );
    Prep_Cell(FEED_SINGLE(feed));
    mutable_LINK(Splice, s) = nullptr;
    mutable_MISC(Pending, s) = nullptr;

    feed->flags.bits = flags;

    return feed;
}

inline static Feed(*) Prep_Array_Feed(
    void* preallocated,
    option(Cell(const*)) first,
    Array(const*) array,
    REBLEN index,
    REBSPC *specifier,
    Flags flags
){
    Feed(*) feed = Prep_Feed_Common(preallocated, flags);

    if (first) {
        feed->value = unwrap(first);
        assert(Not_End(feed->value));
        Init_Array_Cell_At_Core(
            FEED_SINGLE(feed), REB_BLOCK, array, index, specifier
        );
        assert(VAL_TYPE_UNCHECKED(feed->value) != REB_0_END);
            // ^-- faster than Not_End()
    }
    else {
        feed->value = ARR_AT(array, index);
        if (feed->value == ARR_TAIL(array))
            feed->value = END;
        Init_Array_Cell_At_Core(
            FEED_SINGLE(feed), REB_BLOCK, array, index + 1, specifier
        );
    }

    // !!! The temp locking was not done on end positions, because the feed
    // is not advanced (and hence does not get to the "drop hold" point).
    // This could be an issue for splices, as they could be modified while
    // their time to run comes up to not be END anymore.  But if we put a
    // hold on conservatively, it won't be dropped by Free_Feed() time.
    //
    if (Is_End(feed->value) or GET_SERIES_INFO(array, HOLD))
        NOOP;  // already temp-locked
    else {
        SET_SERIES_INFO(m_cast(Array(*), array), HOLD);
        Set_Feed_Flag(feed, TOOK_HOLD);
    }

    feed->gotten = nullptr;
    if (Is_End(feed->value))
        assert(FEED_PENDING(feed) == nullptr);
    else
        assert(READABLE(feed->value));

    return feed;
}

#define Make_Array_Feed_Core(array,index,specifier) \
    Prep_Array_Feed( \
        Alloc_Feed(), \
        nullptr, (array), (index), (specifier), \
        FEED_MASK_DEFAULT \
    )


inline static Feed(*) Prep_Va_Feed(
    void* preallocated,
    const void *p,
    option(va_list*) vaptr,
    Flags flags
){
    Feed(*) feed = Prep_Feed_Common(preallocated, flags);

    // We want to initialize with something that will give back SPECIFIED.
    // It must therefore be bindable.  Try a COMMA!
    //
    Init_Comma(FEED_SINGLE(feed));

    if (not vaptr) {  // `p` should be treated as a packed void* array
        FEED_VAPTR_POINTER(feed) = nullptr;
        FEED_PACKED(feed) = cast(const void* const*, p);
        p = *FEED_PACKED(feed)++;
    }
    else {
        FEED_VAPTR_POINTER(feed) = unwrap(vaptr);
        FEED_PACKED(feed) = nullptr;
    }
    Detect_Feed_Pointer_Maybe_Fetch(feed, p);

    feed->gotten = nullptr;
    assert(Is_End(feed->value) or READABLE(feed->value));
    return feed;
}

// The flags is passed in by the macro here by default, because it does a
// fetch as part of the initialization from the `first`...and if you want
// the flags to take effect, they must be passed in up front.
//
#define Make_Variadic_Feed(p,vaptr,flags) \
    Prep_Va_Feed(Alloc_Feed(), (p), (vaptr), (flags))

inline static Feed(*) Prep_Feed_At(
    void *preallocated,
    noquote(Cell(const*)) any_array,  // array is extracted and HOLD put on
    REBSPC *specifier,
    Flags parent_flags  // only reads FEED_FLAG_CONST out of this
){
    STATIC_ASSERT(CELL_FLAG_CONST == FEED_FLAG_CONST);

    Flags flags;
    if (Get_Cell_Flag(any_array, EXPLICITLY_MUTABLE))
        flags = FEED_MASK_DEFAULT;  // override const from parent frame
    else
        flags = FEED_MASK_DEFAULT
            | (parent_flags & FEED_FLAG_CONST)  // inherit
            | (any_array->header.bits & CELL_FLAG_CONST);  // heed

    return Prep_Array_Feed(
        preallocated,
        nullptr,  // `first` = nullptr, don't inject arbitrary 1st element
        VAL_ARRAY(any_array),
        VAL_INDEX(any_array),
        Derive_Specifier(specifier, any_array),
        flags
    );;
}

#define Make_Feed_At_Core(any_array,specifier) \
    Prep_Feed_At( \
        Alloc_Feed(), \
        (any_array), (specifier), TOP_FRAME->feed->flags.bits \
    );

#define Make_Feed_At(name,any_array) \
    Make_Feed_At_Core(name, (any_array), SPECIFIED)
