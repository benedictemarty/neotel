/**
 * @file test_drcs.c
 * @brief Tests hote des jeux DRCS du Minitel 2 (STUM 2, chapitre L'ecran par. 2.2-2.5)
 *
 * Reference : docs/ref/STUM2-NOTES.md. L'exemple du par. 2.3.5 (forme en 5/3
 * du jeu G'1, 14 octets 4/4 4/3 6/0 5/0 4/4 4/1 4/0 6/8 5/1 4/4 5/0 6/8 4/4 4/X)
 * sert de vecteur : rangees attendues 10 38 10 10 10 28 44 44 28 10.
 *
 * Couvre : en-tete G'0/G'1 (valide, erronee, resync C0), transfert (B1,
 * 14 octets, formes successives, B1 anticipe, octets excedentaires filtres,
 * code > 7/E ignore, C0 et colonnes 2-3 = fond), sortie par US, associations
 * ESC 2/8|2/9, mapping des cellules (2/0 et 7/F restent au jeu de base),
 * acces rangee 00 = jeux de base, SS2 en G'0 et ignore en G1, profil 1B
 * (ignore), CSI 6n, rendu display (8x10 -> 8x9), vtx_init efface tout.
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "videotex.h"
#include "terminal.h"
#include "display.h"
#include "neo_stub.h"

unsigned char g_blink_phase;
unsigned char g_global_mask = 1;

static int run, pass;
#define CHECK(c, name) do { ++run; if (c) ++pass; else printf("FAIL : %s (ligne %d)\n", name, __LINE__); } while (0)

static vtx_context_t ctx;

static void feed(const unsigned char* d, int n) { while (n-- > 0) vtx_process(&ctx, *d++); }
#define FEED(a) feed((a), (int)sizeof(a))

static const unsigned char HDR_G1[] = { 0x1F, 0x23, 0x20, 0x20, 0x20, 0x43, 0x49 };
static const unsigned char HDR_G0[] = { 0x1F, 0x23, 0x20, 0x20, 0x20, 0x42, 0x49 };
static const unsigned char FORM53[] = { 0x44, 0x43, 0x60, 0x50, 0x44, 0x41, 0x40, 0x68, 0x51, 0x44, 0x50, 0x68, 0x44, 0x40 };
static const unsigned char EXPECT[10] = { 0x10, 0x38, 0x10, 0x10, 0x10, 0x28, 0x44, 0x44, 0x28, 0x10 };
static const unsigned char ASSOC_G1_DRCS[] = { 0x1B, 0x29, 0x20, 0x43 };
static const unsigned char ASSOC_G1_BASE[] = { 0x1B, 0x29, 0x63 };
static const unsigned char ASSOC_G0_DRCS[] = { 0x1B, 0x28, 0x20, 0x42 };
static const unsigned char ASSOC_G0_BASE[] = { 0x1B, 0x28, 0x40 };

/* US 2/3 5/3 puis B1 + forme */
static void xfer_53(void)
{
    static const unsigned char start[] = { 0x1F, 0x23, 0x53, 0x30 };
    FEED(start);
    FEED(FORM53);
}

static void fresh(void)
{
    term_set_model(TERM_MINITEL_2);
    vtx_init(&ctx);
    ctx.cur_visible = 0;
}

