/**
 * @file fuzz_teleinfo.c
 * @brief Fuzzer libFuzzer du decodeur 80 colonnes + rendu (teleinfo.c, display80.c)
 *
 *   make -C tests/host fuzz_teleinfo && tests/host/fuzz_teleinfo -max_total_time=30
 *
 * Chaque entree est un flux d'octets arbitraire, alternativement en profil
 * Minitel 1B et Minitel 2 ; apres decodage, chaque rangee est composee
 * (chemin C de l'oracle) pour attraper les debordements de tampon.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "teleinfo.h"
#include "display80.h"
#include "terminal.h"
#include "neo_stub.h"

unsigned char g_blink_phase;
unsigned char g_global_mask = 1;
unsigned char display_rowbuf[2880];

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static ti_context_t ctx;
    size_t i;
    unsigned char r;
    term_set_model((size & 1) ? TERM_MINITEL_2 : TERM_MINITEL_1B);
    ti_init(&ctx);
    for (i = 0; i < size; ++i) ti_process(&ctx, data[i]);
    g_blink_phase = (unsigned char)(size & 2 ? 1 : 0);
    for (r = 0; r < TI_ROWS; ++r) display80_compose_row(&ctx, r);
    return 0;
}
