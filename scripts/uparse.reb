Rebol [
    Title: {UPARSE: Usermode Implementation of PARSE in Ren-C}
    License: {LGPL 3.0}

    Description: {
        Rebol's PARSE is a tool for performing RegEx-style tasks using an
        English-like dialect.  It permits breaking down complex expressions
        into named subrules, and has a very freeform model for escaping into
        and back out of imperative code:

        http://www.rebol.com/docs/core23/rebolcore-15.html
        http://www.codeconscious.com/rebol/parse-tutorial.html
        https://www.red-lang.org/2013/11/041-introducing-parse.html

        The implementation of PARSE engines has traditionally been as
        optimized systems-level code (e.g. C or Red/System), built into the
        interpreter.  This does not offer flexibility to make any minor or
        major changes to the dialect that a user might imagine.

        This script attempts to make PARSE more "hackable", by factoring the
        implementation out so that each keyword or data behavior is handled
        by an individual `parser` function.  The parameters to this function
        are stylized so that the parse engine can compose these smaller
        parsers together as part of larger processing operations.  This
        approach is typically called "Parser Combinators":

        https://en.wikipedia.org/wiki/Parser_combinator

        While this overlaps with typical parser combinators, Rebol's design
        affords it a unique spin.  By building the backbone of the dialect as
        a BLOCK!, there's no SEQUENCE or ALTERNATIVE combinators.  Instead,
        blocks make sequencing implicit just by steps being ordered after one
        another.  The alternates are managed by means of `|` markers, which
        are detected by the implementation of the block combinator--and not
        combinators in their own right.

        By making the combinator list passed to UPARSE as a MAP!, is possible
        to easily create overrides or variations of the dialect.  (For
        instance, a version that is compatible with older Rebols.)  But the
        goal is to facilitate even more ambitious features.
    }

    Notes: {
        * This implementation will be *extremely* slow for the foreseeable
          future.  But since it is built on usermode facilities, any
          optimizations that are applied to it will bring systemic benefits.
          Ultimately the goal is to merge this architecture in with the
          "messier" C code...hopefully preserving enough of the hackability
          while leveraging low-level optimizations where possible.
    }
]

