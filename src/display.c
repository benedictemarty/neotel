/**
 * @file display.c
 * @brief Moteur d'affichage de NeoTel (voir display.h)
 *
 * Herite de display.c d'OricTel la logique de rendu budgete (lignes sales,
 * plages de colonnes, curseur, doubles tailles dessinees sur deux lignes),
 * mais la rasterisation est refaite pour un framebuffer a 1 octet par pixel :
 * plus d'attributs serie ni de dithering, chaque cellule est expansee en
 * pixels encre/fond dans un tampon de ligne, puis la ligne part en VRAM par
 * le blitter du RP2040.
 */

#include <string.h>
#include "display.h"
#include "settings.h"
#include "fonts.h"
#include "neo_gfx.h"
#include "neo_time.h"

/* Phase de clignotement et masque global (main.c / videotex.c) */
extern unsigned char g_blink_phase;
extern unsigned char g_global_mask;

/* Tampon de demi-rangee : CELL_H lignes video de HALF_W pixels (display.h) */
unsigned char display_rowbuf[CELL_H * HALF_W];

/* Colonne de tampon d'une colonne ecran */
#define BUFCOL(col) ((unsigned char)((col) >= HALF_COLS ? (col) - HALF_COLS : (col)))

/* Aspect courant */
static unsigned char s_look = DISPLAY_LOOK_COLOR;

/* ===================================================================
 *  Expansion d'une cellule : motif 8x9 -> pixels encre/fond
 *  (display_asm.s : blit_cell9 ; version C pour les tests hote)
 * =================================================================== */
/* Interface avec display_asm.s (versions C sur l'hote, plus bas) :
 *  - blit_run : rend jusqu'a run_count cellules de taille normale depuis
 *    run_cells a la colonne run_col ; s'arrete sur une double taille et
 *    retourne le nombre de cellules rendues ;
 *  - blit_cell9 : rend un motif 8x9 deja construit (blit_pat) a blit_col. */
#ifndef TEST_HOST
extern const unsigned char* run_cells;
extern unsigned char        run_col;
extern unsigned char        run_count;
unsigned char __fastcall__  blit_run(void);
unsigned char __fastcall__  scan_dblh(void);
extern const unsigned char* blit_pat;
extern unsigned char        blit_col;
extern unsigned char        blit_fg;
extern unsigned char        blit_bg;
extern unsigned char        rb_state[HALF_COLS];  /* etat du tampon par colonne de tampon (asm) */
#define rb_invalidate(col)  (rb_state[BUFCOL(col)] = 0)
#define rb_reset()          memset(rb_state, 0, HALF_COLS)
void __fastcall__           blit_cell9(void);
#else
static const unsigned char* run_cells;
static unsigned char        run_col;
static unsigned char        run_count;
static unsigned char        blit_run(void);
static unsigned char        scan_dblh(void);
static const unsigned char* blit_pat;
static unsigned char        blit_col;
static unsigned char        blit_fg;
static unsigned char        blit_bg;
#define rb_invalidate(col)  ((void)0)     /* pas de cache sur l'hote : l'oracle reste le rendu direct */
#define rb_reset()          ((void)0)
static void blit_cell9(void)
{
    unsigned char l, b, m;
    unsigned char* d = display_rowbuf + BUFCOL(blit_col) * CELL_W;
    for (l = 0; l < CELL_H; ++l) {
        b = blit_pat[l];
        for (m = 0x80; m; m >>= 1) {
            *d++ = (b & m) ? blit_fg : blit_bg;
        }
        d += HALF_W - CELL_W;
    }
}
#endif

/* ===================================================================
 *  Palette
 * =================================================================== */

/* Couleurs Videotex : noir, rouge, vert, jaune, bleu, magenta, cyan, blanc */
static const unsigned char pal_color[8][3] = {
    { 0, 0, 0 }, { 255, 0, 0 }, { 0, 255, 0 }, { 255, 255, 0 },
    { 0, 0, 255 }, { 255, 0, 255 }, { 0, 255, 255 }, { 255, 255, 255 },
};

/* Minitel 1B monochrome : les 8 couleurs deviennent 8 niveaux de gris
 * ordonnes par luminance (Rec. 601 : 0,299 R + 0,587 V + 0,114 B), soit
 * noir 0 < bleu 29 < rouge 76 < magenta 105 < vert 150 < cyan 179 <
 * jaune 226 < blanc 255. Meme hierarchie que le dithering d'OricTel. */
