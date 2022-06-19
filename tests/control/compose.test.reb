; %compose.test.reb
;
; Ren-C's COMPOSE has many more features than historical Rebol.  These features
; range from having two types of slots: (single) and ((spliced)), to being
; able to put sigils or quotes on the spliced material.

; Splicing vs. non
;
([[a b] * a b] = compose [([a b]) * (([a b]))])

; Preserve one element rule vs. tolerate vaporization.  ~null~ isotopes are
; treated the same by the compose site as pure NULL.
;
([~null~ *] = compose [(reify null) * ((maybe null))])
([~null~ *] = compose [(reify ~null~) * ((maybe ~null~))])

; Voids vaporize regardless of form.

([*] = compose [(comment "single") * ((comment "spliced"))])
([* <ok>] = compose [(void) * <ok>])
([<ok> *] = compose [<ok> * ((void))])

; Isotopes raise errors if they are non-decaying.  For the moment, NULL gives
; a bad ~null~ isotope error as well (should likely be a different error)
(
    e: trap [compose [(~none~) * <ok>]]
    e.id = 'bad-isotope
)
(
    e: trap [compose [(null) * <ok>]]
    e.id = 'need-non-null
)
(
    e: trap [compose [(~null~) * <ok>]]
    e.id = 'need-non-null
)
([#[false]] = compose [(~false~)])

; BLANK!s are as-is in the single form, but vanish in the spliced form, and
; other rules are just generally the append rules for the type.  Evaluative
; types can't be spliced.
;
([_ *] = compose [(_) * ((_))])
(['a * a] = compose [(the 'a) * ((the 'a))])
([1020 * 304] = compose [(1020) * ((304))])
([@ae * ae] = compose [(@ae) * ((@ae))])

([(group) * <bad>] = compose [(the (group)) * <bad>])
(error? trap [compose [<ok> * ((the (group)))]])


(
    num: 1
    [1 num] = compose [(num) num]
)
([] = compose [])
(
    blk: []
    append blk [trap [1 / 0]]
    blk = compose blk
)
; RETURN stops the evaluation
(
    f1: func [return: [integer!]] [compose [(return 1)] 2]
    1 = f1
)
; THROW stops the evaluation
(1 = catch [compose [(throw 1 2)] 2])
; BREAK stops the evaluation
(null? repeat 1 [compose [(break 2)] 2])
; Test that errors do not stop the evaluation:
(block? compose [(trap [1 / 0])])
(
    blk: []
    not same? blk compose blk
)
(
    blk: [[]]
    same? first blk first compose blk
)
(
    blk: []
    same? blk first compose [((reduce [blk]))]
)
(
    blk: []
    same? blk first compose [(blk)]
)
; recursion
(
    num: 1
    [num 1] = compose [num ((compose [(num)]))]
)
; infinite recursion
(
    x: 0
    blk: [(x: x + 1, if x = 5000 [throw <deep-enough>]) (compose blk)]
    <deep-enough> = catch blk
)

; #1906
(
    b: copy [] insert/dup b 1 32768 compose b
    sum: 0
    for-each i b [sum: me + i]
    sum = 32768
)

; COMPOSE with implicit /ONLY-ing

(
    block: [a b c]
    [splice: a b c only: [a b c]] = compose [splice: ((block)) only: (block)]
)

; COMPOSE with pattern, beginning tests

(
    [(1 + 2) 3] = compose <*> [(1 + 2) (<*> 1 + 2)]
)(
    [(1 + 2)] = compose <*> [(1 + 2) (<*>)]
)(
    'a/(b)/3/c = compose <?> 'a/(b)/(<?> 1 + 2)/c
)(
    [(a b c) [((d) 1 + 2)]] = compose/deep </> [(a (</> 'b) c) [((d) 1 + 2)]]
)

(
    [(left alone) [c b a] c b a ((left alone))]
    = compose <$> [
        (left alone) (<$> reverse copy [a b c]) ((<$> reverse copy [a b c]))
        ((left alone))
    ]
)


; While some proposals for COMPOSE handling of QUOTED! would knock one quote
; level off a group, protecting groups from composition is better done with
; labeled compose...saving it for quoting composed material.

([3 '3 ''3] == compose [(1 + 2) '(1 + 2) ''(1 + 2)])
(['] = compose ['(if false [<vanish>])])

; Quoting should be preserved by deep composition

([a ''[b 3 c] d] == compose/deep [a ''[b (1 + 2) c] d])


; Using a SET-GROUP! will *try* to convert the composed value to a set form

([x:] = compose [('x):])
([x:] = compose [('x:):])
([x:] = compose [(':x):])

; Running code during SETIFY/GETIFY internally was dropped, because the
; scanner was using it...and it had DS_PUSH()es extant.  The feature is still
; possible, but it's not clear it's a great idea.  Punt on letting you
; getify or setify things that aren't guaranteed to succeed (e.g. a string
; might have spaces in it, and can't be turned into a SET-WORD!)
;
(error? trap [[x:] = compose [(#x):]])
(error? trap [[x:] = compose [("x"):]])

([x/y:] = compose [( 'x/y ):])
([x/y:] = compose [( 'x/y: ):])
([x/y:] = compose [( ':x/y ):])

([(x y):] = compose [( '(x y) ):])
([(x y):] = compose [( '(x y): ):])
([(x y):] = compose [( ':(x y) ):])

([[x y]:] = compose [( '[x y] ):])
([[x y]:] = compose [( '[x y]: ):])
([[x y]:] = compose [( ':[x y] ):])


; Using a GET-GROUP! will *try* to convert the composed value to a get form
;
; Note: string conversions to unbound words were done at one point, but have
; been dropped, at least for the moment:
;
;    ([:x] = compose [:(#x)])
;    ([:x] = compose [:("x")])
;
; They may be worth considering for the future.

([:x] = compose [:('x)])
([:x] = compose [:('x:)])
([:x] = compose [:(':x)])

([:x/y] = compose [:( 'x/y )])
([:x/y] = compose [:( 'x/y: )])
([:x/y] = compose [:( ':x/y )])

([:(x y)] = compose [:( '(x y) )])
([:(x y)] = compose [:( '(x y): )])
([:(x y)] = compose [:( ':(x y) )])

([:[x y]] = compose [:( '[x y] )])
([:[x y]] = compose [:( '[x y]: )])
([:[x y]] = compose [:( ':[x y] )])

; !!! This was an interesting concept, but now that REFINEMENT! and PATH! are
; unified it can't be done with PATH!, as you might say `compose obj/block`
; and mean that.  The notation for predicates have to be rethought.
;
; ([a b c d e f] = compose /identity [([a b c]) (([d e f]))])
; ([[a b c] d e f] = compose /enblock [([a b c]) (([d e f]))])
; ([-30 70] = compose /negate [(10 + 20) ((30 + 40))])


; isotopes are not legal in compose, but you can reify them
[
    ([<a> ~null~ <b>] = apply :compose [
        [<a> (if true [null]) <b>]
        /predicate chain [:reify :quote]
    ])
    (error? trap [compose [<a> (~)]])
]

[
    ([a :a a: @a ^a] = compose [('a) :('a) ('a): @('a) ^('a)])

    ([[a] :[a] [a]: @[a] ^[a]] = compose [
        ([a]) :([a]) ([a]): @([a]) ^([a])
    ])

    ([(a) :(a) (a): @(a) ^(a)] = compose [
        ('(a)) :('(a)) ('(a)): @('(a)) ^('(a))
    ])

    ([a/b :a/b a/b: @a/b ^a/b] = compose [
        ('a/b) :('a/b) ('a/b): @('a/b) ^('a/b)
    ])

    ([a.b :a.b a.b: @a.b ^a.b] = compose [
        ('a.b) :('a.b) ('a.b): @('a.b) ^('a.b)
    ])
]

; More tests of crazy quoting depths needed, as it's tricky.
[
    (['''''''] = compose ['''''''(if false [<a>])])
]

; Void slots are not offered to predicates
[
    ([] = compose/predicate [()] x -> [fail "Not called"])

    ; !!! should this be reconsidered for predicates which say they are willing
    ; to take <void>?  This would possibly break intents like REIFY unless
    ; REIFY had a version that did not take voids.
    ;
    ([] = compose/predicate [()] [^x [<void>]] -> [fail "Call?"])
]
