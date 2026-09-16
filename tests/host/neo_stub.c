/**
 * @file neo_stub.c
 * @brief Neo6502 logiciel pour les tests hote : bloc $FF00, VRAM 320x240,
 *        clavier scripte, UART en memoire, base de temps.
 *
 * Fournit :
 *  - neo_regs[] / neo_host_dispatch() (neo.h en TEST_HOST) : groupes 1, 2,
 *    5, 8, 10, 12, 14 utilises par NeoTel ;
 *  - neo_gfx.h : gfx_* sur host_vram[][] et host_palette[][] ;
 *  - neo_time.h : timer pilote par le test (host_timer), delais comptes.
 */

#include <string.h>
#include "neo.h"
#include "neo_gfx.h"
#include "neo_time.h"
#include "neo_stub.h"

unsigned char neo_regs[16];

/* --- video ------------------------------------------------------------- */
unsigned char host_vram[240][320];
unsigned char host_palette[256][3];
int host_blits;                 /* nombre de blits (12,3) */
int host_fills;                 /* nombre de rectangles (5,3) */

void gfx_init(void)
{
    memset(host_vram, 0, sizeof host_vram);
}

void gfx_fill_rect(unsigned int x0, unsigned char y0,
                   unsigned int x1, unsigned char y1, unsigned char colour)
{
    unsigned int x; unsigned char y;
    ++host_fills;
    for (y = y0; y <= y1; ++y)
        for (x = x0; x <= x1; ++x)
            host_vram[y][x] = colour;
}

void gfx_set_palette(unsigned char idx, unsigned char r,
                     unsigned char g, unsigned char b)
{
    host_palette[idx][0] = r;
    host_palette[idx][1] = g;
    host_palette[idx][2] = b;
}

void gfx_blit(const unsigned char* src, unsigned int x, unsigned char y,
              unsigned int w, unsigned char h)
{
    unsigned char l;
    ++host_blits;
    for (l = 0; l < h; ++l) {
        memcpy(&host_vram[y + l][x], src + l * 320, w);
    }
}

/* --- temps -------------------------------------------------------------- */
unsigned long host_timer;       /* tics de 10 ms, pilote par le test */
unsigned long host_delay_ms;    /* somme des delais demandes */
int host_beeps;

void neo_beep(void) { ++host_beeps; }
void neo_delay_ms(unsigned int ms) { host_delay_ms += ms; host_timer += ms / 10; }

/* --- clavier scripte ----------------------------------------------------- */
static unsigned char kq[256];
static int kq_head, kq_tail;
unsigned char host_keys_down[256];      /* etat HID (1,2) */

void host_key_push(unsigned char ascii)
{
    kq[kq_tail++ & 0xFF] = ascii;
}

/* --- UART en memoire ----------------------------------------------------- */
static unsigned char rx[4096];
static int rx_head, rx_tail;
unsigned char host_tx[4096];
int host_tx_len;
unsigned char host_cdc_connected = 1;
unsigned char host_uart_route = 2;
unsigned long host_uart_baud;

void host_rx_push(const unsigned char* data, int n)
{
    while (n-- > 0) rx[rx_tail++ & 0xFFF] = *data++;
}

void host_tx_reset(void) { host_tx_len = 0; }

/* --- dispatch ----------------------------------------------------------- */
void neo_host_dispatch(void)
{
    unsigned char g = NEO_CMD, f = NEO_FN;
    NEO_ERR = 0;
    switch (g) {
    case NEO_G_SYSTEM:
        if (f == NEO_F_TIMER) {
            NEO_P[0] = (unsigned char)host_timer;
            NEO_P[1] = (unsigned char)(host_timer >> 8);
            NEO_P[2] = (unsigned char)(host_timer >> 16);
            NEO_P[3] = (unsigned char)(host_timer >> 24);
        } else if (f == NEO_F_KEY_STATUS) {
            NEO_P[0] = host_keys_down[NEO_P[0]];
            NEO_P[1] = 0;
        }
        break;
    case NEO_G_CONSOLE:
        if (f == NEO_F_READ_CHAR) {
            NEO_P[0] = (kq_head != kq_tail) ? kq[kq_head++ & 0xFF] : 0;
        } else if (f == NEO_F_CON_STATUS) {
            NEO_P[0] = (kq_head == kq_tail) ? 0xFF : 0x00;
        }
        break;
    case NEO_G_GRAPHICS:
    case NEO_G_BLITTER:
        /* les tests passent par neo_gfx.h, jamais par le bloc $FF00 */
        break;
    case NEO_G_SOUND:
        if (f == NEO_F_SND_BEEP) ++host_beeps;
        break;
    case NEO_G_UEXT:
        switch (f) {
        case NEO_F_UART_FORMAT:
            host_uart_baud = NEO_P[0] | ((unsigned long)NEO_P[1] << 8)
                           | ((unsigned long)NEO_P[2] << 16) | ((unsigned long)NEO_P[3] << 24);
            break;
        case NEO_F_UART_WRITE:
            if (host_tx_len < (int)sizeof host_tx) host_tx[host_tx_len++] = NEO_P[0];
            break;
        case NEO_F_UART_READ:
            if (rx_head != rx_tail) NEO_P[0] = rx[rx_head++ & 0xFFF];
            else NEO_ERR = 1;
            break;
        case NEO_F_UART_AVAIL:
            NEO_P[0] = (rx_head != rx_tail) ? 1 : 0;
            break;
        case NEO_F_UART_ROUTE:
            host_uart_route = NEO_P[0];
            break;
        default: break;
        }
        break;
    case NEO_G_CDC:
        if (f == NEO_F_CDC_STATUS) {
            if (host_cdc_connected == 0xFF) { NEO_ERR = 1; NEO_P[0] = 0; }  /* firmware amont */
            else { NEO_P[0] = host_cdc_connected; NEO_P[1] = 0; NEO_P[2] = 0; }
        }
        break;
    default:
        NEO_ERR = 1;
        break;
    }
    NEO_CMD = 0;
}
