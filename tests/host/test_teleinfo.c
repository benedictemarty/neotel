/**
 * @file test_teleinfo.c
 * @brief Tests hote de l'ecran 80 colonnes (teleinfo.c, display80.c)
 *
 * Reference : STUM 1B partie 3 chapitre 2 (p. 160-170), STUM 2 par. 3.3/3.4.
 * Decodeur : initialisation, ecriture et jeu francais, C0 (BS HT LF VT FF CR
 * SO SI CAN SUB BEL NUL), ESC D/E/M/7/8/c, CSI A B C D H J K @ L M P h l m,
 * rouleau / page, insertion SM4, filtrage des sequences inconnues,
 * resynchronisation C0, rangee 00 (US 4/0 X/Y, LF), CSI ? { , CSI 6 n,
 * formats 40/80 (STUM 2). Rendu : pixels du tampon 1 bpp (position col*9,
 * inverse, souligne, gras, clignotement, curseur, 40 colonnes, rangee 00).
 */

#include <stdio.h>
#include <string.h>
#include "teleinfo.h"
#include "display80.h"
#include "font80.h"
#include "terminal.h"
#include "neo_stub.h"

unsigned char g_blink_phase;
unsigned char g_global_mask = 1;
unsigned char display_rowbuf[2880];

static int run, pass;
#define CHECK(c, name) do { ++run; if (c) ++pass; else printf("FAIL : %s (ligne %d)\n", name, __LINE__); } while (0)

static ti_context_t ctx;
static void feed(const char* s) { while (*s) ti_process(&ctx, (unsigned char)*s++); }
static unsigned char ch_at(unsigned char r, unsigned char c) { return ctx.screen[r][c].ch; }
static unsigned char at_at(unsigned char r, unsigned char c) { return ctx.screen[r][c].attr; }

/* pixel (x, y) du tampon de rangee (1 bpp, bit 7 = gauche) */
static int px(unsigned int x, unsigned char y)
{
    return (display_rowbuf[y * D80_STRIDE + (x >> 3)] >> (7 - (x & 7))) & 1;
}

