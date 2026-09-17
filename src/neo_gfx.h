/**
 * @file neo_gfx.h
 * @brief Primitives video du Neo6502 utilisees par display.c
 *
 * Couche mince au-dessus de l'API (groupes 2, 5 et 12). Sur l'hote
 * (TEST_HOST) le harnais fournit une implementation sur une VRAM logicielle
 * de 320x240 octets (tests/host/neo_stub.c), ce qui rend display.c testable
 * pixel par pixel.
 */

#ifndef NEO_GFX_H
#define NEO_GFX_H

/** Efface l'ecran (2,12), cache le curseur console (2,19), pose les
 *  reglages graphiques (5,1 : dessin opaque, taille 1). */
void gfx_init(void);

/** Rectangle plein [x0,y0]-[x1,y1] de la couleur donnee (5,3). */
void gfx_fill_rect(unsigned int x0, unsigned char y0,
                   unsigned int x1, unsigned char y1, unsigned char colour);

/** Palette : index -> RGB (5,32). */
void gfx_set_palette(unsigned char idx, unsigned char r,
                     unsigned char g, unsigned char b);

/** Copie h lignes de w octets depuis src (pas 320) vers la VRAM en (x, y)
 *  par le blitter (12,3). x + w <= 320, y + h <= 240. */
void gfx_blit(const unsigned char* src, unsigned int x, unsigned char y,
              unsigned int w, unsigned char h);

/** Copie generique : h lignes de w octets, pas source src_stride, vers
 *  l'offset VRAM dst_off (octets depuis le debut de la page video), pas
 *  dst_stride. Mode 1 (Hercules, 1 bpp) : pas 90, offset = y * 90 + x / 8. */
void gfx_blit_ex(const unsigned char* src, unsigned int src_stride,
                 unsigned long dst_off, unsigned int dst_stride,
                 unsigned int w, unsigned char h);

/** Mode video (5,9) : 0 = 320x240x256, 1 = Hercules 720x350 1 bpp (fork).
 *  Retourne 1 si le firmware l'accepte, 0 sinon (drapeau d'erreur). */
unsigned char gfx_set_mode(unsigned char mode);

#endif /* NEO_GFX_H */
