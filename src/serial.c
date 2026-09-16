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

void serial_tx_pump(void)
{
}

void serial_tx_flush(void)
{
}

unsigned char serial_poll(void)
{
    neo_wait();
    NEO_P[0] = 0;
    neo_call(NEO_G_UEXT, NEO_F_UART_AVAIL);
    return NEO_P[0] ? 1 : 0;
}

unsigned char serial_recv(void)
{
    neo_wait();
    neo_call(NEO_G_UEXT, NEO_F_UART_READ);
    if (NEO_ERR) return 0xFF;                   /* rien de disponible */
    return NEO_P[0];
}
