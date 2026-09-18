/**
 * @file main.c
 * @brief NeoTel - Terminal Minitel 1B / Minitel 2 pour Neo6502
 *
 * Portage d'OricTel (Oric 1/Atmos, meme auteur) sur le Neo6502 : le decodeur
 * Videotex, les polices et la logique de session sont repris ; l'affichage,
 * le clavier, la liaison serie et la base de temps passent par l'API du
 * RP2040 (bloc $FF00).
 *
 * Liaison : le modem est un PicoWiFiModemUSB (modem Hayes) branche sur le
 * port USB hote du Neo6502 (firmware bmarty : routage UART 10,19 AUTO vers le
 * CDC), ou tout modem AT sur l'UART de l'UEXT. La connexion passe TOUJOURS
 * par les commandes AT (ATZ/ATDT).
 *
 * Tests : Phosphoneo avec un faux modem sur pty (tools/fake_modem.py) :
 *   NEO_CDC_TTY=$(cat pty.txt) phosphoneo build/neotel.neo
 */

#include <string.h>
#include <stddef.h>
#include "serial.h"
#include "videotex.h"
#include "display.h"
#include "keyboard.h"
#include "at_modem.h"
#include "ui.h"
#include "neo_time.h"
#include "terminal.h"
#include "settings.h"
#include "teleinfo.h"
#include "display80.h"
#include "record.h"
#include "help.h"

/* Version NeoTel affichee au splash. A garder synchronisee avec CHANGELOG.md
 * et VERSION a chaque release. */
#define NEOTEL_VERSION "v0.9.3"

/* Silence exige, en millisecondes, pour CONFIRMER une presomption de perte de
 * porteuse (un vrai NO CARRIER n'est suivi de RIEN, une page qui citerait ces
 * mots continuerait de defiler). Meme valeur qu'OricTel (4 s), comptee ici
 * sur le timer 100 Hz du firmware. */
#define CARRIER_CONFIRM_MS   4000u
#define CARRIER_CONFIRM_TICKS (CARRIER_CONFIRM_MS / 10u)

/* Silence apres lequel l'indicateur de connexion repasse a 'F'. */
#define LINK_IDLE_MS         30000u
#define LINK_IDLE_TICKS      (LINK_IDLE_MS / 10u)

/* Contexte Videotex global */
vtx_context_t vtx;                  /* non statique : lu dans les dumps RAM des tests (build/neotel.lbl) */

/* Ecran 80 colonnes (mode Mixte / Teleinformatique) et mode d'ecran courant.
 * Le contexte 80 colonnes (~4,1 Ko) est LOGE dans vtx, a partir de vtx.drcs
 * (1 880 o, jeux telecharges : inutilises en mode Mixte) et en continuant
 * dans vtx.screen (4 Ko), contigu (verifie a la compilation). En mode Mixte
 * les seuls octets qui vont encore au decodeur Videotex sont les sequences
 * Protocole (mixte_byte), qui n'ecrivent ni l'ecran ni les DRCS, et le
 * retour au mode Videotex refait vtx_init. La RAM du Neo6502 ne permet pas
 * les deux ecrans cote a cote. (v0.9.2 : cellule Videotex de 4 octets, le
 * contexte ne tenait plus dans screen seul.) */
#define ti (*(ti_context_t*)&vtx.drcs[0][0][0])
/* Tampon de sauvegarde de la rangee 00 pour display80_status : juste derriere
 * le contexte, toujours dans drcs + screen. */
#define ti_save ((ti_cell_t*)((ti_context_t*)&vtx.drcs[0][0][0] + 1))
#define VTX_DRCS_SCREEN_BYTES (sizeof(((vtx_context_t*)0)->drcs) + sizeof(((vtx_context_t*)0)->screen))
typedef char ti_fits_in_vtx[(sizeof(ti_context_t) + TI_COLS * sizeof(ti_cell_t)
                             <= VTX_DRCS_SCREEN_BYTES) ? 1 : -1];
typedef char vtx_drcs_then_screen[(offsetof(vtx_context_t, screen)
                                   == offsetof(vtx_context_t, drcs) + sizeof(((vtx_context_t*)0)->drcs)) ? 1 : -1];
unsigned char g_screen80;           /* 1 = ecran 80 colonnes actif (mode 1) */

/* Phase de clignotement (lue par display.c) : bascule toutes les 500 ms */
unsigned char g_blink_phase;
static unsigned char blink_ticks;

/* Mask global Videotex (ESC # $20 $58/$5F). 1 = cacher cellules concealed. */
unsigned char g_global_mask = 1;

/* Etat courant, pour les tests cible (Phosphoneo --dump-ram-when). */
unsigned char g_dbg_state;
unsigned char g_dbg_hangups;    /* sessions quittees par ESC (persistant) */
#define ST_SPLASH     1
#define ST_INTERFACE  2
#define ST_MENU       3
#define ST_SERVER     4
#define ST_CONNECT    5
#define ST_SESSION    6
#define ST_FAILED     7
#define ST_CARRIER    8
#define ST_WIFI       9
#define ST_EXIT       10
#define ST_ESCAPE     11
#define ST_HUNGUP     12   /* session quittee, modem raccroche */
#define ST_MIXTE      13   /* session en mode Mixte (80 colonnes) */
#define ST_REPLAY     14   /* relecture d'un fichier .vdt */
#define ST_REPLAY_END 15   /* relecture terminee, attente d'une touche */
#define ST_HELP       16   /* ecran d'aide (menu H) */

/* Routage serie choisi (SERIAL_ROUTE_*) */
static unsigned char s_route = SERIAL_ROUTE_AUTO;

/* ===================================================================
 *  Ecran splash
 * =================================================================== */
