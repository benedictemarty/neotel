/* neo_stub.h — interface du Neo6502 logiciel des tests hote (neo_stub.c) */
#ifndef NEO_STUB_H
#define NEO_STUB_H

extern unsigned char host_vram[240][320];
extern unsigned char host_palette[256][3];
extern int host_blits, host_fills, host_beeps;
extern unsigned char host_mode, host_mode_refuse;   /* mode video 5,9 */
extern unsigned long host_timer, host_delay_ms;
extern unsigned char host_keys_down[256];
extern unsigned char host_tx[4096];
extern int host_tx_len;
extern unsigned char host_cdc_connected;    /* 0xFF = groupe 14 absent */
extern unsigned char host_uart_route;
extern unsigned long host_uart_baud;

extern unsigned char host_file[256];        /* contenu de neotel.cfg */
extern int host_file_len;                   /* 0 = absent */
extern unsigned char host_file_ro;          /* 1 = ecriture refusee */

/* Fichiers par canal (3,4/5/8/9) : disque en memoire, HOST_FILES fichiers
 * de HOST_FILE_MAX octets, nom prefixe par sa longueur. */
#define HOST_FILES     8
#define HOST_FILE_MAX  8192
typedef struct { char name[32]; unsigned char data[HOST_FILE_MAX]; int len; } host_disk_file_t;
extern host_disk_file_t host_disk[HOST_FILES];
extern int host_open_count;                 /* canaux ouverts */
extern unsigned char host_disk_ro;          /* 1 = ouverture en creation refusee */
host_disk_file_t* host_disk_find(const char* name);

void host_key_push(unsigned char ascii);
void host_rx_push(const unsigned char* data, int n);
void host_tx_reset(void);

#endif
