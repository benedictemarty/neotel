/**
 * @file render_page.c
 * @brief Rendu hote d'une page Videotex avec le moteur de NeoTel (oracle)
 *
 *   render_page [--m2] [--mixte] PAGE.vdt SORTIE0.ppm SORTIE1.ppm
 *       (--m2 : profil Minitel 2, DRCS ; --mixte : la page bascule en mode
 *       Mixte, sortie 720x350 monochrome de l'ecran 80 colonnes) decode PAGE.vdt (flux Videotex brut, comme envoye par le serveur),
 *       rend la page (y 0-224) avec display.c + fonts.c en C (chemin hote),
 *       palette couleur, et ecrit deux PPM : phase de clignotement 0 et 1.
 *       La zone de statut (y >= 225) est laissee noire.
 *   render_page --screen-offset
 *       affiche offsetof(vtx_context_t, screen) (pour lire un dump RAM).
 *
 * tests/run.sh compare la capture Phosphoneo (rendu ASSEMBLEUR sur cible) a
 * ces images : c'est la preuve que display_asm.s et le C font la meme chose.
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "display.h"
#include "videotex.h"
#include "terminal.h"
#include "teleinfo.h"
#include "display80.h"
#include "neo_stub.h"

unsigned char g_blink_phase;
unsigned char g_global_mask = 1;
void serial_send(unsigned char b) { (void)b; }
void serial_tx_flush(void) {}

static vtx_context_t ctx;
static ti_context_t ti;
static int mixte;

/* Meme aiguillage que main.c : sequences Protocole au decodeur Videotex, le
 * reste a l'ecran 80 colonnes, bascule au moment de PRO2 MIXTE 1. */
static unsigned char pro_pending, pro_left, in80;
static void feed_byte(unsigned char b)
{
    b &= 0x7F;
    if (!in80) {
        vtx_process(&ctx, b);
        if (ctx.terminal_mode == TERM_MODE_MIXED) { in80 = 1; ti_init(&ti); }
        return;
    }
    if (pro_left) { vtx_process(&ctx, b); --pro_left; return; }
    if (pro_pending) {
        pro_pending = 0;
        if (b >= 0x39 && b <= 0x3B) { vtx_process(&ctx, 0x1B); vtx_process(&ctx, b); pro_left = (unsigned char)(b - 0x38); return; }
        ti_process(&ti, 0x1B); ti_process(&ti, b); return;
    }
    if (b == 0x1B) { pro_pending = 1; return; }
    ti_process(&ti, b);
}

static int write_ppm80(const char* path)
{
    FILE* f = fopen(path, "wb");
    int x, y;
    static const unsigned char on[3] = { 255, 255, 255 }, off[3] = { 0, 0, 0 };
    if (!f) return 1;
    fprintf(f, "P6\n720 350\n255\n");
    for (y = 0; y < 350; ++y)
        for (x = 0; x < 720; ++x)
            fwrite((((&host_vram[0][0])[y * 90 + (x >> 3)] >> (7 - (x & 7))) & 1) ? on : off, 1, 3, f);
    fclose(f);
    return 0;
}

static const unsigned char pal[8][3] = {
    { 0, 0, 0 }, { 255, 0, 0 }, { 0, 255, 0 }, { 255, 255, 0 },
    { 0, 0, 255 }, { 255, 0, 255 }, { 0, 255, 255 }, { 255, 255, 255 },
};

static int write_ppm(const char* path)
{
    FILE* f = fopen(path, "wb");
    int x, y;
    if (!f) return 1;
    fprintf(f, "P6\n320 240\n255\n");
    for (y = 0; y < 240; ++y) {
        for (x = 0; x < 320; ++x) {
            unsigned char c = (y < PAGE_H) ? (host_vram[y][x] & 7) : 0;
            fwrite(pal[c], 1, 3, f);
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char** argv)
{
    FILE* f;
    int c;

    if (argc == 2 && strcmp(argv[1], "--screen-offset") == 0) {
        printf("%u\n", (unsigned)offsetof(vtx_context_t, screen));
        return 0;
    }
    while (argc > 4) {
        if (strcmp(argv[1], "--m2") == 0) term_set_model(TERM_MINITEL_2);
        else if (strcmp(argv[1], "--mixte") == 0) mixte = 1;
        else break;
        ++argv; --argc;
    }
    if (argc != 4) {
        fprintf(stderr, "usage : render_page [--m2] PAGE.vdt PHASE0.ppm PHASE1.ppm | --screen-offset\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }

    vtx_init(&ctx);
    display_init();
    while ((c = fgetc(f)) != EOF) {
        if (mixte) feed_byte((unsigned char)c); else vtx_process(&ctx, (unsigned char)c);
    }
    fclose(f);

    g_blink_phase = 0;
    if (mixte) {
        if (!in80) { fprintf(stderr, "render_page : la page n'est pas passee en mode Mixte\n"); return 1; }
        display80_init();
        ti.full_refresh = 1;
        display80_render_all(&ti);
        if (write_ppm80(argv[2])) return 1;
        display80_blink_toggle(&ti);
        display80_render_all(&ti);
        if (write_ppm80(argv[3])) return 1;
        return 0;
    }
    ctx.full_refresh = 1;
    display_render_all(&ctx);
    if (write_ppm(argv[2])) return 1;

    display_blink_toggle(&ctx);
    display_render_all(&ctx);
    if (write_ppm(argv[3])) return 1;
    return 0;
}
