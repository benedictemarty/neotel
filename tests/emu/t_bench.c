/* t_bench.c - banc de mesure du rendu Videotex sur cible (Phosphoneo).
 *
 * Rend plusieurs pages completes avec display_render_all() et encadre chaque
 * rendu de deux appels 5,37 (compteur de trames) : `make bench` lit les
 * cycles entre les deux dans le journal API (--api-log ...:5) et affiche le
 * cout par page. Pages : vide, texte dense, mosaiques, page de test
 * (tests/page_test.vdt integree par tools/vdt2c.py). Auteur : bmarty.
 */
#include <string.h>
#include "neo.h"
#include "neo_gfx.h"
#include "videotex.h"
#include "display.h"
#include "terminal.h"
#include "page_test_data.h"

unsigned char g_blink_phase;
unsigned char g_global_mask = 1;

static vtx_context_t ctx;

static void mark(void)
{
    neo_wait();
    neo_call(NEO_G_GRAPHICS, NEO_F_FRAME_COUNT);
}

static void bench(void)
{
    ctx.full_refresh = 1;
    mark();
    display_render_all(&ctx);
    mark();
}

static void fill(unsigned char ch, unsigned char charset)
{
    unsigned char r, c;
    for (r = 1; r < VTX_ROWS; ++r)
        for (c = 0; c < VTX_COLS; ++c) {
            ctx.screen[r][c].ch = ch;
            ctx.screen[r][c].charset = charset;
            cell_set_fg(&ctx.screen[r][c], 7);
        }
}

int main(void)
{
    unsigned int i;
    vtx_init(&ctx);
    display_init();
    bench();                                    /* 1 : page vide */
    fill('A', CHARSET_G0);
    bench();                                    /* 2 : 960 lettres */
    fill(0x2A, CHARSET_G1);
    bench();                                    /* 3 : 960 mosaiques */
    vtx_init(&ctx);
    for (i = 0; i < PAGE_TEST_DATA_LEN; ++i) vtx_process(&ctx, page_test_data[i]);
    bench();                                    /* 4 : page de test */
    for (;;) ;
}
