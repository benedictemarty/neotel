/**
 * @file test_ui.c
 * @brief Tests host des helpers UI (ui.c) — verrouille les bornes de saisie.
 *
 * Cible en particulier la non-regression des findings revue #2-#5 (ecritures
 * hors borne dans ui_print et les saisies) : on verifie qu'AUCUNE ecriture ne
 * deborde au-dela de la colonne VTX_COLS-1 ni sur la ligne suivante.
 *
 *   gcc -Wall -Wextra -Isrc -o build/test_ui tests/test_ui.c src/ui.c
 */

#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "keyboard.h"   /* KEY_* */

/* --- clavier scripte (remplace keyboard_scan) --- */
static unsigned char keyq[80];
static int keyn, keyi;
static void key_feed(const unsigned char* k, int n) { memcpy(keyq, k, n); keyn = n; keyi = 0; }
unsigned char keyboard_scan(void) { return keyi < keyn ? keyq[keyi++] : KEY_NONE; }

/* --- rendu neutralise --- */
void display_render_all(vtx_context_t* c) { (void)c; }

/* --- harnais --- */
static int run, pass;
#define CHECK(c, name) do {                                   \
    ++run;                                                    \
    if (c) { ++pass; printf("ok   : %s\n", name); }           \
    else   { printf("FAIL : %s\n", name); }                   \
} while (0)

static vtx_context_t ctx;

