/**
 * @file display.h
 * @brief Moteur d'affichage de NeoTel : page Minitel 40x25 sur le 320x240 du Neo6502
 *
 * Geometrie : cellules de 8x9 pixels, 25 lignes = 225 lignes video pour la
 * page Videotex (y 0-224), une ligne de statut de 9 pixels en bas (y 230).
 * Les glyphes G0/G2 d'OricTel (6x8, bits 5-0) sont centres dans la cellule
 * (colonnes 1-6, lignes 0-7) ; la ligne 8 porte le souligne. Les mosaiques
 * G1 sont generees en natif 8x9 (blocs de 4x3, separes = 3x2 + interstice).
 *
 * Rendu : chaque ligne modifiee est dessinee dans un TAMPON DE LIGNE en RAM
 * 6502 (9 x 320 octets, 1 octet = 1 pixel = index de palette), puis copiee
 * dans la VRAM du RP2040 par le blitter (12,3, copie rectangulaire avec
 * pas). Une ligne = un appel API ; le 6502 ne fait que l'expansion des
 * glyphes (display_asm.s, blit_cell9). Pas d'attribut serie a ruser comme
 * sur l'Oric : chaque pixel a sa vraie couleur.
 *
 * Couleurs : les index 0-7 de la palette sont programmes par NeoTel selon
 * l'aspect choisi (DISPLAY_LOOK_COLOR : couleurs Videotex ; DISPLAY_LOOK_GREY :
 * les 8 niveaux de gris d'un Minitel 1B monochrome, ordonnes par luminance).
 * Changer d'aspect ne re-rend rien : 8 ecritures de palette.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "videotex.h"

#ifndef __CC65__
#define __fastcall__
#endif

/* Dimensions */
#define SCREEN_COLS   40
#define SCREEN_ROWS   25
#define CELL_W        8
#define CELL_H        9
#define SCREEN_W      320
#define SCREEN_H      240
#define PAGE_H        (SCREEN_ROWS * CELL_H)   /* 225 */
#define STATUS_Y      230                      /* ligne de statut (9 px) */
#define STATUS_COLS   40

/* Aspect (palette) */
#define DISPLAY_LOOK_COLOR 0
#define DISPLAY_LOOK_GREY  1

/**
 * Initialise l'affichage : ecran efface, curseur console cache, palette.
 */
void display_init(void);

/**
 * Programme la palette 0-7 selon l'aspect (DISPLAY_LOOK_*).
 */
void display_set_look(unsigned char look);
unsigned char display_get_look(void);

/**
 * Rend les lignes modifiees, avec budget (1 ligne par appel) pour borner la
 * latence de la boucle principale. Les lignes restantes partent aux appels
 * suivants.
 */
void display_render(vtx_context_t* ctx);

/**
 * Rend toutes les lignes modifiees en un appel (menus, ecrans d'attente).
 */
void display_render_all(vtx_context_t* ctx);

/**
 * Indique s'il reste des lignes a rendre.
 */
unsigned char display_dirty_pending(vtx_context_t* ctx);

/**
 * Rend une ligne complete, sans toucher aux drapeaux dirty.
 */
void display_render_cell_row(vtx_context_t* ctx, unsigned char row);

/**
 * Bascule la phase de clignotement (g_blink_phase, ctx->blink_phase) et
 * marque a re-rendre les lignes qui contiennent une cellule ATTR_FLASH.
 */
void display_blink_toggle(vtx_context_t* ctx);

/**
 * Efface la page (y 0-224) en noir dans la VRAM.
 */
void display_clear(void);

/* --- Ligne de statut (y = STATUS_Y) ------------------------------------
 * 40 colonnes de texte G0, encre par appel, fond noir. */

/**
 * Ecrit s a partir de col (clippe a 40 colonnes) dans la ligne de statut,
 * SANS l'afficher (display_status_show). inverse != 0 : video inverse
 * (fond = encre, encre = noir).
 */
void display_status_text(unsigned char col, const char* s,
                         unsigned char ink, unsigned char inverse);

/**
 * Efface la ligne de statut (sans l'afficher).
 */
void display_status_clear(void);

/**
 * Affiche la ligne de statut (un rendu + un blit pour toute la ligne).
 */
void display_status_show(void);

/**
 * Message transitoire : efface la ligne de statut et y ecrit msg en jaune.
 */
void display_status(const char* msg);

/**
 * Bip (8,3).
 */
void display_beep(void);

/* --- Interface interne exposee pour les tests hote --------------------- */

/**
 * Construit le motif 8x9 d'une cellule (bit 7 = pixel de gauche) dans pat[9],
 * hors doubles tailles. Renvoie l'encre et le fond effectifs (inversion,
 * masquage, clignotement appliques) dans *fg / *bg.
 */
void display_cell_pattern(const vtx_cell_t* cell, unsigned char pat[CELL_H],
                          unsigned char* fg, unsigned char* bg);

/**
 * Motif 8x9 de la forme DRCS ch du jeu drcs_pattern_set (0 = G'0, 1 = G'1)
 * du contexte courant (vtx_current) ; rangees 9 et 10 fusionnees.
 */
extern unsigned char drcs_pattern_set;
const unsigned char* __fastcall__ drcs_pattern9(unsigned char ch);

/**
 * Tampon de rangee : une DEMI-rangee (CELL_H x HALF_W octets, v0.9.0). Une
 * rangee est rendue et blittee en deux moities (colonnes 0-19, 20-39) ; la
 * colonne de tampon est col mod 20. 1 440 octets au lieu de 2 880 ; le
 * mode 80 colonnes (14 x 90 = 1 260 octets) y tient toujours.
 */
#define HALF_COLS     20
#define HALF_W        (HALF_COLS * CELL_W)   /* 160 */
extern unsigned char display_rowbuf[CELL_H * HALF_W];

#endif /* DISPLAY_H */
