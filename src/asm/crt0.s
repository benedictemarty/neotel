;=================================================================
; crt0.s — demarrage cc65 pour le Neo6502 (NeoTel)
;
; Charge a $0800, entre par l'adresse d'execution de l'en-tete .neo (ou le
; vecteur reset patche par les emulateurs, option « cold »). Initialise la
; pile, la pile C (sp), BSS/DATA, appelle main(), puis recharge NeoBASIC
; (API 1,3) et lui rend la main. Repris d'AsteroNeo (meme auteur).
;=================================================================

        .export   _init, _exit
        .export   __STARTUP__ : absolute = 1
        .import   _main
        .import   __RAM_START__, __RAM_SIZE__, __STACKSIZE__
        .import   copydata, zerobss, initlib, donelib

        .include  "zeropage.inc"

        .segment  "STARTUP"

_init:
        ldx  #$FF
        txs
        cld

        lda  #<(__RAM_START__ + __RAM_SIZE__ + __STACKSIZE__)
        ldx  #>(__RAM_START__ + __RAM_SIZE__ + __STACKSIZE__)
        sta  c_sp
        stx  c_sp+1

        jsr  zerobss
        jsr  copydata
        jsr  initlib

        jsr  _main

_exit:
        jsr  donelib
        ; Retour a NeoBASIC : 1,3 « Load BASIC » recharge l'interpreteur a
        ; $0800 (par-dessus ce programme) et place son adresse de depart en
        ; $0000, puis jmp (0).
        sei
        ldx  #$FF
        txs
@w:     lda  $FF00
        bne  @w
        lda  #3
        sta  $FF01
        lda  #1
        sta  $FF00
@w2:    lda  $FF00
        bne  @w2
        jmp  ($0000)
