/**
 * @file test_terminal.c
 * @brief Tests hote du profil terminal (terminal.c) et de la serie (serial.c)
 *
 *   - modele 1B / 2 : noms, octet de type, identification (desactivee par
 *     defaut, SOH..EOT quand activee), vitesses PRO2 PROG (9600 = Minitel 2) ;
 *   - integration videotex.c : PRO2 PROG memorise la vitesse, ENQ/ENQROM
 *     repondent selon le modele quand l'identification est active ;
 *   - serial.c sur le bloc $FF00 : routage, format, emission, reception,
 *     presence CDC (y compris firmware amont sans groupe 14).
 */

#include <stdio.h>
#include <string.h>
#include "terminal.h"
#include "serial.h"
#include "videotex.h"
#include "neo.h"
#include "neo_stub.h"

unsigned char g_global_mask = 1;
void display_clear(void) {}
void display_beep(void) {}

static int run, pass;
#define CHECK(c, name) do { ++run; if (c) ++pass; else printf("FAIL : %s (ligne %d)\n", name, __LINE__); } while (0)

int main(void)
{
    vtx_context_t ctx;
    static const unsigned char enqrom[] = { 0x1B, 0x39, 0x7B };
    static const unsigned char prog9600[] = { 0x1B, 0x3A, 0x6B, SPEED_CODE_9600 };
    static const unsigned char prog4800[] = { 0x1B, 0x3A, 0x6B, SPEED_CODE_4800 };
    int i;

    /* --- profil --- */
    term_set_model(TERM_MINITEL_1B);
    CHECK(strcmp(term_model_name(), "Minitel 1B") == 0 && strcmp(term_model_short(), "1B") == 0, "nom 1B");
    CHECK(term_ident_type() == 'u', "type 1B = 'u'");
    CHECK(term_speed() == 1200, "vitesse initiale 1200");
    CHECK(term_prog_speed(SPEED_CODE_4800) == 1 && term_speed() == 4800, "1B : 4800 accepte");
    CHECK(term_prog_speed(SPEED_CODE_9600) == 0 && term_speed() == 4800, "1B : 9600 refuse");
    CHECK(term_prog_speed(SPEED_CODE_300) == 1 && term_speed() == 300, "1B : 300 accepte");
    CHECK(term_prog_speed(0x00) == 0, "code inconnu refuse");
    term_set_model(TERM_MINITEL_2);
    CHECK(strcmp(term_model_name(), "Minitel 2") == 0 && strcmp(term_model_short(), "M2") == 0, "nom M2");
    CHECK(term_ident_type() == 'v', "type M2 = 'v'");
    CHECK(term_speed() == 1200, "changement de modele : vitesse remise a 1200");
    CHECK(term_prog_speed(SPEED_CODE_9600) == 1 && term_speed() == 9600, "M2 : 9600 accepte");
    term_set_model(99);
    CHECK(g_term_model == TERM_MINITEL_1B, "modele inconnu -> 1B");

    /* --- identification --- */
    host_tx_reset();
    g_ident_enabled = 0;
    term_send_ident();
    CHECK(host_tx_len == 0, "identification desactivee par defaut : rien");
    g_ident_enabled = 1;
    term_set_model(TERM_MINITEL_2);
    term_send_ident();
    CHECK(host_tx_len == 5 && host_tx[0] == 0x01 && host_tx[2] == 'v' && host_tx[4] == 0x04,
          "identification M2 : SOH C 'v' V EOT");

    /* --- integration decodeur --- */
    term_set_model(TERM_MINITEL_1B);
    vtx_init(&ctx);
    host_tx_reset();
    for (i = 0; i < 3; ++i) vtx_process(&ctx, enqrom[i]);
    CHECK(host_tx_len == 5 && host_tx[2] == 'u', "PRO1 ENQROM : identification 1B");
    g_ident_enabled = 0;
    host_tx_reset();
    vtx_process(&ctx, 0x05);
    CHECK(host_tx_len == 0, "ENQ, identification desactivee : rien");
    for (i = 0; i < 4; ++i) vtx_process(&ctx, prog9600[i]);
    CHECK(term_speed() == 1200 && host_tx_len == 0 && ctx.state == VTX_STATE_NORMAL,
          "PRO2 PROG 9600 sur 1B : refuse, sans reponse, sync conserve");
    for (i = 0; i < 4; ++i) vtx_process(&ctx, prog4800[i]);
    CHECK(term_speed() == 4800, "PRO2 PROG 4800 sur 1B : memorise");

    /* --- serie --- */
    serial_init(SERIAL_ROUTE_AUTO);
    CHECK(host_uart_route == SERIAL_ROUTE_AUTO && host_uart_baud == SERIAL_BAUD_UEXT, "serial_init : routage AUTO, 115200");
    CHECK(serial_poll() == 0 && serial_recv() == 0xFF, "reception vide : poll 0, recv $FF");
    host_rx_push((const unsigned char*)"OK", 2);
    CHECK(serial_poll() == 1 && serial_recv() == 'O' && serial_recv() == 'K' && serial_poll() == 0, "reception : octets dans l'ordre");
    host_tx_reset();
    serial_send('A'); serial_send('T'); serial_tx_pump(); serial_tx_flush();
    CHECK(host_tx_len == 2 && host_tx[0] == 'A' && host_tx[1] == 'T', "emission immediate");
    host_cdc_connected = 1;
    CHECK(serial_cdc_status() == SERIAL_CDC_PRESENT, "CDC present");
    host_cdc_connected = 0;
    CHECK(serial_cdc_status() == SERIAL_CDC_ABSENT, "CDC absent");
    host_cdc_connected = 0xFF;
    CHECK(serial_cdc_status() == SERIAL_CDC_UNSUPPORTED, "firmware amont (groupe 14 absent) : non supporte");
    host_cdc_connected = 1;

    printf("test_terminal : %d/%d OK\n", pass, run);
    return pass == run ? 0 : 1;
}
