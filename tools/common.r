REBOL [
    System: "Ren-C Core Extraction of the Rebol System"
    Title: "Common Routines for Tools"
    Type: module
    Name: Prep-Common
    Rights: {
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Version: 2.100.0
    Needs: 2.100.100
    Purpose: {
        These are some common routines used by the utilities
        that build the system, which are found in %src/tools/
    }
]

if trap [:import/into] [  ; See %import-shim.r
    do load append copy system/script/path %import-shim.r
]

import <bootstrap-shim.r>


; When you run a Rebol script, the `current-path` is the directory where the
; script is.  We assume that the Rebol source enlistment's root directory is
; one level above this file (which should be %tools/common.r)
;
export repo-dir: clean-path %../

export spaced-tab: unspaced [space space space space]

export to-c-name: function [
    {Take a Rebol value and transliterate it as a (likely) valid C identifier}

    return: [<opt> text!]
    value "Will be converted to text (via UNSPACED if BLOCK!)"
        [<maybe> text! block! word!]
    /scope "[#global #local #prefixed] see http://stackoverflow.com/q/228783/"
        [issue!]
][
    all [text? value, empty? value] then [
        fail/where ["TO-C-NAME received empty input"] 'value
    ]

    c-chars: charset [
        #"a" - #"z"
        #"A" - #"Z"
        #"0" - #"9"
        #"_"
    ]

    string: either block? :value [unspaced value] [form value]

    string: switch string [
        ; Used specifically by t-routine.c to make SYM_ELLIPSIS
        ;
        ; !!! Note: this was ... but that is now a TUPLE!  So this has to
        ; be changed for the moment.
        ;
        "***" ["ellipsis_1"]

        ; Used to make SYM_HYPHEN which is needed by `charset [#"A" - #"Z"]`
        ;
        "-" ["hyphen_1"]

        ; Used to deal with the /? refinements (which may not last)
        ;
        "?" ["question_1"]

        ; None of these are used at present, but included just in case
        ;
        "*" ["asterisk_1"]
        "!" ["exclamation_1"]
        "+" ["plus_1"]
        "~" ["tilde_1"]
        "|" ["bar_1"]

        ; Special mechanics are required so that PATH! and TUPLE! collapse
        ; to make these words:
        ;
        ;     >> compose '(_)/(_)
        ;     == /  ; a word
        ;
        "." ["dot_1"]
        "/" ["slash_1"]

        "@" ["at_1"]
        "^^" ["caret_1"]
        ":" ["colon_1"]
        "&" ["ampersand_1"]

        ; These are in the set of what are known as "alterative tokens".  They
        ; aren't exactly keywords (and in C they're just done with #define).
        ; Hence they are involved with the preproessor which means that
        ; "clever" macros like ARG(not) or REF(and) will be invoked as
        ; ARG(!) or REF(&&).  So instead use ARG(_not_) and REF(_and_), etc.
        ;
        ; (Complete list here for completeness, despite many being unused.)
        ;
        "and" ["and_1"]
        "and_eq" ["and_eq_1"]
        "bitand" ["bitand_1"]
        "bitor" ["bitor_1"]
        "compl" ["compl_1"]
        "not" ["not_1"]
        "not_eq" ["not_eq_1"]
        "or" ["or_1"]
        "or_eq" ["or_eq_1"]
        "xor" ["xor_1"]
        "xor_eq" ["xor_eq_1"]

        "did" ["did_1"]  ; This is a macro in Ren-C code
    ] then (func [s] [
        return copy s
    ]) else [
        ;
        ; If these symbols occur composite in a longer word, they use a
        ; shorthand; e.g. `foo?` => `foo_q`

        for-each [reb c] [
            #"'"  ""      ; isn't => isnt, didn't => didnt
            -   "_"     ; foo-bar => foo_bar
            *   "_p"    ; !!! because it symbolizes a (p)ointer in C??
            .   "_"     ; !!! same as hyphen?
            ?   "_q"    ; (q)uestion
            !   "_x"    ; e(x)clamation
            +   "_a"    ; (a)ddition
            |   "_b"    ; (b)ar
            #"^^"  "_c" ; (c)aret
            #"@" "_z"   ; a was taken, doesn't make less sense than * => p
        ][
            replace/all string (form reb) c
        ]

        string
    ]

    if empty? string [
        fail [
            "empty identifier produced by to-c-name for"
            (mold value) "of kind" (mold kind of value)
        ]
    ]

    scope: default [#global]

    for-next s string [
        all [
            scope <> #prefixed
            head? s
            pick charset [#"0" - #"9"] s/1
        ] then [
            fail ["identifier" string "starts with digit in to-c-name"]
        ]

        pick c-chars s/1 else [
            fail ["Non-alphanumeric or hyphen in" string "in to-c-name"]
        ]
    ]

    case [
        scope = #prefixed [<ok>]  ; assume legitimate prefix

        string/1 != #"_" [<ok>]

        scope = #global [
            fail ["global C ids starting with _ are reserved:" string]
        ]

        scope = #local [
            find charset [#"A" - #"Z"] string/2 then [
                fail [
                    "local C ids starting with _ and uppercase are reserved:"
                        string
                ]
            ]
        ]

        fail "/SCOPE must be #global or #local"
    ]

    return string
]


; http://stackoverflow.com/questions/11488616/
export binary-to-c: function [
    {Converts a binary to a string of C source that represents an initializer
    for a character array.  To be "strict" C standard compatible, we do not
    use a string literal due to length limits (509 characters in C89, and
    4095 characters in C99).  Instead we produce an array formatted as
    '{0xYY, ...}' with 8 bytes per line}

    return: [text!]
    data [binary!]
    <local> data-len
][
    data-len: length of data

    out: make text! 6 * (length of data)
    while [not empty-or-null? data] [
        ; grab hexes in groups of 8 bytes
        hexed: enbase/base (copy/part data 8) 16
        data: skip data 8
        for-each [digit1 digit2] hexed [
            append out unspaced [{0x} digit1 digit2 {,} space]
        ]

        take/last out  ; drop the last space
        if empty-or-null? data [
            take/last out  ; lose that last comma
        ]
        append out newline  ; newline after each group, and at end
    ]

    ; Sanity check (should be one more byte in source than commas out)
    parse2 out [
        (comma-count: 0)
        some [thru "," (comma-count: comma-count + 1)]
        to end
    ]
    assert [(comma-count + 1) = data-len]

    return out
]


export parse-args: function [
    return: [block!]
    args
][
    ret: make block! 4
    standalone: make block! 4
    args: any [args copy []]
    if not block? args [args: split args [some " "]]
    iterate args [
        name: null
        value: args/1
        case [
            idx: find value #"=" [; name=value
                name: to word! copy/part value (index of idx) - 1
                value: copy next idx
            ]
            #":" = last value [; name=value
                name: to word! copy/part value (length of value) - 1
                args: next args
                if empty? args [
                    fail ["Missing value after" value]
                ]
                value: args/1
            ]
        ]
        if all [; value1,value2,...,valueN
            not find value "["
            find value ","
        ][value: mold split value ","]
        either name [
            append ret spread reduce [name value]
        ][  ; standalone-arg
            append standalone value
        ]
    ]
    if not empty? standalone [
        append ret '|
        append ret standalone
    ]
    return ret
]

