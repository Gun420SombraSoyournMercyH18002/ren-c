; %parse-quoted.test.reb
;
; Quoted values are matched as is, but for strings they are formed.

(
    did all [
        pos: uparse* [... [a b]] [to '[a b], <here>]
        pos = [[a b]]
    ]
)
([a b] == uparse [... [a b]] [thru '[a b]])
(1 == uparse [1 1 1] [some '1])

; !!! Review: how do we SUBPARSE a QUOTED! series?
;
;   pos: uparse* [''[1 + 2]] [subparse quoted! [copy x to <end>], <here>]
;   [] == pos
;   x == [1 + 2]
;

[
    (none? uparse [] [])
    ('a == uparse [a] ['a])
    (didn't uparse [a] ['b])
    ('b == uparse [a b] ['a 'b])
    ('a == uparse [a] [['a]])
    ('b == uparse [a b] [['a] 'b])
    ('b == uparse [a b] ['a ['b]])
    ('b == uparse [a b] [['a] ['b]])

    (
        res: ~
        did all [
            1 == uparse [] [(res: 1)]
            res = 1
        ]
    )
    (
        res: ~
        did all [
            1 == uparse [a] ['a (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse [a] ['b (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            1 == uparse [] [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: ~
        did all [
            1 == uparse [a] [['a (res: 1)]]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse [a] [['b (res: 1)]]
            res = '~before~
        ]
    )
]


; Ren-C made it possible to use quoted WORD!s in place of CHAR! or TEXT! to
; match in strings.  This gives a cleaner look, as you drop off 3 vertical
; tick marks from everything like ["ab"] to become just ['ab]
[
    (did all [
        pos: uparse* "abbbbbc" ['a some ['b], <here>]
        "c" = pos
    ])
    (did all [
        pos: uparse* "abbbbc" ['ab, some ['bc | 'b], <here>]
        "" = pos
    ])
    (did all [
        pos: uparse* "abc10def" ['abc '10, <here>]
        "def" = pos
    ])
]

[#682 (  ; like the old way...
    t: ~
    uparse "<tag>text</tag>" [thru '<tag> t: across to '</tag> to <end>]
    t == "text"
)(
    "text" = uparse "<tag>text</tag>" [between '<tag> '</tag>]  ; ah, uparse!
)]

[
    (
        res: ~
        did all [
            'a == uparse [a] [res: 'a]
            res = 'a
        ]
    )
    (
        res: ~
        did all [
            'a == uparse [a a] [res: 2 'a]
            res = 'a
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse [a a] [res: 3 'a]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            'a == uparse [a] [res: ['a]]
            res = 'a
        ]
    )
    (
        res: 0
        did all [
            'b == uparse [a a b] [<any> res: 'a <any>]
            res = 'a
        ]
    )
]
