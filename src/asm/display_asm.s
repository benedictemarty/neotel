;=================================================================
; display_asm.s — rasterisation des cellules Minitel (NeoTel)
;
; Deux routines exportees vers C (display.c) :
;
; unsigned char __fastcall__ blit_run(void)
;   Rend une suite de cellules de TAILLE NORMALE dans le tampon de ligne, a
;   partir de run_cells (vtx_cell_t*, 6 octets : ch, charset, fg, bg, flags,
;   size) et de la colonne run_col, au plus run_count cellules. S'arrete a la
;   premiere cellule de taille non normale (double hauteur/largeur), rendue
;   par le C. Retourne le nombre de cellules rendues.
;   Gere : inversion, masquage (g_global_mask), clignotement (g_blink_phase),
;   G0 (font_g0, 6 pixels centres), G1 (cache g1_cache, 8x9 natif, $60 =
;   trait haut), G2 (font_get_g2 en C), souligne (ligne 8).
;
; void __fastcall__ blit_cell9(void)
;   Rend UN motif 8x9 deja construit (blit_pat, 9 octets, bit 7 = pixel de
;   gauche) avec blit_fg / blit_bg a la colonne blit_col. Chemin des doubles
;   tailles et de la ligne de statut.
;
; Ecriture des pixels : pour chaque ligne, remplissage du fond (8 sta
; abs,x) puis pose de l'encre sur les bits a 1 (asl zp / bcc / sta abs,x).
; X = decalage de colonne : col*8 depasse 255 a partir de la colonne 32, d'ou
; DEUX copies du bloc de lignes (base et base+256). ~120 cycles par ligne,
; ~1 200 cycles par cellule tout compris, contre ~3 800 en C.
;=================================================================

        .export   _blit_run, _blit_cell9
        .export   _run_cells, _run_col, _run_count
        .export   _blit_pat, _blit_col, _blit_fg, _blit_bg
        .import   _display_rowbuf, _font_g0, _g1_cache, _font_get_g2
        .import   _g_global_mask, _g_blink_phase

ROWBUF  = _display_rowbuf
STRIDE  = 320
CELLS   = 6                 ; sizeof(vtx_cell_t)

; --- attributs (videotex.h) ---
ATTR_FLASH      = $01
ATTR_CONCEALED  = $02
ATTR_INVERT     = $04
ATTR_UNDERLINE  = $08
ATTR_SEPARATED  = $10

        .zeropage
cellp:  .res 2              ; cellule courante
glyph:  .res 2              ; glyphe / motif source
pat:    .res 9              ; motif 8x9 de la cellule (bit 7 = gauche)
cfg:    .res 1              ; encre
cbg:    .res 1              ; fond
cflags: .res 1
xoff:   .res 1              ; (col*8) & $FF
ccount: .res 1              ; cellules rendues
ccol:   .res 1              ; colonne courante

        .segment "BSS"
_run_cells: .res 2
_run_col:   .res 1
_run_count: .res 1
_blit_pat:  .res 2
_blit_col:  .res 1
_blit_fg:   .res 1
_blit_bg:   .res 1

        .segment "CODE"

;-----------------------------------------------------------------
; Bloc de rendu d'une ligne : A doit valoir le fond a l'entree.
;   base = adresse de la ligne dans le tampon (+ 256 pour la variante B)
;   p    = octet de motif (zeropage)
;-----------------------------------------------------------------
.macro  LINE    base, p
        lda  cbg
        sta  base+0,x
        sta  base+1,x
        sta  base+2,x
        sta  base+3,x
        sta  base+4,x
        sta  base+5,x
        sta  base+6,x
        sta  base+7,x
        lda  p
        beq  :++++++++      ; motif vide : ligne finie
        lda  cfg
        asl  p
        bcc  :+
        sta  base+0,x
:       asl  p
        bcc  :+
        sta  base+1,x
:       asl  p
        bcc  :+
        sta  base+2,x
:       asl  p
        bcc  :+
        sta  base+3,x
:       asl  p
        bcc  :+
        sta  base+4,x
:       asl  p
        bcc  :+
        sta  base+5,x
:       asl  p
        bcc  :+
        sta  base+6,x
:       asl  p
        bcc  :+
        sta  base+7,x
:
.endmacro

; Les 9 lignes, variante A (colonnes 0-31) et B (colonnes 32-39).
draw_a:
        LINE ROWBUF+0*STRIDE, pat+0
        LINE ROWBUF+1*STRIDE, pat+1
        LINE ROWBUF+2*STRIDE, pat+2
        LINE ROWBUF+3*STRIDE, pat+3
        LINE ROWBUF+4*STRIDE, pat+4
        LINE ROWBUF+5*STRIDE, pat+5
        LINE ROWBUF+6*STRIDE, pat+6
        LINE ROWBUF+7*STRIDE, pat+7
        LINE ROWBUF+8*STRIDE, pat+8
        rts
draw_b:
        LINE ROWBUF+0*STRIDE+256, pat+0
        LINE ROWBUF+1*STRIDE+256, pat+1
        LINE ROWBUF+2*STRIDE+256, pat+2
        LINE ROWBUF+3*STRIDE+256, pat+3
        LINE ROWBUF+4*STRIDE+256, pat+4
        LINE ROWBUF+5*STRIDE+256, pat+5
        LINE ROWBUF+6*STRIDE+256, pat+6
        LINE ROWBUF+7*STRIDE+256, pat+7
        LINE ROWBUF+8*STRIDE+256, pat+8
        rts

