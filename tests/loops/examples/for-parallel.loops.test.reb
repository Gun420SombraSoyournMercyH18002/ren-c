; %for-parallel.loop.test.reb
;
; Function requested by @gchiu, serves as another test of loop composition.

[
    (for-parallel: lambda [
        vars [block!]
        blk1 [blank! block!]
        blk2 [blank! block!]
        body [block!]
    ][
        while [(not empty? blk1) or (not empty? blk2)] [  ; _ and [] are EMPTY?
            ;
            ; Use the cool UNPACK facility to set the variables:
            ; https://forum.rebol.info/t/1634
            ;
            (vars): unpack [(try first blk1) (try first blk2)]

            do body  ; BREAK from body break the outer while, it returns NULL

            ; Now ELIDE the increment, so body evaluation above is result
            ;
            elide blk1: ((try next blk1) else '_)
            elide blk2: ((try next blk2) else '_)
        ]
    ], true)

    ([[a 1] [b 2]] = collect [
        assert [
            20 = for-parallel [x y] [a b] [1 2] [
                keep/only :[x y]
                y * 10
            ]
        ]
    ])

    ([[a 1] [b 2] [c ~null~]] = collect [
        assert [
            <exhausted> = for-parallel [x y] [a b c] [1 2] [
                keep/only :[x reify y]
                if y [y * 10] else [<exhausted>]
            ]
        ]
    ])

    ([[a 1] [b 2] [~null~ 3]] = collect [
        assert [
            30 = for-parallel [x y] [a b] [1 2 3] [
                keep/only :[reify x y]
                y * 10
            ]
        ]
    ])
]