static const unsigned char pal_grey[8] = { 0, 76, 150, 226, 29, 105, 179, 255 };

void display_set_look(unsigned char look)
{
    unsigned char i;
    s_look = look;
    for (i = 0; i < 8; ++i) {
        if (look == DISPLAY_LOOK_GREY) {
            gfx_set_palette(i, pal_grey[i], pal_grey[i], pal_grey[i]);
        } else {
            gfx_set_palette(i, pal_color[i][0], pal_color[i][1], pal_color[i][2]);
        }
    }
}

unsigned char display_get_look(void)
{
    return s_look;
}

/* ===================================================================
 *  Motifs de cellule
 * =================================================================== */

/* Mosaique G1 en 8x9 : trois rangees de 3 lignes, blocs de 4 pixels.
 * Encodage Minitel (cf. display.c d'OricTel, telenet emulateur.js) :
 *   bit 0 = haut-gauche, 1 = haut-droit, 2 = milieu-gauche,
 *   3 = milieu-droit, 4 = bas-gauche, 6 = bas-droit (PAS le bit 5).
 * Cas special $60 (ROM EF9345, capture Minitel reel) : trait horizontal
 * plein sur la premiere ligne. Separe : blocs de 3x2 avec interstice a
 * droite et en bas. */
static void mosaic_pattern(unsigned char code, unsigned char separated,
                           unsigned char pat[CELL_H])
{
    unsigned char pattern, k, row, left, right, line;

    if (code == 0x60) {
        pat[0] = 0xFF;
        for (k = 1; k < CELL_H; ++k) pat[k] = 0;
        return;
    }
    pattern = code & 0x1F;
    if (code & 0x40) pattern |= 0x20;
    left  = separated ? 0xE0 : 0xF0;
    right = separated ? 0x0E : 0x0F;
    for (k = 0; k < 3; ++k) {
        line = 0;
        if (pattern & (1 << (2 * k)))     line |= left;
        if (pattern & (2 << (2 * k)))     line |= right;
        row = k * 3;
        pat[row]     = line;
        pat[row + 1] = line;
        pat[row + 2] = separated ? 0 : line;
    }
}

/* Motif G1 calcule a la volee (le cache de 1 152 octets de BSS a ete
 * supprime en v0.5.1 : la RAM manquait ; display_asm.s fait le meme calcul
 * avec la table g1_tbl). */
static unsigned char s_g1_pat[CELL_H];

static const unsigned char* g1_pattern(unsigned char ch, unsigned char separated)
{
    mosaic_pattern(ch, separated, s_g1_pat);
    return s_g1_pat;
}

/* Motif 8x9 d'une forme DRCS (Minitel 2). La forme fait 8x10 (STUM 2
 * par. 2.3.1) et la cellule de NeoTel 8x9 : les rangees 9 et 10 sont
 * fusionnees (OU) sur la ligne 8, pour ne perdre aucun pixel d'encre. Set
 * dans drcs_pattern_set (0 = G'0, 1 = G'1) ; appele aussi par blit_run. */
unsigned char drcs_pattern_set;
static unsigned char s_drcs_pat[CELL_H];

const unsigned char* __fastcall__ drcs_pattern9(unsigned char ch)
{
    const unsigned char* form = vtx_current ? vtx_drcs_form(vtx_current, drcs_pattern_set, ch) : 0;
    if (form) {
        memcpy(s_drcs_pat, form, 8);
        s_drcs_pat[8] = (unsigned char)(form[8] | form[9]);
    } else {
        memset(s_drcs_pat, 0, CELL_H);
    }
    return s_drcs_pat;
}

