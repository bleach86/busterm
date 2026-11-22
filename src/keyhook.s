; busterm's keyboard hook: the X16 ROM calls it (through $032E) with each
; key's number in A, bit 7 set as it's let go, before the key is typed.
;
; Alt (keys 60 and 62) is followed. A key pressed while Alt is held is kept
; in key_alt_code for busterm's main loop, and neither it nor its release is
; typed (A is 0 on return). Everything else goes on to the ROM's own
; handler, as before.

.global busterm_key_hook

.section .text.busterm_key_hook,"ax",@progbits
busterm_key_hook:
    pha
    and #$7f
    cmp #60                 ; left Alt
    beq alt
    cmp #62                 ; right Alt
    beq alt
    lda key_alt_down
    beq pass
    pla                     ; a key with Alt held
    bmi drop                ; its release: nothing
    sta key_alt_code        ; its press: busterm's
drop:
    lda #0
    rts
alt:
    pla
    pha
    bmi alt_up
    lda #1
    sta key_alt_down
    pla
    jmp (key_old_hook)
alt_up:
    lda #0
    sta key_alt_down
    pla
    jmp (key_old_hook)
pass:
    pla
    jmp (key_old_hook)
