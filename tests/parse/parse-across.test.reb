; %parse-across.test.reb
;
; ACROSS is UPARSE's version of historical PARSE COPY.
;
; !!! It is likely that this will change back, due to making peace with the
; ambiguity that COPY represents as a term, given all the other ambiguities.
; However, ACROSS is distinct and hence can be mixed in for compatibility
; while hybrid syntax is still around.


(did all [
    "bbb" == parse "aaabbb" [x: across some "a", y: across [some "b"]]
    x = "aaa"
    y = "bbb"
])

[https://github.com/red/red/issues/1093
   (
        se53-copied: copy ""
        did all [
            "abcde" == parse "abcde" [
                "xyz" | s: across to <end> (se53-copied: :s)
            ]
            "abcde" = se53-copied
        ]
    )
    (
        se53-copied: copy #{}
        did all [
            #{0102030405} == parse #{0102030405} [
                #{AABBCC} | s: across to <end> (se53-copied: :s)
            ]
            #{0102030405} = se53-copied
        ]
    )
]

; BLOCK! copying tests from %parse-test.red
[
    (
        res: ~
        did all [
            [a] == parse [a] [res: across <any>]
            res = [a]
        ]
    )
    (
        res: ~
        did all [
            [a] == parse [a] [res: across 'a]
            res = [a]
        ]
    )
    (
        res: ~
        did all [
            [a] == parse [a] [res: across word!]
            res = [a]
        ]
    )
    (
        res: ~
        res2: ~
        did all [
            [a] == parse [a] [res: across res2: across 'a]
            res = [a]
            res2 = [a]
        ]
    )
    (
        res: ~
        did all [
            [a a] == parse [a a] [res: across repeat 2 'a]
            res = [a a]
        ]
    )
    (
        res: '~before~
        did all [
            raised? parse [a a] [res: across repeat 3 'a]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            [a] == parse [a] [res: across ['a]]
            res = [a]
        ]
    )
    (
        res: ~
        did all [
            'b == parse [a a b] [<any> res: across 'a <any>]
            res = [a]
        ]
    )
    (
        res: ~
        did all [
            'b == parse [a a b] [<any> res: across ['a | 'b] <any>]
            res = [a]
        ]
    )
    (
        res: '~before~
        did all [
            raised? parse [a] [res: across ['c | 'b]]
            res = '~before~
        ]
    )
    (
        wa: ['a]
        res: ~
        did all [
            [a] == parse [a] [res: across wa]
            res = [a]
        ]
    )
    (
        wa: ['a]
        res: ~
        did all [
            [a a] == parse [a a] [res: across repeat 2 wa]
            res = [a a]
        ]
    )
]


; ACROSS tests with /PART from %parse-test.red
; !!! At time of writing the /PART feature in UPARSE is fake
[
    (
        input: [h 5 #l "l" o]
        input2: [a a a b b]
        true
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across repeat 3 <any>] 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            [h 5 #l] == parse/part input [v: across repeat 3 <any>] 3
            v = [h 5 #l]
        ]
    )
    (
        v: ~
        did all [
            raised? parse/part input [v: across repeat 3 <any>] 4
            v = [h 5 #l]
        ]
    )
    (
        v: ~
        did all [
            "l" == parse/part input [v: across repeat 3 <any> <any>] 4
            v = [h 5 #l]
        ]
    )
    (
        v: ~
        did all [
            [5 #l "l"] == parse/part next input [v: across repeat 3 <any>] 3
            v = [5 #l "l"]
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across to 'o <any>] 3
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            'o == parse/part input [v: across to 'o <any>] 5
            v = [h 5 #l "l"]
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input2 [v: across repeat 3 'a] 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            [a a a] == parse/part input2 [v: across repeat 3 'a] 3
            v = [a a a]
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across repeat 3 <any>] skip input 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            [h 5 #l] == parse/part input [v: across repeat 3 <any>] skip input 3
            v = [h 5 #l]
        ]
    )
    (
        v: ~
        did all [
            raised? parse/part input [v: across repeat 3 <any>] skip input 4
            v = [h 5 #l]
        ]
    )
    (
        v: ~
        did all [
            "l" == parse/part input [
                v: across repeat 3 <any> <any>
            ] skip input 4
            v = [h 5 #l]
        ]
    )
    (
        v: ~
        did all [
            [5 #l "l"] == parse/part next input [
                v: across repeat 3 <any>
            ] skip input 4
            v = [5 #l "l"]
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across to 'o <any>] skip input 3
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            'o == parse/part input [v: across to 'o <any>] skip input 5
            v = [h 5 #l "l"]
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input2 [v: across repeat 3 'a] skip input2 2
            v = '~before~
        ]
    )
    (
        v: blank
        did all [
            [a a a] == parse/part input2 [v: across repeat 3 'a] skip input2 3
            v = [a a a]
        ]
    )
]


; TEXT! copying tests from %parse-test.red
[
    (
        res: ~
        did all [
            "a" == parse "a" [res: across <any>]
            res = "a"
        ]
    )
    (
        res: ~
        did all [
            "a" == parse "a" [res: across #a]
            res = "a"
        ]
    )
    (
        res: ~
        res2: ~
        did all [
            "a" == parse "a" [res: across res2: across #a]
            res = "a"
            res2 = "a"
        ]
    )
    (
        res: ~
        did all [
            "aa" == parse "aa" [res: across repeat 2 #a]
            res = "aa"
        ]
    )
    (
        res: '~before~
        did all [
            raised? parse "aa" [res: across repeat 3 #a]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            "a" == parse "a" [res: across [#a]]
            res = "a"
        ]
    )
    (
        wa: [#a]
        res: ~
        did all [
            "a" == parse "a" [res: across wa]
            res = "a"
        ]
    )
    (
        wa: [#a]
        res: ~
        did all [
            "aa" == parse "aa" [res: across repeat 2 wa]
            res = "aa"
        ]
    )
    (
        res: ~
        did all [
            #b == parse "aab" [<any> res: across #a <any>]
            res = "a"
        ]
    )
    (
        res: ~
        did all [
            #b == parse "aab" [<any> res: across [#a | #b] <any>]
            res = "a"
        ]
    )
    (
        res: '~before~
        did all [
            raised? parse "a" [res: across [#c | #b]]
            res = '~before~
        ]
    )
]

; Testing ACROSS with /PART on Strings from %parse-test.red
; !!! At time of writing, the /PART feature in UPARSE is faked.
[
    (
        input: "hello"
        input2: "aaabb"
        letters: charset [#a - #o]
        true
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across repeat 3 <any>] 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            "hel" == parse/part input [v: across repeat 3 <any>] 3
            v = "hel"
        ]
    )
    (
        v: ~
        did all [
            raised? parse/part input [v: across repeat 3 <any>] 4
            v = "hel"
        ]
    )
    (
        v: ~
        did all [
            #l == parse/part input [v: across repeat 3 <any> <any>] 4
            v = "hel"
        ]
    )
    (
        v: ~
        did all [
            "ell" == parse/part next input [v: across repeat 3 <any>] 3
            v = "ell"
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across to #o <any>] 3
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            #o == parse/part input [v: across to #o <any>] 5
            v = "hell"
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across repeat 3 letters] 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            "hel" == parse/part input [v: across repeat 3 letters] 3
            v = "hel"
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input2 [v: across repeat 3 #a] 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            "aaa" == parse/part input2 [v: across repeat 3 #a] 3
            v = "aaa"
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across repeat 3 <any>] skip input 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            "hel" == parse/part input [v: across repeat 3 <any>] skip input 3
            v = "hel"
        ]
    )
    (
        v: ~
        did all [
            raised? parse/part input [v: across repeat 3 <any>] skip input 4
            v = "hel"
        ]
    )
    (
        v: ~
        did all [
            #l == parse/part input [v: across skip 3, <any>] skip input 4
            v = "hel"
        ]
    )
    (
        v: ~
        did all [
            "ell" == parse/part next input [v: across skip 3] skip input 4
            v = "ell"
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across to #o <any>] skip input 3
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            #o == parse/part input [v: across to #o <any>] skip input 5
            v = "hell"
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across repeat 3 letters] skip input 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            "hel" == parse/part input [v: across repeat 3 letters] skip input 3
            v = "hel"
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input2 [v: across repeat 3 #a] skip input2 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            "aaa" == parse/part input2 [v: across repeat 3 #a] skip input2 3
            v = "aaa"
        ]
    )
]


; BINARY! copying tests from %parse-test.red
[
    (
        res: ~
        did all [
            #{0A} == parse #{0A} [res: across <any>]
            res = #{0A}
        ]
    )
    (
        res: ~
        did all [
            #{0A} == parse #{0A} [res: across #{0A}]
            res = #{0A}
        ]
    )
    (
        res: ~
        res2: ~
        did all [
            #{0A} == parse #{0A} [res: across res2: across #{0A}]
            res = #{0A}
            res2 = #{0A}
        ]
    )
    (
        res: ~
        did all [
            #{0A0A} == parse #{0A0A} [res: across repeat 2 #{0A}]
            res = #{0A0A}
        ]
    )
    (
        res: '~before~
        did all [
            raised? parse #{0A0A} [res: across repeat 3 #{0A}]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            #{0A} == parse #{0A} [res: across [#{0A}]]
            res = #{0A}
        ]
    )
    (
        res: ~
        did all [
            11 == parse #{0A0A0B} [<any> res: across #{0A} <any>]
            res = #{0A}
        ]
    )
    (
        res: ~
        did all [
            11 == parse #{0A0A0B} [<any> res: across [#{0A} | #{0B}] <any>]
            res = #{0A}
        ]
    )
    (
        res: '~before~
        did all [
            raised? parse #{0A} [res: across [#"^L" | #{0B}]]
            res = '~before~
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        did all [
            #{0A} == parse #{0A} [res: across wa]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        did all [
            #{0A0A} == parse #{0A0A} [res: across repeat 2 wa]
            res = #{0A0A}
        ]
    )
]

; Testing ACROSS with /PART on BINARY! from %parse-test.red
; !!! At time of writing, the /PART feature in UPARSE is fake
[
    (
        input: #{DEADBEEF}
        input2: #{0A0A0A0B0B}
        letters: charset [#­ - #Þ]
        true
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across repeat 3 <any>] 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            #{DEADBE} == parse/part input [v: across skip 3] 3
            v = #{DEADBE}
        ]
    )
    (
        v: ~
        did all [
            raised? parse/part input [v: across repeat 3 <any>] 4
            v = #{DEADBE}
        ]
    )
    (
        v: ~
        did all [
            239 == parse/part input [v: across skip 3, <any>] 4
            v = #{DEADBE}
        ]
    )
    (
        v: ~
        did all [
            #{ADBEEF} == parse/part next input [v: across skip 3] 3
            v = #{ADBEEF}
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across to #o <any>] 3
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            239 == parse/part input [v: across to #{EF} <any>] 5
            v = #{DEADBE}
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across repeat 3 letters] 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            #{DEADBE} == parse/part input [v: across repeat 3 letters] 3
            v = #{DEADBE}
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input2 [v: across repeat 3 #{0A}] 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            #{0A0A0A} == parse/part input2 [v: across repeat 3 #{0A}] 3
            v = #{0A0A0A}
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across skip 3] skip input 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            #{DEADBE} == parse/part input [v: across skip 3] skip input 3
            v = #{DEADBE}
        ]
    )
    (
        v: ~
        did all [
            raised? parse/part input [v: across repeat 3 <any>] skip input 4
            v = #{DEADBE}
        ]
    )
    (
        v: ~
        did all [
            239 == parse/part input [v: across skip 3, <any>] skip input 4
            v = #{DEADBE}
        ]
    )
    (
        v: ~
        did all [
            #{ADBEEF} == parse/part next input [v: across skip 3] skip input 4
            v = #{ADBEEF}
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across to #o <any>] skip input 3
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            239 == parse/part input [v: across to #{EF} <any>] skip input 5
            v = #{DEADBE}
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input [v: across repeat 3 letters] skip input 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            #{DEADBE} == parse/part input [
                v: across repeat 3 letters
            ] skip input 3
            v = #{DEADBE}
        ]
    )
    (
        v: '~before~
        did all [
            raised? parse/part input2 [v: across repeat 3 #{0A}] skip input2 2
            v = '~before~
        ]
    )
    (
        v: ~
        did all [
            #{0A0A0A} == parse/part input2 [
                v: across repeat 3 #{0A}
            ] skip input2 3
            v = #{0A0A0A}
        ]
    )
]

; Parsing URL!s and ANY-SEQUENCE! is read-only
[(
    did all [
        "example" == parse http://example.com [
            "http:" some "/" name: between <here> ".com"
        ]
        name = "example"
    ]
)(
    did all [
        'jkl == parse 'abc.<def>.<ghi>.jkl [word! tags: across some tag! word!]
        tags = [<def> <ghi>]
    ]
)]
