//
//  File: %c-typechecker.c
//  Summary: "Function generator for an optimized typechecker"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2020 Ren-C Open Source Contributors
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
// Making a typechecker is very easy:
//
//     >> integer?: func [v [any-value!]] [integer! = type of :v]
//
//     >> integer? 10
//     == #[true]
//
//     >> integer? <foo>
//     == #[false]
//
// But given that it is done so often, it's more efficient to have a custom
// dispatcher for making a typechecker:
//
//     >> integer?: typechecker integer!
//
// This makes a near-native optimized version of the type checker which uses
// a custom dispatcher.  It works for both datatypes and typesets.
//

#include "sys-core.h"

enum {
    IDX_TYPECHECKER_TYPE = 1,  // datatype or typeset to check
    IDX_TYPECHECKER_MAX
};


//
//  typecheck-internal?: native [
//
//      return: [logic!]
//      optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(typecheck_internal_q)
//
// Note: This prototype is used by all TYPECHECKER instances.  (It steals the
// paramlist from this native.)
{
    INCLUDE_PARAMS_OF_TYPECHECK_INTERNAL_Q;

    UNUSED(ARG(optional));
    panic (nullptr);
}


//
//  Datatype_Checker_Dispatcher: C
//
// Dispatcher used by TYPECHECKER generator for when argument is a datatype.
//
Bounce Datatype_Checker_Dispatcher(Frame(*) frame_)
{
    Frame(*) f = frame_;

    Array(*) details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_TYPECHECKER_MAX);

    REBVAL *datatype = DETAILS_AT(details, IDX_TYPECHECKER_TYPE);

    if (VAL_TYPE_KIND_OR_CUSTOM(datatype) == REB_CUSTOM) {
        if (VAL_TYPE(FRM_ARG(f, 2)) != REB_CUSTOM)
            return Init_False(OUT);

        REBTYP *typ = VAL_TYPE_CUSTOM(datatype);
        return Init_Logic(
            OUT,
            CELL_CUSTOM_TYPE(FRM_ARG(f, 2)) == typ
        );
    }

    assert(KEY_SYM(ACT_KEY(FRM_PHASE(f), 1)) == SYM_RETURN);  // skip arg 1

    return Init_Logic(  // otherwise won't be equal to any custom type
        OUT,
        VAL_TYPE(FRM_ARG(f, 2)) == VAL_TYPE_KIND_OR_CUSTOM(datatype)
    );
}


//
//  Typeset_Checker_Dispatcher: C
//
// Dispatcher used by TYPECHECKER generator for when argument is a typeset.
//
Bounce Typeset_Checker_Dispatcher(Frame(*) frame_)
{
    Frame(*) f = frame_;

    Array(*) details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_TYPECHECKER_MAX);

    REBVAL *typeset = DETAILS_AT(details, IDX_TYPECHECKER_TYPE);
    assert(IS_TYPESET(typeset));

    assert(KEY_SYM(ACT_KEY(FRM_PHASE(f), 1)) == SYM_RETURN);  // skip arg 1

    return Init_Logic(OUT, TYPE_CHECK(typeset, VAL_TYPE(FRM_ARG(f, 2))));
}


//
//  typechecker: native [
//
//  {Generator for an optimized typechecking ACTION!}
//
//      return: [action!]
//      type [datatype! typeset!]
//  ]
//
DECLARE_NATIVE(typechecker)
{
    INCLUDE_PARAMS_OF_TYPECHECKER;

    REBVAL *type = ARG(type);

    Action(*) typechecker = Make_Action(
        ACT_PARAMLIST(VAL_ACTION(Lib(TYPECHECK_INTERNAL_Q))),
        nullptr,  // no partials
        IS_DATATYPE(type)
            ? &Datatype_Checker_Dispatcher
            : &Typeset_Checker_Dispatcher,
        IDX_TYPECHECKER_MAX  // details array capacity
    );
    Copy_Cell(ARR_AT(ACT_DETAILS(typechecker), IDX_TYPECHECKER_TYPE), type);

    return Init_Action(OUT, typechecker, ANONYMOUS, UNBOUND);
}