; A combinator is a function that takes in an input series as well as some
; parameters, and decides whether to advance the input or return NULL to
; signal the failure of a match.
;
; One of the parameter types that can be given to these functions are another
; parser, to combine them (hence "parser combinator").  So you can take a
; combinator like OPT and parameterize it with SOME which is parameterized
; with "A", to get the parser `opt some "a"`.
;
; But if a parameter to a combinator is marked as quoted, then that will take
; a value from the callsite literally.
;
; !!! We use a MAP! here instead of an OBJECT! because if it were MAKE OBJECT!
; then the parse keywords would override the Rebol functions (so you couldn't
; use ANY inside the implementation of a combinator, because there's a
; combinator named ANY).  This is part of the general issues with binding that
; need to have answers.
;
default-combinators: make map! reduce [

    === {BASIC KEYWORDS} ===

    'opt func [
        {If the parser given as a parameter fails, return input undisturbed}
        return: [any-series!]
        result: [<opt> any-value!]
        input [any-series!]
        parser [action!]
    ][
        ; If the argument given as a rule has a result, then OPT will also.
        ;
        let f: make frame! :parser
        f/input: input
        if find f 'result [
            f/result: result
        ]

        return any [
            do f  ; success means nested parser should have set result
            elide if result [set result null]  ; on failure OPT nulls result
            input
        ]
    ]

    'end func [
        {Only match if the input is at the end}
        return: [<opt> any-series!]
        input [any-series!]
        ; Note: no arguments besides the input (neither parsers nor literals)
    ][
        if tail? input [return input]
        return null
    ]

    'not func [
        {Fail if the parser rule given succeeds, else continue}
        return: [<opt> any-series!]
        input [any-series!]
        parser [action!]
    ][
        if parser input [
            return null
        ]
        return input
    ]

    'ahead func [
        {Leave the parse position at the same location, but fail if no match}
        return: [<opt> any-series!]
        input [any-series!]
        parser [action!]
    ][
        if not parser input [
            return null
        ]
        return input
    ]

    === {LOOPING CONSTRUCT KEYWORDS} ===

    'any func [
        {Any number of matches (including 0)}
        return: [<opt> any-series!]
        input [any-series!]
        parser [action!]
    ][
        let pos: input
        cycle [
            ;
            ; Note that the distinction between ANY and WHILE is that ANY
            ; stops running when it reaches the end.  WHILE keeps running
            ; until something actively fails.
            ;
            ;     >> uparse "a" [any [opt "a"]]
            ;     == "a"
            ;
            ; https://github.com/metaeducation/rebol-issues/issues/1268
            ;
            if tail? pos: any [parser pos, return pos] [
                return pos
            ]
        ]
    ]

    'some func [
        {Must run at least one match}
        return: [<opt> any-series!]
        input [any-series!]
        parser [action!]
    ][
        if let pos: parser input [
            cycle [
                if tail? pos: any [parser pos, return pos] [
                    return pos
                ]
            ]
        ]
        return null
    ]

    'while func [
        {Keep matching while rule doesn't fail, regardless of hitting end}
        return: [<opt> any-series!]
        input [any-series!]
        parser [action!]
    ][
        let pos: input
        cycle [
            ;
            ; Note that the distinction between ANY and WHILE is that ANY
            ; stops running when it reaches the end.  WHILE keeps running
            ; until something actively fails.
            ;
            ;     >> uparse "a" [while [opt "a"]]
            ;     ; infinite loop
            ;
            ; https://github.com/metaeducation/rebol-issues/issues/1268
            ;
            pos: any [parser pos, return pos]
        ]
    ]

    === {MUTATING KEYWORDS} ===

    'change func [
        {Substitute a match with new data}
        return: [<opt> any-series!]
        input [any-series!]
        parser [action!]
        'data [<opt> any-value!]
    ][
        if not let limit: parser input [
            return null
        ]
        return change/part input data limit  ; CHANGE returns change's tail
    ]

    'remove func [
        {Remove data that matches a parse rule}
        return: [<opt> any-series!]
        input [any-series!]
        parser [action!]
    ][
        if not let limit: parser input [
            return null
        ]
        return remove/part input limit  ; REMOVE returns its initial position
    ]

    'insert func [
        {Unconditionally insert literal data into the input series}
        return: [any-series!]
        input [any-series!]
        'data [any-value!]
    ][
        return insert input data
    ]

    === {SEEKING KEYWORDS} ===

    'skip func [
        {Skip one item, succeeding so long as input isn't at END}
        return: [<opt> any-series!]
        result: [any-value!]
        input [any-series!]
    ][
        if tail? input [return null]
        if result [
            set result input/1
        ]
        return next input
    ]

    'to func [
        {Match up TO a certain rule (result position before succeeding rule)}
        return: [<opt> any-series!]
        input [any-series!]
        parser [action!]
    ][
        cycle [
            if parser input [  ; could be `to end`, check TAIL? *after*
                return input
            ]
            if tail? input [
                return null
            ]
            input: next input
        ]
    ]

    'thru func [
        {Match up THRU a certain rule (result position after succeeding rule)}
        return: [<opt> any-series!]
        input [any-series!]
        parser [action!]
    ][
        let pos
        cycle [
            if pos: parser input [  ; could be `thru end`, check TAIL? *after*
                return pos
            ]
            if tail? input [
                return null
            ]
            input: next input
        ]
        return null
    ]

    'here func [
         return: [<opt> any-series!]
         result: [any-series!]
         input [any-series!]
    ][
        if result [  ; don't assume SET-WORD! on left (for generality)
            set result input
        ]
        return input
    ]

    'seek func [
         return: [<opt> any-series!]
         input [any-series!]
         'var [word! path! integer!]
    ][
        if integer? var [
            return at head input var
        ]
        if not same? head input head get var [
            fail "SEEK in UPARSE must be in the same series"
        ]
        return get var
    ]

    'between func [
        return: [<opt> any-series!]
        result: [any-series!]
        input [<opt> any-series!]
        parser-left [action!]
        parser-right [action!]
    ][
        if not let start: parser-left input [return null]

        let limit: start
        cycle [
            if input: parser-right limit [  ; found it
                if result [
                    set result copy/part start limit
                ]
                return input
            ]
            if tail? limit [return null]
            limit: next limit
        ]
        return null
    ]

    === {ASSIGNING KEYWORDS} ===

    'copy func [
        {Copy from the current parse position through a rule}
        return: [<opt> any-series!]
        result: [any-series!]
        input [any-series!]
        parser [action!]
    ][
        if let pos: parser input [
            if not result [
                fail "COPY must be used with a SET-WORD! in UPARSE"
            ]
            set result copy/part input pos
            return pos
        ]
        return null
    ]

    === {INTO KEYWORD} ===

    'into func [
        {Perform a recursion into another datatype with a rule}
        return: [<opt> any-array!]
        input [any-array!]
        parser [action!]
    ][
        if not any-series? input/1 [
            fail "Need ANY-SERIES! datatype for use with INTO in UPARSE"
        ]

        ; If the entirety of the item at the input array is matched by the
        ; supplied parser rule, then we advance past the item.
        ;
        let pos: parser input/1
        if pos = tail input/1 [
            return next input
        ]
        return null
    ]

    === {COLLECT AND KEEP} ===

    ; The COLLECT feature was first added by Red.  However, it did not use
    ; rollback across any KEEPs that happened when a parse rule failed, which
    ; makes the feature of limited use.
    ;
    ; This is a kind of lousy implementation that leverages a baked-in
    ; mechanism to manage collecting where the UPARSE holds the collect buffer
    ; and the block combinator is complicit.  It's just to show a first
    ; working option...but a more general architecture for designing features
    ; that want "rollback" is desired.

    'collect func [
        return: [<opt> any-series!]
        result: [any-series!]
        p [frame!]
        input [any-series!]
        parser [action!]
    ][
        if not p/collecting [
            p/collecting: make block! 10
        ]
        
        let collect-base: tail p/collecting
        if not input: parser input [
            ;
            ; Although the block rules roll back, keep might be used with
            ; other combinators that run more than one rule...and one rule
            ; might succeed, then the next fail:
            ;
            ;     uparse "(abc>" [x: collect between keep "(" keep ")"]
            ;
            clear collect-base
            return null
        ]
        if result [
            set result copy collect-base
        ]
        clear collect-base
        return input
    ]

    'keep func [
        return: [<opt> any-series!]
        p [frame!]
        input [any-series!]
        parser [action!]
    ][
        assert [p/collecting]
        if not let limit: parser input [
            return null
        ]
        append p/collecting copy/part input limit
        return limit
    ]

    === {SET-WORD! COMBINATOR} ===

    ; The concept behind Ren-C's SET-WORD! in PARSE is that some parse
    ; combinators are able to yield a result in addition to updating the
    ; position of the parse input.  If these appear to the right of a
    ; set-word, then the set word will be assigned on a match.

    set-word! func [
        return: [<opt> any-series!]
        input [any-series!]
        value [set-word!]
        parser [action!]
    ][
        if not find (parameters of :parser) '/result [
            fail "SET-WORD! in UPARSE needs result-bearing combinator"
        ]
        if not let ['input temp]: parser input [
            ;
            ; A failed rule leaves the set target word at whatever value it
            ; was before the set.
            ;
            return null
        ]
        set value temp  ; success means change variable
        return input
    ]

    === {TEXT! COMBINATOR} ===

    ; For now we just make text act as FIND/MATCH, though this needs to be
    ; sensitive to whether we are operating on blocks or text/binary.

    text! func [
        return: [<opt> any-series!]
        p [frame!]
        input [any-series!]
        value [text!]
    ][
        case [
            any-array? input [
                if input/1 = value [return next input]
                return null
            ]
            any-string? input [
                return find/match/(if p/case 'case) input value
            ]
            true [
                assert [binary? input]
                return find/match input as binary! value
            ]
        ]
    ]

    === {TOKEN! COMBINATOR (currently ISSUE! and CHAR!)} ===

    ; The TOKEN! type is an optimized immutable form of string that will
    ; often be able to fit into a cell with no series allocation.  This makes
    ; it good for representing characters, but it can also represent short
    ; strings.  It matches case-sensitively.

    issue! func [
        return: [any-series!]
        input [any-series!]
        value [issue!]
    ][
        case [
            any-array? input [
                if input/1 = value [return next input]
                return null
            ]
            any-string? input [
                return find/match input value
            ]
            true [
                assert [binary? input]
                return find/match input as binary! value
            ]
        ]
    ]

    === {BINARY! COMBINATOR} ===

    ; Arbitrary matching of binary against text is a bit of a can of worms,
    ; because if we AS alias it then that would constrain the binary...which
    ; may not be desirable.  Also you could match partial characters and
    ; then not be able to set a string position.  So we don't do that.

    binary! func [
        return: [any-series!]
        input [any-series!]
        value [issue!]
    ][
        case [
            any-array? input [
                if input/1 = value [return next input]
                return null
            ]
            any-string? input [
                fail "Can't match BINARY! against TEXT! (use AS to alias)"
            ]
            true [
                assert [binary? input]
                return find/match input value
            ]
        ]
    ]

    === {GROUP! COMBINATOR} ===

    ; Does not advance the input, just runs the group.

    group! func [
        return: [any-series!]
        input [any-series!]
        value [group!]
    ][
        do value
        return input  ; just give back same input passed in
    ]

    === {BITSET! COMBINATOR} ===

    ; There is some question here about whether a bitset used with a BINARY!
    ; can be used to match UTF-8 characters, or only bytes.  This may suggest
    ; a sort of "INTO" switch that could change the way the input is being
    ; viewed, e.g. being able to do INTO BINARY! on a TEXT! (?)

    bitset! func [
        return: [<opt> any-series!]
        input [any-series!]
        value [bitset!]
    ][
        case [
            any-array? input [
                if input/1 = value [
                    return next input
                ]
            ]
            any-string? input [
                if find value input/1 [
                    return next input
                ]
            ]
            true [
                assert [binary? input]
                if find value input/1 [
                    return next input
                ]
            ]
        ]
        return null
    ]

    === {QUOTED! COMBINATOR} ===

    ; Recognizes the value literally.  Test making it work only on the
    ; ANY-ARRAY! type, just to see if type checking can work.

    quoted! func [
         return: [<opt> any-array!]
         p [frame!]
         input [any-array!]
         value [quoted!]
    ][
        if :input/1 = unquote value [
            return next input
        ]
        return null
    ]

    === {LOGIC! COMBINATOR} ===

    ; Handling of LOGIC! in Ren-C replaces the idea of FAIL, because a logic
    ; #[true] is treated as "continue parsing" while #[false] is "rule did
    ; not match".  When combined with GET-GROUP!, this fully replaces the
    ; need for the IF construct.

    logic! func [
         return: [<opt> any-series!]
         input [any-series!]
         value [logic!]
    ][
        if value [
            return input
        ]
        return null
    ]

    === {INTEGER! COMBINATOR} ===

    ; !!! There's currently no way for an integer to be used to represent a
    ; range of matches, e.g. between 1 and 10.  This would need skippable
    ; parameters.  For now we just go for a plain repeat count.

    integer! func [
         return: [<opt> any-series!]
         input [any-series!]
         value [integer!]
         parser [action!]
    ][
        loop value [
            if not input: parser input [
                return null
            ]
        ]
        return input
    ]

    === {DATATYPE! COMBINATOR} ===

    ; Traditionally you could only use a datatype with ANY-ARRAY! types,
    ; but since Ren-C uses UTF-8 Everywhere it makes it practical to merge in
    ; transcoding:
    ;
    ;     >> uparse "{Neat!} 1020" [t: text! i: integer!]
    ;     == "{Neat!} 1020"
    ;
    ;     >> t
    ;     == "Neat!"
    ;
    ;     >> i
    ;     == 1020
    ;
    ; !!! TYPESET! is on somewhat shaky ground as "a thing", so it has to
    ; be thought about as to how `s: any-series!` or `v: any-value!` might
    ; work.  It could be that there's a generic TRANSCODE operation and
    ; then you can filter the result of that.

    datatype! func [
         return: [<opt> any-series!]
         result: [any-value!]
         input [any-series!]
         value [datatype!]
    ][
        either any-array? input [
            if value = type of input/1 [
                if result [
                    set result input/1
                ]
                return next input
            ]
            return null
        ][
            let [item 'input error]: transcode input
            if error [
                return null
            ]
            if value != type of item [
                return null
            ]
            if result [
                set result item
            ]
            return input
        ]
    ]

    === {SYM-XXX! COMBINATORS} ===

    ; The concept behind SYM-XXX! is to match exactly the thing that is
    ; held by the variable.  So if it holds a block, it does not act as
    ; a rule...but as the actual value itself.

    sym-word! func [
         return: [<opt> any-series!]
         input [any-series!]
         value [sym-word!]
    ][
        either any-array? input [
            if input/1 = get value [  ; use SYM-GROUP! for unsets
                return next input
            ]
            return null
        ][
            fail "SYM-WORD! feature only available for arrays right now."
        ]
    ]

    sym-path! func [
         return: [<opt> any-series!]
         input [any-series!]
         value [sym-path!]
    ][
        either any-array? input [
            if input/1 = get value [  ; use SYM-GROUP! for unsets
                return next input
            ]
            return null
        ][
            fail "SYM-PATH! feature only available for arrays right now."
        ]
    ]

    sym-group! func [
         return: [<opt> any-series!]
         input [any-series!]
         value [sym-group!]
    ][
        either any-array? input [
            if input/1 = do value [  ; could evaluate to an unset
                return next input
            ]
            return null
        ][
            fail "SYM-GORUP! feature only available for arrays right now."
        ]
    ]

    === {BLOCK! COMBINATOR} ===

    ; Handling of BLOCK! is the most complex combinator.  It is processed as
    ; a set of alternatives separated by `|`.  The bar is treated specially
    ; and not an "OR combinator" due to semantics, because if arguments to
    ; all the steps were captured in one giant ACTION! that would mean changes
    ; to variables by a GROUP! would not be percieved by later steps...since
    ; once a rule like SOME captures a variable it won't see changes:
    ;
    ; https://forum.rebol.info/t/when-should-parse-notice-changes/1528
    ;
    ; (There is also a performance benefit, since if | was a combinator then
    ; a sequence of 100 alternates would have to build a very large OR
    ; function...rather than being able to build a small function for each
    ; step that could short circuit before the others were needed.)

    block! func [
        return: [<opt> any-series!]
        result: [<opt> any-value!]

        p [frame!]
        input [any-series!]
        value [block!]
    ][
        let rules: value
        let pos: input

        let collect-baseline: tail try p/collecting  ; see COLLECT

        let at-least-one-success: false
        while [not tail? rules] [
            if p/verbose [
                print ["RULE:" mold/limit rules 60]
                print ["INPUT:" mold/limit pos 60]
                print "---"
            ]

            if rules/1 = ', [  ; COMMA! is only legal between steps
                rules: my next
                continue
            ]

            if rules/1 = '| [
                ;
                ; Rule alternative was fulfilled.  Base case is a match, e.g.
                ; with input "cde" then [| "ab"] will consider itself to be a
                ; match before any input is consumed, e.g. before the "c".
                ;
                return pos
            ]

            all [at-least-one-success, result] then [
                fail "SET-WORD! result requested, but more than one match"
            ]

            ; Do one "Parse Step".  This involves turning whatever is at the
            ; next parse position into an ACTION!, then running it.
            ;
            let [action 'rules]: parsify p rules
            let f: make frame! :action
            f/input: pos
            if result [
                if not find f 'result [
                    fail ["Rule does not have result to use with SET-WORD!"]
                ]
                f/result: result
            ]

            if pos: do f [
                at-least-one-success: true
            ] else [
                if p/collecting [  ; toss collected values from this pass
                    if collect-baseline [  ; we marked how far along we were
                        clear collect-baseline
                    ] else [
                        clear p/collecting  ; no mark, so must have been empty
                    ]
                ]

                ; If we fail a match, we skip ahead to the next alternate rule
                ; by looking for an `|`, resetting the input position to where
                ; it was when we started.  If there are no more `|` then all
                ; the alternates failed, so return NULL.
                ;
                pos: catch [
                    let r
                    while [r: rules/1] [
                        rules: my next
                        if r = '| [throw input]  ; reset POS
                    ]
                ] else [
                    return null
                ]
            ]
        ]
        return pos
    ]
]


