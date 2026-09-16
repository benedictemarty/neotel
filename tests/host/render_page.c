/**
 * @file render_page.c
 * @brief Rendu hote d'une page Videotex avec le moteur de NeoTel (oracle)
 *
 *   render_page [--m2] PAGE.vdt SORTIE0.ppm SORTIE1.ppm
 *       (--m2 : profil Minitel 2, DRCS) decode PAGE.vdt (flux Videotex brut, comme envoye par le serveur),
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
#include "neo_stub.h"

unsigned char g_blink_phase;
unsigned char g_global_mask = 1;
void serial_send(unsigned char b) { (void)b; }
void serial_tx_flush(void) {}

static vtx_context_t ctx;

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
    if (argc == 5 && strcmp(argv[1], "--m2") == 0) {
        term_set_model(TERM_MINITEL_2);
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
        vtx_process(&ctx, (unsigned char)c);
    }
    fclose(f);

    g_blink_phase = 0;
    ctx.full_refresh = 1;
    display_render_all(&ctx);
    if (write_ppm(argv[2])) return 1;

    display_blink_toggle(&ctx);
    display_render_all(&ctx);
    if (write_ppm(argv[3])) return 1;
    return 0;
}
