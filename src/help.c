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

/* Deux pages, deux langues. Chaque page = titre de section puis des paires
 * (touche, action), terminees par NULL ; rendu en deux colonnes. */
static const char* const fr1[] = {
    "Menu",
    "1",       "Modem AT (choix du serveur)",
    "2",       "Config WiFi du modem",
    "3",       "Terminal Minitel 1B / 2",
    "4",       "Aspect couleur / gris",
    "5",       "Identification ON / OFF",
    "6",       "Relire un enregistrement",
    "7",       "Son ON / OFF",
    "Fleches", "choisir, ENVOI valide",
    "ESC",     "quitter vers le systeme",
    0
};
static const char* const en1[] = {
    "Menu",
    "1",       "AT modem (server list)",
    "2",       "Modem WiFi setup",
    "3",       "Minitel 1B / 2 terminal",
    "4",       "Colour / grey look",
    "5",       "Identification ON / OFF",
    "6",       "Replay a recording",
    "7",       "Sound ON / OFF",
    "Arrows",  "select, ENVOI confirms",
    "ESC",     "quit to the system",
    0
};
static const char* const fr2[] = {
    "En session",
    "F1-F9",   "Sommaire .. Connexion/Fin",
    "<- ->",   "Retour / Suite",
    "Suppr",   "Annulation",
    "Entree",  "Envoi",
    "CTRL+O",  "enregistrer / arreter",
    "CTRL+D",  "couleur / gris",
    "F10",     "cette aide",
    "CTRL+L",  "effacer la page",
    "CTRL+F",  "reinitialiser la liaison",
    "ESC ESC", "raccrocher, retour menu",
    "Menu 6",  "lettre = rejoue un .vdt,",
    "",        "Suppr + lettre = efface",
    0
};
static const char* const en2[] = {
    "In a session",
    "F1-F9",   "Index .. Connect/End",
    "<- ->",   "Back / Next",
    "Del",     "Cancel",
    "Return",  "Send (ENVOI)",
    "CTRL+O",  "start / stop recording",
    "CTRL+D",  "colour / grey",
    "F10",     "this help",
    "CTRL+L",  "clear the page",
    "CTRL+F",  "reset the serial link",
    "ESC ESC", "hang up, back to menu",
    "Menu 6",  "letter = replay a .vdt,",
    "",        "Del + letter = delete",
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
        unsigned char row = 5, key;
        char num[4];

        vtx_clear_page(ctx);
        num[0] = (char)('1' + page); num[1] = '/'; num[2] = '0' + HELP_PAGES; num[3] = 0;
        ui_header(ctx, lang ? "NEOTEL HELP" : "AIDE NEOTEL", num);
        ui_print(ctx, row, 2, *line++, VTX_CYAN);       /* titre de section */
        for (row += 2; *line && row < UI_ROW_FOOTRULE - 1; line += 2, ++row) {
            ui_print(ctx, row, 2, line[0], VTX_YELLOW);
            ui_print(ctx, row, 11, line[1], VTX_WHITE);
        }
        ui_footer(ctx, lang ? "Space next  P back  L lang"
                            : "Espace suite  P prec.  L langue", "ESC");
        display_render_all(ctx);
        g_dbg_state = 16;              /* ST_HELP */

        keyboard_flush();
        do { key = keyboard_scan(); } while (key == KEY_NONE);

        if (key == KEY_LOCAL_ESCAPE) return;
        if ((key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_SOMMAIRE) return;
        if (key == 'L' || key == 'l') { g_settings.lang ^= 1; settings_save(); }
        else if (key == 'P' || key == 'p' || key == KEY_ARROW_LEFT ||
                 ((key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_RETOUR)) { if (page) --page; }
        else if (key == ' ' || key == 0x0D || key == 'N' || key == 'n' || key == KEY_ARROW_RIGHT ||
                 ((key & KEY_FUNC_FLAG) && (key & 0x7F) == KEY_SUITE)) {
            if (page + 1 < HELP_PAGES) ++page; else return;
        }
    }
}
