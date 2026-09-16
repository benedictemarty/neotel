/**
 * @file test_keyboard.c
 * @brief Tests hote du clavier Neo6502 -> Minitel (keyboard.c)
 *
 * Le clavier est scripte par neo_stub.c (file 2,1 / 2,2, etat HID 1,2) :
 *   - keyboard_translate : Entree, retour arriere, ESC, CTRL+lettre, hotkeys
 *     F1-F10, fleches distinguees de CTRL+A/D/S/W par l'etat HID, ASCII ;
 *   - keyboard_scan : file vide, injection de test, fleche physique ;
 *   - keyboard_process : SEP + code, aiguillages PRO3, fleches en mode curseur.
 */

#include <stdio.h>
#include <string.h>
#include "keyboard.h"
#include "neo.h"
#include "neo_stub.h"

/* vtx_process capture (echo local) */
static unsigned char echo[16]; static int echon;
void vtx_process(vtx_context_t* c, unsigned char b) { (void)c; if (echon < 16) echo[echon++] = b; }

static int run, pass;
#define CHECK(c, name) do { ++run; if (c) ++pass; else printf("FAIL : %s (ligne %d)\n", name, __LINE__); } while (0)

int main(void)
{
    vtx_context_t ctx;

    keyboard_init();

    /* --- traduction pure --- */
    CHECK(keyboard_translate(0x0D, 0) == (KEY_FUNC_FLAG | KEY_ENVOI), "Entree = Envoi");
    CHECK(keyboard_translate(0x08, 0) == (KEY_FUNC_FLAG | KEY_CORRECTION), "Backspace = Correction");
    CHECK(keyboard_translate(0x1A, 0) == (KEY_FUNC_FLAG | KEY_CORRECTION), "Suppr = Correction");
    CHECK(keyboard_translate(0x1B, 0) == KEY_LOCAL_ESCAPE, "ESC = sortie locale");
    CHECK(keyboard_translate(0x01, 0) == (KEY_FUNC_FLAG | KEY_ANNULATION), "CTRL+A = Annulation");
    CHECK(keyboard_translate(0x03, 0) == (KEY_FUNC_FLAG | KEY_CONNEXION), "CTRL+C = Connexion/Fin");
    CHECK(keyboard_translate(0x05, 0) == (KEY_FUNC_FLAG | KEY_REPETITION), "CTRL+E = Repetition");
    CHECK(keyboard_translate(0x07, 0) == (KEY_FUNC_FLAG | KEY_GUIDE), "CTRL+G = Guide");
    CHECK(keyboard_translate(0x0E, 0) == (KEY_FUNC_FLAG | KEY_SUITE), "CTRL+N = Suite");
    CHECK(keyboard_translate(0x12, 0) == (KEY_FUNC_FLAG | KEY_RETOUR), "CTRL+R = Retour");
    CHECK(keyboard_translate(0x13, 0) == (KEY_FUNC_FLAG | KEY_SOMMAIRE), "CTRL+S = Sommaire");
    CHECK(keyboard_translate(0x04, 0) == KEY_TOGGLE_RENDER, "CTRL+D = aspect");
    CHECK(keyboard_translate(0x0C, 0) == KEY_LOCAL_CLEAR, "CTRL+L = effacer");
    CHECK(keyboard_translate(0x06, 0) == KEY_LOCAL_RESET, "CTRL+F = reset serie");
    CHECK(keyboard_translate(0x09, 0) == KEY_NONE, "TAB ignore");
    CHECK(keyboard_translate(0x17, 0) == KEY_NONE, "CTRL+W sans fleche : ignore");
    CHECK(keyboard_translate('a', 0) == 'a' && keyboard_translate('Z', 0) == 'Z', "ASCII passe tel quel");
    CHECK(keyboard_translate(0x7E, 0) == 0x7E && keyboard_translate(0x7F, 0) == (KEY_FUNC_FLAG | KEY_CORRECTION), "bornes ASCII");
    CHECK(keyboard_translate(0, 0) == KEY_NONE, "0 = rien");

    /* hotkeys */
    CHECK(keyboard_translate(0x81, 0) == (KEY_FUNC_FLAG | KEY_SOMMAIRE), "F1 = Sommaire");
    CHECK(keyboard_translate(0x82, 0) == (KEY_FUNC_FLAG | KEY_ANNULATION), "F2 = Annulation");
    CHECK(keyboard_translate(0x83, 0) == (KEY_FUNC_FLAG | KEY_RETOUR), "F3 = Retour");
    CHECK(keyboard_translate(0x84, 0) == (KEY_FUNC_FLAG | KEY_REPETITION), "F4 = Repetition");
    CHECK(keyboard_translate(0x85, 0) == (KEY_FUNC_FLAG | KEY_GUIDE), "F5 = Guide");
    CHECK(keyboard_translate(0x86, 0) == (KEY_FUNC_FLAG | KEY_CORRECTION), "F6 = Correction");
    CHECK(keyboard_translate(0x87, 0) == (KEY_FUNC_FLAG | KEY_SUITE), "F7 = Suite");
    CHECK(keyboard_translate(0x88, 0) == (KEY_FUNC_FLAG | KEY_ENVOI), "F8 = Envoi");
    CHECK(keyboard_translate(0x89, 0) == (KEY_FUNC_FLAG | KEY_CONNEXION), "F9 = Connexion/Fin");
    CHECK(keyboard_translate(0x8A, 0) == KEY_TOGGLE_RENDER, "F10 = aspect");
    CHECK(keyboard_translate(0x8B, 0) == KEY_NONE, "$8B : inconnu");

    /* fleches : meme code console que CTRL+lettre, tranchees par le HID */
    CHECK(keyboard_translate(0x01, HID_LEFT) == KEY_ARROW_LEFT, "fleche gauche");
    CHECK(keyboard_translate(0x04, HID_RIGHT) == KEY_ARROW_RIGHT, "fleche droite");
    CHECK(keyboard_translate(0x17, HID_UP) == KEY_ARROW_UP, "fleche haut");
    CHECK(keyboard_translate(0x13, HID_DOWN) == KEY_ARROW_DOWN, "fleche bas");

    /* --- scan sur la file du firmware --- */
    CHECK(keyboard_scan() == KEY_NONE && keyboard_pending() == 0, "file vide");
    host_key_push('q');
    CHECK(keyboard_pending() == 1, "pending avec une touche");
    CHECK(keyboard_scan() == 'q', "scan lit la file");
    host_keys_down[HID_DOWN] = 1;
    host_key_push(0x13);
    CHECK(keyboard_scan() == KEY_ARROW_DOWN, "scan : fleche bas physique");
    host_keys_down[HID_DOWN] = 0;
    host_key_push(0x13);
    CHECK(keyboard_scan() == (KEY_FUNC_FLAG | KEY_SOMMAIRE), "scan : CTRL+S sans fleche");
    keyboard_inject = '7';
    CHECK(keyboard_pending() == 1 && keyboard_scan() == '7' && keyboard_inject == 0, "injection de test");
    host_key_push('a'); host_key_push('b');
    keyboard_flush();
    CHECK(keyboard_scan() == KEY_NONE, "flush vide la file");
    keyboard_inject = 'x';
    keyboard_flush();
    CHECK(keyboard_scan() == 'x', "flush conserve l'injection de test");

    /* --- emission --- */
    memset(&ctx, 0, sizeof ctx);
    ctx.aiguillages = AIG_KBD_TO_MDM;
    host_tx_reset(); echon = 0;
    keyboard_process(&ctx, KEY_FUNC_FLAG | KEY_ENVOI);
    CHECK(host_tx_len == 2 && host_tx[0] == 0x13 && host_tx[1] == 0x41, "Envoi : SEP $41 vers le modem");
    keyboard_process(&ctx, 'A');
    CHECK(host_tx_len == 3 && host_tx[2] == 'A' && echon == 0, "ASCII vers le modem, pas d'echo");
    keyboard_process(&ctx, KEY_ARROW_LEFT);
    CHECK(host_tx_len == 3, "fleche hors mode curseur : rien");
    ctx.kbd_cursor = 1;
    keyboard_process(&ctx, KEY_ARROW_LEFT);
    CHECK(host_tx_len == 6 && host_tx[3] == 0x1B && host_tx[4] == 0x5B && host_tx[5] == 0x44, "fleche gauche : CSI D");
    keyboard_process(&ctx, KEY_ARROW_UP);
    CHECK(host_tx_len == 9 && host_tx[8] == 0x41, "fleche haut : CSI A");
    keyboard_process(&ctx, KEY_NONE);
    CHECK(host_tx_len == 9, "KEY_NONE : rien");
    ctx.aiguillages = AIG_KBD_TO_SCR;
    host_tx_reset(); echon = 0;
    keyboard_process(&ctx, 'B');
    CHECK(host_tx_len == 0 && echon == 1 && echo[0] == 'B', "aiguillage clavier->ecran : echo local seul");
    ctx.aiguillages = AIG_KBD_TO_SCR | AIG_KBD_TO_MDM;
    keyboard_process(&ctx, 'C');
    CHECK(host_tx_len == 1 && echon == 2, "les deux aiguillages");

    printf("test_keyboard : %d/%d OK\n", pass, run);
    return pass == run ? 0 : 1;
}