void display_cell_pattern(const vtx_cell_t* cell, unsigned char pat[CELL_H],
                          unsigned char* fg, unsigned char* bg)
{
    unsigned char ch = cell->ch;
    unsigned char l;
    const unsigned char* glyph;

    if (cell->flags & ATTR_INVERT) {
        *fg = cell_bg(cell);
        *bg = cell_fg(cell);
    } else {
        *fg = cell_fg(cell);
        *bg = cell_bg(cell);
    }

    /* Masque (concealed + masque global) et phase eteinte du clignotement :
     * cellule vide, le fond (eventuellement inverse) subsiste. */
    if (((cell->flags & ATTR_CONCEALED) && g_global_mask) ||
        ((cell->flags & ATTR_FLASH) && g_blink_phase)) {
        for (l = 0; l < CELL_H; ++l) pat[l] = 0;
        return;
    }

    if (cell->charset == CHARSET_G1) {
        memcpy(pat, g1_pattern(ch, (cell->flags & ATTR_SEPARATED) ? 1 : 0), CELL_H);
    } else if (cell->charset == CHARSET_DRCS0 || cell->charset == CHARSET_DRCS1) {
        drcs_pattern_set = (unsigned char)(cell->charset - CHARSET_DRCS0);
        memcpy(pat, drcs_pattern9(ch), CELL_H);
        if (cell->charset == CHARSET_DRCS1) return;   /* pas de lignage en G'1 */
    } else {
        glyph = (cell->charset == CHARSET_G2) ? font_get_g2(ch) : font_get_g0(ch);
        /* 6 pixels utiles (bits 5-0) centres : colonnes 1-6 */
        for (l = 0; l < 8; ++l) pat[l] = (unsigned char)((glyph[l] << 1) & 0x7E);
        pat[8] = 0;
    }
    if (cell->flags & ATTR_UNDERLINE) {
        pat[8] = 0xFF;
    }
}

/* Double largeur : chaque bit d'un quartet est double (abcd -> aabbccdd) */
static const unsigned char dw_expand[16] = {
    0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
    0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF,
};

/* Double hauteur : ligne source de chaque ligne de destination.
 * Moitie haute (ligne au-dessus) : 0,0,1,1,2,2,3,3,4
 * Moitie basse (ligne courante)  : 4,5,5,6,6,7,7,8,8 */
static const unsigned char dh_upper[CELL_H] = { 0, 0, 1, 1, 2, 2, 3, 3, 4 };
static const unsigned char dh_lower[CELL_H] = { 4, 5, 5, 6, 6, 7, 7, 8, 8 };

/* Parties verticales d'une cellule */
#define VPART_FULL   0      /* taille normale : 9 lignes du motif */
#define VPART_LOWER  1      /* double hauteur, moitie basse (ligne propre) */
#define VPART_UPPER  2      /* double hauteur, moitie haute (ligne du dessus) */

static unsigned char s_pat[CELL_H];
static unsigned char s_tmp[CELL_H];
static unsigned char s_right[CELL_H];
static unsigned char s_base;        /* premiere colonne de la demi-rangee en cours */

/* La colonne col tombe-t-elle dans la demi-rangee en cours ? */
#define in_half(col) ((unsigned char)((col) - s_base) < HALF_COLS)

/* Dessine une cellule dans le tampon de ligne, a la colonne col. */
static void render_cell_into_row(const vtx_cell_t* cell, unsigned char col,
                                 unsigned char vpart)
{
    unsigned char l;
    unsigned char fg, bg;

    display_cell_pattern(cell, s_pat, &fg, &bg);

    if (vpart != VPART_FULL) {
        const unsigned char* map = (vpart == VPART_UPPER) ? dh_upper : dh_lower;
        for (l = 0; l < CELL_H; ++l) s_tmp[l] = s_pat[map[l]];
        memcpy(s_pat, s_tmp, CELL_H);
    }

    blit_fg = fg;
    blit_bg = bg;
    blit_col = col;
    if (cell_size(cell) == SIZE_DOUBLE_WIDTH || cell_size(cell) == SIZE_DOUBLE_SIZE) {
        for (l = 0; l < CELL_H; ++l) {
            s_right[l] = dw_expand[s_pat[l] & 0x0F];
            s_tmp[l]   = dw_expand[s_pat[l] >> 4];
        }
        /* Chaque moitie n'est dessinee que si elle tombe dans la demi-rangee
         * en cours : une double largeur en colonne 19 est rendue deux fois,
         * moitie gauche par la premiere, moitie droite par la seconde. */
        if (in_half(col)) {
            blit_pat = s_tmp;
            blit_cell9();
        }
        if (col + 1 < SCREEN_COLS && in_half(col + 1)) {
            blit_pat = s_right;
            blit_col = col + 1;
            blit_cell9();
        }
    } else if (in_half(col)) {
        blit_pat = s_pat;
        blit_cell9();
    }
}

