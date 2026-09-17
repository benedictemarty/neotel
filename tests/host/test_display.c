/**
 * @file test_display.c
 * @brief Tests hote du moteur d'affichage (display.c) sur la VRAM logicielle
 *
 * Verifie pixel par pixel, via neo_stub.c :
 *   - geometrie 8x9 et centrage des glyphes G0 (colonnes 1-6) ;
 *   - mosaiques G1 8x9 (blocs 4x3, separees, cas $60) ;
 *   - attributs : inversion, souligne (ligne 8), masquage, clignotement ;
 *   - double largeur, double hauteur (moitie haute sur la ligne du dessus),
 *     double taille ;
 *   - budget de rendu (1 ligne par appel), plages dirty, full_refresh ;
 *   - curseur (barre sur la ligne 8, suit le curseur, disparait en phase 1) ;
 *   - ligne de statut a y = 230, video inverse ;
 *   - palettes couleur / gris (ordre de luminance) ;
 *   - display_blink_toggle ne salit que les lignes qui clignotent.
 *
 *   gcc -Wall -Wextra -Isrc -DTEST_HOST -o test_display tests/host/test_display.c
 *       tests/host/neo_stub.c src/display.c src/fonts.c src/videotex.c src/terminal.c
 */

#include <stdio.h>
#include <string.h>
#include "display.h"
#include "settings.h"
#include "fonts.h"
#include "neo_stub.h"

/* Globaux attendus par display.c / videotex.c */
unsigned char g_blink_phase;
unsigned char g_global_mask = 1;

/* Stubs serie (videotex.c, terminal.c) */
void serial_send(unsigned char b) { (void)b; }
void serial_tx_flush(void) {}

static int run, pass;
#define CHECK(c, name) do {                                   \
    ++run;                                                    \
    if (c) { ++pass; }                                        \
    else   { printf("FAIL : %s (ligne %d)\n", name, __LINE__); } \
} while (0)

static vtx_context_t ctx;

/* Pixel (x, y) de la VRAM */
static unsigned char px(unsigned int x, unsigned int y) { return host_vram[y][x]; }

/* Pixel (i, l) de la cellule (col, row) de la page */
static unsigned char cpx(unsigned char col, unsigned char row,
                         unsigned char i, unsigned char l)
{
    return host_vram[row * CELL_H + l][col * CELL_W + i];
}

/* Compte les pixels de couleur c dans la cellule */
static int cell_count(unsigned char col, unsigned char row, unsigned char c)
{
    int n = 0; unsigned char i, l;
    for (l = 0; l < CELL_H; ++l)
        for (i = 0; i < CELL_W; ++i)
            if (cpx(col, row, i, l) == c) ++n;
    return n;
}

static void fresh(void)
{
    g_blink_phase = 0;
    g_global_mask = 1;
    vtx_init(&ctx);
    ctx.cur_visible = 0;
    display_init();
    display_render_all(&ctx);
}

static void put(unsigned char row, unsigned char col, unsigned char ch,
                unsigned char cs, unsigned char fg, unsigned char bg,
                unsigned char flags, unsigned char size)
{
    vtx_cell_t* c = &ctx.screen[row][col];
    c->ch = ch; c->charset = cs; c->fg = fg; c->bg = bg;
    c->flags = (unsigned char)(flags | (size << SIZE_SHIFT));
    vtx_touch(&ctx, row, col, col);
    if (size == SIZE_DOUBLE_HEIGHT || size == SIZE_DOUBLE_SIZE) {
        if (row > 0) vtx_touch(&ctx, row - 1, col, col);
    }
}