int main(void)
{
    int i, ok;

    term_set_model(TERM_MINITEL_1B);
    ti_init(&ctx);
    CHECK(ctx.cols == 80 && ctx.cur_x == 0 && ctx.cur_y == 1 && ctx.roll == 1 && ctx.shift == 0 &&
          ctx.attr == 0 && ctx.cur_visible == 1 && ch_at(1, 0) == ' ', "init : 80 col, (1,1), rouleau, americain");

    /* --- ecriture, jeux --- */
    feed("Hello");
    CHECK(ch_at(1, 0) == 'H' && ch_at(1, 4) == 'o' && ctx.cur_x == 5 && at_at(1, 0) == 0, "texte en (1,1..5)");
    feed("\x0E#\x0F#");
    CHECK(at_at(1, 5) == TI_ATTR_FRENCH && at_at(1, 6) == 0 && ch_at(1, 6) == '#', "SO : jeu francais fige dans la cellule, SI : americain");
    ti_init(&ctx);
    for (i = 0; i < 80; ++i) ti_process(&ctx, 'x');
    CHECK(ctx.cur_y == 2 && ctx.cur_x == 0 && ch_at(1, 79) == 'x', "80e caractere : passage a la rangee suivante (hypothese)");
    feed("\x7F");
    CHECK(ch_at(2, 0) == ' ', "DEL : non visualisable");

    /* --- C0 --- */
    ti_init(&ctx);
    feed("abc\x08\x08z");
    CHECK(ch_at(1, 1) == 'z' && ctx.cur_x == 2, "BS puis ecriture");
    feed("\x08\x08\x08\x08");
    CHECK(ctx.cur_x == 0, "BS : pas de debordement en colonne 1");
    feed("\x09");
    CHECK(ctx.cur_x == 8, "HT : pas de 8");
    feed("\x09\x09\x09\x09\x09\x09\x09\x09\x09\x09\x09");
    CHECK(ctx.cur_x == 79, "HT : arret en fin de rangee");
    feed("\x0D");
    CHECK(ctx.cur_x == 0 && ctx.cur_y == 2, "CR : colonne 1 de la rangee suivante (p. 166)");
    feed("\x0B\x0C");
    CHECK(ctx.cur_y == 4, "VT, FF = LF");
    feed("\x07");
    CHECK(ctx.req_beep == 1, "BEL : demande de bip");
    ctx.req_beep = 0;
    feed("\x18");
    CHECK((at_at(4, 0) & TI_ATTR_ERROR) && ctx.cur_x == 1, "CAN : symbole d'erreur");
    feed("\x1B[\x1A");
    CHECK(ctx.state == TI_STATE_NORMAL && (at_at(4, 1) & TI_ATTR_ERROR), "SUB dans une commande : annule et affiche le pave");
    { static const unsigned char nul[] = { 0x1B, '[', '2', 0x00, 0x00, 'J' }; int k;
      for (k = 0; k < 6; ++k) ti_process(&ctx, nul[k]); }
    CHECK(ch_at(4, 0) == ' ', "NUL en cours de commande : ignore (CSI 2 J effectue)");
    feed("\x1B[1\x0D");
    CHECK(ctx.state == TI_STATE_NORMAL && ctx.cur_y == 5 && ctx.cur_x == 0, "C0 en cours de CSI : resynchronisation, CR execute");

    /* --- rouleau / page --- */
    ti_init(&ctx);
    feed("\x1B[24;1Hfin");
    CHECK(ctx.cur_y == 24 && ch_at(24, 0) == 'f', "CUP 24;1");
    feed("\x0A");
    CHECK(ctx.cur_y == 24 && ch_at(23, 0) == 'f' && ch_at(24, 0) == ' ', "LF en rangee 24, rouleau : montee d'une rangee");
    feed("\x1B[1;1H\x1BM");
    CHECK(ctx.cur_y == 1 && ch_at(24, 0) == 'f' && ch_at(23, 0) == ' ', "RI en rangee 1, rouleau : descente");
    feed("\x1B[<4h\x1B[24;5H\x0A");
    CHECK(ctx.roll == 0 && ctx.cur_y == 1 && ctx.cur_x == 4, "mode page (STUM 2) : LF en 24 -> rangee 1, colonne conservee");
    feed("\x1BM");
    CHECK(ctx.cur_y == 24, "mode page : RI en 1 -> rangee 24");
    feed("\x1B[<4l");
    CHECK(ctx.roll == 1, "retour rouleau");

    /* --- CSI deplacements et bornes --- */
    ti_init(&ctx);
    feed("\x1B[5;10H");
    CHECK(ctx.cur_y == 5 && ctx.cur_x == 9, "CUP 5;10");
    feed("\x1B[2A\x1B[3C");
    CHECK(ctx.cur_y == 3 && ctx.cur_x == 12, "CUU 2, CUF 3");
    feed("\x1B[A\x1B[D");
    CHECK(ctx.cur_y == 2 && ctx.cur_x == 11, "CUU, CUB par defaut 1");
    feed("\x1B[99A\x1B[99D\x1B[99B\x1B[99C");
    CHECK(ctx.cur_y == 24 && ctx.cur_x == 79, "arret aux bords");
    feed("\x1B[H");
    CHECK(ctx.cur_y == 1 && ctx.cur_x == 0, "CUP sans parametre : home");
    feed("\x1B[0;0H");
    CHECK(ctx.cur_y == 1 && ctx.cur_x == 0, "CUP 0;0 = home");

    /* --- effacements --- */
    ti_init(&ctx);
    for (i = 1; i < 25; ++i) { char b[16]; sprintf(b, "\x1B[%d;1H", i); feed(b); feed("ABCDEFGH"); }
    feed("\x1B[10;4H\x1B[K");
    CHECK(ch_at(10, 2) == 'C' && ch_at(10, 3) == ' ' && ch_at(10, 7) == ' ', "EL 0 : du curseur inclus a la fin");
    feed("\x1B[11;4H\x1B[1K");
    CHECK(ch_at(11, 3) == ' ' && ch_at(11, 0) == ' ' && ch_at(11, 4) == 'E', "EL 1 : du debut au curseur inclus");
    feed("\x1B[12;4H\x1B[2K");
    CHECK(ch_at(12, 0) == ' ' && ch_at(12, 7) == ' ' && ctx.cur_x == 3, "EL 2 : rangee entiere, curseur inchange");
    feed("\x1B[20;4H\x1B[J");
    CHECK(ch_at(20, 2) == 'C' && ch_at(20, 3) == ' ' && ch_at(24, 0) == ' ' && ch_at(19, 0) == 'A', "ED 0 : fin de page");
    feed("\x1B[3;4H\x1B[1J");
    CHECK(ch_at(1, 0) == ' ' && ch_at(3, 3) == ' ' && ch_at(3, 4) == 'E', "ED 1 : debut de page");
    feed("\x1B[2J");
    CHECK(ch_at(5, 0) == ' ' && ctx.cur_y == 3, "ED 2 : tout, curseur inchange");

    /* --- insertion / suppression --- */
    ti_init(&ctx);
    feed("ABCDEF\x1B[1;3H\x1B[2P");
    CHECK(ch_at(1, 0) == 'A' && ch_at(1, 1) == 'B' && ch_at(1, 2) == 'E' && ch_at(1, 3) == 'F' && ch_at(1, 4) == ' ', "DCH 2");
    feed("\x1B[2@");
    CHECK(ch_at(1, 2) == ' ' && ch_at(1, 3) == ' ' && ch_at(1, 4) == 'E' && ch_at(1, 5) == 'F', "ICH 2");
    feed("\x1B[4hXY\x1B[4l");
    CHECK(ch_at(1, 2) == 'X' && ch_at(1, 3) == 'Y' && ch_at(1, 4) == ' ' && ch_at(1, 6) == 'E' && ctx.insert == 0, "SM4 : insertion, RM4");
    feed("\x1B[2;1Hligne2\x1B[3;1Hligne3\x1B[2;1H\x1B[L");
    CHECK(ch_at(2, 0) == ' ' && ch_at(3, 0) == 'l' && ch_at(3, 5) == '2' && ch_at(4, 5) == '3' && ctx.cur_x == 0, "IL : rangee inseree");
    feed("\x1B[2;5H\x1B[2M");
    CHECK(ch_at(2, 5) == '3' && ch_at(3, 0) == ' ' && ctx.cur_x == 0, "DL 2 : rangees supprimees, colonne 1");

    /* --- attributs --- */
    ti_init(&ctx);
    feed("\x1B[1ma\x1B[4mb\x1B[5mc\x1B[7md\x1B[22me\x1B[24;25;27mf\x1B[0mg");
    CHECK(at_at(1, 0) == TI_ATTR_BOLD, "CSI 1 m : surintensite");
    CHECK(at_at(1, 1) == (TI_ATTR_BOLD | TI_ATTR_UNDERLINE), "CSI 4 m : souligne (cumul)");
    CHECK((at_at(1, 3) & TI_ATTR_INVERSE) && (at_at(1, 3) & TI_ATTR_BLINK), "CSI 5, 7 m");
    CHECK(!(at_at(1, 4) & TI_ATTR_BOLD) && (at_at(1, 4) & TI_ATTR_UNDERLINE), "CSI 22 m : intensite normale");
    CHECK(at_at(1, 5) == (TI_ATTR_BLINK | TI_ATTR_INVERSE), "CSI 24;25;27 : seul le premier parametre est interprete (Telic/Matra)");
    CHECK(at_at(1, 6) == 0, "CSI 0 m : aucun attribut");

    /* --- contexte ESC 7 / ESC 8, ESC c --- */
    ti_init(&ctx);
    feed("\x1B[5;6H\x1B[4m\x0E\x1B" "7\x1B[1;1H\x1B[0m\x0F\x1B" "8");
    CHECK(ctx.cur_y == 5 && ctx.cur_x == 5 && ctx.attr == TI_ATTR_UNDERLINE && ctx.shift == 1, "ESC 7 / ESC 8");
    ti_init(&ctx);
    feed("\x1B" "8");
    CHECK(ctx.cur_y == 1 && ctx.cur_x == 0 && ctx.attr == 0, "ESC 8 sans memorisation : (1,1), sans attribut");
    feed("\x1F\x40\x41top\x0A\x1B[4mZ\x1B" "c");
    CHECK(ch_at(0, 0) == ' ' && ch_at(1, 0) == ' ' && ctx.attr == 0 && ctx.cols == 80 && ctx.req_format == 1,
          "ESC c : etat initial, rangee 00 effacee");

    /* --- filtrage --- */
    ti_init(&ctx);
    feed("\x1B[3z\x1B" "Q\x1B[?9hA");
    CHECK(ch_at(1, 0) == 'A' && ctx.cur_x == 1, "sequences inconnues filtrees");

    /* --- rangee 00 --- */
    ti_init(&ctx);
    feed("\x1B[7;3H\x1B[7m\x0E");
    feed("\x1F\x40\x43INFO\x1B\x47\x0E\x21\x0F\x19\x42" "e\x12\x43\x0A");
    CHECK(ch_at(0, 2) == 'I' && ch_at(0, 5) == 'O' && at_at(0, 2) == 0, "US 4/0 4/3 : rangee 00 colonne 3, sans attribut");
    CHECK(ch_at(0, 6) == ' ' && ch_at(0, 7) == 'e' && ch_at(0, 8) == 'e' && ch_at(0, 10) == 'e',
          "rangee 00 : ESC+1 avale, SO -> espace, SS2 accent, REP");
    CHECK(ctx.state == TI_STATE_NORMAL && ctx.cur_y == 7 && ctx.cur_x == 2 && ctx.attr == TI_ATTR_INVERSE && ctx.shift == 1,
          "LF : retour avec position, attributs et jeu");
    feed("\x1F\x40\x41" "a\x1F\x40\x50" "b\x0A");
    CHECK(ch_at(0, 0) == 'a' && ch_at(0, 15) == 'b' && ctx.cur_y == 7, "nouvel acces US dans la rangee 00");
    feed("\x1F\x41\x41Q");
    CHECK(ch_at(7, 2) == 'A' && ch_at(7, 3) == 'Q', "US autre que 4/0 : US et son octet filtres");

    /* --- CSI ? { , CSI 6 n --- */
    feed("\x1B[?{");
    CHECK(ctx.req_videotex == 1, "CSI ? { : demande de retour Videotex");
    host_tx_reset();
    term_set_model(TERM_MINITEL_2);
    feed("\x1B[12;7H\x1B[6n");
    CHECK(host_tx_len == 7 && memcmp(host_tx, "\x1B[12;7R", 7) == 0, "CSI 6 n -> CSI 12;7 R (Minitel 2)");
    host_tx_reset();
    term_set_model(TERM_MINITEL_1B);
    feed("\x1B[6n");
    CHECK(host_tx_len == 0, "CSI 6 n : sans reponse sur 1B");

    /* --- jeux complementaire / DEC (STUM 2 annexes 3.12 / 3.13), curseur --- */
    term_set_model(TERM_MINITEL_2);
    ti_init(&ctx);
    feed("\x1B(3\x41\x1B)0\x0E\x6A\x0F\x1B(BA\x1B(R#\x1B)R\x0E#\x0F");
    CHECK(TI_ATTR_SET(at_at(1, 0)) == TI_SET_COMP && ch_at(1, 0) == 0x41, "ESC ( 3 : G0 = complementaire (4/1 = a grave)");
    CHECK(TI_ATTR_SET(at_at(1, 1)) == TI_SET_DEC && ch_at(1, 1) == 0x6A, "ESC ) 0 puis SO : G1 = DEC (6/A = coin)");
    CHECK(TI_ATTR_SET(at_at(1, 2)) == TI_SET_US && TI_ATTR_SET(at_at(1, 3)) == TI_SET_FR && TI_ATTR_SET(at_at(1, 4)) == TI_SET_FR,
          "ESC ( B / ESC ( R / ESC ) R : americain, francais");
    display80_compose_row(&ctx, 1);
    {
        const unsigned char* g = &font80[font80_comp_index[0x41 - 0x20] * FONT80_H];
        const unsigned char* d = &font80[font80_dec_index[0x6A - 0x20] * FONT80_H];
        ok = 1;
        for (i = 0; i < 8; ++i) { if (px(i, 6) != ((g[6] >> (7 - i)) & 1)) ok = 0; if (px(9 + i, 6) != ((d[6] >> (7 - i)) & 1)) ok = 0; }
        CHECK(ok && g != &font80[('A' - 0x20) * FONT80_H], "rendu : glyphes du jeu complementaire et DEC");
    }
    CHECK(font80_offset[FONT80_COUNT - 1] == (FONT80_COUNT - 1) * FONT80_H, "font80 : FONT80_COUNT coherent avec le generateur");
    feed("\x1B[<1h");
    CHECK(ctx.cur_visible == 0, "CSI < 1 h : curseur eteint (Minitel 2)");
    feed("\x1B[<1l");
    CHECK(ctx.cur_visible == 1, "CSI < 1 l : curseur allume");
    term_set_model(TERM_MINITEL_1B);
    ti_init(&ctx);
    feed("\x1B(3A\x1B[<1hB");
    CHECK(TI_ATTR_SET(at_at(1, 0)) == TI_SET_US && ctx.cur_visible == 1 && ch_at(1, 1) == 'B', "1B : jeu DEC/complementaire et extinction du curseur ignores");

    /* --- formats 40 / 80 (STUM 2) --- */
    ti_init(&ctx);
    feed("abc\x1B[<3h");
    CHECK(ctx.cols == 40 && ch_at(1, 0) == ' ' && ctx.cur_x == 0 && ctx.req_format == 1, "CSI < 3 h : 40 colonnes, ecran reinitialise");
    ctx.req_format = 0;
    feed("\x1B[7mX");
    CHECK(at_at(1, 0) == 0, "40 colonnes : attributs filtres");
    for (i = 0; i < 40; ++i) ti_process(&ctx, 'y');
    CHECK(ctx.cur_y == 2, "40 colonnes : passage a la rangee suivante apres 40");
    feed("\x1B[?3l");
    CHECK(ctx.cols == 80 && ctx.req_format == 1, "CSI ? 3 l : 80 colonnes");

    /* --- rendu --- */
    ti_init(&ctx);
    g_blink_phase = 0;
    feed("A\x1B[7mB\x1B[0m\x1B[4mC\x1B[0m\x1B[1mD\x1B[0m\x1B[5mE\x1B[0m\x0E#\x0F");
    display80_compose_row(&ctx, 1);
    {
        const unsigned char* gA = &font80[('A' - 0x20) * FONT80_H];
        ok = 1;
        for (i = 0; i < 8; ++i) if (px(i, 3) != ((gA[3] >> (7 - i)) & 1)) ok = 0;
        if (px(8, 3) != 0) ok = 0;
        CHECK(ok, "glyphe A en colonne 0, 9e colonne vide");
        /* B inverse en colonne 1 : pixels de la 9e colonne allumes, glyphe inverse */
        ok = 1;
        for (i = 0; i < 9; ++i) {
            int g = (i < 8) ? (font80[('B' - 0x20) * FONT80_H + 3] >> (7 - i)) & 1 : 0;
            if (px(9 + i, 3) != !g) ok = 0;
        }
        CHECK(ok, "inversion : 9 pixels inverses a la position col*9");
        ok = 1;
        for (i = 0; i < 9; ++i) if (px(18 + i, 13) != 1) ok = 0;
        CHECK(ok, "souligne : 14e ligne pleine");
        {
            unsigned char gD = font80[('D' - 0x20) * FONT80_H + 5];
            unsigned char bold = gD | (gD >> 1);
            ok = 1;
            for (i = 0; i < 8; ++i) if (px(27 + i, 5) != ((bold >> (7 - i)) & 1)) ok = 0;
            CHECK(ok, "gras : double frappe");
        }
        CHECK(px(36 + 2, 3) == ((font80[('E' - 0x20) * FONT80_H + 3] >> 5) & 1), "clignotant phase 0 : visible");
        {
            const unsigned char* gl = &font80[font80_fr_index['#' - 0x20] * FONT80_H];
            ok = 1;
            for (i = 0; i < 8; ++i) if (px(45 + i, 6) != ((gl[6] >> (7 - i)) & 1)) ok = 0;
            CHECK(ok, "jeu francais : # rendu avec le glyphe livre");
        }
        /* curseur : tiret sur la 14e ligne de la cellule courante (col 6) */
        ok = 1;
        for (i = 0; i < 9; ++i) if (px(54 + i, 13) != 1) ok = 0;
        CHECK(ok && px(54, 12) == 0, "curseur : tiret sur la derniere ligne");
    }
    g_blink_phase = 1;
    display80_compose_row(&ctx, 1);
    ok = 1;
    for (i = 0; i < 9; ++i) if (px(36 + i, 3) != 0) ok = 0;
    CHECK(ok && px(54, 13) == 0, "phase 1 : clignotant eteint, curseur eteint");
    g_blink_phase = 0;
    /* pixel hors de toute cellule : le tampon reste vide */
    CHECK(px(700, 0) == 0 && px(719, 13) == 0, "fin de ligne vide");

    /* 40 colonnes : pixels doubles a col*18 */
    ti_init(&ctx);
    feed("\x1B[<3h" "\x1B[1;2HA");
    display80_compose_row(&ctx, 1);
    {
        const unsigned char* gA = &font80[('A' - 0x20) * FONT80_H];
        ok = 1;
        for (i = 0; i < 16; ++i) if (px(18 + i, 3) != ((gA[3] >> (7 - i / 2)) & 1)) ok = 0;
        if (px(34, 3) || px(35, 3)) ok = 0;
        CHECK(ok, "40 colonnes : pixels doubles a col*18, deux pixels de fond");
    }

    /* rendu complet et budget */
    ti_init(&ctx);
    host_blits = 0;
    display80_render_all(&ctx);
    CHECK(host_blits == 25, "render_all : 25 rangees");
    feed("x");
    host_blits = 0;
    display80_render(&ctx);
    CHECK(host_blits == 1 && display80_dirty_pending(&ctx) == 0, "render : une rangee sale");
    feed("\x1B[5;1H");
    display80_render(&ctx); display80_render(&ctx);
    CHECK(display80_dirty_pending(&ctx) == 0, "curseur deplace : ancienne et nouvelle rangees re-rendues");
    {
        unsigned char* vram = &host_vram[0][0];
        CHECK(vram[5 * D80_ROWBYTES + 13 * D80_STRIDE] == 0xFF && vram[1 * D80_ROWBYTES + 13 * D80_STRIDE] != 0xFF,
              "VRAM : tiret du curseur en rangee 5, plus en rangee 1");
    }
    display80_blink_toggle(&ctx);
    CHECK(ctx.dirty[5] == 1 && g_blink_phase == 1, "blink_toggle : rangee du curseur salie");
    g_blink_phase = 0;

    /* mode 1 refuse (firmware amont) */
    host_mode_refuse = 1;
    CHECK(display80_init() == 0, "display80_init : firmware sans mode 1 -> 0");
    host_mode_refuse = 0;
    CHECK(display80_init() == 1 && host_mode == 1, "display80_init : mode 1");
    display80_leave();
    CHECK(host_mode == 0, "display80_leave : mode 0");

    printf("test_teleinfo : %d/%d OK\n", pass, run);
    return pass == run ? 0 : 1;
}