non-comma: func [
    value [<opt> any-value!]
][
    ; The concept behind COMMA! is to provide a delimiting between rules.
    ; That is handled by the block combinator.  So if you see a thing like
    ; `[some, "a"]` in PARSIFY, that is just running out of turn.
    ;
    ; It may seem like making a dummy "comma combinator" for the comma
    ; type is a good idea.  But that's an unwanted axis of flexibility
    ; in this model...and also if we defer the error until the combinator
    ; is run, it might never be run.
    ;
    if comma? :value [
        fail "COMMA! can only be run between PARSE steps, not inside them"
    ]
    return :value
]


combinatorize: func [
    return: [frame!]
    advanced: [block!]

    rules [block!]
    p "Parse State" [frame!]
    c "Combinator" [action!]
    /value "Initiating value (if datatype)" [any-value!]
][
    ; Combinators take arguments.  If the arguments are quoted, then they are
    ; taken literally from the rules feed.  If they are not quoted, they will
    ; be another "parser" generated from the rules.
    ;
    ; For instance: CHANGE takes two arguments.  The first is a parser and has
    ; to be constructed with PARSIFY from the rules.  But the replacement is a
    ; literal value, e.g.
    ;
    ;      >> data: "aaabbb"
    ;      >> parse data [change some "a" "literal" some "b"]
    ;      == "literalbbb"
    ;
    ; So we see that CHANGE got SOME "A" turned into a parser action, but it
    ; received "literal" literally.  The definition of the combinator is used
    ; to determine the arguments and which kind they are.

    let f: make frame! :c

    for-each param parameters of :c [
        case [
            param = 'input [
                ; All combinators should have an input.  But the
                ; idea is that we leave this unspecialized.
            ]
            param = '/result [
                ; Actually a return value.  These are still being figured out.
            ]
            param = 'value [
                f/value: value
            ]
            param = 'p [  ; taking the UPARSE frame is optional
                f/p: p
            ]
            quoted? param [  ; literal element captured from rules
                let r: non-comma rules/1
                rules: my next

                f/(unquote param): :r
            ]
            true [  ; another parser to combine with
                ;
                ; !!! At the moment we disallow SET with GROUP!.
                ; This could be more conservative to stop calling
                ; functions.  For now, just work around it.
                ;
                let [temp 'rules]: parsify p rules
                f/(param): :temp
            ]
        ]
    ]

    set advanced rules
    return f
]


