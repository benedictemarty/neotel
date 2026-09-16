/* neo_stub.h — interface du Neo6502 logiciel des tests hote (neo_stub.c) */
#ifndef NEO_STUB_H
#define NEO_STUB_H

extern unsigned char host_vram[240][320];
extern unsigned char host_palette[256][3];
extern int host_blits, host_fills, host_beeps;
extern unsigned long host_timer, host_delay_ms;
extern unsigned char host_keys_down[256];
extern unsigned char host_tx[4096];
extern int host_tx_len;
extern unsigned char host_cdc_connected;    /* 0xFF = groupe 14 absent */
extern unsigned char host_uart_route;
extern unsigned long host_uart_baud;

void host_key_push(unsigned char ascii);
void host_rx_push(const unsigned char* data, int n);
void host_tx_reset(void);

#endif