#ifdef TEST_HOST
/* Version hote de blit_run : meme contrat que l'assembleur. */
static unsigned char blit_run(void)
{
    const vtx_cell_t* c = (const vtx_cell_t*)run_cells;
    unsigned char n = 0;
    while (n < run_count && cell_size(c) == SIZE_NORMAL) {
        render_cell_into_row(c, (unsigned char)(run_col + n), VPART_FULL);
        ++c;
        ++n;
    }
    return n;
}
static unsigned char scan_dblh(void)
{
    const vtx_cell_t* c = (const vtx_cell_t*)run_cells;
    unsigned char n;
    for (n = 0; n < run_count; ++n, ++c)
        if (cell_size(c) & SIZE_DOUBLE_HEIGHT) return n;
    return 0xFF;
}
#endif

/* ===================================================================
 *  Rendu d'une ligne (plage de colonnes) puis blit
 * =================================================================== */

/* Derniere position ou la barre curseur a ete dessinee */
static unsigned char cur_drawn;
static unsigned char cur_drawn_x;
static unsigned char cur_drawn_y;
static unsigned char s_want_cursor;

#define is_dbl_w(c) (cell_size(c) == SIZE_DOUBLE_WIDTH || cell_size(c) == SIZE_DOUBLE_SIZE)
#define is_dbl_h(c) (cell_size(c) == SIZE_DOUBLE_HEIGHT || cell_size(c) == SIZE_DOUBLE_SIZE)

/* Rend et blitte les colonnes c0..c1 d'une rangee, toutes dans la MEME
 * moitie d'ecran (tampon de demi-rangee). Une double largeur qui chevauche
 * la frontiere (colonne 19) est visitee par les deux moities et
 * render_cell_into_row ne dessine que la moitie de glyphe qui tombe dans la
 * demi-rangee en cours (s_base). */
static void render_half(vtx_context_t* ctx, unsigned char row,
                        unsigned char c0, unsigned char c1)
{
    const vtx_cell_t* rowp = &ctx->screen[row][0];   /* pointeur hisse */
    unsigned char c;
    unsigned char base = (c0 >= HALF_COLS) ? HALF_COLS : 0;
    unsigned char c1max = (unsigned char)(base + HALF_COLS - 1);

    s_base = base;

    if (c1 > c1max) c1 = c1max;
    if (c0 > c1) c0 = c1;

    /* Une double largeur juste avant la plage couvre sa premiere colonne */
    if (c0 > 0 && is_dbl_w(&rowp[c0 - 1])) --c0;

    c = c0;
    while (c <= c1) {
        const vtx_cell_t* cell;
        unsigned char n;
        /* Course de cellules taille normale en assembleur */
        run_cells = (const unsigned char*)&rowp[c];
        run_col = c;
        run_count = (unsigned char)(c1 - c + 1);
        n = blit_run();
        c += n;
        if (c > c1) break;
        /* Cellule double : chemin C */
        cell = &rowp[c];
        render_cell_into_row(cell, c, is_dbl_h(cell) ? VPART_LOWER : VPART_FULL);
        if (is_dbl_w(cell)) {
            if (c == c1 && c1 < c1max) ++c1;   /* moitie droite */
            c += 2;
        } else {
            ++c;
        }
    }

    /* Moities hautes des doubles hauteurs de la ligne du dessous. Le
     * pointeur avance de cellule en cellule (pas d'indexation *6 a chaque
     * tour : c'etait 18 000 cycles par rangee, v0.6.1) ; bit 0 de size =
     * double hauteur (1) ou double taille (3). */
    if (row + 1 < SCREEN_ROWS) {
        const vtx_cell_t* below = &ctx->screen[row + 1][c0];
        c = c0;
        for (;;) {
            unsigned char n;
            run_cells = (const unsigned char*)below;
            run_count = (unsigned char)(c1 - c + 1);
            n = scan_dblh();            /* asm : premiere double hauteur */
            if (n == 0xFF) break;
            c += n; below += n;
            render_cell_into_row(below, c, VPART_UPPER);
            if (cell_size(below) & SIZE_DOUBLE_WIDTH) {
                if (c == c1 && c1 < c1max) ++c1;
                ++c; ++below;
            }
            ++c; ++below;
            if (c > c1) break;
        }
    }

    /* Curseur : barre encre sur la derniere ligne de la cellule */
    if (s_want_cursor && row == ctx->cur_y &&
        ctx->cur_x >= c0 && ctx->cur_x <= c1) {
        memset(display_rowbuf + (CELL_H - 1) * HALF_W + BUFCOL(ctx->cur_x) * CELL_W,
               VTX_WHITE, CELL_W);
        rb_invalidate(ctx->cur_x);
        cur_drawn = 1;
        cur_drawn_x = ctx->cur_x;
        cur_drawn_y = row;
    }

    if (c0 < base) c0 = base;               /* la double largeur d'avant : autre moitie */
    gfx_blit(display_rowbuf + (c0 - base) * CELL_W, HALF_W,
             (unsigned int)c0 * CELL_W, (unsigned char)(row * CELL_H),
             (unsigned int)(c1 - c0 + 1) * CELL_W, CELL_H);
}

