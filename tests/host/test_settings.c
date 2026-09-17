/**
 * @file test_settings.c
 * @brief Tests hote des reglages persistants (settings.c) sur le fichier
 *        en memoire de neo_stub.c : defauts sans fichier, aller-retour,
 *        magie/version invalides, bornes d'un fichier corrompu, echec d'ecriture.
 */

#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "terminal.h"
#include "display.h"
#include "neo_stub.h"

static int run, pass;
#define CHECK(c, name) do { ++run; if (c) ++pass; else printf("FAIL : %s (ligne %d)\n", name, __LINE__); } while (0)

int main(void)
{
    host_file_len = 0;
    CHECK(settings_load() == 0 && g_settings.model == TERM_MINITEL_1B &&
          g_settings.look == DISPLAY_LOOK_COLOR && g_settings.ident == 0 &&
          g_settings.server_idx == 0 && g_settings.magic0 == 'N' && g_settings.version == SETTINGS_VERSION &&
          g_settings.rec_index == 0 && g_settings.sound == 1,
          "sans fichier : defauts");

    g_settings.model = TERM_MINITEL_2; g_settings.look = DISPLAY_LOOK_GREY; g_settings.ident = 1;
    g_settings.server_idx = 255; strcpy(g_settings.server, "mon.serveur.fr:1234");
    g_settings.rec_index = 17; g_settings.sound = 0;
    CHECK(settings_save() == 1 && host_file_len == (int)sizeof(settings_t), "sauvegarde : taille de la structure");
    memset(&g_settings, 0xEE, sizeof g_settings);
    CHECK(settings_load() == 1 && g_settings.model == TERM_MINITEL_2 && g_settings.look == DISPLAY_LOOK_GREY &&
          g_settings.ident == 1 && g_settings.server_idx == 255 && strcmp(g_settings.server, "mon.serveur.fr:1234") == 0 &&
          g_settings.rec_index == 17 && g_settings.sound == 0,
          "aller-retour");

    host_file[0] = 'X';
    CHECK(settings_load() == 0 && g_settings.model == TERM_MINITEL_1B, "magie invalide : defauts");
    host_file[0] = 'N'; host_file[2] = SETTINGS_VERSION + 1;
    CHECK(settings_load() == 0 && g_settings.look == DISPLAY_LOOK_COLOR, "version inconnue : defauts");
    host_file[2] = 1; host_file[7 + SETTINGS_SERVER_MAX] = 55;
    CHECK(settings_load() == 1 && g_settings.version == SETTINGS_VERSION && g_settings.rec_index == 0 &&
          g_settings.sound == 1 && g_settings.look == DISPLAY_LOOK_GREY,
          "version 1 acceptee : compteur d'enregistrement a 0, son actif");
    host_file[2] = 2; host_file[7 + SETTINGS_SERVER_MAX + 1] = 0;
    CHECK(settings_load() == 1 && g_settings.sound == 1 && g_settings.rec_index == 55,
          "version 2 acceptee : son actif, compteur conserve");
    host_file[2] = SETTINGS_VERSION; host_file[7 + SETTINGS_SERVER_MAX + 1] = 9;
    CHECK(settings_load() == 1 && g_settings.sound == 1, "son > 1 : ramene a 1");
    host_file[2] = SETTINGS_VERSION; host_file[7 + SETTINGS_SERVER_MAX] = 150;
    CHECK(settings_load() == 1 && g_settings.rec_index == 0, "compteur > 99 : remis a 0");
    host_file[2] = SETTINGS_VERSION; host_file[3] = 7; host_file[4] = 9; host_file[5] = 3;
    memset(host_file + 7, 'a', SETTINGS_SERVER_MAX);
    CHECK(settings_load() == 1 && g_settings.model == TERM_MINITEL_1B && g_settings.look == DISPLAY_LOOK_COLOR &&
          g_settings.ident == 1 && g_settings.server[SETTINGS_SERVER_MAX - 1] == 0,
          "fichier corrompu : valeurs bornees, chaine terminee");

    host_file_ro = 1;
    CHECK(settings_save() == 0, "ecriture refusee : 0, sans plantage");
    host_file_ro = 0;

    printf("test_settings : %d/%d OK\n", pass, run);
    return pass == run ? 0 : 1;
}
