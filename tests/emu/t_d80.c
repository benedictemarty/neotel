/* t_d80.c - programme cible : display80 avec du texte sur les rangees 1, 13, 14, 15, 24 */
#include "teleinfo.h"
#include "display80.h"
#include "terminal.h"
unsigned char g_blink_phase; unsigned char g_global_mask = 1;
void display_clear(void) {} void display_beep(void) {}
unsigned char display_rowbuf[2880];
static ti_context_t ti;
static void put(unsigned char r, const char* s) { unsigned char c = 0; while (*s) { ti.screen[r][c].ch = (unsigned char)*s++; ti.screen[r][c].attr = 0; ++c; } }
int main(void)
{
    display80_init();
    ti_init(&ti);
    put(1, "RANGEE 1"); put(13, "RANGEE 13"); put(14, "RANGEE 14"); put(15, "RANGEE 15"); put(24, "RANGEE 24");
    display80_render_all(&ti);
    for (;;) ;
}