/* Rangee row, colonnes c0..c1 : une ou deux demi-rangees */
static void render_row_span(vtx_context_t* ctx, unsigned char row,
                            unsigned char c0, unsigned char c1)
{
    if (c1 >= SCREEN_COLS) c1 = SCREEN_COLS - 1;
    if (c0 > c1) c0 = c1;
    /* Double largeur en colonne 19 : sa moitie droite est en colonne 20 */
    if (c1 == HALF_COLS - 1 && is_dbl_w(&ctx->screen[row][c1])) ++c1;
    if (c0 < HALF_COLS) render_half(ctx, row, c0, c1);
    if (c1 >= HALF_COLS) render_half(ctx, row, (c0 < HALF_COLS) ? HALF_COLS : c0, c1);
}

/* Rendu des lignes sales, au plus max_rows par appel (logique OricTel). */
static void render_dirty(vtx_context_t* ctx, unsigned char max_rows)
{
    unsigned char row;
    unsigned char rendered;

    if (ctx->full_refresh) {
        for (row = 0; row < SCREEN_ROWS; ++row) {
            ctx->dirty[row] = 1;
            ctx->dirty_min[row] = 0;
            ctx->dirty_max[row] = SCREEN_COLS - 1;
        }
        ctx->full_refresh = 0;
    }

    s_want_cursor = (ctx->cur_visible && g_blink_phase == 0 &&
                     ctx->cur_y < SCREEN_ROWS && ctx->cur_x < SCREEN_COLS)
                    ? 1 : 0;

    /* Effacer la barre precedente si le curseur a bouge ou si la phase
     * blink la cache : re-rendre sa cellule. */
    if (cur_drawn && (!s_want_cursor ||
                      cur_drawn_x != ctx->cur_x ||
                      cur_drawn_y != ctx->cur_y)) {
        vtx_touch(ctx, cur_drawn_y, cur_drawn_x, cur_drawn_x);
        cur_drawn = 0;
    }
    /* Curseur voulu mais pas encore dessine a sa position : salir sa cellule */
    if (s_want_cursor && !cur_drawn) {
        vtx_touch(ctx, ctx->cur_y, ctx->cur_x, ctx->cur_x);
    }

    /* Deux passes : toutes les moities gauches des rangees retenues, puis
     * toutes les moities droites. Le cache de cellules vides du tampon
     * (rb_state, asm) est par colonne de tampon : alterner les moities a
     * chaque rangee le viderait sans cesse (page vide : 441 k -> 1,2 M
     * cycles mesures). dirty = 2 marque une rangee retenue pour la passe 2. */
    rendered = 0;
    for (row = 0; row < SCREEN_ROWS && rendered < max_rows; ++row) {
        unsigned char c0, c1;
        if (!ctx->dirty[row]) continue;
        c0 = ctx->dirty_min[row];
        c1 = ctx->dirty_max[row];
        if (c1 >= SCREEN_COLS) c1 = SCREEN_COLS - 1;
        if (c0 > c1) c0 = c1;
        /* Double largeur en colonne 19 : sa moitie droite est en colonne 20 */
        if (c1 == HALF_COLS - 1 && is_dbl_w(&ctx->screen[row][c1])) ++c1;
        ctx->dirty_min[row] = c0;
        ctx->dirty_max[row] = c1;
        if (c0 < HALF_COLS) render_half(ctx, row, c0, c1);
        ctx->dirty[row] = 2;
        ++rendered;
    }
    for (row = 0; row < SCREEN_ROWS; ++row) {
        unsigned char c0, c1;
        if (ctx->dirty[row] != 2) continue;
        c0 = ctx->dirty_min[row];
        c1 = ctx->dirty_max[row];
        if (c1 >= HALF_COLS) render_half(ctx, row, (c0 < HALF_COLS) ? HALF_COLS : c0, c1);
        ctx->dirty[row] = 0;
        ctx->dirty_min[row] = 0;
        ctx->dirty_max[row] = SCREEN_COLS - 1;
    }
}

