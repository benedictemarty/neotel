/**
 * @file serial.h
 * @brief Liaison serie de NeoTel : UART du Neo6502 via l'API (groupe 10)
 *
 * Sur le Neo6502 la liaison serie n'est pas un composant sur le bus (pas
 * d'ACIA) : c'est le RP2040 qui tient l'UART de l'UEXT et, avec le firmware
 * bmarty (F-90/F-93), un modem USB CDC branche sur le port hote. Les
 * fonctions 10,15-10,18 (format, ecriture, lecture, disponibilite) sont
 * ROUTEES par 10,19 : mode AUTO = le modem CDC s'il est branche, sinon
 * l'UART materielle. NeoTel parle donc a un PicoWiFiModemUSB (modem Hayes)
 * exactement comme OricTel, sans se soucier du support physique.
 *
 * Le firmware tamponne la reception (1 Ko cote CDC, FIFO UART cote UEXT) :
 * il n'y a plus l'overrun du 6551 nu d'OricTel, mais la discipline reste
 * la meme (drainer avant de rendre) car le tampon n'est pas infini.
 *
 * L'emission est immediate (10,16 depose l'octet dans le tampon TX du
 * firmware) ; la file logicielle d'OricTel n'est plus necessaire, mais son
 * API (serial_send / serial_tx_pump / serial_tx_flush) est conservee pour
 * garder videotex.c et at_modem.c inchanges.
 */

#ifndef SERIAL_H
#define SERIAL_H

#ifndef __CC65__
#define __fastcall__
#endif

/* Routage 10,19 */
#define SERIAL_ROUTE_UEXT 0     /* UART materielle de l'UEXT */
#define SERIAL_ROUTE_CDC  1     /* premier modem USB CDC */
#define SERIAL_ROUTE_AUTO 2     /* CDC si present, sinon UEXT (defaut) */

/* Vitesse programmee sur l'UART UEXT (ignoree par les modems USB). */
#define SERIAL_BAUD_UEXT  115200UL

/**
 * Programme le routage (10,19) et le format 8N1 (10,15).
 * @param route SERIAL_ROUTE_UEXT / _CDC / _AUTO
 */
void serial_init(unsigned char route);

/* Retours de serial_cdc_status */
#define SERIAL_CDC_ABSENT      0
#define SERIAL_CDC_PRESENT     1
#define SERIAL_CDC_UNSUPPORTED 2    /* firmware amont : groupe 14 absent */

/**
 * Presence d'un modem USB CDC (14,1). Sur le firmware amont le groupe 14
 * n'existe pas : l'appel leve le drapeau d'erreur -> SERIAL_CDC_UNSUPPORTED.
 */
unsigned char serial_cdc_status(void);

/**
 * Envoie un octet (10,16). Immediat, non bloquant.
 */
void serial_send(unsigned char byte);

/** Compatibilite OricTel : rien a pomper, l'emission est immediate. */
void serial_tx_pump(void);
void serial_tx_flush(void);

/**
 * Verifie si un octet attend en reception (10,18).
 * @return Non nul si une donnee est disponible
 */
unsigned char serial_poll(void);

/**
 * Recoit un octet (10,17), non bloquant.
 * @return Octet recu, ou 0xFF si aucune donnee disponible
 */
unsigned char serial_recv(void);

#endif /* SERIAL_H */
