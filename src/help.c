/**
 * @file help.c
 * @brief Ecran d'aide bilingue (voir help.h). Texte compact en RODATA.
 */
#include "help.h"
#include "ui.h"
#include "keyboard.h"
#include "display.h"
#include "settings.h"

extern unsigned char g_dbg_state;  /* pose par help_show pour les tests cible */

/* Deux pages, deux langues. Chaque page = lignes terminees par NULL. */
static const char* const fr1[] = {
    "Menu :",
    "1 Modem  2 WiFi  3 Term. 1B/2",
    "4 Couleur/gris  5 Identification",
    "6 Relire .vdt  7 Son  ESC Sortie",
    "Serveur 1/2/3ws/4 saisie  ENVOI=der.",
    0
};
static const char* const en1[] = {
    "Menu:",
    "1 Modem  2 WiFi  3 Term. 1B/2",
    "4 Colour/grey  5 Identification",
    "6 Replay .vdt  7 Sound  ESC Quit",
    "Server 1/2/3ws/4 free   ENVOI=last",
    0
};
static const char* const fr2[] = {
    "En session :",
    "F1-F9 Sommaire..Connexion/Fin",
    "CTRL+O enregistrer/arreter",
    "F10 couleur/gris  CTRL+L effacer",
    "CTRL+F reinit.  ESC ESC menu",
    "Menu 6 : lettre rejoue, Suppr+lettre",
    "efface un .vdt.",
    0
};
static const char* const en2[] = {
    "In a session:",
    "F1-F9 Index..Connect/End",
    "CTRL+O start/stop recording",
    "F10 colour/grey  CTRL+L clear",
    "CTRL+F reset  ESC ESC menu",
    "Menu 6: a letter replays,",
    "Suppr+letter deletes a .vdt.",
    0
};

#define HELP_PAGES 2
static const char* const* const PAGES[2][HELP_PAGES] = {
    { fr1, fr2 }, { en1, en2 },
};

void help_show(vtx_context_t* ctx)
{
    unsigned char page = 0;

    for (;;) {
        unsigned char lang = g_settings.lang ? 1 : 0;
        const char* const* line = PAGES[lang][page];
        unsigned char row = 4, key;
        char num[4];

        vtx_clear_page(ctx);
        ui_print(ctx, 1, 2, lang ? "NEOTEL HELP" : "AIDE NEOTEL", VTX_CYAN);
        num[0] = (char)('1' + page); num[1] = '/'; num[2] = '0' + HELP_PAGES; num[3] = 0;
        ui_print(ctx, 1, 36, num, VTX_WHITE);
        for (; *line && row < 22; ++line, ++row)
            ui_print(ctx, row, 2, *line, VTX_WHITE);
        ui_print(ctx, 23, 2, lang ? "Space/P/L lang/ESC"
                                  : "Espace/P/L langue/ESC", VTX_YELLOW);
        display_render_all(ctx);
        g_dbg_state = 16;              /* ST_HELP */

        keyboard_flush();
        do { key = keyboard_scan(); } while (key == KEY_NONE);

        if (key == KEY_LOCAL_ESCAPE) return;
        if ((key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_SOMMAIRE) return;
        if (key == 'L' || key == 'l') { g_settings.lang ^= 1; settings_save(); }
        else if (key == 'P' || key == 'p') { if (page) --page; }
        else if (key == ' ' || key == 0x0D || key == 'N' || key == 'n' ||
                 ((key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_SUITE)) {
            if (page + 1 < HELP_PAGES) ++page; else return;
        }
    }
}
