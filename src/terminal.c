/**
 * @file terminal.c
 * @brief Profil du terminal emule (voir terminal.h)
 */

#include "terminal.h"
#include "serial.h"

unsigned char g_term_model = TERM_MINITEL_1B;
unsigned char g_ident_enabled = 0;

static unsigned int s_speed = 1200;

/* Constructeur et version annonces : 'C' = Telic ; "Cv1" est l'identifiant
 * d'un Minitel 2 Telic (STUM 2 annexe 6.6 p. 103). */
#define IDENT_MAKER   'C'
#define IDENT_VERSION '1'

void term_set_model(unsigned char model)
{
    g_term_model = (model == TERM_MINITEL_2) ? TERM_MINITEL_2 : TERM_MINITEL_1B;
    s_speed = 1200;
}

const char* term_model_name(void)
{
    return (g_term_model == TERM_MINITEL_2) ? "Minitel 2" : "Minitel 1B";
}

const char* term_model_short(void)
{
    return (g_term_model == TERM_MINITEL_2) ? "M2" : "1B";
}

unsigned char term_ident_type(void)
{
    return (g_term_model == TERM_MINITEL_2) ? 'v' : 'u';
}

void term_send_ident(void)
{
    if (!g_ident_enabled) return;
    serial_send(0x01);                  /* SOH */
    serial_send(IDENT_MAKER);
    serial_send(term_ident_type());
    serial_send(IDENT_VERSION);
    serial_send(0x04);                  /* EOT */
    serial_tx_flush();
}

unsigned char term_prog_speed(unsigned char code)
{
    switch (code) {
        case SPEED_CODE_300:  s_speed = 300;  return 1;
        case SPEED_CODE_1200: s_speed = 1200; return 1;
        case SPEED_CODE_4800: s_speed = 4800; return 1;
        case SPEED_CODE_9600:
            if (g_term_model == TERM_MINITEL_2) { s_speed = 9600; return 1; }
            return 0;
        default:
            return 0;
    }
}

unsigned int term_speed(void)
{
    return s_speed;
}
