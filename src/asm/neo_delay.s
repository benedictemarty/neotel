;=================================================================
; neo_delay.s — delai actif calibre pour le 65C02 du Neo6502 (6,25 MHz)
;
; void __fastcall__ neo_delay_ms(unsigned int ms) : A/X = ms.
; Chaque milliseconde = 5 tours de 250 iterations (dex/bne = 5 cycles),
; soit ~6 250 cycles + surcout de boucle (~1 %).
;=================================================================

        .export   _neo_delay_ms

        .segment "CODE"

_neo_delay_ms:
        sta  ms_lo
        stx  ms_hi
        ora  ms_hi
        beq  done
@ms:    ldy  #5
@outer: ldx  #250
@inner: dex
        bne  @inner
        dey
        bne  @outer
        lda  ms_lo
        bne  @dec
        dec  ms_hi
@dec:   dec  ms_lo
        lda  ms_lo
        ora  ms_hi
        bne  @ms
done:   rts

        .segment "BSS"
ms_lo:  .res 1
ms_hi:  .res 1