; Dessine pat[] a la colonne ccol avec cfg/cbg.
draw_cell:
        lda  ccol
        asl
        asl
        asl                 ; col*8, retenue = col >= 32
        tax
        bcs  @b
        jmp  draw_a
@b:     jmp  draw_b

; pat[] := 0
clear_pat:
        lda  #0
        sta  pat+0
        sta  pat+1
        sta  pat+2
        sta  pat+3
        sta  pat+4
        sta  pat+5
        sta  pat+6
        sta  pat+7
        sta  pat+8
        rts

;-----------------------------------------------------------------
; blit_cell9 : motif deja construit
;-----------------------------------------------------------------
_blit_cell9:
        lda  _blit_pat
        sta  glyph
        lda  _blit_pat+1
        sta  glyph+1
        ldy  #8
:       lda  (glyph),y
        sta  pat,y
        dey
        bpl  :-
        lda  _blit_fg
        sta  cfg
        lda  _blit_bg
        sta  cbg
        lda  _blit_col
        sta  ccol
        jmp  draw_cell

;-----------------------------------------------------------------
; blit_run : suite de cellules taille normale
;-----------------------------------------------------------------
_blit_run:
        lda  _run_cells
        sta  cellp
        lda  _run_cells+1
        sta  cellp+1
        lda  _run_col
        sta  ccol
        lda  #0
        sta  ccount
@next:
        lda  ccount
        cmp  _run_count
        bcs  @done0
        ldy  #5
        lda  (cellp),y      ; size
        beq  @cell
@done0: jmp  @done          ; taille non normale : au C
@cell:
        ldy  #4
        lda  (cellp),y      ; flags
        sta  cflags
        ldy  #2
        lda  (cellp),y      ; fg
        sta  cfg
        iny
        lda  (cellp),y      ; bg
        sta  cbg
        lda  cflags
        and  #ATTR_INVERT
        beq  @noinv
        lda  cfg
        ldy  cbg
        sty  cfg
        sta  cbg
@noinv:
        ; masquage / clignotement : motif vide
        lda  cflags
        and  #ATTR_CONCEALED
        beq  @nocon
        lda  _g_global_mask
        bne  @blank
@nocon: lda  cflags
        and  #ATTR_FLASH
        beq  @visible
        lda  _g_blink_phase
        beq  @visible
@blank: jsr  clear_pat
        jmp  @paint
@visible:
        ldy  #1
        lda  (cellp),y      ; charset
        beq  @g0
        cmp  #1
        beq  @g1
        ; G2 : glyphe par la table C (fastcall : A = code)
        lda  (cellp)
        jsr  _font_get_g2
        sta  glyph
        stx  glyph+1
        bra  @glyph6
@g0:    lda  (cellp)        ; ch
        sec
        sbc  #$20
        bcc  @space
        cmp  #$60
        bcs  @space
        ; glyph = font_g0 + (ch-$20)*8
        stz  glyph+1
        asl
        rol  glyph+1
        asl
        rol  glyph+1
        asl
        rol  glyph+1
        clc
        adc  #<_font_g0
        sta  glyph
        lda  glyph+1
        adc  #>_font_g0
        sta  glyph+1
        bra  @glyph6
@space: jsr  clear_pat
        bra  @underline
@glyph6:
        ; 8 lignes de 6 pixels (bits 5-0) centrees : (g << 1) & $7E
        ldy  #7
:       lda  (glyph),y
        asl
        and  #$7E
        sta  pat,y
        dey
        bpl  :-
        stz  pat+8
        bra  @underline
@g1:    lda  (cellp)        ; ch
        cmp  #$60
        bne  @g1c
        ; trait horizontal plein sur la premiere ligne (cas special ROM)
        jsr  clear_pat
        lda  #$FF
        sta  pat+0
        bra  @underline
@g1c:   ; index = (ch & $1F) | ((ch & $40) >> 1), separe = flag $10
        tay
        and  #$1F
        sta  glyph
        tya
        and  #$40
        lsr
        ora  glyph          ; 0..63
        ; glyph = g1_cache + sep*576 + index*9
        sta  glyph
        stz  glyph+1
        asl  glyph
        rol  glyph+1        ; *2
        asl  glyph
        rol  glyph+1        ; *4
        asl  glyph
        rol  glyph+1        ; *8
        clc
        adc  glyph          ; *9
        sta  glyph
        bcc  :+
        inc  glyph+1
:       lda  cflags
        and  #ATTR_SEPARATED
        beq  @g1sum
        clc
        lda  glyph
        adc  #<576
        sta  glyph
        lda  glyph+1
        adc  #>576
        sta  glyph+1
@g1sum: clc
        lda  glyph
        adc  #<_g1_cache
        sta  glyph
        lda  glyph+1
        adc  #>_g1_cache
        sta  glyph+1
        ldy  #8
:       lda  (glyph),y
        sta  pat,y
        dey
        bpl  :-
@underline:
        lda  cflags
        and  #ATTR_UNDERLINE
        beq  @paint
        lda  #$FF
        sta  pat+8
@paint:
        jsr  draw_cell
        ; cellule suivante
        clc
        lda  cellp
        adc  #CELLS
        sta  cellp
        bcc  :+
        inc  cellp+1
:       inc  ccol
        inc  ccount
        jmp  @next
@done:
        lda  ccount
        ldx  #0
        rts