/* --------------------------------------------------------------------- */
static void test_geometry_g0(void)
{
    const unsigned char* g = font_get_g0('A');
    unsigned char l, i;
    int ok = 1;

    fresh();
    put(3, 5, 'A', CHARSET_G0, VTX_WHITE, VTX_BLACK, 0, SIZE_NORMAL);
    display_render_all(&ctx);

    /* colonnes 1-6 = bits 5-0 du glyphe, colonnes 0 et 7 vides, ligne 8 vide */
    for (l = 0; l < 8; ++l) {
        for (i = 0; i < 6; ++i) {
            unsigned char want = (g[l] & (0x20 >> i)) ? VTX_WHITE : VTX_BLACK;
            if (cpx(5, 3, i + 1, l) != want) ok = 0;
        }
        if (cpx(5, 3, 0, l) != VTX_BLACK || cpx(5, 3, 7, l) != VTX_BLACK) ok = 0;
    }
    for (i = 0; i < 8; ++i) if (cpx(5, 3, i, 8) != VTX_BLACK) ok = 0;
    CHECK(ok, "G0 'A' : glyphe 6x8 centre dans 8x9");
    CHECK(cell_count(5, 3, VTX_WHITE) > 10, "G0 'A' : pixels d'encre presents");
    CHECK(px(5 * 8 + 1, 3 * 9) == ((g[0] & 0x20) ? 7 : 0), "position absolue y = row*9");
}

static void test_mosaic(void)
{
    int ok = 1; unsigned char i, l;

    fresh();
    put(2, 0, 0x21, CHARSET_G1, VTX_GREEN, VTX_BLACK, 0, SIZE_NORMAL);  /* haut-gauche */
    put(2, 1, 0x22, CHARSET_G1, VTX_GREEN, VTX_BLACK, 0, SIZE_NORMAL);  /* haut-droit */
    put(2, 2, 0x60, CHARSET_G1, VTX_GREEN, VTX_BLACK, 0, SIZE_NORMAL);  /* bas-droit ($40) */
    put(2, 3, 0x7F, CHARSET_G1, VTX_GREEN, VTX_BLACK, 0, SIZE_NORMAL);  /* plein */
    put(2, 4, 0x7F, CHARSET_G1, VTX_GREEN, VTX_BLACK, ATTR_SEPARATED, SIZE_NORMAL);
    put(2, 5, 0x61, CHARSET_G1, VTX_GREEN, VTX_BLACK, 0, SIZE_NORMAL);  /* HG + BD */
    display_render_all(&ctx);

    /* $21 : bloc 4x3 en haut a gauche, rien ailleurs */
    for (l = 0; l < 9; ++l) for (i = 0; i < 8; ++i) {
        unsigned char want = (l < 3 && i < 4) ? VTX_GREEN : VTX_BLACK;
        if (cpx(0, 2, i, l) != want) ok = 0;
    }
    CHECK(ok, "G1 $21 : bloc haut-gauche 4x3");
    ok = 1;
    for (l = 0; l < 9; ++l) for (i = 0; i < 8; ++i) {
        unsigned char want = (l < 3 && i >= 4) ? VTX_GREEN : VTX_BLACK;
        if (cpx(1, 2, i, l) != want) ok = 0;
    }
    CHECK(ok, "G1 $22 : bloc haut-droit");
    /* $60 : cas special, trait plein sur la ligne 0 uniquement */
    ok = 1;
    for (l = 0; l < 9; ++l) for (i = 0; i < 8; ++i) {
        unsigned char want = (l == 0) ? VTX_GREEN : VTX_BLACK;
        if (cpx(2, 2, i, l) != want) ok = 0;
    }
    CHECK(ok, "G1 $60 : trait horizontal haut (cas special ROM)");
    CHECK(cell_count(3, 2, VTX_GREEN) == 72, "G1 $7F : cellule pleine 8x9");
    /* separe : blocs 3x2, colonnes 3 et 7 vides, lignes 2, 5, 8 vides */
    ok = 1;
    for (l = 0; l < 9; ++l) for (i = 0; i < 8; ++i) {
        unsigned char gap = (i == 3 || i == 7 || l == 2 || l == 5 || l == 8);
        unsigned char want = gap ? VTX_BLACK : VTX_GREEN;
        if (cpx(4, 2, i, l) != want) ok = 0;
    }
    CHECK(ok, "G1 $7F separe : interstices droite et bas");
    /* $61 : HG (bit 0) + BD (bit 6) */
    ok = 1;
    for (l = 0; l < 9; ++l) for (i = 0; i < 8; ++i) {
        unsigned char want = ((l < 3 && i < 4) || (l >= 6 && i >= 4)) ? VTX_GREEN : VTX_BLACK;
        if (cpx(5, 2, i, l) != want) ok = 0;
    }
    CHECK(ok, "G1 $61 : haut-gauche + bas-droit (bit 6)");
}

