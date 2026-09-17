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
;   G0 (font_g0, 6 pixels centres), G1 (mosaiques calculees, 8x9, $60 =
;   trait haut), G2 (font_get_g2 en C), DRCS G'0/G'1 (drcs_pattern9 en C),
;   souligne (ligne 8).
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

        .export   _blit_run, _blit_cell9, _rb_state, _scan_dblh
        .export   _run_cells, _run_col, _run_count
        .export   _blit_pat, _blit_col, _blit_fg, _blit_bg
        .import   _display_rowbuf, _font_g0, _font_get_g2
        .import   _g_global_mask, _g_blink_phase
        .import   _drcs_pattern9, _drcs_pattern_set

ROWBUF  = _display_rowbuf
STRIDE  = 160               ; demi-rangee (HALF_W, display.h) : la colonne de
                            ; tampon est ccol mod 20, X = bufcol*8 < 160
CELLS   = 5                 ; sizeof(vtx_cell_t) : size loge dans flags (bits 5-6)

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
_rb_state:  .res 20         ; par colonne de TAMPON : 0 = quelconque, sinon fond+1,
                            ; bit 4 = moitie droite (colonnes 20-39)

        .segment "RODATA"
; Ligne de pixels d'une rangee de mosaique selon ses deux blocs (bit 0 =
; gauche, bit 1 = droit) : contigu (4 + 4 pixels) puis separe (3 + 3, colonne
; de droite de chaque bloc vide) — meme table que mosaic_pattern (display.c).
g1_tbl: .byte $00, $F0, $0F, $FF
        .byte $00, $E0, $0E, $EE

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

; Les 9 lignes : une seule variante, bufcol*8 <= 152 (demi-rangee, v0.9.0 ;
; auparavant deux copies pour les colonnes 0-31 et 32-39).
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

; X := colonne de tampon (ccol mod 20) ; A := marque de moitie ($10 si ccol >= 20)
bufcol_x:
        lda  ccol
        cmp  #20
        bcc  @left
        sbc  #20            ; retenue deja a 1
        tax
        lda  #$10
        rts
@left:  tax
        lda  #0
        rts

; Dessine pat[] a la colonne ccol avec cfg/cbg.
draw_cell:
        jsr  bufcol_x
        txa
        asl
        asl
        asl                 ; bufcol*8 (< 160)
        tax
        jmp  draw_a

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
        jsr  bufcol_x
        stz  _rb_state,x    ; le tampon ne contient plus une cellule vide
        jmp  draw_cell

;-----------------------------------------------------------------
; scan_dblh : index (A) de la premiere cellule double hauteur (size & 1)
; parmi run_count cellules a partir de run_cells, $FF si aucune. Remplace
; une boucle C de 7 800 cycles par rangee (v0.6.1).
;-----------------------------------------------------------------
_scan_dblh:
        lda  _run_cells
        sta  cellp
        lda  _run_cells+1
        sta  cellp+1
        ldx  #0
        ldy  #4
@loop:  cpx  _run_count
        bcs  @none
        lda  (cellp),y      ; flags (bits 5-6 = taille)
        and  #$20           ; bit 5 = double hauteur
        bne  @found
        clc
        lda  cellp
        adc  #CELLS
        sta  cellp
        bcc  :+
        inc  cellp+1
:       inx
        bra  @loop
@none:  ldx  #$FF
@found: txa
        ldx  #0
        rts

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
        ldy  #4
        lda  (cellp),y      ; flags (bits 5-6 = taille)
        and  #$60
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
        cmp  #2
        beq  @g2
        ; DRCS G'0 (3) / G'1 (4) : motif 8x9 par le C (fastcall : A = code)
        sec
        sbc  #3
        sta  _drcs_pattern_set
        lda  (cellp)
        jsr  _drcs_pattern9
        sta  glyph
        stx  glyph+1
        ldy  #8
:       lda  (glyph),y
        sta  pat,y
        dey
        bpl  :-
        lda  _drcs_pattern_set
        bne  :+
        jmp  @underline     ; G'0 : lignage comme G0
:       jmp  @paint         ; G'1 : le lignage n'a pas d'effet visuel
@g2:    ; G2 : glyphe par la table C (fastcall : A = code)
        lda  (cellp)
        jsr  _font_get_g2
        sta  glyph
        stx  glyph+1
        bra  @glyph6
@g0:    lda  (cellp)        ; ch
        sec
        sbc  #$20
        bcc  @space
        beq  @space         ; espace : motif nul sans lire le glyphe
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
@g1c:   ; index = (ch & $1F) | ((ch & $40) >> 1) : bits (2k, 2k+1) = blocs
        ; gauche / droit de la rangee k ; separe = flag $10 (v0.5.1 : calcule
        ; a la volee, plus de cache de 1 152 octets)
        tay
        and  #$1F
        sta  glyph
        tya
        and  #$40
        lsr
        ora  glyph          ; 0..63
        sta  glyph
        ldx  #0             ; table contigue
        lda  cflags
        and  #ATTR_SEPARATED
        beq  :+
        ldx  #4             ; table separee
:       stx  glyph+1
        ldy  #0
@g1k:   lda  glyph
        and  #3
        clc
        adc  glyph+1
        tax
        lda  g1_tbl,x       ; ligne des deux blocs
        sta  pat,y
        iny
        sta  pat,y
        iny
        ldx  glyph+1
        beq  :+
        lda  #0             ; separe : 3e ligne vide
:       sta  pat,y
        iny
        lsr  glyph
        lsr  glyph
        cpy  #9
        bne  @g1k
@underline:
        lda  cflags
        and  #ATTR_UNDERLINE
        beq  @paint
        lda  #$FF
        sta  pat+8
@paint:
        ; Cellule vide (motif nul) : si le tampon contient deja, a cette
        ; colonne, une cellule vide du meme fond (rb_state = fond + 1), ne
        ; rien redessiner (72 ecritures economisees ; v0.6.1). Le C remet
        ; rb_state a 0 quand il ecrit lui-meme dans le tampon.
        lda  pat+0
        ora  pat+1
        ora  pat+2
        ora  pat+3
        ora  pat+4
        ora  pat+5
        ora  pat+6
        ora  pat+7
        ora  pat+8
        bne  @paint_nb
        jsr  bufcol_x       ; X = colonne de tampon, A = marque de moitie
        ora  cbg
        inc  a              ; fond + 1 (+ $10 a droite)
        cmp  _rb_state,x
        beq  @advance
        sta  _rb_state,x
        bra  @paint_do
@paint_nb:
        jsr  bufcol_x
        stz  _rb_state,x
@paint_do:
        jsr  draw_cell
@advance:
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