static void splash_screen(vtx_context_t* ctx)
{
    unsigned char i;
    const char* p;
    unsigned int waited;

    g_dbg_state = ST_SPLASH;
    ctx->cur_visible = 0;           /* pas de curseur sur les ecrans locaux */

    ui_banner(ctx, "NEOTEL", NEOTEL_VERSION, 1);
    ui_print(ctx, 6, 2, "Terminal Minitel 1B / Minitel 2", VTX_WHITE);
    ui_print(ctx, 7, 2, "pour Neo6502", VTX_WHITE);
    ui_rule(ctx, 9, VTX_YELLOW);
    ui_print(ctx, 11, 2, "par Benedicte Marty", VTX_WHITE);
    ui_print(ctx, 12, 2, "Licence EUPL 1.2", VTX_YELLOW);
    ui_print(ctx, 14, 2, "d'apres OricTel (Oric 1/Atmos)", VTX_GREEN);
    ui_rule(ctx, 16, VTX_YELLOW);

    p = "Appuyez sur une touche...";
    for (i = 0; p[i]; ++i) {
        ctx->screen[19][7 + i].ch = p[i];
        ui_set_fg(&ctx->screen[19][7 + i], VTX_WHITE);
        ctx->screen[19][7 + i].flags = ATTR_FLASH;
    }
    ctx->dirty[19] = 1;
    ui_footer(ctx, "H aide au menu", "ESC quitter");

    display_render_all(ctx);
    display_beep();
    /* Attendre une touche ou ~5 secondes (clignotement anime) */
    waited = 0;
    tick_reset();
    while (waited < 500) {
        unsigned char t = tick_10ms();
        if (keyboard_scan() != KEY_NONE) break;
        waited += t;
        blink_ticks += t;
        if (blink_ticks >= 50) {
            blink_ticks = 0;
            display_blink_toggle(ctx);
            display_render_all(ctx);
        }
    }
    g_blink_phase = 0;
    ctx->blink_phase = 0;

    vtx_clear_page(ctx);
    display_render_all(ctx);
}

/* ===================================================================
 *  Menus
 * =================================================================== */

#define MODE_MODEM  0
#define MODE_WIFI   2
#define MODE_QUIT   3
#define MODE_REPLAY 4

/* Ecran de la liaison serie : rappel du montage, presence du modem USB. */
static void interface_page(vtx_context_t* ctx)
{
    unsigned char st = serial_cdc_status();

    g_dbg_state = ST_INTERFACE;
    vtx_clear_page(ctx);
    ui_header(ctx, "LIAISON SERIE", NEOTEL_VERSION);

    /* Carte d'etat : bandeau vert (modem USB) ou jaune (UART UEXT) */
    ui_fill(ctx, 6, st == SERIAL_CDC_PRESENT ? VTX_GREEN : VTX_YELLOW);
    if (st == SERIAL_CDC_PRESENT) {
        ui_print(ctx, 6, 2, "MODEM USB CDC detecte (port hote)", VTX_BLACK);
        ui_print(ctx, 8, 2, "API UART routee vers le modem", VTX_CYAN);
    } else {
        ui_print(ctx, 6, 2, "UART de l'UEXT  115200 8N1", VTX_BLACK);
        if (st == SERIAL_CDC_ABSENT) {
            ui_print(ctx, 8, 2, "Pas de modem USB CDC detecte.", VTX_WHITE);
            ui_print(ctx, 10, 2, "Brancher le PicoWiFiModemUSB sur le", VTX_WHITE);
            ui_print(ctx, 11, 2, "port USB hote, puis CTRL+F en session.", VTX_WHITE);
        } else {
            ui_print(ctx, 8, 2, "Firmware amont : pas de groupe 14.", VTX_WHITE);
            ui_print(ctx, 10, 2, "Le modem USB demande le firmware", VTX_WHITE);
            ui_print(ctx, 11, 2, "bmarty (Neo6502firmware, F-90/F-93).", VTX_WHITE);
        }
    }
    ui_footer(ctx, "une touche : continuer", NULL);
    display_render_all(ctx);

    keyboard_flush();
    while (keyboard_scan() == KEY_NONE) {
        /* attente d'un appui */
    }
}
/* Serveurs disponibles */
/* Cibles ATDT : hote:port (TCP) ou ws:// / wss:// (WebSocket, relaye par
 * tools/fake_modem.py sur PC ; non verifie sur un PicoWiFiModemUSB). */
static const char* servers[] = {
    "pavi.3617.fr:3617",
    "go.minipavi.fr:516",
    "wss://3617.fr/ws",
};
static const char* server_names[] = {
    "PAVI 3617",
    "MiniPavi",
    "3617.fr (WS)",
};
#define NUM_SERVERS 3
#define KEY_OTHER_SERVER ('1' + NUM_SERVERS)

/* Menu principal. Retourne MODE_MODEM, MODE_WIFI ou MODE_QUIT (ESC). */
#define MENU_ITEMS 7
static const unsigned char menu_rows[MENU_ITEMS] = { 5, 7, 9, 11, 13, 15, 17 };
static const char* const menu_labels[MENU_ITEMS] = {
    "Modem AT", "Config WiFi", "Terminal", "Aspect", "Identification",
    "Relire", "Son",
};

static const char* menu_value(unsigned char i)
{
    switch (i) {
    case 0:
        if (g_settings.server_idx == 255) return g_settings.server[0] ? g_settings.server : NULL;
        return g_settings.server_idx < NUM_SERVERS ? server_names[g_settings.server_idx] : NULL;
    case 2: return g_term_model == TERM_MINITEL_2 ? "Minitel 2" : "Minitel 1B";
    case 3: return display_get_look() == DISPLAY_LOOK_GREY ? "gris (1B mono)" : "couleur";
    case 4: return g_ident_enabled ? "ON" : "OFF";
    case 5:
        if (!g_settings.rec_index) return "un .vdt";
        record_make_name((char*)&vtx.drcs[0][0][0], g_settings.rec_index);
        return (const char*)&vtx.drcs[0][0][0];    /* tampon transitoire dans vtx.drcs */
    case 6: return g_settings.sound ? "ON" : "OFF";
    default: return NULL;
    }
}

static void menu_draw_item(vtx_context_t* ctx, unsigned char i, unsigned char sel)
{
    ui_item(ctx, menu_rows[i], (char)('1' + i), menu_labels[i], menu_value(i), sel);
}

