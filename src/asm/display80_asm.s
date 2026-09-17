;=================================================================
; display80_asm.s — composition d'une rangee 80 colonnes en 1 bpp (NeoTel)
;
; void __fastcall__ blit80_row(void)
;   Compose dans display_rowbuf (14 lignes de 90 octets, effacees ici) les
;   cellules b80_cells[0..79] (ti_cell_t : ch, attr) au format 80 colonnes :
;   cellule = 9 pixels (glyphe 8x14 + colonne de fond), position bit col*9.
;   b80_row0 != 0 : attributs ignores (rangee 00). b80_cursor = colonne du
;   tiret (0xFF = aucun). g_blink_phase : cellules clignotantes vides.
;
;   Attributs : gras = glyphe OU glyphe>>1 ; inverse = 9 pixels inverses ;
;   souligne = 14e ligne pleine ; curseur = 14e ligne inversee.
;
;   Depot : pour la colonne c, octet b = c + c/8, decalage s = c & 7 ;
;   octet haut = g >> s, octet bas = g << (8-s), 9e pixel = $80 >> s dans
;   l'octet b+1. Les decalages variables passent par des chaines de LSR/ASL
;   deroulees dont le point d'entree est patche par cellule (jmp modifie).
;   Une ligne de glyphe nulle ne coute que le test.
;=================================================================

        .export   _blit80_row
        .export   _b80_cells, _b80_row0, _b80_cursor, _b80_ncols
        .import   _display_rowbuf, _font80, _font80_fr_index, _font80_offset
        .import   _g_blink_phase

ROWBUF  = _display_rowbuf
STRIDE  = 90

TI_ATTR_BOLD      = $01
TI_ATTR_UNDERLINE = $02
TI_ATTR_BLINK     = $04
TI_ATTR_INVERSE   = $08
TI_ATTR_FRENCH    = $10
TI_ATTR_ERROR     = $20
FONT80_BLOCK      = 107

        .zeropage
cellp:  .res 2              ; cellule courante
glyph:  .res 2              ; glyphe 14 octets
gbyte:  .res 1              ; octet de glyphe courant (apres gras/inversion)
tmp:    .res 1
xormask: .res 1             ; $FF si inverse
ninth:  .res 1              ; masque du 9e pixel ($80 >> s) si inverse, sinon 0
mask9:  .res 1              ; $80 >> s
col:    .res 1
bidx:   .res 1              ; b = col + col/8
cattr:  .res 1
bold:   .res 1

        .segment "BSS"
_b80_cells:  .res 2
_b80_row0:   .res 1
_b80_cursor: .res 1
_b80_ncols:  .res 1         ; 80 : pas de 9 pixels ; 40 : pas de 18 (glyphe non double)

        .segment "CODE"

;-----------------------------------------------------------------
; Chaines de decalage : shr_entry(s) execute s LSR, shl_entry(s) 8-s ASL.
;-----------------------------------------------------------------
shr7:   lsr  a
shr6:   lsr  a
shr5:   lsr  a
shr4:   lsr  a
shr3:   lsr  a
shr2:   lsr  a
shr1:   lsr  a
shr0:   rts

shl8:   asl  a
shl7:   asl  a
shl6:   asl  a
shl5:   asl  a
shl4:   asl  a
shl3:   asl  a
shl2:   asl  a
shl1:   asl  a
shl0:   rts

shr_tab: .word shr0, shr1, shr2, shr3, shr4, shr5, shr6, shr7   ; index s
shl_tab: .word shl8, shl7, shl6, shl5, shl4, shl3, shl2, shl1   ; index s : 8-s ASL

; jmp patches (operande modifiee par cellule)
do_shr: jmp  shr0
do_shl: jmp  shl0

;-----------------------------------------------------------------
; Depot d'une ligne : gbyte a la ligne l (base = ROWBUF + l*STRIDE), X = b.
;-----------------------------------------------------------------
.macro  DEPOSIT base
        lda  gbyte
        beq  :+
        jsr  do_shr
        ora  base,x
        sta  base,x
        lda  gbyte
        jsr  do_shl
        ora  base+1,x
        sta  base+1,x
:       lda  ninth
        beq  :+
        ora  base+1,x
        sta  base+1,x
:
.endmacro

; Charge l'octet de glyphe y (avec gras et inversion) dans gbyte.
.macro  LOADG
        lda  (glyph),y
        bit  bold
        bpl  :+
        sta  tmp
        lsr  a
        ora  tmp
:       eor  xormask
        sta  gbyte
.endmacro

;-----------------------------------------------------------------
; blit80_row
;-----------------------------------------------------------------
_blit80_row:
        ; effacer le tampon (1260 octets)
        lda  #0
        ldx  #0
@clr:   sta  ROWBUF,x
        sta  ROWBUF+256,x
        sta  ROWBUF+512,x
        sta  ROWBUF+768,x
        sta  ROWBUF+1024,x
        inx
        bne  @clr                   ; 1280 octets (>= 1260)

        lda  _b80_cells
        sta  cellp
        lda  _b80_cells+1
        sta  cellp+1
        lda  #0
        sta  col