int main(void)
{
    const unsigned char* f;
    unsigned char pat[CELL_H], fg, bg;
    int i;

    /* --- exemple STUM 2 par. 2.3.5 --- */
    fresh();
    FEED(HDR_G1);
    CHECK(ctx.state == VTX_STATE_NORMAL && ctx.drcs_hdr_set == 1, "en-tete G'1 reconnue");
    xfer_53();
    CHECK(ctx.state == VTX_STATE_DRCS_XFER && ctx.drcs_nbyte == 14 && ctx.drcs_nrow == 10, "14 octets -> 10 rangees");
    vtx_process(&ctx, 0x1F); vtx_process(&ctx, 0x41); vtx_process(&ctx, 0x41);   /* US 4/1 4/1 : sortie */
    CHECK(ctx.state == VTX_STATE_NORMAL && ctx.cur_y == 1 && ctx.cur_x == 0, "sortie par US : le US X est interprete");
    f = vtx_drcs_form(&ctx, 1, 0x53);
    CHECK(f && memcmp(f, EXPECT, 10) == 0, "forme 5/3 de G'1 conforme a l'exemple (10 38 10 10 10 28 44 44 28 10)");
    CHECK(vtx_drcs_form(&ctx, 0, 0x53)[0] == 0, "G'0 non touche");
    CHECK(vtx_drcs_form(&ctx, 1, 0x20) == 0 && vtx_drcs_form(&ctx, 1, 0x7F) == 0, "2/0 et 7/F non telechargeables");

    /* --- formes successives, B1 anticipe, excedent filtre --- */
    fresh();
    FEED(HDR_G1);
    {
        static const unsigned char seq[] = {
            0x1F, 0x23, 0x41,               /* Y = 4/1 */
            0x30, 0x7F, 0x7F,               /* forme 4/1 : 2 octets puis B1 anticipe */
            0x30, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7F,   /* forme 4/2 : 13 x 0 + 1 */
            0x7F, 0x7F, 0x7F,               /* excedent : filtre */
            0x30, 0x1B, 0x41, 0x21, 0x7F,   /* forme 4/3 : ESC, 4/1, 2/1 = fond, puis 7/F */
            0x1F, 0x41, 0x41 };             /* sortie par US rangee 1 */
        FEED(seq);
    }
    f = vtx_drcs_form(&ctx, 1, 0x41);
    CHECK(f[0] == 0xFF && f[1] == 0xF0 && f[2] == 0 && f[9] == 0, "B1 anticipe : 12 bits d'encre puis fond");
    f = vtx_drcs_form(&ctx, 1, 0x42);
    CHECK(f[9] == 0x03 && f[8] == 0x00, "14e octet : seuls b5,b4 comptent (rangee 10 = 0000 0011)");
    f = vtx_drcs_form(&ctx, 1, 0x43);
    CHECK(f[0] == 0x00 && f[1] == 0x10 && f[2] == 0x3F && f[3] == 0x00, "ESC / 2/1 = fond (pas de resync), 4/1 et 7/F = donnees");
    CHECK(ctx.state == VTX_STATE_NORMAL, "sortie propre");

    /* --- rangee 00 : suspension, reprise sur LF, abandon sur FF (par. 2.3.4) --- */
    fresh();
    FEED(HDR_G1);
    {
        static const unsigned char part1[] = { 0x1F, 0x23, 0x53, 0x30, 0x44, 0x43, 0x60, 0x50, 0x44, 0x41, 0x40 };
        static const unsigned char row0[]  = { 0x1F, 0x40, 0x41, 'I', 'N', 'F', 'O', 0x0A };   /* rangee 00 puis LF */
        static const unsigned char part2[] = { 0x68, 0x51, 0x44, 0x50, 0x68, 0x44, 0x40, 0x1F, 0x41, 0x41 };
        FEED(part1);
        FEED(row0);
        CHECK(ctx.state == VTX_STATE_DRCS_XFER && ctx.drcs_suspended == 0 && ctx.screen[0][0].ch == 'I' && ctx.cur_y == 1,
              "rangee 00 : texte affiche, LF reprend le transfert");
        FEED(part2);
    }
    f = vtx_drcs_form(&ctx, 1, 0x53);
    CHECK(memcmp(f, EXPECT, 10) == 0, "forme complete apres interruption par la rangee 00");
    {   /* FF en rangee 00 : sortie sans completer -> forme 5/4 abandonnee */
        static const unsigned char abort_[] = { 0x1F, 0x23, 0x54, 0x30, 0x7F, 0x7F, 0x1F, 0x40, 0x41, 0x0C, 'A' };
        FEED(abort_);
    }
    CHECK(ctx.state == VTX_STATE_NORMAL && ctx.drcs_suspended == 0 && vtx_drcs_form(&ctx, 1, 0x54)[0] == 0
          && ctx.screen[1][0].ch == 'A', "FF en rangee 00 : telechargement abandonne, FF execute");
    {   /* US vers une autre rangee depuis la rangee 00 : sortie en completant */
        static const unsigned char cpl[] = { 0x1F, 0x23, 0x55, 0x30, 0x7F, 0x1F, 0x40, 0x41, 0x1F, 0x42, 0x41 };
        FEED(cpl);
    }
    CHECK(ctx.state == VTX_STATE_NORMAL && vtx_drcs_form(&ctx, 1, 0x55)[0] == 0xFC && ctx.cur_y == 2,
          "US rangee 00 puis US rangee 2 : forme completee, curseur place");
    {   /* en-tete interrompue par la rangee 00 puis reprise */
        static const unsigned char hdr[] = { 0x1F, 0x23, 0x20, 0x20, 0x1F, 0x40, 0x41, 0x0A, 0x20, 0x42, 0x49 };
        FEED(hdr);
    }
    CHECK(ctx.state == VTX_STATE_NORMAL && ctx.drcs_hdr_set == 0, "en-tete G'0 reprise apres la rangee 00");

    /* --- en-tete erronee : la precedente reste valide ; C0 resynchronise --- */
    fresh();
    FEED(HDR_G1);
    {
        static const unsigned char bad[] = { 0x1F, 0x23, 0x20, 0x20, 0x20, 0x43, 0x58 };   /* 5/8 au lieu de 4/9 */
        FEED(bad);
    }
    CHECK(ctx.drcs_hdr_set == 1 && ctx.state == VTX_STATE_NORMAL, "en-tete erronee ignoree, G'1 reste");
    {
        static const unsigned char c0[] = { 0x1F, 0x23, 0x20, 0x0C, 'A' };   /* FF en cours d'en-tete */
        ctx.cur_x = 5; ctx.cur_y = 5;
        FEED(c0);
    }
    CHECK(ctx.cur_y == 1 && ctx.screen[1][0].ch == 'A', "C0 en cours d'en-tete : resync, FF execute");
    FEED(HDR_G0);
    CHECK(ctx.drcs_hdr_set == 0, "en-tete G'0");
    {
        static const unsigned char big[] = { 0x1F, 0x23, 0x7E, 0x30, 0x7F, 0x30, 0x7F, 0x1F, 0x41, 0x41 };   /* 7/E puis 7/F (ignoree) */
        FEED(big);
    }
    CHECK(vtx_drcs_form(&ctx, 0, 0x7E)[0] == 0xFC, "forme 7/E chargee (6 bits)");

    /* --- associations et mapping des cellules --- */
    fresh();
    FEED(HDR_G1);
    xfer_53();
    vtx_process(&ctx, 0x1F); vtx_process(&ctx, 0x43); vtx_process(&ctx, 0x41);   /* US 4/3 4/1 */
    vtx_process(&ctx, 0x0E);                    /* SO : G1 */
    vtx_process(&ctx, 'S');                     /* 5/3 en G1 de base */
    FEED(ASSOC_G1_DRCS);
    vtx_process(&ctx, 'S'); vtx_process(&ctx, 0x20); vtx_process(&ctx, 0x7F);
    CHECK(ctx.screen[3][0].charset == CHARSET_G1, "avant association : G1 de base");
    CHECK(ctx.screen[3][1].charset == CHARSET_DRCS1 && ctx.screen[3][1].ch == 'S', "apres ESC 2/9 2/0 4/3 : cellule G'1");
    CHECK(ctx.screen[3][2].charset == CHARSET_G1 && ctx.screen[3][3].charset == CHARSET_G1, "2/0 et 7/F restent au jeu de base");
    FEED(ASSOC_G1_BASE);
    vtx_process(&ctx, 'S');
    CHECK(ctx.screen[3][4].charset == CHARSET_G1, "ESC 2/9 6/3 : retour au jeu de base");
    vtx_process(&ctx, 0x0F);                    /* SI */
    FEED(ASSOC_G0_DRCS);
    vtx_process(&ctx, 'a');
    CHECK(ctx.screen[3][5].charset == CHARSET_DRCS0 && ctx.screen[3][5].ch == 'a', "G'0 : pas de majuscule forcee, code conserve");
    {   /* SS2 accent X en G'0 -> forme X du DRCS */
        static const unsigned char ss2[] = { 0x19, 0x42, 'e' };
        FEED(ss2);
    }
    CHECK(ctx.screen[3][6].charset == CHARSET_DRCS0 && ctx.screen[3][6].ch == 'e', "SS2 <accent> X en G'0 : forme X");
    FEED(ASSOC_G0_BASE);
    vtx_process(&ctx, 'b');
    CHECK(ctx.screen[3][7].charset == CHARSET_G0 && ctx.screen[3][7].ch == 'B', "ESC 2/8 4/0 : jeu de base (majuscule forcee)");
    FEED(ASSOC_G0_DRCS); FEED(ASSOC_G1_DRCS);
    vtx_process(&ctx, 0x1F); vtx_process(&ctx, 0x40); vtx_process(&ctx, 0x41);   /* rangee 00 */
    CHECK(ctx.drcs_g0 == 0 && ctx.drcs_g1 == 0, "acces en rangee 00 : jeux de base reassocies");
    vtx_process(&ctx, 0x0E);
    {   /* SS2 en G1 : ignore, 'e' s'affiche en G1 */
        static const unsigned char ss2[] = { 0x19, 'e' };
        ctx.cur_x = 10; ctx.cur_y = 4;
        FEED(ss2);
    }
    CHECK(ctx.screen[4][10].charset == CHARSET_G1 && ctx.screen[4][10].ch == 'e', "SS2 ignore quand G1 est invoque (Minitel 2)");
    {   /* association inconnue : ignoree */
        static const unsigned char bad[] = { 0x1B, 0x28, 0x20, 0x55, 'Z' };
        vtx_process(&ctx, 0x0F);
        ctx.cur_x = 12; ctx.cur_y = 4;
        FEED(bad);
    }
    CHECK(ctx.state == VTX_STATE_NORMAL && ctx.screen[4][12].ch == 'Z' && ctx.drcs_g0 == 0, "ESC 2/8 2/0 5/5 : ignore");

    /* --- rendu 8x10 -> 8x9 --- */
    fresh();
    FEED(HDR_G1);
    xfer_53();
    vtx_process(&ctx, 0x1F); vtx_process(&ctx, 0x42); vtx_process(&ctx, 0x41);
    vtx_process(&ctx, 0x0E);
    FEED(ASSOC_G1_DRCS);
    vtx_process(&ctx, 'S');
    display_init();
    display_render_all(&ctx);
    display_cell_pattern(&ctx.screen[2][0], pat, &fg, &bg);
    CHECK(memcmp(pat, EXPECT, 8) == 0 && pat[8] == (EXPECT[8] | EXPECT[9]), "motif 8x9 : rangees 1-8 puis 9|10");
    for (i = 0; i < 8; ++i) {
        if (host_vram[2 * CELL_H + 1][i] != ((0x38 >> (7 - i)) & 1 ? VTX_WHITE : VTX_BLACK)) break;
    }
    CHECK(i == 8, "VRAM : rangee 2 de la forme (0011 1000) rendue");
    ctx.screen[2][0].flags = ATTR_UNDERLINE;
    display_cell_pattern(&ctx.screen[2][0], pat, &fg, &bg);
    CHECK(pat[8] == (EXPECT[8] | EXPECT[9]), "G'1 : le lignage n'a pas d'effet");
    ctx.screen[2][0].charset = CHARSET_DRCS0;
    display_cell_pattern(&ctx.screen[2][0], pat, &fg, &bg);
    CHECK(pat[0] == 0 && pat[8] == 0xFF, "G'0 vide + souligne : ligne 8 pleine");

    /* --- profil Minitel 1B : rien de tout cela --- */
    term_set_model(TERM_MINITEL_1B);
    vtx_init(&ctx);
    FEED(ASSOC_G0_DRCS);
    CHECK(ctx.drcs_g0 == 0 && ctx.state == VTX_STATE_NORMAL, "1B : ESC 2/8 ignore (un octet), pas d'association");
    vtx_init(&ctx);
    FEED(HDR_G1);
    CHECK(ctx.state != VTX_STATE_DRCS_HDR && ctx.state != VTX_STATE_DRCS_XFER, "1B : US 2/3 n'ouvre pas de telechargement");

    /* --- CSI 3/6 6/E : position curseur (Minitel 2 seulement) --- */
    fresh();
    host_tx_reset();
    vtx_process(&ctx, 0x1F); vtx_process(&ctx, 0x4C); vtx_process(&ctx, 0x4B);   /* rangee 12, colonne 10 */
    {
        static const unsigned char req[] = { 0x1B, 0x5B, '6', 'n' };
        FEED(req);
    }
    CHECK(host_tx_len == 8 && memcmp(host_tx, "\x1B[12;11R", 8) == 0, "CSI 6n -> CSI 12;11 R");
    term_set_model(TERM_MINITEL_1B);
    vtx_init(&ctx);
    host_tx_reset();
    {
        static const unsigned char req[] = { 0x1B, 0x5B, '6', 'n' };
        FEED(req);
    }
    CHECK(host_tx_len == 0, "1B : CSI 6n sans reponse");

    /* --- disposition partagee hote / cc65 (dumps RAM des tests cible) : aucun
     * bourrage possible, drcs_acc (short) a un offset pair --- */
    CHECK(offsetof(vtx_context_t, drcs_acc) % 2 == 0, "drcs_acc a un offset pair (pas de bourrage gcc)");
    CHECK(offsetof(vtx_context_t, screen) == 37 + 9 + 2 + DRCS_ROWS + 1 + 2 * DRCS_COUNT * DRCS_ROWS,
          "offset de screen = somme des champs (identique sur cc65)");

    /* --- vtx_init efface les formes --- */
    fresh();
    FEED(HDR_G1); xfer_53(); vtx_process(&ctx, 0x1F); vtx_process(&ctx, 0x41); vtx_process(&ctx, 0x41);
    vtx_init(&ctx);
    CHECK(vtx_drcs_form(&ctx, 1, 0x53)[1] == 0 && ctx.drcs_hdr_set == 0, "vtx_init : formes effacees, en-tete G'0 par defaut");

    printf("test_drcs : %d/%d OK\n", pass, run);
    return pass == run ? 0 : 1;
}