static unsigned char select_mode(vtx_context_t* ctx)
{
    static unsigned char sel;       /* item courant, conserve entre deux menus */
    unsigned char key, i, act;

    g_dbg_state = ST_MENU;
    for (;;) {
        vtx_clear_page(ctx);
        ui_header(ctx, "NEOTEL", g_term_model == TERM_MINITEL_2 ? "Minitel 2" : "Minitel 1B");
        for (i = 0; i < MENU_ITEMS; ++i) menu_draw_item(ctx, i, i == sel);
        ui_print(ctx, 20, 2, "Fleches / chiffre, ENVOI valide", VTX_CYAN);
        ui_footer(ctx, "H aide", "ESC quitter (NeoBASIC)");
        display_render_all(ctx);

        keyboard_flush();
        for (;;) {
            key = keyboard_scan();
            if (key == KEY_NONE) continue;
            if (key == KEY_LOCAL_ESCAPE) return MODE_QUIT;
            if (key == 'H' || key == 'h') { help_show(ctx); break; }
            if (key == KEY_TOGGLE_RENDER) { key = '4'; }
            act = 0xFF;
            if (key >= '1' && key < '1' + MENU_ITEMS) act = key - '1';
            else {
                unsigned char r = ui_nav(key, &sel, MENU_ITEMS);
                if (r == 1) {
                    for (i = 0; i < MENU_ITEMS; ++i) menu_draw_item(ctx, i, i == sel);
                    display_render_all(ctx);
                    continue;
                }
                if (r == 2) act = sel;
            }
            if (act == 0xFF) continue;
            sel = act;
            switch (act) {
            case 0: return MODE_MODEM;
            case 1: return MODE_WIFI;
            case 5: return MODE_REPLAY;
            case 2:
                term_set_model(g_term_model == TERM_MINITEL_2
                               ? TERM_MINITEL_1B : TERM_MINITEL_2);
                g_settings.model = g_term_model;
                break;
            case 3:
                display_set_look(display_get_look() == DISPLAY_LOOK_GREY
                                 ? DISPLAY_LOOK_COLOR : DISPLAY_LOOK_GREY);
                g_settings.look = display_get_look();
                break;
            case 4:
                g_ident_enabled ^= 1;
                g_settings.ident = g_ident_enabled;
                break;
            default:
                g_settings.sound ^= 1;
                if (g_settings.sound) display_beep();   /* confirmation audible */
                break;
            }
            settings_save();
            break;
        }
    }
}

/* Menu de selection serveur. Retourne 0-2 pour les predefinis, 255 pour
 * saisie libre (g_settings.server), 0xFE sur ESC (retour au menu). */
static void server_draw(vtx_context_t* ctx, unsigned char sel)
{
    unsigned char i;
    for (i = 0; i < NUM_SERVERS; ++i) {
        ui_item(ctx, (unsigned char)(5 + i * 2), (char)('1' + i), server_names[i],
                servers[i], i == sel);
    }
    ui_item(ctx, 5 + NUM_SERVERS * 2, KEY_OTHER_SERVER, "Autre",
            g_settings.server[0] ? g_settings.server : "host:port, ws://",
            sel == NUM_SERVERS);
}

static unsigned char select_server(vtx_context_t* ctx)
{
    unsigned char sel, n;

    g_dbg_state = ST_SERVER;
    vtx_clear_page(ctx);
    ui_header(ctx, "SERVEUR", NULL);
    /* Item courant = dernier serveur (reglages sauves) : ENVOI le reprend */
    sel = (g_settings.server_idx == 255) ? NUM_SERVERS
        : (g_settings.server_idx < NUM_SERVERS) ? g_settings.server_idx : 0;
    server_draw(ctx, sel);
    ui_print(ctx, 16, 2, "Fleches / chiffre, ENVOI = compose", VTX_CYAN);
    ui_footer(ctx, "4 saisir une adresse", "ESC retour");
    display_render_all(ctx);

    keyboard_flush();
    for (;;) {
        unsigned char key = keyboard_scan();
        unsigned char r;
        if (key == KEY_NONE) continue;
        if (key == KEY_LOCAL_ESCAPE) return 0xFE;
        if (key >= '1' && key < '1' + NUM_SERVERS) return key - '1';
        r = ui_nav(key, &sel, NUM_SERVERS + 1);
        if (r == 1) { server_draw(ctx, sel); display_render_all(ctx); continue; }
        if (r == 2) {
            if (sel < NUM_SERVERS) return sel;
            if (g_settings.server[0]) return 255;   /* "Autre" deja saisi */
            key = KEY_OTHER_SERVER;
        }
        if (key == KEY_OTHER_SERVER) {
            unsigned char row = 5 + NUM_SERVERS * 2 + 2;
            ui_print(ctx, row, 2, "Adresse >", VTX_WHITE);
            display_render_all(ctx);
            n = ui_text_input(ctx, row, 12, g_settings.server,
                              SETTINGS_SERVER_MAX, 0);
            if (n != 0xFF && n > 0) return 255;
            ui_fill(ctx, row, VTX_BLACK);
            server_draw(ctx, sel);
            display_render_all(ctx);
        }
    }
}

/* ===================================================================
 *  Page de configuration WiFi du PicoWiFiModemUSB (AT$SCAN, AT$SSID,
 *  AT$PASS, ATC1, AT&W) - reprise d'OricTel.
 * =================================================================== */

#define WIFI_MAX 8
/* Ces tampons ne servent qu'a la page Config WiFi (hors session) : ils
 * logent dans vtx.drcs, que vtx_init() remet a zero avant chaque session
 * (meme economie de RAM que le contexte 80 colonnes dans vtx.screen). */
typedef struct { char ssid[WIFI_MAX][33]; char sec[WIFI_MAX]; char pass[40]; } wifi_bufs_t;
#define wifi_bufs (*(wifi_bufs_t*)&vtx.drcs[0][0][0])
#define wifi_ssid (wifi_bufs.ssid)
#define wifi_sec  (wifi_bufs.sec)
#define wifi_pass (wifi_bufs.pass)
typedef char wifi_fits_in_drcs[(sizeof(wifi_bufs_t) <= sizeof(((vtx_context_t*)0)->drcs)) ? 1 : -1];
static unsigned char wifi_count;