int main(void)
{
    printf("=== OricTel - Tests helpers UI (bornes saisie) ===\n\n");

    /* --- ui_print: clip a la largeur ecran (#2) --- */
    memset(&ctx, 0, sizeof ctx);
    ui_print(&ctx, 5, 30, "ABCDEFGHIJKLMNOPQRST", VTX_YELLOW);  /* 20 car. a col 30 */
    CHECK(ctx.screen[5][39].ch == 'J', "ui_print: derniere cellule = col 39 ('J')");
    CHECK(ctx.screen[6][0].ch == 0,    "ui_print: pas de debordement ligne suivante");

    /* --- ui_text_input: longueur bornee par VTX_COLS-col (#3/#4) --- */
    {
        static char buf[40];
        unsigned char k[40];
        int i;
        for (i = 0; i < 30; ++i) k[i] = 'X';            /* 30 X (> 26 admissibles) */
        k[30] = KEY_FUNC_FLAG | KEY_ENVOI;
        memset(&ctx, 0, sizeof ctx);
        key_feed(k, 31);
        i = ui_text_input(&ctx, 7, 14, buf, sizeof buf, 0);  /* col 14 -> maxlen 26 */
        CHECK(i == 26,                       "ui_text_input: longueur bornee a VTX_COLS-col (26)");
        CHECK(ctx.screen[7][39].ch == 'X',   "ui_text_input: derniere cellule = col 39");
        CHECK(ctx.screen[8][0].ch == 0,      "ui_text_input: pas de debordement ligne suivante");
        CHECK(buf[26] == 0 && strlen(buf) == 26, "ui_text_input: buf borne et termine");
    }

    /* --- ANNULATION -> 0xFF --- */
    {
        static char buf[40];
        unsigned char k[2];
        k[0] = 'A'; k[1] = KEY_FUNC_FLAG | KEY_ANNULATION;
        memset(&ctx, 0, sizeof ctx);
        key_feed(k, 2);
        CHECK(ui_text_input(&ctx, 7, 3, buf, sizeof buf, 0) == 0xFF, "ANNULATION -> 0xFF");
    }

    /* --- ESC annule aussi la saisie (touche de sortie universelle) --- */
    {
        static char buf[40];
        unsigned char k[2];
        k[0] = 'A'; k[1] = KEY_LOCAL_ESCAPE;
        memset(&ctx, 0, sizeof ctx);
        key_feed(k, 2);
        CHECK(ui_text_input(&ctx, 7, 3, buf, sizeof buf, 0) == 0xFF, "ESC -> 0xFF (saisie annulee)");
        CHECK(buf[0] == 0, "ESC -> tampon vide");
    }

    /* --- CORRECTION efface le dernier caractere --- */
    {
        static char buf[40];
        unsigned char k[4];
        k[0] = 'A'; k[1] = 'B';
        k[2] = KEY_FUNC_FLAG | KEY_CORRECTION;
        k[3] = KEY_FUNC_FLAG | KEY_ENVOI;
        memset(&ctx, 0, sizeof ctx);
        key_feed(k, 4);
        CHECK(ui_text_input(&ctx, 7, 3, buf, sizeof buf, 0) == 1 &&
              buf[0] == 'A' && buf[1] == 0, "CORRECTION efface le dernier caractere");
    }

    /* --- masque '*' a l'ecran, vrai texte dans buf --- */
    {
        static char buf[40];
        unsigned char k[3];
        k[0] = 'S'; k[1] = 'E'; k[2] = KEY_FUNC_FLAG | KEY_ENVOI;
        memset(&ctx, 0, sizeof ctx);
        key_feed(k, 3);
        ui_text_input(&ctx, 7, 3, buf, sizeof buf, '*');
        CHECK(buf[0] == 'S' && buf[1] == 'E' &&
              ctx.screen[7][3].ch == '*' && ctx.screen[7][4].ch == '*',
              "masque '*' a l'ecran, vrai texte conserve dans buf");
    }

    /* --- charte v0.9.1 : bandeau, items, navigation --- */
    {
        unsigned char sel = 0;
        memset(&ctx, 0, sizeof ctx);
        ui_header(&ctx, "NEOTEL", "v9");
        CHECK(cell_bg(&ctx.screen[1][0]) == VTX_BLUE && cell_bg(&ctx.screen[2][39]) == VTX_BLUE,
              "ui_header: bandeau bleu sur les rangees 1-2");
        CHECK(ctx.screen[2][2].ch == 'N' && (ctx.screen[2][2].flags >> SIZE_SHIFT) == SIZE_DOUBLE_HEIGHT,
              "ui_header: titre double hauteur en (2,2)");
        CHECK(ctx.screen[2][36].ch == 'v' && ctx.screen[2][37].ch == '9' && cell_fg(&ctx.screen[2][36]) == VTX_YELLOW,
              "ui_header: texte de droite aligne en colonne 37");
        CHECK(ctx.screen[3][0].charset == CHARSET_G1 && ctx.screen[3][39].ch == 0x60 && ctx.dirty[3],
              "ui_header: filet mosaique en rangee 3");
        ui_banner(&ctx, "AB", NULL, 1);
        CHECK(ctx.screen[2][2].ch == 'A' && ctx.screen[2][4].ch == 'B' &&
              (ctx.screen[2][2].flags >> SIZE_SHIFT) == SIZE_DOUBLE_SIZE,
              "ui_banner: double taille, une lettre toutes les 2 colonnes");

        ui_item(&ctx, 7, '3', "Terminal", "Minitel 2", 0);
        CHECK(ctx.screen[7][2].ch == '[' && ctx.screen[7][3].ch == '3' && cell_fg(&ctx.screen[7][3]) == VTX_CYAN,
              "ui_item: [3] avec la touche en cyan");
        CHECK(ctx.screen[7][6].ch == 'T' && cell_fg(&ctx.screen[7][6]) == VTX_YELLOW, "ui_item: libelle jaune en colonne 6");
        CHECK(ctx.screen[7][16].ch == '.' && ctx.screen[7][20].ch == '.' && ctx.screen[7][21].ch == ' ',
              "ui_item: points de conduite jusqu'a la colonne 20");
        CHECK(ctx.screen[7][22].ch == 'M' && cell_fg(&ctx.screen[7][22]) == VTX_WHITE, "ui_item: valeur blanche en colonne 22");
        CHECK(cell_bg(&ctx.screen[7][0]) == VTX_BLACK && cell_bg(&ctx.screen[7][20]) == VTX_BLACK, "ui_item: non selectionne, fond noir");
        ui_item(&ctx, 7, '3', "Terminal", "Minitel 2", 1);
        CHECK(cell_bg(&ctx.screen[7][1]) == VTX_BLUE && cell_bg(&ctx.screen[7][38]) == VTX_BLUE && cell_bg(&ctx.screen[7][0]) == VTX_BLACK,
              "ui_item: selectionne, fond bleu des colonnes 1 a 38");
        CHECK(cell_fg(&ctx.screen[7][6]) == VTX_WHITE && cell_fg(&ctx.screen[7][3]) == VTX_WHITE, "ui_item: selectionne, encre blanche");
        ui_item(&ctx, 8, '5', "Identification", NULL, 0);
        CHECK(ctx.screen[8][21].ch == ' ' && ctx.screen[8][22].ch == ' ', "ui_item: sans valeur, pas de points");

        ui_footer(&ctx, "H aide", "ESC");
        CHECK(ctx.screen[22][0].charset == CHARSET_G1 && ctx.screen[23][2].ch == 'H' &&
              cell_fg(&ctx.screen[23][2]) == VTX_GREEN && ctx.screen[23][37].ch == 'C',
              "ui_footer: filet, texte gauche vert, droite alignee");

        CHECK(ui_nav(KEY_ARROW_DOWN, &sel, 3) == 1 && sel == 1, "ui_nav: bas -> 1");
        CHECK(ui_nav(KEY_ARROW_DOWN, &sel, 3) == 1 && sel == 2, "ui_nav: bas -> 2");
        CHECK(ui_nav(KEY_ARROW_DOWN, &sel, 3) == 1 && sel == 0, "ui_nav: bas -> boucle a 0");
        CHECK(ui_nav(KEY_ARROW_UP, &sel, 3) == 1 && sel == 2, "ui_nav: haut -> boucle a 2");
        CHECK(ui_nav(KEY_FUNC_FLAG | KEY_ENVOI, &sel, 3) == 2, "ui_nav: ENVOI valide");
        CHECK(ui_nav(KEY_FUNC_FLAG | KEY_SUITE, &sel, 3) == 2, "ui_nav: Suite valide");
        CHECK(ui_nav(KEY_ARROW_RIGHT, &sel, 3) == 2, "ui_nav: fleche droite valide");
        CHECK(ui_nav('x', &sel, 3) == 0 && sel == 2, "ui_nav: autre touche ignoree");
    }

    printf("\n=== Resultats: %d/%d passes ===\n", pass, run);
    return (pass == run) ? 0 : 1;
}
