/**
 * @file neo_gfx.c
 * @brief Primitives video du Neo6502 (voir neo_gfx.h)
 */

#include "neo_gfx.h"
#include "neo.h"

/* Descripteurs de rectangle du blitter (struct BlitterArea du firmware :
 * adresse 16 bits, page, octet nul, pas signe 16 bits, format, transparent,
 * solide, hauteur, largeur 16 bits = 12 octets). Statiques : le blitter les
 * lit dans la RAM 6502 a l'adresse passee en parametre. */
static unsigned char blt_src[12];
unsigned char blt_dst[12];

#define VRAM_PAGE 0x80

void gfx_init(void)
{
    neo_wait();
    neo_call(NEO_G_CONSOLE, NEO_F_CLEAR_SCREEN);
    NEO_P[0] = 0;
    neo_call(NEO_G_CONSOLE, NEO_F_CURSOR_SHOW);
    NEO_P[0] = 0xFF;            /* And : garde la couleur demandee */
    NEO_P[1] = 0;               /* Or  : rien */
    NEO_P[2] = 1;               /* rempli */
    NEO_P[3] = 1;               /* taille 1 */
    NEO_P[4] = 0;               /* pas de flip */
    neo_call(NEO_G_GRAPHICS, NEO_F_GFX_DEFAULTS);
}

void gfx_fill_rect(unsigned int x0, unsigned char y0,
                   unsigned int x1, unsigned char y1, unsigned char colour)
{
    neo_wait();
    /* 5,1 : And = 0 et Or = couleur => le pixel devient exactement colour */
    NEO_P[0] = 0;
    NEO_P[1] = colour;
    NEO_P[2] = 1;
    NEO_P[3] = 1;
    NEO_P[4] = 0;
    neo_call(NEO_G_GRAPHICS, NEO_F_GFX_DEFAULTS);
    NEO_P[0] = (unsigned char)(x0 & 0xFF);
    NEO_P[1] = (unsigned char)(x0 >> 8);
    NEO_P[2] = y0;
    NEO_P[3] = 0;
    NEO_P[4] = (unsigned char)(x1 & 0xFF);
    NEO_P[5] = (unsigned char)(x1 >> 8);
    NEO_P[6] = y1;
    NEO_P[7] = 0;
    neo_call(NEO_G_GRAPHICS, NEO_F_DRAW_RECT);
}

void gfx_set_palette(unsigned char idx, unsigned char r,
                     unsigned char g, unsigned char b)
{
    neo_wait();
    NEO_P[0] = idx;
    NEO_P[1] = r;
    NEO_P[2] = g;
    NEO_P[3] = b;
    neo_call(NEO_G_GRAPHICS, NEO_F_SET_PALETTE);
}

void gfx_blit(const unsigned char* src, unsigned int x, unsigned char y,
              unsigned int w, unsigned char h)
{
    gfx_blit_ex(src, 320, (unsigned long)y * 320u + x, 320, w, h);
}

unsigned char gfx_set_mode(unsigned char mode)
{
    neo_wait();
    NEO_P[0] = mode;
    neo_call(NEO_G_GRAPHICS, 9);
    return NEO_ERR ? 0 : 1;
}

void gfx_blit_ex(const unsigned char* src, unsigned int src_stride,
                 unsigned long off, unsigned int dst_stride,
                 unsigned int w, unsigned char h)
{

    blt_src[0] = (unsigned char)((unsigned int)src & 0xFF);
    blt_src[1] = (unsigned char)((unsigned int)src >> 8);
    blt_src[2] = 0;                             /* page 0 : RAM 6502 */
    blt_src[3] = 0;
    blt_src[4] = (unsigned char)(src_stride & 0xFF);
    blt_src[5] = (unsigned char)(src_stride >> 8);
    blt_src[6] = 0;                             /* format octets */
    blt_src[7] = 0;
    blt_src[8] = 0;
    blt_src[9] = h;
    blt_src[10] = (unsigned char)(w & 0xFF);
    blt_src[11] = (unsigned char)(w >> 8);

    blt_dst[0] = (unsigned char)(off & 0xFF);
    blt_dst[1] = (unsigned char)((off >> 8) & 0xFF);
    blt_dst[2] = (unsigned char)(VRAM_PAGE + (unsigned char)(off >> 16));
    blt_dst[3] = 0;
    blt_dst[4] = (unsigned char)(dst_stride & 0xFF);
    blt_dst[5] = (unsigned char)(dst_stride >> 8);
    blt_dst[6] = 0;

    neo_wait();
    NEO_P[0] = 0;                               /* action : copie */
    NEO_P[1] = (unsigned char)((unsigned int)blt_src & 0xFF);
    NEO_P[2] = (unsigned char)((unsigned int)blt_src >> 8);
    NEO_P[3] = (unsigned char)((unsigned int)blt_dst & 0xFF);
    NEO_P[4] = (unsigned char)((unsigned int)blt_dst >> 8);
    neo_call(NEO_G_BLITTER, NEO_F_BLIT_COMPLEX);
}
