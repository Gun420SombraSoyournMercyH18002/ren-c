; better-than-nothing ENCLOSE tests

(
    e-multiply: enclose :multiply lambda [f [frame!]] [
        let diff: abs (f.value1 - f.value2)
        diff + do f
    ]

    73 = e-multiply 7 10
)
(
    n-add: enclose :add lambda [f [frame!]] [
        if 10 <> f.value1 [
            f.value1: 5
            do f
        ]
    ]

    did all [
        void? n-add 10 20
        25 = n-add 20 20
    ]
)

; Enclose should be able to be invisible
[(
    var: #before
    inner: func [] [
        return var: 1020
    ]
    outer: enclose :inner func [f] [
        assert [1020 = do f]
        return nihil
    ]
    did all [
        304 = (304 outer)
        nihil? outer
        var = 1020
    ]
)(
    var: #before
    inner: func [return: [<nihil>]] [
        var: 1020
        return nihil
    ]
    outer: enclose :inner func [return: [<nihil> <opt> any-value!] f] [
        return ^(eval f)  ; don't unquote it here
    ]
    did all [
        nihil' = outer
        var = 1020
    ]
)(
    var: #before
    inner: func [return: [<nihil>]] [
        var: 1020
        return nihil
    ]
    outer: enclose :inner func [return: [<nihil> <opt> <void> any-value!] f] [
        return eval f  ; now try unquoting
    ]
    did all [
        nihil' = ^(outer)
        var = 1020
    ]
)]

(
    wrapped: enclose (
        func [@out in] [out: in + 1]
    ) f -> [
        x: f.in
        do f
        f.out * 10
    ]
    110 = wrapped 10
)

~bad-pick~ !! (
    wrapped: enclose (
        func [@out in] [out: in + 1]
    ) f -> [
        x: f.in
        do f
        f.in
    ]

    wrapped 10
)
