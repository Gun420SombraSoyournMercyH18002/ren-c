; %parse-any.test.reb
;
; ANY in UPARSE is not an iterative construct, but a way of treating a block as
; a list of alternative rules.  This can be more convenient than having to make
; a rule block that has the alternatives separated by `|`, which has difficult
; issues to resolve on the edges.

("b" = parse "ab" [some any (["a" "b"])])
(raised? parse "abc" [some any (["a" "b"])])

(3 = parse ["foo" <baz> 3] [some any ([tag! integer! text!])])

("b" = parse "ab" [some any ["a" "b"]])
(raised? parse "abc" [some any ["a" "b"]])

(3 = parse ["foo" <baz> 3] [some any [tag! integer! text!]])
