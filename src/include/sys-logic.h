//
//  File: %sys-logic.h
//  Summary: "LOGIC! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A logic can be either true or false.  For purposes of optimization, logical
// falsehood is indicated by one of the value option bits in the header--as
// opposed to in the value payload.  This means it can be tested quickly, and
// that a single check can test for BLANK!, logic false, or nulled.
//

#define FALSE_VALUE \
    c_cast(const REBVAL*, &PG_False_Value)

#define TRUE_VALUE \
    c_cast(const REBVAL*, &PG_True_Value)

inline static bool VAL_LOGIC(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_LOGIC);
    return PAYLOAD(Logic, v).flag;
}

inline static bool IS_TRUTHY(const RELVAL *v) {
    if (KIND_BYTE(v) >= REB_64) {
        //
        // QUOTED! at an escape level low enough to reuse cell.  So if that
        // cell happens to be false/blank/nulled, CELL_FLAG_FALSEY will
        // be set, but don't heed it! `if lit '_ [-- "this is truthy"]`
        //
        return true;
    }
    if (KIND_BYTE(v) < REB_LOGIC)
        return true;
    if (KIND_BYTE(v) == REB_LOGIC)
        return VAL_LOGIC(v);
    if (IS_VOID(v))
        fail (Error_Void_Conditional_Raw());
    return false;
}

#define IS_FALSEY(v) \
    (not IS_TRUTHY(v))

inline static REBVAL *Init_Logic(RELVAL *out, bool flag) {
    RESET_CELL(out, REB_LOGIC, CELL_MASK_NONE);
    PAYLOAD(Logic, out).flag = flag;
    return KNOWN(out);
}

#define Init_True(out) \
    Init_Logic((out), true)

#define Init_False(out) \
    Init_Logic((out), false)


// Although a BLOCK! value is true, some constructs are safer by not allowing
// literal blocks.  e.g. `if [x] [print "this is not safe"]`.  The evaluated
// bit can let these instances be distinguished.  Note that making *all*
// evaluations safe would be limiting, e.g. `foo: any [false-thing []]`...
// So ANY and ALL use IS_TRUTHY() directly
//
inline static bool IS_CONDITIONAL_TRUE(const REBVAL *v) {
    if (IS_FALSEY(v))
        return false;
    if (KIND_BYTE(v) == REB_BLOCK)
        if (GET_CELL_FLAG(v, UNEVALUATED))
            fail (Error_Block_Conditional_Raw(v));
    return true;
}

#define IS_CONDITIONAL_FALSE(v) \
    (not IS_CONDITIONAL_TRUE(v))
