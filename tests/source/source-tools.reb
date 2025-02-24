REBOL [
    Title: "Rebol 'Lint'-style Checking Tool for source code invariants"
    Type: module
    Name: Source-Tools
    Rights: {
        Copyright 2015 Brett Handley
        Copyright 2015-2021 Ren-C Open Source Contributors
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        This tool arose from wanting to use Rebol for a pre-commit hook:

        https://codeinthehole.com/tips/tips-for-using-a-git-pre-commit-hook/

        It can scan for simple things like inconsistent CR LF line endings, or
        more complex policies for the codebase.  Since a C tokenizer using
        PARSE rules had already been created for auomatically generating
        header files for the API, that tokenizer is used here to give some
        level of "C syntax awareness".

        (Note: Since that C parser is used in bootstrap, not all cutting edge
        features can be used in it...since it must build with older Rebols.)

        Some of the checks are fully enforced, such as that lines not end in
        stray whitespace...or that the names in comment blocks are actually in
        sync with the corresponding C identifiers.  Other rule violations are
        just given as warnings, but not formally registered as failures yet
        (such as when lines of code are longer than 80 columns long).

        It is a baseline for implementing more experiments, and by using
        Rebol code for the checks it also exercises more code paths.
    }
]

; Root folder of the repository.
; This script makes some assumptions about the structure of the repo.
;

import %../../tools/common.r  ; sets REPO-DIR (among other things)

c-lexical: import %% (repo-dir)/tools/c-lexicals.r
import %% (repo-dir)/tools/common-parsers.r
import %% (repo-dir)/tools/text-lines.reb
import %% (repo-dir)/tools/read-deep.reb

; rebsource is organised along the lines of a context sensitive vocabulary.
;

logfn: func [message][print mold new-line/all compose message false]
log: :logfn

standard: context [
    ;
    ; Not counting newline, lines should be no longer than this.
    ;
    std-line-length: 79

    ; Not counting newline, lines over this length have an extra warning.
    ;
    max-line-length: 127

    ; Parse Rule which specifies the standard spacing between functions,
    ; from final right brace of leading function
    ; to intro comment of following function.
    ;
    function-spacing: [3 eol]
]

; Source paths are recursively read.
;
source-paths: [
    %src/
    %tests/
    %extensions/
]

extensions: [
    %.c c
    %.r rebol
    %.reb rebol
]

; Third party files don't obey Rebol source rules, so don't bother to
; check them.
;
; !!! Should be sure whitelisted files actually exist by trying to READ
; them...this list had gotten stale.
;
whitelisted: [
    %src/core/u-zlib.c
    %src/core/f-dtoa.c

    %extensions/bmp/mod-bmp.c

    %extensions/gif/mod-gif.c

    %extensions/jpg/u-jpg.c

    %extensions/png/lodepng.h
    %extensions/png/lodepng.c
]