static void test_attributes(void)
{
    fresh();
    put(1, 0, 'X', CHARSET_G0, VTX_WHITE, VTX_BLUE, ATTR_INVERT, SIZE_NORMAL);
    put(1, 1, 'X', CHARSET_G0, VTX_WHITE, VTX_BLACK, ATTR_UNDERLINE, SIZE_NORMAL);
    put(1, 2, 'X', CHARSET_G0, VTX_WHITE, VTX_RED, ATTR_CONCEALED, SIZE_NORMAL);
    put(1, 3, 'X', CHARSET_G0, VTX_WHITE, VTX_BLACK, ATTR_FLASH, SIZE_NORMAL);
    put(1, 4, 'X', CHARSET_G0, VTX_YELLOW, VTX_MAGENTA, 0, SIZE_NORMAL);
    display_render_all(&ctx);

    CHECK(cpx(0, 1, 0, 0) == VTX_WHITE && cell_count(0, 1, VTX_BLUE) > 0,
          "inversion : fond = encre, glyphe = fond");
    {
        int ok = 1; unsigned char i;
        for (i = 0; i < 8; ++i) if (cpx(1, 1, i, 8) != VTX_WHITE) ok = 0;
        CHECK(ok, "souligne : ligne 8 pleine");
    }
    CHECK(cell_count(2, 1, VTX_WHITE) == 0 && cell_count(2, 1, VTX_RED) == 72,
          "masquage (masque global actif) : fond seul");
    CHECK(cell_count(3, 1, VTX_WHITE) > 0, "flash phase 0 : visible");
    CHECK(cell_count(4, 1, VTX_YELLOW) > 0 && cell_count(4, 1, VTX_MAGENTA) > 0
          && cell_count(4, 1, VTX_YELLOW) + cell_count(4, 1, VTX_MAGENTA) == 72,
          "encre / fond de couleur");

    /* masque global leve : le texte cache apparait */
    g_global_mask = 0;
    vtx_touch(&ctx, 1, 2, 2);
    display_render_all(&ctx);
    CHECK(cell_count(2, 1, VTX_WHITE) > 0, "masque global leve : texte revele");
    g_global_mask = 1;

    /* phase de clignotement 1 : cellule flash effacee, les autres intactes */
    display_blink_toggle(&ctx);
    CHECK(g_blink_phase == 1 && ctx.dirty[1] == 1 && ctx.dirty[2] == 0,
          "blink_toggle : seule la ligne qui clignote est salie");
    display_render_all(&ctx);
    CHECK(cell_count(3, 1, VTX_WHITE) == 0, "flash phase 1 : eteint");
    CHECK(cell_count(1, 1, VTX_WHITE) > 0, "phase 1 : voisin non clignotant intact");
}

