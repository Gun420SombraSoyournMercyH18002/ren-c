; %loops/cycle.test.reb
;
; Historical Rebol had a FOREVER loop which was typically exited via a BREAK.
; Ren-C reserves FOREVER for an actual unbreakable loop, and calls it CYCLE.
;
; CYCLE is unique because it is allowed to violate the "loop result protocol"
; and end the loop returning something other than NULL...because it is known
; that all instances of CYCLE were "stopped".  So there's no ambiguity about
; whether a result represents a stopping condition or a fully completed loop.

(
    num: 0
    cycle [
        num: num + 1
        if num = 10 [break]
    ]
    num = 10
)
; Test break and continue
(null? cycle [break])
(
    success: true
    cycle?: true
    cycle [if cycle? [cycle?: false, continue, success: false] break]
    success
)
; Test that arity-1 return stops the loop
(
    f1: func [return: [integer!]] [cycle [return 1]]
    1 = f1
)
; Test that arity-0 return stops the loop
(none? reeval unrun func [return: <none>] [cycle [return none]])

; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    e: null
    cycle [
        num: num + 1
        if num = 10 [e: trap [1 / 0] break]
        trap [1 / 0]
    ]
    all [error? e, num = 10]
)

; Recursion check
(
    num1: 0
    num3: 0
    cycle [
        if num1 = 5 [break]
        num2: 0
        cycle [
            if num2 = 2 [break]
            num3: num3 + 1
            num2: num2 + 1
        ]
        num1: num1 + 1
    ]
    10 = num3
)

; Unlike loops with ordinary termination conditions, CYCLE can return a
; value with STOP.  Plain STOP is not conflated with BREAK.
;
('~[']~ = ^ cycle [stop])
(10 = cycle [stop/with 10])
(null = cycle [break])

; !!! Right now null implements refinement revocation, which makes STOP/WITH
; a null equivalent to STOP with no argument.  This is being reviewed:
;
; https://forum.rebol.info/t/line-continuation-and-arity-bugs-thoughts/1965/3
;
('~[']~ = ^ cycle [stop/with null])  ; allowed... for now