log-emit: function [
    {Append a COMPOSE'd block to a log block, clearing any new-line flags}

    return: <none>
    log [block!]
    label [tag!]
    body [block!]
][
    body: new-line/all compose body false
    append/line log spread (head insert body label)
]

export analyse: context [

    files: function [
        {Analyse the source files of REBOL.}
        return: [block!]
    ][
        return collect [
            for-each source list.source-files [
                if find whitelisted source [continue]

                keep try analyse.file source
            ]
        ]
    ]

    file: function [
        {Analyse a file returning facts.}
        return: [<opt> block!]
        file
    ][
        return all [
            filetype: select extensions extension-of file
            type: in source filetype
            (reeval (ensure action! get type) file
                (read %% (repo-dir)/(file)))
        ]
    ]

    source: context [

        c: function [
            {Analyse a C file at the C preprocessing token level}

            return: [block!]
                "Facts about the file (lines that are too long, etc.)"
            file [file!]
            data [binary!]
        ][
            analysis: analyse.text file data
            emit: specialize :log-emit [log: analysis]

            data: as text! data

            identifier: c-lexical.grammar.identifier
            c-pp-token: c-lexical.grammar.c-pp-token

            malloc-found: copy []

            malloc-check: [
                and identifier "malloc" (
                    append malloc-found try text-line-of position
                )
            ]

            parse3/case data [
                some [
                    position: <here>
                    malloc-check
                    | c-pp-token
                ]
            ]

            if not empty? malloc-found [
                emit <malloc> [(file) (malloc-found)]
            ]

            all [
                not tail? data
                not equal? newline last data
            ] then [
                emit <eof-eol-missing> [(file)]
            ]

            emit-proto: function [return: <none> proto] [
                if not block? proto-parser.data [return none]

                do in c-parser-extension [
                    if last-func-end [
                        all [
                            did parse3 last-func-end [
                                function-spacing-rule
                                position: <here>
                                to <end>
                            ]
                            same? position proto-parser.parse-position
                        ] else [
                            line: text-line-of proto-parser.parse-position
                            append
                                non-std-func-space: default [copy []]
                                line  ; should it be appending BLANK! ?
                        ]
                    ]
                ]

                parse3 proto-parser.data [
                    try 'export
                    set name: set-word! (name: to-word name)
                    try 'enfix
                    [
                        'native
                        | 'native/combinator
                    ]
                    to <end>
                ] also [
                    ;
                    ; It's a `some-name?: native [...]`, so we expect
                    ; `DECLARE_NATIVE(some_name_q)` to be correctly lined up
                    ; as the "to-c-name" of the Rebol set-word
                    ;
                    if (
                        proto-parser.proto-arg-1
                        <> to-c-name/scope name #prefixed
                    )[
                        line: text-line-of proto-parser.parse-position
                        emit <id-mismatch> [
                            (mold proto-parser.data.1) (file) (line)
                        ]
                    ]
                ] else [
                    ;
                    ; ... ? (not a native)
                    ;
                    any [
                        (proto-parser.proto-id =
                            form to word! proto-parser.data.1)
                        (proto-parser.proto-id
                            unspaced [
                                "RL_" to word! proto-parser.data.1
                            ])
                    ] else [
                        line: text-line-of proto-parser.parse-position
                        emit <id-mismatch> [
                            (mold proto-parser.data.1) (file) (line)
                        ]
                    ]
                ]
            ]

            non-std-func-space: null
            proto-parser.emit-proto: :emit-proto
            proto-parser.process data

            if non-std-func-space [
                emit <non-std-func-space> [(file) (non-std-func-space)]
            ]

            return analysis
        ]

        rebol: function [
            {Analyse a Rebol file (no checks beyond those for text yet)}

            return: [block!]
                "Facts about the file (end of line whitespace, etc.)"
            file [file!]
            data
        ][
            analysis: analyse.text file data
            return analysis
        ]
    ]

    text: function [
        {Analyse textual formatting irrespective of language}

        return: [block!]
            "Facts about the text file (inconsistent line endings, etc)"
        file [file!]
        data
    ][
        analysis: copy []
        emit: specialize :log-emit [log: analysis]

        data: read %% (repo-dir)/(file)

        bol: null
        line: null

        stop-char: charset { ^-^M^/}
        ws-char: charset { ^-}
        wsp: [some ws-char]

        eol: [line-ending | alt-ending (append inconsistent-eol line)]
        line-ending: null

        ;
        ; Identify line termination.

        all [
            position: find data #{0a}
            1 < index of position
            13 = first back position
        ] also [
            line-ending: unspaced [CR LF]
            alt-ending: LF
        ] else [
            line-ending: LF
            alt-ending: unspaced [CR LF]
        ]

        count-line: [
            (
                line-len: subtract index of position index of bol
                if line-len > standard.std-line-length [
                    append over-std-len line
                    if line-len > standard.max-line-length [
                        append over-max-len line
                    ]
                ]
                line: 1 + line
            )
            bol: <here>
        ]

        tabbed: copy []
        whitespace-at-eol: copy []
        over-std-len: copy []
        over-max-len: copy []
        inconsistent-eol: copy []

        parse3/case data [

            last-pos: <here>

            try [
                bol: <here>
                skip (line: 1)
                seek bol
            ]

            try some [
                to stop-char
                position: <here>
                [
                    eol count-line
                    | #"^-" (append tabbed line)
                    | wsp and [line-ending | alt-ending] (
                        append whitespace-at-eol line
                    )
                    | skip
                ]
            ]
            position: <here>

            to <end>
        ]

        if not empty? over-std-len [
            emit <line-exceeds> [
                (standard.std-line-length) (file) (over-std-len)
            ]
        ]

        if not empty? over-max-len [
            emit <line-exceeds> [
                (standard.max-line-length) (file) (over-max-len)
            ]
        ]

        for-each list [tabbed whitespace-at-eol] [
            if not empty? get list [
                emit as tag! list [(file) (get list)]
            ]
        ]

        if not empty? inconsistent-eol [
            emit <inconsistent-eol> [(file) (inconsistent-eol)]
        ]

        all [
            not tail? data
            not equal? 10 last data ; Check for newline.
        ] then [
            emit <eof-eol-missing> [
                    (file) (reduce [try text-line-of tail of to text! data])
            ]
        ]

        return analysis
    ]
]

list: context [

    source-files: function [
        {Retrieves a list of source files (relative paths).}
    ][
        files: read-deep/full/strategy source-paths :source-files-seq

        sort files
        new-line/all files true

        return files
    ]

    source-files-seq: function [
        {Take next file from a sequence that is represented by a queue.}
        return: [<opt> file!]
        queue [block!]
    ][
        item: ensure file! take queue

        if equal? #"/" last item [
            contents: read %% (repo-dir)/(item)
            insert queue spread map-each x contents [join item x]
            item: null
        ] else [
            any [
                did parse3 split-path item ["tmp-" ...]
                not find extensions extension-of item
            ] then [
                item: null
            ]
        ]

        return item  ; nulled items are to be filtered out
    ]
]

c-parser-extension: context bind bind [

    ; Extend parser to support checking of function spacing.

    last-func-end: null

    lbrace: [and punctuator "{"]
    rbrace: [and punctuator "}"]
    braced: [lbrace try some [braced | not rbrace skip] rbrace]

    function-spacing-rule: (
        bind/copy standard.function-spacing c-lexical.grammar
    )

    grammar.function-body: braced

    append grammar.format-func-section spread [
        last-func-end: <here>
        try some [nl | eol | wsp]
    ]

    append grammar.other-segment '(
        last-func-end: null
    )

] proto-parser c-lexical.grammar

extension-of: function [
    {Return file extension for file.}
    return: [file!]
    file [file!]
][
    return find-last file "." else [copy %""]
]