static void test_double_sizes(void)
{
    const unsigned char* g = font_get_g0('H');
    int ok = 1; unsigned char l, i;

    fresh();
    /* double largeur : occupe les colonnes 2 et 3 */
    put(4, 2, 'H', CHARSET_G0, VTX_WHITE, VTX_BLACK, 0, SIZE_DOUBLE_WIDTH);
    vtx_touch(&ctx, 4, 2, 3);
    display_render_all(&ctx);
    for (l = 0; l < 8; ++l) {
        unsigned char pat = (unsigned char)((g[l] << 1) & 0x7E);
        for (i = 0; i < 8; ++i) {
            unsigned char bit = (pat >> (7 - (i / 2))) & 1;   /* quartet haut double */
            if (cpx(2, 4, i, l) != (bit ? VTX_WHITE : VTX_BLACK)) ok = 0;
            bit = (pat >> (3 - (i / 2))) & 1;                  /* quartet bas double */
            if (cpx(3, 4, i, l) != (bit ? VTX_WHITE : VTX_BLACK)) ok = 0;
        }
    }
    CHECK(ok, "double largeur : moities gauche/droite doublees");

    /* double hauteur en ligne 8 : moitie haute sur la ligne 7 */
    put(8, 0, 'H', CHARSET_G0, VTX_CYAN, VTX_BLACK, 0, SIZE_DOUBLE_HEIGHT);
    display_render_all(&ctx);
    CHECK(cell_count(0, 7, VTX_CYAN) > 0, "double hauteur : moitie haute sur la ligne du dessus");
    CHECK(cell_count(0, 8, VTX_CYAN) > 0, "double hauteur : moitie basse sur la ligne");
    /* ligne 0 du glyphe etiree : lignes 0 et 1 de la ligne 7 identiques */
    ok = 1;
    for (i = 0; i < 8; ++i) if (cpx(0, 7, i, 0) != cpx(0, 7, i, 1)) ok = 0;
    CHECK(ok, "double hauteur : lignes source doublees");
    /* re-rendre la ligne 7 seule ne doit pas effacer la moitie haute */
    vtx_touch(&ctx, 7, 0, 0);
    display_render_all(&ctx);
    CHECK(cell_count(0, 7, VTX_CYAN) > 0, "ligne du dessus re-rendue : moitie haute conservee");

    /* double taille : 2 colonnes x 2 lignes */
    put(12, 10, 'H', CHARSET_G0, VTX_GREEN, VTX_BLACK, 0, SIZE_DOUBLE_SIZE);
    vtx_touch(&ctx, 12, 10, 11);
    vtx_touch(&ctx, 11, 10, 11);
    display_render_all(&ctx);
    CHECK(cell_count(10, 11, VTX_GREEN) > 0 && cell_count(11, 11, VTX_GREEN) > 0 &&
          cell_count(10, 12, VTX_GREEN) > 0 && cell_count(11, 12, VTX_GREEN) > 0,
          "double taille : 4 cellules touchees");
    /* double largeur en colonne 39 : pas de debordement (rien en colonne 0 de la ligne suivante) */
    put(15, 39, 'H', CHARSET_G0, VTX_WHITE, VTX_BLACK, 0, SIZE_DOUBLE_WIDTH);
    display_render_all(&ctx);
    CHECK(cell_count(39, 15, VTX_WHITE) > 0 && cell_count(0, 16, VTX_WHITE) == 0,
          "double largeur colonne 39 : moitie droite clippee");

    /* double largeur en colonne 19 : chevauche les deux demi-rangees (v0.9.0).
     * Les deux moities doivent etre a l'ecran, la colonne 0 intacte. */
    put(17, 0, 'X', CHARSET_G0, VTX_YELLOW, VTX_BLACK, 0, SIZE_NORMAL);
    put(17, 19, 'H', CHARSET_G0, VTX_WHITE, VTX_BLACK, 0, SIZE_DOUBLE_WIDTH);
    display_render_all(&ctx);
    CHECK(cell_count(19, 17, VTX_WHITE) > 0 && cell_count(20, 17, VTX_WHITE) > 0,
          "double largeur colonne 19 : deux moities rendues");
    CHECK(cell_count(0, 17, VTX_YELLOW) > 0 && cell_count(0, 17, VTX_WHITE) == 0,
          "double largeur colonne 19 : colonne 0 intacte");
    /* re-rendu de la seule colonne 19 : la moitie droite (colonne 20) suit */
    put(17, 19, 'I', CHARSET_G0, VTX_GREEN, VTX_BLACK, 0, SIZE_DOUBLE_WIDTH);
    display_render_all(&ctx);
    CHECK(cell_count(20, 17, VTX_GREEN) > 0 && cell_count(20, 17, VTX_WHITE) == 0,
          "double largeur colonne 19 : moitie droite mise a jour");
    /* re-rendu de la seule colonne 20 (ex. curseur) : moitie gauche conservee */
    vtx_touch(&ctx, 17, 20, 20);
    display_render_all(&ctx);
    CHECK(cell_count(19, 17, VTX_GREEN) > 0 && cell_count(20, 17, VTX_GREEN) > 0,
          "re-rendu colonne 20 : les deux moities restent");
}

