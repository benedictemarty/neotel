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
#include "settings.h"

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

unsigned char host_mode;            /* mode video (5,9) ; 0xFF = 5,9 refuse (amont) */
unsigned char host_mode_refuse;

unsigned char gfx_set_mode(unsigned char mode)
{
    if (host_mode_refuse && mode != 0) return 0;
    host_mode = mode;
    return 1;
}

void gfx_blit_ex(const unsigned char* src, unsigned int src_stride,
                 unsigned long dst_off, unsigned int dst_stride,
                 unsigned int w, unsigned char h)
{
    unsigned char l;
    unsigned char* vram = &host_vram[0][0];
    ++host_blits;
    for (l = 0; l < h; ++l) {
        memcpy(vram + dst_off + (unsigned long)l * dst_stride, src + l * src_stride, w);
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

/* --- fichier unique (neotel.cfg) ---------------------------------------- */
unsigned char host_file[256];
int host_file_len;
unsigned char host_file_ro;
unsigned char* neo_host_ptr;

host_disk_file_t host_disk[HOST_FILES];
int host_open_count;
unsigned char host_disk_ro;
static struct { host_disk_file_t* f; int pos; } chan[8];
static int dir_open, dir_next;      /* enumeration 3,17-3,19 */

host_disk_file_t* host_disk_find(const char* name)
{
    int i;
    for (i = 0; i < HOST_FILES; ++i)
        if (host_disk[i].name[0] && strcmp(host_disk[i].name, name) == 0) return &host_disk[i];
    return 0;
}

static void disk_op(unsigned char f)
{
    unsigned char ch = NEO_P[0];
    int n = NEO_P[3] | (NEO_P[4] << 8);
    char name[32];
    host_disk_file_t* df;
    if (ch >= 8) { NEO_ERR = 1; return; }
    switch (f) {
    case 4:                                 /* open */
        if (chan[ch].f) { NEO_ERR = 1; return; }
        memcpy(name, neo_host_ptr + 1, neo_host_ptr[0]); name[neo_host_ptr[0]] = 0;
        df = host_disk_find(name);
        if (NEO_P[3] == 3) {
            if (host_disk_ro) { NEO_ERR = 1; return; }
            if (!df) { int i; for (i = 0; i < HOST_FILES && host_disk[i].name[0]; ++i) ; if (i == HOST_FILES) { NEO_ERR = 1; return; } df = &host_disk[i]; strcpy(df->name, name); }
            df->len = 0;
        } else if (!df) { NEO_ERR = 1; return; }
        chan[ch].f = df; chan[ch].pos = 0; ++host_open_count;
        return;
    case 5:                                 /* close */
        if (!chan[ch].f) { NEO_ERR = 1; return; }
        chan[ch].f = 0; --host_open_count;
        return;
    case 17:                                /* open directory (P0,P1 -> nom) */
        dir_open = 1; dir_next = 0;
        return;
    case 18: {                              /* read directory */
        int k, len;
        if (!dir_open) { NEO_ERR = 1; return; }
        while (dir_next < HOST_FILES && !host_disk[dir_next].name[0]) ++dir_next;
        if (dir_next >= HOST_FILES) { NEO_ERR = 1; return; }
        len = (int)strlen(host_disk[dir_next].name);
        if (len > neo_host_ptr[0]) len = neo_host_ptr[0];   /* capacite (3,18) */
        neo_host_ptr[0] = (unsigned char)len;
        for (k = 0; k < len; ++k) neo_host_ptr[1 + k] = (unsigned char)host_disk[dir_next].name[k];
        NEO_P[2] = (unsigned char)(host_disk[dir_next].len & 0xFF);
        NEO_P[3] = (unsigned char)((host_disk[dir_next].len >> 8) & 0xFF);
        NEO_P[4] = 0; NEO_P[5] = 0; NEO_P[6] = 0;   /* attributs : fichier */
        ++dir_next;
        return;
    }
    case 19:                                /* close directory */
        dir_open = 0;
        return;
    case 8:                                 /* read */
        df = chan[ch].f;
        if (!df) { NEO_ERR = 1; return; }
        if (n > df->len - chan[ch].pos) n = df->len - chan[ch].pos;
        memcpy(neo_host_ptr, df->data + chan[ch].pos, n);
        chan[ch].pos += n;
        NEO_P[3] = (unsigned char)n; NEO_P[4] = (unsigned char)(n >> 8);
        if (n == 0) NEO_ERR = 1;            /* FIOERROR_EOF */
        return;
    case 9:                                 /* write */
        df = chan[ch].f;
        if (!df || chan[ch].pos + n > HOST_FILE_MAX) { NEO_ERR = 1; return; }
        memcpy(df->data + chan[ch].pos, neo_host_ptr, n);
        chan[ch].pos += n;
        if (chan[ch].pos > df->len) df->len = chan[ch].pos;
        return;
    }
    NEO_ERR = 1;
}

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
    case 3:     /* fichiers : un seul fichier en memoire, neotel.cfg. Les
                 * adresses 16 bits du bloc $FF00 ne sont pas des pointeurs
                 * hote : le stub lit/ecrit g_settings directement. */
        if (f == 2) {                       /* Load File */
            if (host_file_len) memcpy(&g_settings, host_file, host_file_len);
            else NEO_ERR = 1;
        } else if (f == 3) {                /* Store File */
            if (host_file_ro) NEO_ERR = 1;
            else { host_file_len = sizeof g_settings; memcpy(host_file, &g_settings, host_file_len); }
        } else disk_op(f);
        break;
    case NEO_G_GRAPHICS:
    case NEO_G_BLITTER:
        /* les tests passent par neo_gfx.h, jamais par le bloc $FF00 */
        break;
    case NEO_G_SOUND:
        if (f == NEO_F_SND_BEEP || f == NEO_F_SND_QUEUE_EXT) ++host_beeps;
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
