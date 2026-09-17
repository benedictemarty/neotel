/* t_blit1.c - programme cible : mode 1, blits 1 bpp a differents offsets VRAM.
 * Attendu : quatre barres pleines de 14 lignes en y = 0, 182, 196 et 336. */
#include "neo.h"
#include "neo_gfx.h"
#include <string.h>
static unsigned char buf[90 * 14];
int main(void)
{
    gfx_set_mode(1);
    gfx_init();
    memset(buf, 0xFF, sizeof buf);
    gfx_blit_ex(buf, 90, 0UL, 90, 90, 14);
    gfx_blit_ex(buf, 90, 182UL * 90, 90, 90, 14);
    gfx_blit_ex(buf, 90, 196UL * 90, 90, 90, 14);
    gfx_blit_ex(buf, 90, 336UL * 90, 90, 90, 14);
    for (;;) ;
}