@cell:
        ; --- attributs ---
        ldy  #1
        lda  (cellp),y
        ldx  _b80_row0
        beq  :+
        lda  #0
:       sta  cattr
        ; gras
        lda  #0
        sta  bold
        lda  cattr
        and  #TI_ATTR_BOLD
        beq  :+
        lda  #$80
        sta  bold
:       ; inversion
        lda  #0
        sta  xormask
        sta  ninth
        lda  cattr
        and  #TI_ATTR_INVERSE
        beq  :+
        lda  #$FF
        sta  xormask
:       ; --- glyphe ---
        lda  cattr
        and  #TI_ATTR_ERROR
        beq  @notblock
        ldx  #FONT80_BLOCK
        bra  @haveidx
@notblock:
        lda  cattr
        and  #TI_ATTR_BLINK
        beq  @noblink
        lda  _g_blink_phase
        beq  @noblink
        ldx  #0                     ; phase eteinte : espace
        bra  @haveidx
@noblink:
        lda  (cellp)                ; ch
        sec
        sbc  #$20
        bcc  @space
        cmp  #96
        bcc  @inrange
@space: lda  #0
@inrange:
        tax
        lda  cattr
        and  #TI_ATTR_FRENCH
        beq  @haveidx
        lda  _font80_fr_index,x
        tax
@haveidx:
        txa
        asl  a                      ; index * 2 (table de mots)
        tax
        lda  _font80_offset,x
        clc
        adc  #<_font80
        sta  glyph
        lda  _font80_offset+1,x
        adc  #>_font80
        sta  glyph+1

        ; --- position : 80 col. : bit = col*9 -> b = col + col/8, s = col & 7
        ;                40 col. : bit = col*18 -> b = 2*col + col/4, s = (2*col) & 7
        lda  _b80_ncols
        cmp  #40
        beq  @pos40
        lda  col
        lsr  a
        lsr  a
        lsr  a
        clc
        adc  col
        sta  bidx
        lda  col
        and  #7
        sta  tmp                    ; s
        bra  @pos_ok
@pos40: lda  col
        lsr  a
        lsr  a
        sta  tmp
        lda  col
        asl  a
        clc
        adc  tmp
        sta  bidx
        lda  col
        asl  a
        and  #7
        sta  tmp
@pos_ok:
        asl  a
        tax                         ; s * 2
        lda  shr_tab,x
        sta  do_shr+1
        lda  shr_tab+1,x
        sta  do_shr+2
        lda  shl_tab,x
        sta  do_shl+1
        lda  shl_tab+1,x
        sta  do_shl+2
        lda  #$80
        ldx  tmp
        beq  :++
:       lsr  a
        dex
        bne  :-
:       sta  mask9
        lda  xormask
        beq  :+
        lda  mask9
:       sta  ninth                  ; 9e pixel allume si inverse

        ; --- 13 premieres lignes ---
        ldx  bidx
        ldy  #0
        LOADG
        DEPOSIT ROWBUF+0*STRIDE
        iny
        LOADG
        DEPOSIT ROWBUF+1*STRIDE
        iny
        LOADG
        DEPOSIT ROWBUF+2*STRIDE
        iny
        LOADG
        DEPOSIT ROWBUF+3*STRIDE
        iny
        LOADG
        DEPOSIT ROWBUF+4*STRIDE
        iny
        LOADG
        DEPOSIT ROWBUF+5*STRIDE
        iny
        LOADG
        DEPOSIT ROWBUF+6*STRIDE
        iny
        LOADG
        DEPOSIT ROWBUF+7*STRIDE
        iny
        LOADG
        DEPOSIT ROWBUF+8*STRIDE
        iny
        LOADG
        DEPOSIT ROWBUF+9*STRIDE
        iny
        LOADG
        DEPOSIT ROWBUF+10*STRIDE
        iny
        LOADG
        DEPOSIT ROWBUF+11*STRIDE
        iny
        LOADG
        DEPOSIT ROWBUF+12*STRIDE

        ; --- 14e ligne : souligne (pleine), curseur (inversee) ---
        iny
        LOADG
        lda  cattr
        and  #TI_ATTR_UNDERLINE
        beq  :+
        lda  #$FF
        eor  xormask
        sta  gbyte                  ; 9 pixels : gbyte inverse si inverse
        lda  ninth
        eor  mask9
        sta  ninth
:       lda  col
        cmp  _b80_cursor
        bne  :+
        lda  gbyte
        eor  #$FF
        sta  gbyte
        lda  ninth
        eor  mask9
        sta  ninth
:       DEPOSIT ROWBUF+13*STRIDE

        ; --- cellule suivante ---
        clc
        lda  cellp
        adc  #2
        sta  cellp
        bcc  :+
        inc  cellp+1
:       inc  col
        lda  col
        cmp  _b80_ncols
        beq  @done
        jmp  @cell
@done:  rts