parsify: func [
    {Transform one "step's worth" of rules into a parser combinator action}

    return: "Parser action for input processing corresponding to a full rule"
        [action!]
    advanced: "Rules position advanced past the elements used for the action"
        [block!]

    p "Parse context"
        [frame!]
    rules "Parse rules to (partially) convert to a combinator action"
        [block!]
][
    let r: non-comma rules/1
    rules: my next

    ; Not sure if it's good, but the original GET-GROUP! concept allowed:
    ;
    ;     parse "aaa" [:('some) "a"]
    ;
    ; So evaluate any GET-GROUP!s before the combinator lookup.
    ;
    if get-group? :r [
        r: do r
    ]

    ; As a first step look up any keywords.  There is no WORD! combinator, so
    ; this cannot be abstracted (beyond the GET-GROUP! method above), e.g.
    ; you can't say `s: 'some, parse "aaa" [s "a"]`
    ;
    case [
        null? :r [
            set advanced rules
            return :identity
        ]

        word? :r [
            if let c: select p/combinators r [
                let [f 'rules]: combinatorize rules p :c

                set advanced rules  ; !!! Should `[:advanced]: ...` be ok?
                return make action! f
            ]
            r: get r
        ]

        path? :r [
            r: get r
        ]

        ; !!! Here is where we would let GET-PATH! and GET-WORD! be used to
        ; subvert keywords if SEEK were universally adopted.
    ]

    ; Non-keywords are also handled as combinators, where we just pass the
    ; data value itself to the handler for that type.
    ;
    ; !!! This won't work with INTEGER!, as they are actually rules with
    ; arguments.  Does this mean the block rule has to hardcode handling of
    ; integers, or that when we do these rules they may have skippable types?

    if not let c: select p/combinators kind of :r [
        fail ["Unhandled type in PARSIFY:" kind of :r]
    ]

    let [f 'rules]: combinatorize/value rules p :c r

    set advanced rules

    return make action! f  ; leave INPUT unspecialized
]


uparse: func [
    return: "Input if the parse succeeded, or NULL otherwise"
        [<opt> any-series!]
    progress: "Partial progress if requested"
        [<opt> any-series!]

    series "Input series"
        [any-series!]
    rules "Block of parse rules"
        [block!]
    /combinators "List of keyword and datatype handlers used for this parse"
        [map!]
    /case "Do case-sensitive matching"

    /verbose "Print some additional debug information"

    <local> collecting (null)
][
    combinators: default [default-combinators]

    ; Each UPARSE operation can have a different set of combinators in
    ; effect.  So it's necessary to need to have some way to get at that
    ; information.  For now, we use the FRAME! of the parse itself as the
    ; way that data is threaded...as it gives access to not just the
    ; combinators, but also the /VERBOSE or other settings...we can add more.
    ;
    let p: binding of 'return

    let f: make frame! :combinators/(block!)
    if in f 'p [  ; combinators are allowed to not take parser state
        f/p: p
    ]
    f/input: series
    f/value: rules
    let pos: do f

    ; If /PROGRESS was requested as an output, then they don't care whether
    ; the tail was reached or not.  Main return is the input unless there
    ; was an actual failure.
    ;
    if progress [
        set progress pos
        return if pos [series]
    ]

    ; Note: SERIES may change during the process of the PARSE.  This means
    ; we don't want to precalculate TAIL SERIES, since the tail may change.
    ;
    if pos = tail series [
        return series
    ]
    return null
]


=== {AND COMPATIBILITY} ===

; We do add AND as a backwards compatible form of AHEAD...even to the default
; for now (it has no competing meaning)
;
default-combinators/('and): :default-combinators/('ahead)


=== {REBOL2/R3-ALPHA/RED COMPATIBILITY} ===

; One of the early applications of UPARSE is to be able to implement backward
; compatible parse behavior by means of a series of tweaks.

redbol-combinators: copy default-combinators

append redbol-combinators reduce [

    === {OLD STYLE SET AND COPY COMBINATORS} ===

    ; Historical Rebol's PARSE had SET and COPY keywords which would take
    ; a WORD! as their first argument, and then a rule.  This was done because
    ; the SET-WORD! was taken for capturing the parse position into a
    ; variable.  That idea was first overturned by a JavaScript implementation
    ; of Rebol called Topaz, using SET-WORD! for generic captures of data
    ; out of the parse input:
    ;
    ; https://github.com/giesse/red-topaz-parse
    ;
    ; Ren-C goes along with this change, including that the position is
    ; captured by `pos: here` instead of simply by `pos:`.  However, the
    ; concept of how combinators can produce a result to be captured is
    ; rethought.

    'copy func [
        {(Old style) Copy input series elements into a SET-WORD! or WORD!}
        return: [<opt> any-series!]
        input [any-series!]
        'target [word! set-word!]
        parser [action!]
    ][
        if let pos: parser input [
            set target copy/part input pos
            return pos
        ]
        return null
    ]

    'set func [
        {(Old style) Take single input element into a SET-WORD! or WORD!}
        return: [<opt> any-series!]
        input [any-series!]
        'target [word! set-word!]
        parser [action!]
    ][
        if let pos: parser input [
            if pos = input [  ; no advancement
                set target null
                return pos
            ]
            if pos = next input [  ; one unit of advancement
                set target input/1
                return pos
            ]
            fail "SET in UPARSE can only set up to one element"
        ]
        return null
    ]

    === {OLD STYLE SET-WORD! AND GET-WORD! BEHAVIOR} ===

    ; This is the handling for sets and gets that are standalone...e.g. not
    ; otherwise quoted as arguments to combinators (like COPY X: SOME "A").
    ; These implement the historical behavior of saving the parse position
    ; into the variable or restoring it.

    set-word! func [
        return: [<opt> any-series!]
        input [any-series!]
        value [set-word!]
    ][
        set value input
        return input  ; don't change position
    ]

    get-word! func [
        return: [<opt> any-series!]
        input [any-series!]
        value [get-word!]
    ][
        ; Restriction: seeks must be within the same series.
        ;
        if not same? head input head get value [
            fail "SEEK (via GET-WORD!) in UPARSE must be in the same series"
        ]
        return get value
    ]

    === {OLD-STYLE FAIL INSTRUCTION} ===

    ; In Ren-C, the FAIL word is taken to generally relate to raising errors.
    ; PARSE was using it to mean a forced mismatch.
    ;
    ; Ren-C rethinks this so that logic #[false] is used to indicate a match
    ; has failed, and to roll over to the next alternate (if any).  By making
    ; the logic #[true] mean "keep parsing", this allows evaluated expressions
    ; that are substituted into the parse stream via GET-GROUP! to control
    ; whether parsing continues or not.

    'fail func [
         return: [<opt>]
         input [any-array!]
    ][
        return null
    ]

]

; Kill off any new combinators.

redbol-combinators/('between): null

uparse2: specialize :uparse [
    combinators: redbol-combinators
]