/* ===================================================================
 *  API publique
 * =================================================================== */

void display_render(vtx_context_t* ctx)
{
    render_dirty(ctx, 1);
}

void display_render_all(vtx_context_t* ctx)
{
    render_dirty(ctx, SCREEN_ROWS);
}

unsigned char display_dirty_pending(vtx_context_t* ctx)
{
    unsigned char row;
    if (ctx->full_refresh) return 1;
    for (row = 0; row < SCREEN_ROWS; ++row) {
        if (ctx->dirty[row]) return 1;
    }
    return 0;
}

void display_render_cell_row(vtx_context_t* ctx, unsigned char row)
{
    if (row >= SCREEN_ROWS) return;
    render_row_span(ctx, row, 0, SCREEN_COLS - 1);
}

void display_blink_toggle(vtx_context_t* ctx)
{
    unsigned char row, col;

    g_blink_phase ^= 1;
    ctx->blink_phase = g_blink_phase;
    for (row = 0; row < SCREEN_ROWS; ++row) {
        const vtx_cell_t* c = &ctx->screen[row][0];    /* pointeur hisse */
        for (col = 0; col < SCREEN_COLS; ++col, ++c) {
            if (c->flags & ATTR_FLASH) {
                vtx_touch(ctx, row, 0, SCREEN_COLS - 1);
                break;
            }
        }
    }
}

void display_clear(void)
{
    gfx_fill_rect(0, 0, SCREEN_W - 1, PAGE_H - 1, VTX_BLACK);
    cur_drawn = 0;
}

/* ===================================================================
 *  Ligne de statut
 * =================================================================== */

static vtx_cell_t status_cells[STATUS_COLS];

static void status_render(void)
{
    unsigned char half;
    for (half = 0; half < 2; ++half) {
        run_cells = (const unsigned char*)&status_cells[half * HALF_COLS];
        run_col = (unsigned char)(half * HALF_COLS);
        s_base = run_col;
        run_count = HALF_COLS;
        blit_run();
        gfx_blit(display_rowbuf, HALF_W, (unsigned int)run_col * CELL_W,
                 STATUS_Y, HALF_W, CELL_H);
    }
}

void display_status_clear(void)
{
    unsigned char c;
    for (c = 0; c < STATUS_COLS; ++c) {
        status_cells[c].ch = ' ';
        status_cells[c].charset = CHARSET_G0;
        cell_set_colors(&status_cells[c], VTX_WHITE, VTX_BLACK);
        status_cells[c].flags = 0;
    }
}

void display_status_text(unsigned char col, const char* s,
                         unsigned char ink, unsigned char inverse)
{
    for (; *s && col < STATUS_COLS; ++s, ++col) {
        status_cells[col].ch = (unsigned char)*s;
        status_cells[col].charset = CHARSET_G0;
        if (inverse) cell_set_colors(&status_cells[col], VTX_BLACK, ink);
        else         cell_set_colors(&status_cells[col], ink, VTX_BLACK);
        status_cells[col].flags = 0;
    }
}

void display_status_show(void)
{
    status_render();
}

void display_status(const char* msg)
{
    unsigned char c;
    for (c = 0; c < STATUS_COLS; ++c) {
        status_cells[c].ch = ' ';
        cell_set_colors(&status_cells[c], VTX_WHITE, VTX_BLACK);
        status_cells[c].charset = CHARSET_G0;
        status_cells[c].flags = 0;
    }
    display_status_text(0, msg, VTX_YELLOW, 0);
    status_render();
}

void display_beep(void)
{
    if (!g_settings.sound) return;      /* menu 7 : son coupe */
    neo_beep();
}

void display_init(void)
{
    gfx_init();
    display_set_look(s_look);
    gfx_fill_rect(0, 0, SCREEN_W - 1, SCREEN_H - 1, VTX_BLACK);
    rb_reset();                 /* le tampon de ligne a pu servir au mode 1 */
    cur_drawn = 0;
    display_status_clear();
    display_status_show();
}