static unsigned char wifi_scan(void)
{
    char line[40];
    unsigned char lp = 0;
    unsigned int  elapsed = 0;

    wifi_count = 0;
    at_send("AT$SCAN");

    while (elapsed < 9000) {
        if (serial_poll()) {
            unsigned char b = serial_recv();
            if (b == 0x0D || b == 0x0A) {
                line[lp] = 0;
                if (lp >= 2 && line[0] == 'O' && line[1] == 'K') {
                    return wifi_count;
                }
                if (lp > 0 && line[0] >= '0' && line[0] <= '9'
                    && wifi_count < WIFI_MAX) {
                    unsigned char k = 0;
                    unsigned char d = 0;
                    while (line[k] >= '0' && line[k] <= '9') ++k;
                    while (line[k] == ' ') ++k;
                    while (line[k] && line[k] != 0x09 && d < 32) {
                        wifi_ssid[wifi_count][d++] = line[k++];
                    }
                    wifi_ssid[wifi_count][d] = 0;
                    if (line[k] == 0x09) ++k;
                    wifi_sec[wifi_count] = (line[k] == 'S') ? 'S' : 'O';
                    if (d > 0) ++wifi_count;
                }
                lp = 0;
            } else if (lp < 39) {
                line[lp++] = b;
            }
        } else {
            neo_delay_ms(10);
            elapsed += 10;
        }
    }
    return wifi_count;
}

static void wifi_msg_wait(vtx_context_t* ctx, unsigned char row,
                          unsigned char col, const char* msg)
{
    ui_print(ctx, row, col, msg, VTX_WHITE);
    display_render_all(ctx);
    keyboard_flush();
    while (keyboard_scan() == KEY_NONE) { /* attente touche */ }
}

