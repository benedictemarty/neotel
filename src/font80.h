/**
 * @file font80.h
 * @brief Police 8x14 du mode 80 colonnes (jeux americain et francais)
 *
 * Genere par tools/gen_font80.py depuis Lat15-VGA14.psf.gz (console-setup,
 * domaine public) : 96 glyphes ASCII, 11 glyphes du jeu francais NF Z 62-010
 * (STUM 1B tableau 4), le pave plein (symbole d'erreur) et les glyphes des
 * jeux complementaire et DEC du Minitel 2 (STUM 2 annexes 3.12 et 3.13).
 * 14 octets par glyphe, bit 7 = pixel de gauche.
 */

#ifndef FONT80_INC_H
#define FONT80_INC_H

#define FONT80_H        14
#define FONT80_ASCII    96
#define FONT80_FR_BASE  96      /* index des 11 glyphes francais */
#define FONT80_FR_COUNT 11
#define FONT80_BLOCK    (FONT80_FR_BASE + FONT80_FR_COUNT)   /* pave plein */
#define FONT80_EXTRA    62      /* glyphes des jeux complementaire et DEC + 5 traits de balayage DEC (generateur) */
#define FONT80_COUNT    (FONT80_BLOCK + 1 + FONT80_EXTRA)   /* = 170, voir font80.c */

/* Jeux de caracteres du mode Mixte / Teleinformatique (STUM 2 par. 3.2.2) */
#define TI_SET_US    0          /* americain (ASCII), defaut G0 */
#define TI_SET_FR    1          /* francais NF Z 62-010, defaut G1 */
#define TI_SET_COMP  2          /* complementaire (annexe 3.12), Minitel 2 */
#define TI_SET_DEC   3          /* special graphique DEC (annexe 3.13), Minitel 2 */

extern const unsigned char font80[FONT80_COUNT * FONT80_H];
extern const unsigned char font80_fr_index[96];
extern const unsigned char font80_comp_index[96];
extern const unsigned char font80_dec_index[96];
extern const unsigned int  font80_offset[FONT80_COUNT];

#endif /* FONT80_INC_H */