static void test_budget_and_spans(void)
{
    fresh();
    host_blits = 0;
    put(1, 0, 'A', CHARSET_G0, VTX_WHITE, VTX_BLACK, 0, SIZE_NORMAL);
    put(5, 0, 'B', CHARSET_G0, VTX_WHITE, VTX_BLACK, 0, SIZE_NORMAL);
    put(9, 0, 'C', CHARSET_G0, VTX_WHITE, VTX_BLACK, 0, SIZE_NORMAL);
    display_render(&ctx);
    CHECK(host_blits == 1 && ctx.dirty[1] == 0 && ctx.dirty[5] == 1,
          "display_render : une ligne par appel");
    CHECK(display_dirty_pending(&ctx) == 1, "dirty_pending : il reste des lignes");
    display_render(&ctx);
    display_render(&ctx);
    CHECK(display_dirty_pending(&ctx) == 0 && host_blits == 3, "3 appels : tout rendu");

    /* full_refresh : 25 lignes, chacune en deux demi-rangees (v0.9.0) */
    host_blits = 0;
    ctx.full_refresh = 1;
    display_render_all(&ctx);
    CHECK(host_blits == 50, "full_refresh : 25 rangees = 50 blits (demi-rangees)");

    /* Une plage limitee a une moitie ne blitte que cette moitie */
    host_blits = 0;
    vtx_touch(&ctx, 3, 2, 7);
    display_render(&ctx);
    CHECK(host_blits == 1, "plage 2-7 : un seul blit (moitie gauche)");
    host_blits = 0;
    vtx_touch(&ctx, 3, 18, 22);
    display_render(&ctx);
    CHECK(host_blits == 2, "plage 18-22 : deux blits (frontiere)");

    /* plage : seule la plage touchee est copiee, le reste de la VRAM intact */
    host_vram[2 * 9][100] = 0x55;             /* marqueur hors plage, ligne 2 */
    put(2, 0, 'Z', CHARSET_G0, VTX_WHITE, VTX_BLACK, 0, SIZE_NORMAL);
    display_render_all(&ctx);
    CHECK(host_vram[2 * 9][100] == 0x55, "plage dirty : colonnes hors plage non touchees");
    CHECK(cell_count(0, 2, VTX_WHITE) > 0, "plage dirty : cellule touchee rendue");
}

static void test_cursor(void)
{
    int ok; unsigned char i;

    fresh();
    ctx.cur_visible = 1;
    ctx.cur_y = 3; ctx.cur_x = 4;
    display_render_all(&ctx);
    ok = 1;
    for (i = 0; i < 8; ++i) if (cpx(4, 3, i, 8) != VTX_WHITE) ok = 0;
    CHECK(ok, "curseur : barre blanche sur la ligne 8 de la cellule");

    ctx.cur_x = 6;
    display_render_all(&ctx);
    ok = 1;
    for (i = 0; i < 8; ++i) if (cpx(4, 3, i, 8) != VTX_BLACK) ok = 0;
    for (i = 0; i < 8; ++i) if (cpx(6, 3, i, 8) != VTX_WHITE) ok = 0;
    CHECK(ok, "curseur : l'ancienne position est effacee, la nouvelle dessinee");

    g_blink_phase = 1;
    display_render_all(&ctx);
    ok = 1;
    for (i = 0; i < 8; ++i) if (cpx(6, 3, i, 8) != VTX_BLACK) ok = 0;
    CHECK(ok, "curseur : invisible en phase 1");
    g_blink_phase = 0;

    ctx.cur_visible = 0;
    display_render_all(&ctx);
    ok = 1;
    for (i = 0; i < 8; ++i) if (cpx(6, 3, i, 8) != VTX_BLACK) ok = 0;
    CHECK(ok, "curseur : cache par cur_visible = 0");
}

