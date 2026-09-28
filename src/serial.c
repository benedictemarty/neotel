/**
 * @file serial.c
 * @brief Liaison serie via l'API UART du Neo6502 (voir serial.h)
 */

#include "serial.h"
#include "neo.h"

void serial_init(unsigned char route)
{
    neo_wait();
    NEO_P[0] = route;
    neo_call(NEO_G_UEXT, NEO_F_UART_ROUTE);    /* fork : erreur ignoree en amont */

    NEO_P[0] = (unsigned char)(SERIAL_BAUD_UEXT & 0xFF);
    NEO_P[1] = (unsigned char)((SERIAL_BAUD_UEXT >> 8) & 0xFF);
    NEO_P[2] = (unsigned char)((SERIAL_BAUD_UEXT >> 16) & 0xFF);
    NEO_P[3] = 0;
    NEO_P[4] = 0;                               /* 8N1 */
    neo_call(NEO_G_UEXT, NEO_F_UART_FORMAT);
}

unsigned char serial_cdc_status(void)
{
    neo_wait();
    NEO_P[0] = 0;
    NEO_P[7] = 0;                               /* premier peripherique */
    neo_call(NEO_G_CDC, NEO_F_CDC_STATUS);
    if (NEO_ERR) return SERIAL_CDC_UNSUPPORTED; /* firmware amont : pas de groupe 14 */
    return NEO_P[0] ? SERIAL_CDC_PRESENT : SERIAL_CDC_ABSENT;
}

void serial_send(unsigned char byte)
{
    neo_wait();
    NEO_P[0] = byte;
    neo_call(NEO_G_UEXT, NEO_F_UART_WRITE);
}

#ifndef __CC65__
/* Hote : fonctions reelles (les tests les appellent ou les redefinissent).
 * Cible : macros vides (serial.h), plus aucun jsr. */
void serial_tx_pump(void)
{
}

void serial_tx_flush(void)
{
}
#endif

/* Reception : un seul appel API par octet. 10,17 renvoie l'octet ou leve le
 * drapeau d'erreur si rien n'attend ; serial_poll le lit donc directement et
 * le garde (lecture anticipee d'un octet, sans tampon) au lieu d'interroger
 * 10,18 puis de relire par 10,17 (deux appels par octet jusqu'a la v0.9.5). */
static unsigned char rx_byte;
static unsigned char rx_have;

unsigned char serial_poll(void)
{
    if (rx_have) return 1;
    neo_wait();
    neo_call(NEO_G_UEXT, NEO_F_UART_READ);
    if (NEO_ERR) return 0;                      /* rien de disponible */
    rx_byte = NEO_P[0];
    return rx_have = 1;
}

unsigned char serial_recv(void)
{
    if (!serial_poll()) return 0xFF;
    rx_have = 0;
    return rx_byte;
}