export uppercase-of: func [
    {Copying variant of UPPERCASE, also FORMs words}
    string [text! word!]
][
    return uppercase form string
]

export lowercase-of: func [
    {Copying variant of LOWERCASE, also FORMs words}
    string [text! word!]
][
    return lowercase form string
]

export propercase: func [value] [return uppercase/part (copy value) 1]

export propercase-of: func [
    {Make a copy of a string with just the first character uppercase}
    string [text! word!]
][
    return propercase form string
]

export write-if-changed: function [
    return: <none>
    dest [file!]
    content [text! block!]
][
    if block? content [content: spaced content]
    content: to binary! content

    any [
        not exists? dest
        content != read dest
    ] then [
        print ["DETECTED CHANGE:" dest]
        write dest content
    ]
]

export relative-to-path: func [
    return: [file!]
    target [file!]
    base [file!]
][
    assert [dir? target]
    assert [dir? base]
    target: split clean-path target "/"
    base: split clean-path base "/"
    if "" = last base [take/last base]
    while [all [
        not tail? target
        not tail? base
        base/1 = target/1
    ]] [
        base: next base
        target: next target
    ]
    iterate base [base/1: %..]
    append base spread target

    base: to-file delimit "/" base
    assert [dir? base]
    return base
]


export stripload: function [
    {Get an equivalent to MOLD/FLAT (plus no comments) without using LOAD}

    return: "contents, w/o comments or indentation"
        [text!]
    source "Code to process without LOAD (avoids bootstrap scan differences)"
        [text! file!]
    /header "<output> Request the header as text"  ; no @output in bootstrap
        [word! path!]
    /gather "Collect what look like top-level declarations into variable"
        [word!]
][
    ; Removing spacing and comments from a Rebol file is not exactly trivial
    ; without LOAD.  But not impossible...and any tough cases in the mezzanine
    ; can be dealt with by hand.
    ;
    ; Note: This also removes newlines, which may not ultimately be desirable.
    ; The line numbering information, if preserved, could help correlate with
    ; lines in the original files.  That would require preserving some info
    ; about the file of origin, though.

    pushed: copy []  ; <Q>uoted or <B>raced string delimiter stack

    comment-or-space-rule: [
        ;
        ; Note: IF is deprecated in PARSE, and `:(...)` should be used instead
        ; once the bootstrap executable supports it.
        ;
        if (empty? pushed)  ; string not in effect, okay to proceed

        ; Bootstrap WHILE: https://github.com/rebol/rebol-issues/issues/1401
        while [
            remove [some space]
            |
            ahead ";" remove [to [newline | end]]
        ]
    ]

    rule: [
        ; Bootstrap WHILE: https://github.com/rebol/rebol-issues/issues/1401
        while [
            newline [opt some [comment-or-space-rule remove newline]]
            |
            [ahead [opt some space ";"]] comment-or-space-rule
            |
            "^^{"  ; (actually `^{`) escaped brace, never count
            |
            "^^}"  ; (actually `^}`) escaped brace, never count
            |
            {^^"}  ; (actually `^"`) escaped quote, never count
            |
            "{" (if <Q> != last pushed [append pushed <B>])
            |
            "}" (if <B> = last pushed [take/last pushed])
            |
            {"} (
                case [
                    <Q> = last pushed [take/last pushed]
                    empty? pushed [append pushed <Q>]
                ]
            )
            |
            skip
        ]
    ]

    let file
    either text? source [
        contents: source  ; useful for debugging STRIPLOAD from console
        file: <textual source>
    ][
        text: as text! read source
        contents: next next find text "^/]"  ; /TAIL changed in new builds
        file: source
    ]

    ; This is a somewhat dodgy substitute for finding "top-level declarations"
    ; because it only finds things that look like SET-WORD! that are at the
    ; beginning of a line.  However, if we required the file to be LOAD-able
    ; by a bootstrap Rebol, that would create a dependency that would make
    ; it hard to use new scanner constructs in the mezzanine.
    ;
    ; Currently this is only used by the SYS context in order to generate top
    ; #define constants for easy access to the functions there.
    ;
    if gather [
        append (ensure block! get gather) spread collect [
            for-next t text [
                newline-pos: find t newline else [tail text]
                if not colon-pos: find/part t ":" newline-pos [
                    t: newline-pos
                    continue
                ]
                if space-pos: find/part t space colon-pos [
                    t: newline-pos
                    continue
                ]
                str: copy/part t colon-pos
                all [
                    not find str ";"
                    not find str "{"
                    not find str "}"
                    not find str {"}
                    not find str "/"
                    not find str "."  ; tuple assign is not a top-level decl
                ] then [
                    keep as word! str
                ]
                t: newline-pos
            ]
        ]
    ]

    if header [
        if not hdr: copy/part (next find text "[") (find text "^/]") [
            fail ["Couldn't locate header in STRIPLOAD of" file]
        ]
        parse2 hdr rule else [
            fail ["STRIPLOAD failed to munge header of" file]
        ]
        set header hdr
    ]

    parse2 contents rule else [
        fail ["STRIPLOAD failed to munge contents of" file]
    ]

    if not empty? pushed [
        fail ["String delimiter stack imbalance while parsing" file]
    ]

    return contents
]