static void wifi_config_page(vtx_context_t* ctx)
{
    unsigned char i, sel;

    g_dbg_state = ST_WIFI;
    for (;;) {
        vtx_clear_page(ctx);
        ui_header(ctx, "CONFIG WIFI", NULL);
        ui_print(ctx, 10, 2, "Scan des reseaux en cours...", VTX_WHITE);
        display_render_all(ctx);

        if (wifi_scan() == 0) {
            ui_fill(ctx, 10, VTX_YELLOW);
            ui_print(ctx, 10, 2, "Aucun reseau trouve", VTX_BLACK);
            ui_footer(ctx, "une touche : retour", NULL);
            wifi_msg_wait(ctx, 12, 2, "Le modem repond-il ? (ATI)");
            return;
        }

        vtx_clear_page(ctx);
        ui_header(ctx, "CONFIG WIFI", "reseaux");
        for (i = 0; i < wifi_count; ++i) {
            ui_item(ctx, (unsigned char)(5 + i * 2), (char)('1' + i), wifi_ssid[i],
                    wifi_sec[i] == 'S' ? "cle" : "ouvert", 0);
        }
        ui_footer(ctx, "chiffre choisit  F4 rescan", "ESC retour");
        display_render_all(ctx);

        sel = 0xFF;
        keyboard_flush();
        for (;;) {
            unsigned char key = keyboard_scan();
            if (key == KEY_NONE) continue;
            if (key >= '1' && key < '1' + wifi_count) {
                sel = key - '1';
                break;
            }
            if ((key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_REPETITION) {
                break;
            }
            if (key == KEY_LOCAL_ESCAPE ||
                ((key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_ANNULATION)) {
                return;
            }
        }
        if (sel == 0xFF) continue;

        wifi_pass[0] = 0;
        if (wifi_sec[sel] == 'S') {
            vtx_clear_page(ctx);
            ui_header(ctx, "CONFIG WIFI", wifi_ssid[sel]);
            ui_print(ctx, 8, 2, "Mot de passe :", VTX_WHITE);
            ui_footer(ctx, "ENVOI valide", "ESC annule");
            display_render_all(ctx);
            if (ui_text_input(ctx, 10, 2, wifi_pass, sizeof(wifi_pass), '*') == 0xFF) return;
        }

        vtx_clear_page(ctx);
        ui_header(ctx, "CONFIG WIFI", wifi_ssid[sel]);
        ui_print(ctx, 10, 2, "Association au reseau...", VTX_WHITE);
        display_render_all(ctx);

        at_send_kv("AT$SSID=", wifi_ssid[sel]);
        at_wait_response("OK", 3000);
        at_send_kv("AT$PASS=", wifi_pass);
        at_wait_response("OK", 3000);
        at_send("ATC1");
        at_wait_response("OK", 8000);

        if (at_wait_ip(20000)) {
            at_send("AT&W");
            at_wait_response("OK", 3000);
            ui_fill(ctx, 12, VTX_GREEN);
            ui_print(ctx, 12, 2, "Connecte, configuration sauvee", VTX_BLACK);
        } else {
            ui_fill(ctx, 12, VTX_RED);
            ui_print(ctx, 12, 2, "Pas d'adresse IP : verifier la cle", VTX_WHITE);
        }
        ui_footer(ctx, "une touche : retour", NULL);
        wifi_msg_wait(ctx, 14, 2, "");
        return;
    }
}

/* ===================================================================
 *  Connexion modem AT (ATZ / ATI / ATDT) - reprise d'OricTel
 * =================================================================== */

static unsigned char s_first_byte;      /* octet recu juste apres CONNECT */
static unsigned char s_have_first;

static unsigned char modem_connect(vtx_context_t* ctx, unsigned char server_idx)
{
    g_dbg_state = ST_CONNECT;
    s_have_first = 0;
    vtx_clear_page(ctx);
    ui_print(ctx, 10, 17, "ATZ...", VTX_WHITE);
    display_render_all(ctx);

    at_send("ATZ");
    if (!at_wait_response("OK", 3000)) {
        /* Pas de "OK" : modem absent, ou reste EN LIGNE d'une session
         * precedente. On tente l'echappement Hayes + ATH, puis on rejoue ATZ. */
        vtx_clear_page(ctx);
        ui_print(ctx, 10, 15, "Modem en ligne: ATH...", VTX_WHITE);
        display_render_all(ctx);
        at_hangup();

        vtx_clear_page(ctx);
        ui_print(ctx, 10, 17, "ATZ...", VTX_WHITE);
        display_render_all(ctx);
        at_send("ATZ");
        if (!at_wait_response("OK", 3000)) {
            return 0;
        }
    }

    vtx_clear_page(ctx);
    ui_print(ctx, 10, 11, "Attente IP WiFi...", VTX_WHITE);
    display_render_all(ctx);
    at_wait_ip(15000);

    {
        const char* srv = (server_idx == 255) ? g_settings.server : servers[server_idx];

        vtx_clear_page(ctx);
        ui_print(ctx, 10, 5, "ATDT ", VTX_WHITE);
        ui_print(ctx, 10, 10, srv, VTX_CYAN);
        display_render_all(ctx);

        /* ATDT hote:port : le 'T' est indispensable pour le PicoWiFiModemUSB
         * (sans lui le 1er caractere de l'hote est pris pour un modificateur). */
        serial_send('A'); serial_send('T'); serial_send('D'); serial_send('T');
        while (*srv) { serial_send(*srv); ++srv; }
        serial_send(0x0D);
        serial_tx_flush();
    }

    if (at_wait_response("CONNECT", 10000)) {
        unsigned char drain = 0;
        while (drain < 5) {
            if (serial_poll()) {
                unsigned char b = serial_recv();
                if (b == 0x0D || b == 0x0A) {
                    ++drain;
                    continue;
                }
                /* Premier octet Videotex : garde pour la session (le
                 * decodeur est remis a zero entre-temps ; le passer ici
                 * perdait un ESC de tete, donc l'ENQROM d'ouverture de
                 * MiniPavi et 3617.fr — v0.6.3). */
                s_first_byte = b;
                s_have_first = 1;
                break;
            }
            neo_delay_ms(10);
            ++drain;
        }
        return 1;
    }
    return 0;
}

/* Ecran d'echec de connexion. Retour : 1 = reessayer, 0 = entrer en session
 * malgre tout, 2 = ESC (menu). Peut modifier *srv_idx. */
static unsigned char connect_failed_page(vtx_context_t* ctx,
                                         unsigned char* srv_idx)
{
    unsigned char key;

    vtx_clear_page(ctx);
    ui_header(ctx, "CONNEXION", NULL);
    ui_fill(ctx, 5, VTX_RED);
    ui_print(ctx, 5, 2, "ECHEC DE CONNEXION", VTX_WHITE);
    ui_print(ctx, 7, 2, "Pas de CONNECT : la ligne ne porte", VTX_WHITE);
    ui_print(ctx, 8, 2, "aucun flux Videotex exploitable.", VTX_WHITE);
    ui_item(ctx, 11, '1', "Reessayer", NULL, 0);
    ui_item(ctx, 13, '2', "Choisir un autre serveur", NULL, 0);
    ui_item(ctx, 15, '3', "Entrer quand meme", NULL, 0);
    ui_footer(ctx, NULL, "ESC retour au menu");
    display_render_all(ctx);
    g_dbg_state = ST_FAILED;

    keyboard_flush();
    for (;;) {
        key = keyboard_scan();
        if (key == '1') return 1;
        if (key == KEY_LOCAL_ESCAPE) return 2;
        if (key == '2') {
            vtx_clear_page(ctx);
            *srv_idx = select_server(ctx);
            vtx_clear_page(ctx);
            return (*srv_idx == 0xFE) ? 2 : 1;  /* ESC : menu */
        }
        if (key == '3') return 0;
    }
}

/* Ecran de perte de porteuse. Retour : 1 = recomposer, 0 = rester en local,
 * 2 = ESC (menu). */
static unsigned char carrier_lost_page(vtx_context_t* ctx)
{
    unsigned char key;

    vtx_clear_page(ctx);
    ui_header(ctx, "CONNEXION", NULL);
    ui_fill(ctx, 5, VTX_RED);
    ui_print(ctx, 5, 2, "PERTE DE PORTEUSE", VTX_WHITE);
    ui_print(ctx, 7, 2, "Le modem a signale NO CARRIER :", VTX_WHITE);
    ui_print(ctx, 8, 2, "la communication est terminee.", VTX_WHITE);
    ui_item(ctx, 11, '1', "Reconnecter", NULL, 0);
    ui_item(ctx, 13, '2', "Rester en local", NULL, 0);
    ui_footer(ctx, NULL, "ESC retour au menu");
    display_render_all(ctx);
    g_dbg_state = ST_CARRIER;       /* apres le rendu : l'ecran est lisible */

    keyboard_flush();
    for (;;) {
        key = keyboard_scan();
        if (key == '1') return 1;
        if (key == '2') return 0;
        if (key == KEY_LOCAL_ESCAPE) return 2;
    }
}

/* ===================================================================
 *  Barre de statut (une ligne sous la page)
 *  [C] serveur  mm:ss  1B  1200  COUL
 * =================================================================== */
static const char*   status_server = "";
static unsigned char status_connected;
static unsigned int  status_secs;
static unsigned char status_sec_ticks;

static void status_bar_draw(void)
{
    char clock[6];
    char speed[5];
    unsigned int sp = term_speed();
    unsigned char m = (unsigned char)(status_secs / 60u);
    unsigned char sec = (unsigned char)(status_secs % 60u);

    if (g_screen80) return;         /* pas de barre en mode 1 (25 rangees pleines) */

    if (m > 99) m = 99;
    clock[0] = '0' + m / 10;  clock[1] = '0' + m % 10;  clock[2] = ':';
    clock[3] = '0' + sec / 10; clock[4] = '0' + sec % 10; clock[5] = 0;

    speed[0] = (sp >= 1000) ? (char)('0' + sp / 1000) : ' ';
    speed[1] = (char)('0' + (sp / 100) % 10);
    speed[2] = (char)('0' + (sp / 10) % 10);
    speed[3] = (char)('0' + sp % 10);
    speed[4] = 0;

    display_status_clear();
    display_status_text(0, status_connected ? " C " : " F ", VTX_CYAN, 1);
    display_status_text(4, status_server, VTX_CYAN, 0);
    display_status_text(18, " ", VTX_CYAN, 0);       /* borne un nom trop long */
    display_status_text(19, clock, VTX_WHITE, 0);
    display_status_text(25, term_model_short(), VTX_YELLOW, 0);
    display_status_text(28, speed, VTX_WHITE, 0);
    display_status_text(33, (display_get_look() == DISPLAY_LOOK_GREY)
                            ? "GRIS" : "COUL", VTX_WHITE, 0);
    /* Colonnes 38-39 : "F1" (aide), ou "RE" inverse rouge pendant un
     * enregistrement (CTRL+O) */
    if (record_active()) display_status_text(38, "RE", VTX_RED, 1);
    else display_status_text(38, "F1", VTX_GREEN, 0);
    display_status_show();
}

static void status_bar_init(void)
{
    status_server = "NeoTel " NEOTEL_VERSION;
    status_connected = 0;
    status_secs = 0;
    status_sec_ticks = 0;
    status_bar_draw();
}

static void status_set_connected(unsigned char on)
{
    if (status_connected != on) {
        status_connected = on;
        status_bar_draw();
    }
}

static void status_tick(unsigned char ticks)
{
    status_sec_ticks += ticks;
    if (status_sec_ticks >= 100) {
        status_sec_ticks -= 100;
        ++status_secs;
        status_bar_draw();
    }
}

static void status_bar_draw(void);

/* ===================================================================
 *  Mode Mixte / Teleinformatique : ecran 80 colonnes (teleinfo.c) en mode
 *  video 1. Entree sur PRO2 MIXTE 1 (videotex.c pose terminal_mode), sortie
 *  sur PRO2 MIXTE 2 (acquitte par videotex.c) ou CSI ? { (teleinfo.c).
 * =================================================================== */

/* Entree : 0 si le firmware n'a pas le mode 1 (amont) */
static unsigned char mixte_enter(void)
{
    if (!display80_init()) {
        vtx.terminal_mode = TERM_MODE_VIDEOTEX;
        display_status("80 colonnes: firmware sans mode 1");
        return 0;
    }
    ti_init(&ti);
    g_screen80 = 1;
    keyboard_set_extended(1);
    g_dbg_state = ST_MIXTE;
    display80_render_all(&ti);
    return 1;
}

/* Sortie : retour au mode Videotex, page effacee (STUM 1B p. 2759) */
static void mixte_leave(void)
{
    display80_leave();
    g_screen80 = 0;
    keyboard_set_extended(0);
    display_init();
    vtx_init(&vtx);
    vtx.terminal_mode = TERM_MODE_VIDEOTEX;
    status_bar_draw();
    g_dbg_state = ST_SESSION;
}

/* En mode Mixte, les sequences Protocole (ESC 3/9-3/B + 1..3 octets) sont
 * traitees par la couche Protocole, pas par l'ecran (STUM 1B partie 2 chap. 6) :
 * elles vont au decodeur Videotex (aiguillages, PRO2 MIXTE 2...), le reste
 * a l'ecran 80 colonnes. */
static unsigned char s_pro_pending;     /* ESC recu, en attente du 2e octet */
static unsigned char s_pro_left;        /* octets de PRO restant a router */

static void mixte_byte(unsigned char b)
{
    b &= 0x7F;
    if (s_pro_left) {
        vtx_process(&vtx, b);
        --s_pro_left;
        return;
    }
    if (s_pro_pending) {
        s_pro_pending = 0;
        if (b >= 0x39 && b <= 0x3B) {
            vtx_process(&vtx, 0x1B);
            vtx_process(&vtx, b);
            s_pro_left = (unsigned char)(b - 0x38);
            return;
        }
        ti_process(&ti, 0x1B);
        ti_process(&ti, b);
        return;
    }
    if (b == 0x1B) { s_pro_pending = 1; return; }
    ti_process(&ti, b);
}

/* Un octet du flux de session : au decodeur du mode courant, puis bascule
 * d'ecran si le flux l'a demandee (au meme octet : la suite de la rafale
 * doit deja aller au bon decodeur). */
static void session_byte(unsigned char byte)
{
    if (g_screen80) mixte_byte(byte); else vtx_process(&vtx, byte);

    if (!g_screen80 && vtx.terminal_mode == TERM_MODE_MIXED) {
        mixte_enter();
    } else if (g_screen80 && (vtx.terminal_mode == TERM_MODE_VIDEOTEX || ti.req_videotex)) {
        if (ti.req_videotex) {      /* CSI ? { : acquittement SEP 0x71 (STUM 1B p. 3349) */
            serial_send(0x13); serial_send(0x71); serial_tx_flush();
        }
        mixte_leave();
    } else if (g_screen80 && ti.req_beep) {
        ti.req_beep = 0;
        display_beep();
    }
}

/* ESC en session : question posee sur la barre de statut, la page reste
 * intacte. Retour : 1 = quitter (raccrocher, menu), 0 = reprendre. Le flux
 * serie continue d'etre draine vers le decodeur pendant l'attente. */
static unsigned char session_escape_page(vtx_context_t* ctx)
{
    unsigned char key;

    g_dbg_state = ST_ESCAPE;
    if (g_screen80) display80_status(&ti, "ESC: quitter? ESC=menu autre=reprendre", ti_save);
    else display_status("ESC: quitter? ESC=menu autre=reprendre");

    keyboard_flush();
    for (;;) {
        while (serial_poll()) {
            session_byte(serial_recv());
        }
        key = keyboard_scan();
        if (key == KEY_LOCAL_ESCAPE) return 1;
        if (key != KEY_NONE) break;
    }
    if (g_screen80) {
        display80_status_clear(&ti);
        g_dbg_state = ST_MIXTE;
    } else {
        status_bar_draw();
        ctx->full_refresh = 1;
        g_dbg_state = ST_SESSION;
    }
    return 0;
}

/* ===================================================================
 *  Relecture d'un fichier .vdt (menu 6) : le fichier est ouvert ici puis
 *  rejoue par la boucle de session elle-meme (g_replay = 1), qui lit les
 *  octets dans record.c a la place de la liaison. ESC (confirme) termine.
 * =================================================================== */
static unsigned char g_replay;
static char          rec_name[RECORD_NAME_MAX];

#define REPLAY_LIST_MAX 12
static unsigned char replay_idx[REPLAY_LIST_MAX];

/* Menu de relecture : liste des neoNN.vdt (choix par lettre A.., ENVOI =
 * dernier enregistre), ou saisie libre s'il n'y en a aucun. Ouvre le fichier
 * choisi (1) ou renonce (0). */
static unsigned char replay_prompt(void)
{
    unsigned char n, count, i, del;

  for (;;) {                            /* recommence apres un effacement */
    vtx_clear_page(&vtx);
    ui_header(&vtx, "RELIRE", ".vdt");
    count = record_list(replay_idx, REPLAY_LIST_MAX);

    if (count == 0) {
        ui_print(&vtx, 6, 2, "Aucun enregistrement sur la carte.", VTX_WHITE);
        ui_print(&vtx, 8, 2, "Nom du fichier (ENVOI = dernier) :", VTX_WHITE);
        ui_footer(&vtx, "ENVOI valide", "ESC retour");
        display_render_all(&vtx);
        n = ui_text_input(&vtx, 10, 2, rec_name, sizeof rec_name, 0);
        if (n == 0xFF) return 0;
        if (n == 0) {
            if (!g_settings.rec_index) return 0;
            record_make_name(rec_name, g_settings.rec_index);
        }
        if (replay_open(rec_name)) return 1;
        ui_fill(&vtx, 12, VTX_RED);
        ui_print(&vtx, 12, 2, "Fichier introuvable", VTX_WHITE);
        display_render_all(&vtx);
        keyboard_flush();
        while (keyboard_scan() == KEY_NONE) { }
        return 0;
    }

    for (i = 0; i < count; ++i) {
        /* nom compose dans vtx.drcs (libre au menu) */
        char* name = (char*)&vtx.drcs[0][0][0];
        record_make_name(name, replay_idx[i]);
        ui_item(&vtx, (unsigned char)(5 + i), (char)('A' + i), name,
                replay_idx[i] == g_settings.rec_index ? "dernier" : NULL, 0);
    }
    {
    del = 0;
    ui_footer(&vtx, "lettre rejoue  ENVOI = dernier", "Suppr");
    display_render_all(&vtx);

    keyboard_flush();
    for (;;) {
        unsigned char key = keyboard_scan();
        if (key == KEY_LOCAL_ESCAPE) {
            if (!del) return 0;
            del = 0;
            ui_footer(&vtx, "lettre rejoue  ENVOI = dernier", "Suppr");
            display_render_all(&vtx);
            continue;
        }
        if (!del && (key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_ENVOI) {
            if (!g_settings.rec_index) continue;
            record_make_name(rec_name, g_settings.rec_index);
            if (replay_open(rec_name)) return 1;
            continue;
        }
        /* Suppr (Annulation depuis la v0.8.1), Retour arriere ou Correction */
        if (!del && (key == 0x08 || key == 0x7F ||
            ((key & KEY_FUNC_FLAG) && ((key & 0x7F) == KEY_CORRECTION ||
                                       (key & 0x7F) == KEY_ANNULATION)))) {
            del = 1;
            ui_fill(&vtx, UI_ROW_FOOTER, VTX_RED);
            ui_print(&vtx, UI_ROW_FOOTER, 2, "EFFACER : quelle lettre ?", VTX_WHITE);
            ui_print_right(&vtx, UI_ROW_FOOTER, "ESC annule", VTX_WHITE);
            display_render_all(&vtx);
            continue;
        }
        if (key >= 'a' && key <= 'z') key = (unsigned char)(key - 'a' + 'A');
        if (key < 'A' || key >= 'A' + count) continue;
        if (del) {
            record_delete(replay_idx[key - 'A']);
            break;                      /* rafraichir la liste (boucle externe) */
        }
        record_make_name(rec_name, replay_idx[key - 'A']);
        if (replay_open(rec_name)) return 1;
        ui_fill(&vtx, 20, VTX_RED);
        ui_print(&vtx, 20, 2, "Fichier introuvable", VTX_WHITE);
        display_render_all(&vtx);
    }
    }
  }
}

int main(void)
{
    unsigned char byte;
    unsigned char key;
    unsigned char got_data;
    unsigned int  idle_counter;
    unsigned char ticks;
    unsigned char connected;
    unsigned char srv_idx;
    unsigned char carrier_pending;
    unsigned int  carrier_idle;
    unsigned char r;

    vtx_init(&vtx);
    display_init();
    keyboard_init();

    /* Reglages sauves sur la carte (neotel.cfg) : profil, aspect,
     * identification, dernier serveur. Sans fichier : valeurs par defaut. */
    settings_load();
    term_set_model(g_settings.model);
    display_set_look(g_settings.look);
    g_ident_enabled = g_settings.ident;

    splash_screen(&vtx);

    /* Liaison serie : routage AUTO (modem USB CDC si present, sinon UART UEXT)
     * puis ecran de rappel. Montee avant les menus : la page Config WiFi
     * dialogue avec le modem des le menu. */
    serial_init(s_route);
    interface_page(&vtx);

  /* Cycle complet : menus -> connexion -> session. ESC (confirme) quitte la
   * session, raccroche, et revient ICI avec un decodeur remis a neuf. */
  for (;;) {
    vtx_init(&vtx);
    vtx.cur_visible = 0;            /* menus : pas de curseur */
    status_bar_init();
    {
        unsigned char mode;

        for (;;) {
            mode = select_mode(&vtx);
            if (mode == MODE_WIFI) {
                wifi_config_page(&vtx);
                continue;
            }
            if (mode == MODE_REPLAY) {
                if (replay_prompt()) break;
                vtx_init(&vtx);
                vtx.cur_visible = 0;
                status_bar_init();
                continue;
            }
            if (mode == MODE_QUIT) {
                g_dbg_state = ST_EXIT;
                display_status_clear();
                return 0;           /* crt0 : retour a NeoBASIC */
            }
            break;
        }

        g_replay = (mode == MODE_REPLAY);
        vtx_clear_page(&vtx);
        if (g_replay) {
            status_server = rec_name;
            status_bar_draw();
            neo_delay_ms(100);
        } else {
        srv_idx = select_server(&vtx);
        if (srv_idx == 0xFE) continue;          /* ESC : retour au menu */
        vtx_clear_page(&vtx);
        status_server = (srv_idx == 255) ? g_settings.server : server_names[srv_idx];
        status_bar_draw();
        /* Memoriser le serveur choisi (la saisie libre est deja dans
         * g_settings.server, ecrite par select_server) */
        g_settings.server_idx = srv_idx;
        settings_save();

        (void)mode;
        neo_delay_ms(100);

        r = 1;
        while (!modem_connect(&vtx, srv_idx)) {
            r = connect_failed_page(&vtx, &srv_idx);
            if (r != 1) {
                break;      /* 0: session forcee, 2: ESC -> menu */
            }
        }
        vtx_clear_page(&vtx);
        if (r == 2) {
            at_hangup();
            continue;
        }
        }   /* !g_replay */
    }

    /* Session : curseur visible par defaut (le serveur le pilote par CON/COFF). */
    vtx.cur_visible = 1;
    g_dbg_state = g_replay ? ST_REPLAY : ST_SESSION;
    connected = 0;
    idle_counter = 0;
    carrier_pending = 0;
    carrier_idle = 0;
    at_carrier_reset();
    tick_reset();
    status_secs = 0;
    status_sec_ticks = 0;
    status_set_connected(0);
    status_bar_draw();
    display_render_all(&vtx);
    keyboard_flush();

    for (;;) {
        /* 1. Drainer la reception (d'abord l'octet garde par modem_connect) */
        got_data = 0;
        if (s_have_first) {
            s_have_first = 0;
            record_byte(s_first_byte);
            session_byte(s_first_byte);
            got_data = 1;
        }
        while (g_replay ? replay_pending() : serial_poll()) {
            if (g_replay) {
                byte = replay_next();
            } else {
                byte = serial_recv();
                record_byte(byte);
            }
            if (!g_replay && at_carrier_watch(byte)) {
                carrier_pending = 1;
                carrier_idle = 0;
            } else if (carrier_pending && byte != 0x0D && byte != 0x0A) {
                carrier_pending = 0;    /* la page continue : fausse alerte */
            }
            session_byte(byte);
            got_data = 1;
        }

        /* 2. Clavier */
        key = keyboard_scan();
        if (key == KEY_TOGGLE_RENDER) {
            display_set_look(display_get_look() == DISPLAY_LOOK_GREY
                             ? DISPLAY_LOOK_COLOR : DISPLAY_LOOK_GREY);
            status_bar_draw();
        } else if (key == KEY_LOCAL_CLEAR) {
            vtx_clear_page(&vtx);
            vtx.full_refresh = 1;
        } else if (key == KEY_LOCAL_RECORD && !g_replay) {
            const char* msg;
            if (record_active()) {
                record_stop();
                msg = "Enregistrement termine";
            } else {
                record_make_name(rec_name, (unsigned char)(g_settings.rec_index + 1));
                if (record_start(rec_name)) {
                    g_settings.rec_index = (unsigned char)(rec_name[3] - '0') * 10
                                         + (unsigned char)(rec_name[4] - '0');
                    settings_save();
                    msg = "Enregistrement (CTRL+O = fin)";
                } else {
                    msg = "Enregistrement impossible";
                }
            }
            if (g_screen80) display80_status(&ti, msg, ti_save);
            else { display_status(msg); status_bar_draw(); }
        } else if (key == KEY_LOCAL_RESET && !g_screen80) {
            serial_init(s_route);
            display_status("Liaison serie reinitialisee");
        } else if (key == KEY_LOCAL_ESCAPE) {
            if (session_escape_page(&vtx)) {
                break;
            }
        } else if (key != KEY_NONE && !g_replay) {
            keyboard_process(&vtx, key);
        }
        if (g_replay && g_dbg_state == ST_REPLAY && !replay_pending()) {
            g_dbg_state = ST_REPLAY_END;        /* fichier entierement rejoue */
        }
        if (g_screen80 && key == KEY_TOGGLE_RENDER) {
            /* pas de barre de statut ni de palette en mode 1 : sans effet */
        }

        /* 3. Base de temps, indicateur de connexion, porteuse */
        ticks = tick_10ms();
        status_tick(ticks);
        if (got_data) {
            idle_counter = 0;
            if (!connected) {
                connected = 1;
                status_set_connected(1);
            }
        } else {
            if (carrier_pending &&
                (carrier_idle += ticks) >= CARRIER_CONFIRM_TICKS) {
                carrier_pending = 0;
                carrier_idle = 0;
                connected = 0;
                at_carrier_reset();
                record_stop();
                if (g_screen80) mixte_leave();
                r = carrier_lost_page(&vtx);
                if (r == 2) {
                    break;
                }
                if (r == 1) {
                    vtx_clear_page(&vtx);
                    r = 1;
                    while (!modem_connect(&vtx, srv_idx)) {
                        r = connect_failed_page(&vtx, &srv_idx);
                        if (r != 1) break;
                    }
                    vtx_clear_page(&vtx);
                    keyboard_flush();
                    if (r == 2) {
                        at_hangup();
                        break;
                    }
                }
                g_dbg_state = ST_SESSION;
                status_set_connected(0);
                vtx.full_refresh = 1;
                idle_counter = 0;
                continue;
            }
            if (idle_counter < 60000u) {
                idle_counter += ticks;
            }
            if (connected && idle_counter >= LINK_IDLE_TICKS) {
                connected = 0;
                status_set_connected(0);
            }
        }

        /* 4. Rendu adaptatif : une ligne par passe, puis continuer tant que
         * rien d'autre n'attend (ni octet serie, ni touche). */
        if (g_screen80) {
            display80_render(&ti);
            while (display80_dirty_pending(&ti) &&
                   !serial_poll() && !keyboard_pending()) {
                display80_render(&ti);
            }
        } else {
            display_render(&vtx);
            while (display_dirty_pending(&vtx) &&
                   !serial_poll() && !keyboard_pending()) {
                display_render(&vtx);
            }
        }

        /* 5. Clignotement : 500 ms par phase */
        blink_ticks += ticks;
        if (blink_ticks >= 50) {
            blink_ticks = 0;
            if (g_screen80) display80_blink_toggle(&ti);
            else display_blink_toggle(&vtx);
        }
    }

    /* Sortie de session par ESC : raccrocher proprement, puis menu. */
    record_stop();
    if (g_screen80) mixte_leave();
    if (g_replay) { replay_close(); g_replay = 0; }
    else at_hangup();
    keyboard_flush();
    g_dbg_state = ST_HUNGUP;
    ++g_dbg_hangups;
  }
}
