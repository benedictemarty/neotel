/**
 * @file display80.h
 * @brief Rendu de l'ecran 80 colonnes (teleinfo.c) en mode Hercules
 *
 * Mode video 1 du firmware bmarty (F-52) : 720 x 350 pixels, 1 bit par pixel
 * (bit 7 = pixel de gauche, 90 octets par ligne), 25 rangees de cellules
 * 9 x 14 (police 8 x 14, 9e colonne = fond). Chaque rangee est composee dans
 * un tampon de 14 x 90 octets (display_rowbuf, partage avec le mode 0) puis
 * copiee en VRAM par le blitter.
 *
 * Attributs (STUM 1B p. 160) : inversion (9 pixels inverses), souligne
 * (14e ligne pleine), clignotement (cellule vide une phase sur deux, phase
 * inversee si inverse), surintensite (double frappe : glyphe OU glyphe >> 1,
 * comme le mode MDA du firmware ; un plan 1 bpp n'a pas d'intensite).
 * Format 40 colonnes (STUM 2) : cellules de 18 pixels, pixels doubles.
 * Rangee 00 : memes glyphes, sans attribut. Curseur : tiret clignotant sur
 * la 14e ligne (p. 161).
 *
 * Le firmware amont n'a pas de mode 1 : display80_init le detecte (5,9 en
 * erreur) et main.c refuse alors le passage en mode Mixte.
 */

#ifndef DISPLAY80_H
#define DISPLAY80_H

#include "teleinfo.h"

#ifndef __CC65__
#define __fastcall__
#endif

#define D80_W        720
#define D80_H        350
#define D80_STRIDE   90         /* octets par ligne video */
#define D80_CELL_H   14
#define D80_CELL_W   9
#define D80_ROWBYTES (D80_STRIDE * D80_CELL_H)   /* 1260 */

/** Passe en mode 1 et efface. Retourne 0 si le firmware n'a pas ce mode. */
unsigned char display80_init(void);

/** Revient au mode 0 (l'appelant refait display_init()). */
void display80_leave(void);

/** Rend une rangee (budget : une par appel) ; toutes avec _all. */
void display80_render(ti_context_t* ctx);
void display80_render_all(ti_context_t* ctx);
unsigned char display80_dirty_pending(ti_context_t* ctx);

/** Bascule la phase de clignotement et salit les rangees concernees
 *  (cellules clignotantes, rangee du curseur). */
void display80_blink_toggle(ti_context_t* ctx);

/** Message local (question ESC) sur la rangee 00, sans toucher aux cellules ;
 *  display80_status_clear() la re-rend depuis les cellules. */
void display80_status(ti_context_t* ctx, const char* msg);
void display80_status_clear(ti_context_t* ctx);

/** Compose une rangee dans le tampon (expose pour les tests). */
void display80_compose_row(ti_context_t* ctx, unsigned char row);

/** Tampon de rangee (14 x 90 octets) : display_rowbuf de display.h. */
extern unsigned char display_rowbuf[];

#endif /* DISPLAY80_H */