static void test_status(void)
{
    int n = 0; unsigned int x; unsigned char y;

    fresh();
    display_status_clear();
    display_status_text(0, " C ", VTX_CYAN, 1);
    display_status_text(4, "PAVI", VTX_YELLOW, 0);
    display_status_show();
    for (y = STATUS_Y; y < STATUS_Y + CELL_H; ++y)
        for (x = 0; x < 24; ++x) if (host_vram[y][x] == VTX_CYAN) ++n;
    CHECK(n > 3 * 9 * 5, "statut : ' C ' en video inverse cyan a y = 230");
    n = 0;
    for (y = STATUS_Y; y < STATUS_Y + CELL_H; ++y)
        for (x = 32; x < 64; ++x) if (host_vram[y][x] == VTX_YELLOW) ++n;
    CHECK(n > 0, "statut : texte en colonne 4");
    CHECK(host_vram[229][40] == 0 && host_vram[239][40] == 0, "statut : lignes 229 et 239 vides");

    display_status("Message");
    n = 0;
    for (y = STATUS_Y; y < STATUS_Y + CELL_H; ++y)
        for (x = 0; x < 320; ++x) if (host_vram[y][x] == VTX_CYAN) ++n;
    CHECK(n == 0, "display_status : ligne effacee puis message");
    /* clip a 40 colonnes : pas de debordement */
    display_status_text(38, "ABCDEF", VTX_WHITE, 0);
    display_status_show();
    CHECK(1, "statut : clip a 40 colonnes sans plantage");
}

static void test_palette(void)
{
    fresh();
    display_set_look(DISPLAY_LOOK_COLOR);
    CHECK(host_palette[1][0] == 255 && host_palette[1][1] == 0 &&
          host_palette[4][2] == 255 && host_palette[7][1] == 255,
          "palette couleur : rouge = 1, bleu = 4, blanc = 7");
    display_set_look(DISPLAY_LOOK_GREY);
    CHECK(host_palette[4][0] < host_palette[1][0] && host_palette[1][0] < host_palette[5][0] &&
          host_palette[5][0] < host_palette[2][0] && host_palette[2][0] < host_palette[6][0] &&
          host_palette[6][0] < host_palette[3][0] && host_palette[3][0] < host_palette[7][0],
          "palette gris : bleu < rouge < magenta < vert < cyan < jaune < blanc");
    CHECK(host_palette[2][0] == host_palette[2][1] && host_palette[2][1] == host_palette[2][2],
          "palette gris : R = V = B");
    CHECK(display_get_look() == DISPLAY_LOOK_GREY, "display_get_look");
    display_set_look(DISPLAY_LOOK_COLOR);
}

static void test_clear_and_g2(void)
{
    fresh();
    put(20, 20, 0x80, CHARSET_G2, VTX_WHITE, VTX_BLACK, 0, SIZE_NORMAL);   /* e aigu */
    display_render_all(&ctx);
    CHECK(cell_count(20, 20, VTX_WHITE) > 0, "G2 : accent pre-compose rendu");
    host_fills = 0;
    display_clear();
    CHECK(host_fills == 1 && host_vram[0][0] == 0 && host_vram[224][319] == 0,
          "display_clear : page effacee par un rectangle");
}

/* Bip (BEL, splash) soumis au reglage "son" du menu 7 (v0.6.2) */
static void test_beep_setting(void)
{
    host_beeps = 0;
    g_settings.sound = 1;
    display_beep();
    CHECK(host_beeps == 1, "son ON : bip emis (8,7)");
    g_settings.sound = 0;
    display_beep();
    CHECK(host_beeps == 1, "son OFF : pas de bip");
    g_settings.sound = 1;
}

int main(void)
{
    test_geometry_g0();
    test_mosaic();
    test_attributes();
    test_double_sizes();
    test_budget_and_spans();
    test_cursor();
    test_status();
    test_palette();
    test_clear_and_g2();
    test_beep_setting();
    printf("test_display : %d/%d OK\n", pass, run);
    return pass == run ? 0 : 1;
}
