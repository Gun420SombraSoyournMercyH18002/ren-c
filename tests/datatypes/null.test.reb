; null.test.reb
;
; Note: NULL is not technically a datatype.
;
; It is a transitional state that can be held by variables, but it cannot
; appear in BLOCK!s etc.  It's use is as a kind of "soft failure", and can
; be tested for and reacted to easily with things like DID, DIDN'T, THEN, ELSE.

(null? null)
(null? type of null)
(not null? 1)

; Early designs for NULL did not let you get or set them from plain WORD!
; Responsibility for kind of "ornery-ness" this shifted to isotopes, as NULL
; took on increasing roles as the "true NONE!" and became the value for
; unused refinements.
(
    a: ~
    did all [
        null? a: _
        null? a
        null = a
    ]
)
(
    a: ~
    did all [
        null = set 'a null
        null? a
        null = a
    ]
)

; The specific role of ~null~ isotopes is to be reactive with THEN and not
; ELSE, so that failed branches may be purposefully NULL.
;
; HEAVY is probably not the best name for an operator that creates null
; isotopes out of NULL and passes everything else through.  But it's what it
; was called.
[
    (null' = ^ null)
    ('~null~ = ^ heavy null)

    (x: heavy 10, 10 = x)
    (x: heavy null, null' = ^ x)
    (x: heavy null, null' = ^ :x)

    (304 = (null then [1020] else [304]))
    (1020 = (heavy null then [1020] else [304]))
]

; Conditionals return NULL on failure, and ~null~ isotope on a branch that
; executes and evaluates to either NULL or ~null~ isotope.  If the branch
; wishes to pass the null "as-is" it should use the @ forms.
[
    ('~null~ = ^ if true [null])
    ('~null~ = ^ if true [heavy null])
    ('~void~ = ^ if true [])
    ('~custom~ = ^ if true [~custom~])
    (''~custom~ = ^ if true ['~custom~])

    (void' <> ^ ~void~)  ; tests for isotopes
    (not void' = ^ first [~void~])  ; plain BAD-WORD!s do not count
    (not void' = ^ 'void)  ; ...nor do words, strings, etc

    ; Because ^[] forms replaced @[] forms, there are some stale references
    ; that need to be cleaned up before this behavior is enabled.
    ;
    ; (null = if true ^[null])
    ; ('~null~ = if true ^[heavy null])
    ; ('~none~ = if true ^[])
    ; ('~custom~ = if true ^[~custom~])
    ; (''~custom~ = if true ^['~custom~])
]
