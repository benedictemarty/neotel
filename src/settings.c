/**
 * @file settings.c
 * @brief Reglages persistants (voir settings.h)
 */

#include <string.h>
#include "settings.h"
#include "terminal.h"
#include "display.h"
#include "neo.h"

settings_t g_settings;

/* Nom de fichier prefixe par sa longueur (API fichiers) */
static const unsigned char s_name[] = { 10, 'n', 'e', 'o', 't', 'e', 'l', '.', 'c', 'f', 'g' };

#define NEO_G_FILE      3
#define NEO_F_LOAD_FILE 2
#define NEO_F_STORE_FILE 3

static void settings_defaults(void)
{
    memset(&g_settings, 0, sizeof g_settings);
    g_settings.magic0 = SETTINGS_MAGIC0;
    g_settings.magic1 = SETTINGS_MAGIC1;
    g_settings.version = SETTINGS_VERSION;
    g_settings.model = TERM_MINITEL_1B;
    g_settings.look = DISPLAY_LOOK_COLOR;
    g_settings.ident = 0;
    g_settings.server_idx = 0;
}

unsigned char settings_load(void)
{
    settings_defaults();
    neo_wait();
    NEO_P[0] = (unsigned char)((unsigned int)s_name & 0xFF);
    NEO_P[1] = (unsigned char)((unsigned int)s_name >> 8);
    NEO_P[2] = (unsigned char)((unsigned int)&g_settings & 0xFF);
    NEO_P[3] = (unsigned char)((unsigned int)&g_settings >> 8);
    neo_call(NEO_G_FILE, NEO_F_LOAD_FILE);
    if (NEO_ERR || g_settings.magic0 != SETTINGS_MAGIC0 ||
        g_settings.magic1 != SETTINGS_MAGIC1 ||
        g_settings.version > SETTINGS_VERSION) {
        settings_defaults();
        return 0;
    }
    /* Version 1 (v0.3.0-v0.4.3) : sans compteur d'enregistrement */
    if (g_settings.version < 2) g_settings.rec_index = 0;
    g_settings.version = SETTINGS_VERSION;
    /* Bornes : un fichier corrompu ne doit pas sortir des valeurs admises */
    if (g_settings.model > TERM_MINITEL_2) g_settings.model = TERM_MINITEL_1B;
    if (g_settings.look > DISPLAY_LOOK_GREY) g_settings.look = DISPLAY_LOOK_COLOR;
    if (g_settings.rec_index > 99) g_settings.rec_index = 0;
    g_settings.ident = g_settings.ident ? 1 : 0;
    g_settings.server[SETTINGS_SERVER_MAX - 1] = 0;
    return 1;
}

unsigned char settings_save(void)
{
    neo_wait();
    NEO_P[0] = (unsigned char)((unsigned int)s_name & 0xFF);
    NEO_P[1] = (unsigned char)((unsigned int)s_name >> 8);
    NEO_P[2] = (unsigned char)((unsigned int)&g_settings & 0xFF);
    NEO_P[3] = (unsigned char)((unsigned int)&g_settings >> 8);
    NEO_P[4] = (unsigned char)(sizeof g_settings & 0xFF);
    NEO_P[5] = (unsigned char)(sizeof g_settings >> 8);
    neo_call(NEO_G_FILE, NEO_F_STORE_FILE);
    return NEO_ERR ? 0 : 1;
}
