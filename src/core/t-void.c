//
//  File: %t-void.c
//  Summary: "Symbolic type for representing an 'ornery' variable value"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2020 Ren-C Open Source Contributors
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

#include "sys-core.h"


//
//  MAKE_Quasi: C
//
// See also ISOTOPIC for making isotopes.
//
Bounce MAKE_Quasi(
    Frame(*) frame_,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(not parent);
    UNUSED(parent);

    if (IS_QUOTED(arg))  // QUOTED! competes for quote byte with quasiform
        return RAISE(Error_Bad_Make(kind, arg));

    // !!! Should it allow things that are already QUASI! (?)  This does, but
    // Quasify() does not.

    Copy_Cell(OUT, arg);
    mutable_QUOTE_BYTE(OUT) = QUASI_2;
    return OUT;
}


//
//  TO_Quasi: C
//
// TO is disallowed, e.g. you can't TO convert an integer of 0 to a blank.
//
Bounce TO_Quasi(Frame(*) frame_, enum Reb_Kind kind, const REBVAL *data) {
    return RAISE(Error_Bad_Make(kind, data));
}


//
//  CT_Quasi: C
//
REBINT CT_Quasi(noquote(Cell(const*)) a, noquote(Cell(const*)) b, bool strict)
{
    UNUSED(a); UNUSED(b); UNUSED(strict);
    assert(!"CT_Quasi should never be called");
    return 0;
}


//
//  REBTYPE: C
//
REBTYPE(Quasi)
{
    REBVAL *quasi = D_ARG(1);

    switch (ID_OF_SYMBOL(verb)) {
      case SYM_COPY: { // since `copy/deep [1 ~ 2]` is legal, allow `copy ~`
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(ARG(value)); // already referenced as `unit`

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        UNUSED(REF(deep));

        return COPY(quasi); }

      default: break;
    }

    fail (UNHANDLED);
}
