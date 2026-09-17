/**
 * @file display80.c
 * @brief Rendu de l'ecran 80 colonnes en mode Hercules (voir display80.h)
 */

#include <string.h>
#include "display80.h"
#include "font80.h"
#include "neo_gfx.h"

extern unsigned char g_blink_phase;

static unsigned char s_available;

#ifdef TEST_HOST
/* Glyphe (14 octets) d'une cellule */
static const unsigned char* cell_glyph(const ti_cell_t* c)
{
    unsigned char idx;
    unsigned char ch = c->ch;
    if (c->attr & TI_ATTR_ERROR) return &font80[FONT80_BLOCK * FONT80_H];
    if (ch < 0x20 || ch > 0x7F) ch = 0x20;
    idx = (unsigned char)(ch - 0x20);
    if (c->attr & TI_ATTR_FRENCH) idx = font80_fr_index[idx];
    return &font80[(unsigned int)idx * FONT80_H];
}

/* Depose 9 bits (v, bit 8 = pixel de gauche) a la position bit pos d'une
 * ligne du tampon (OU logique : le tampon est efface avant). */
static void put9(unsigned char* line, unsigned int pos, unsigned int v)
{
    unsigned char b = (unsigned char)(pos >> 3);
    unsigned char s = (unsigned char)(pos & 7);
    unsigned int w = v << (7 - s);              /* 9 + 7 bits max = 16 */
    line[b]     |= (unsigned char)(w >> 8);
    line[b + 1] |= (unsigned char)(w & 0xFF);
}

#endif /* TEST_HOST */

#ifndef TEST_HOST
/* display80_asm.s : composition en assembleur (cible). Le C ci-dessous est
 * l'oracle des tests hote (render_page --mixte) : meme resultat attendu. */
extern const unsigned char* b80_cells;
extern unsigned char        b80_row0;
extern unsigned char        b80_cursor;
extern unsigned char        b80_ncols;
void __fastcall__           blit80_row(void);
#endif

void display80_compose_row(ti_context_t* ctx, unsigned char row)
{
    const ti_cell_t* rowp = &ctx->screen[row][0];
    unsigned char ncols = ctx->cols;
    unsigned char cursor_col = 0xFF;

    /* Curseur : tiret sur la derniere ligne, phase 0 */
    if (ctx->cur_visible && g_blink_phase == 0) {
        if (ctx->r0_active) { if (row == 0) cursor_col = ctx->r0_col; }
        else if (row == ctx->cur_y) cursor_col = ctx->cur_x;
    }

#ifndef TEST_HOST
    b80_cells = (const unsigned char*)rowp;
    b80_row0 = (row == 0) ? 1 : 0;
    b80_cursor = cursor_col;
    b80_ncols = ncols;
    blit80_row();
#else
  {
    unsigned char col, l;
    memset(display_rowbuf, 0, D80_ROWBYTES);
    for (col = 0; col < ncols; ++col) {
        const ti_cell_t* c = &rowp[col];
        const unsigned char* g = cell_glyph(c);
        unsigned char attr = (row == 0) ? 0 : c->attr;
        unsigned char inv = (attr & TI_ATTR_INVERSE) ? 1 : 0;
        unsigned char blank = (attr & TI_ATTR_BLINK) && g_blink_phase;
        unsigned int pos = (unsigned int)col * ((ncols == 40) ? 18 : 9);
        unsigned char* line = display_rowbuf;

        for (l = 0; l < D80_CELL_H; ++l, line += D80_STRIDE) {
            unsigned int v;
            unsigned char gb = blank ? 0 : g[l];
            if (attr & TI_ATTR_BOLD) gb |= (unsigned char)(gb >> 1);
            v = (unsigned int)gb << 1;          /* 9e colonne = fond */
            if ((attr & TI_ATTR_UNDERLINE) && l == D80_CELL_H - 1) v = 0x1FF;
            if (col == cursor_col && l == D80_CELL_H - 1) v ^= 0x1FF;
            if (inv) v ^= 0x1FF;
            if (v) put9(line, pos, v);      /* 40 col. : pas de 18, glyphe non double */
        }
    }
  }
#endif
}

static void render_row(ti_context_t* ctx, unsigned char row)
{
    unsigned int off = (unsigned int)row * D80_ROWBYTES;     /* < 31 500 */
    display80_compose_row(ctx, row);
    gfx_blit_ex(display_rowbuf, D80_STRIDE, (unsigned long)off, D80_STRIDE, D80_STRIDE, D80_CELL_H);
}

static unsigned char s_cursor_row = 0xFF;   /* rangee ou le curseur a ete dessine */

static void render_dirty(ti_context_t* ctx, unsigned char max_rows)
{
    unsigned char row, rendered = 0;
    unsigned char cur_row = ctx->r0_active ? 0 : ctx->cur_y;

    if (ctx->full_refresh) {
        memset(ctx->dirty, 1, TI_ROWS);
        ctx->full_refresh = 0;
    }
    /* Le curseur a bouge de rangee : re-rendre l'ancienne et la nouvelle */
    if (s_cursor_row != cur_row) {
        if (s_cursor_row < TI_ROWS) ctx->dirty[s_cursor_row] = 1;
        ctx->dirty[cur_row] = 1;
        s_cursor_row = cur_row;
    }
    for (row = 0; row < TI_ROWS && rendered < max_rows; ++row) {
        if (!ctx->dirty[row]) continue;
        render_row(ctx, row);
        ctx->dirty[row] = 0;
        ++rendered;
    }
}

void display80_render(ti_context_t* ctx)     { render_dirty(ctx, 1); }
void display80_render_all(ti_context_t* ctx) { render_dirty(ctx, TI_ROWS); }

unsigned char display80_dirty_pending(ti_context_t* ctx)
{
    unsigned char row;
    if (ctx->full_refresh) return 1;
    for (row = 0; row < TI_ROWS; ++row) if (ctx->dirty[row]) return 1;
    return 0;
}

void display80_blink_toggle(ti_context_t* ctx)
{
    unsigned char row, col;
    g_blink_phase ^= 1;
    ctx->blink_phase = g_blink_phase;
    ctx->dirty[ctx->r0_active ? 0 : ctx->cur_y] = 1;      /* tiret du curseur */
    for (row = 1; row < TI_ROWS; ++row) {
        const ti_cell_t* c = &ctx->screen[row][0];
        for (col = 0; col < TI_COLS; ++col, ++c) {
            if (c->attr & TI_ATTR_BLINK) { ctx->dirty[row] = 1; break; }
        }
    }
}

void display80_status(ti_context_t* ctx, const char* msg)
{
    /* Composer la rangee 00 a partir du message, sans toucher aux cellules */
    ti_cell_t save[TI_COLS];
    unsigned char c;
    memcpy(save, &ctx->screen[0][0], sizeof save);
    for (c = 0; c < TI_COLS; ++c) {
        ctx->screen[0][c].ch = (*msg) ? (unsigned char)*msg++ : ' ';
        ctx->screen[0][c].attr = 0;
    }
    render_row(ctx, 0);
    memcpy(&ctx->screen[0][0], save, sizeof save);
}

void display80_status_clear(ti_context_t* ctx)
{
    render_row(ctx, 0);
}

unsigned char display80_init(void)
{
    s_available = gfx_set_mode(1);
    if (!s_available) return 0;
    s_cursor_row = 0xFF;
    gfx_init();                     /* efface, cache le curseur console */
    return 1;
}

void display80_leave(void)
{
    gfx_set_mode(0);
}
