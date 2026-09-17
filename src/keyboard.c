/**
 * @file keyboard.c
 * @brief Clavier USB du Neo6502 avec mapping Minitel (voir keyboard.h)
 */

#include "keyboard.h"
#include "serial.h"
#include "neo.h"

unsigned char keyboard_inject;
static unsigned char s_extended;

void keyboard_set_extended(unsigned char on) { s_extended = on ? 1 : 0; }
unsigned char keyboard_extended(void) { return s_extended; }

/* Chaines prefixees des hotkeys F1-F10 : un octet prive chacune. Statiques :
 * le firmware les copie a la definition, mais on ne parie pas dessus. */
static unsigned char hotkey_str[10][2];

/* ===================================================================
 *  Initialisation : F1-F10 -> octets prives $81-$8A
 * =================================================================== */

void keyboard_init(void)
{
    unsigned char i;

    keyboard_inject = 0;
    for (i = 0; i < 10; ++i) {
        hotkey_str[i][0] = 1;
        hotkey_str[i][1] = (unsigned char)(KEY_HOTKEY_BASE + i);
#ifndef TEST_HOST
        /* Adresse 16 bits de la chaine prefixee (RAM 6502) */
        neo_wait();
        NEO_P[0] = (unsigned char)(i + 1);
        NEO_P[2] = (unsigned char)((unsigned int)hotkey_str[i] & 0xFF);
        NEO_P[3] = (unsigned char)((unsigned int)hotkey_str[i] >> 8);
        neo_call(NEO_G_CONSOLE, NEO_F_DEF_HOTKEY);
#endif
    }
}

/* ===================================================================
 *  Acces a la file clavier du firmware
 * =================================================================== */

/* API 2,2 : P0 = $FF si une touche ATTEND dans la file, 0 sinon
 * (dispatch_code.h du firmware : KBDIsKeyAvailable() ? 0xFF : 0). Le sens
 * etait inverse jusqu'en v0.8.0 : la premiere touche reelle restait dans la
 * file et le clavier semblait mort (les tests injectaient via
 * keyboard_inject, qui ne passe pas par la file). */
static unsigned char queue_empty(void)
{
    neo_wait();
    NEO_P[0] = 0;
    neo_call(NEO_G_CONSOLE, NEO_F_CON_STATUS);
    return NEO_P[0] != 0xFF;
}

static unsigned char queue_read(void)
{
    neo_wait();
    NEO_P[0] = 0;
    neo_call(NEO_G_CONSOLE, NEO_F_READ_CHAR);
    return NEO_P[0];
}

static unsigned char hid_down(unsigned char hid)
{
    neo_wait();
    NEO_P[0] = hid;
    NEO_P[1] = 0;
    neo_call(NEO_G_SYSTEM, NEO_F_KEY_STATUS);
    return NEO_P[0];
}

/* Les fleches partagent leurs codes console avec CTRL+A/D/S/W : on regarde
 * quelle fleche est physiquement enfoncee. 0 si aucune. */
static unsigned char arrow_down(void)
{
    if (hid_down(HID_LEFT))  return HID_LEFT;
    if (hid_down(HID_RIGHT)) return HID_RIGHT;
    if (hid_down(HID_UP))    return HID_UP;
    if (hid_down(HID_DOWN))  return HID_DOWN;
    return 0;
}

unsigned char keyboard_pending(void)
{
    if (keyboard_inject) return 1;
    return queue_empty() ? 0 : 1;
}

void keyboard_flush(void)
{
    unsigned int guard = 0;
    /* keyboard_inject n'est pas purge : une touche injectee par un test
     * vise l'ecran suivant, quel que soit le moment de son depot. */
    while (!queue_empty() && ++guard < 256) {
        queue_read();
    }
}

/* ===================================================================
 *  Traduction
 * =================================================================== */

/* F1..F9 -> touches Minitel (ordre de la colonne de touches du 1B) */
static const unsigned char hotkey_func[9] = {
    KEY_SOMMAIRE, KEY_ANNULATION, KEY_RETOUR, KEY_REPETITION, KEY_GUIDE,
    KEY_CORRECTION, KEY_SUITE, KEY_ENVOI, KEY_CONNEXION,
};

unsigned char keyboard_translate(unsigned char ch, unsigned char arrow_hid)
{
    if (ch == 0) return KEY_NONE;

    /* Hotkeys F1-F10 */
    if (ch >= KEY_HOTKEY_BASE && ch < KEY_HOTKEY_BASE + 9) {
        return KEY_FUNC_FLAG | hotkey_func[ch - KEY_HOTKEY_BASE];
    }
    if (ch == KEY_HOTKEY_BASE + 9) return KEY_TOGGLE_RENDER;     /* F10 */

    /* Fleches (codes console partages avec CTRL+lettre) */
    if (arrow_hid) {
        switch (arrow_hid) {
            case HID_LEFT:  return KEY_ARROW_LEFT;
            case HID_RIGHT: return KEY_ARROW_RIGHT;
            case HID_UP:    return KEY_ARROW_UP;
            case HID_DOWN:  return KEY_ARROW_DOWN;
            default: break;
        }
    }

    if (s_extended) {
        /* Mode Mixte : codes de controle tels quels (Entree = CR, retour
         * arriere = BS, TAB = HT, CTRL+lettre = C0) ; ESC reste local. */
        if (ch == 0x1B) return KEY_LOCAL_ESCAPE;
        if (ch == 0x0F) return KEY_LOCAL_RECORD;    /* CTRL+O local, meme en etendu */
        if (ch == 0x1A) return 0x08;            /* Suppr -> BS */
        if (ch < 0x20) return (unsigned char)(KEY_FUNC_FLAG | 0x20 | ch);   /* C0 brut, marque */
        if (ch < 0x7F) return ch;
        return KEY_NONE;
    }

    switch (ch) {
        case 0x0D:  /* Entree = Envoi */
            return KEY_FUNC_FLAG | KEY_ENVOI;
        case 0x08:  /* Retour arriere (CC_BACKSPACE) = Correction */
        case 0x1A:  /* Suppr (CC_DELETE) = Correction */
        case 0x7F:
            return KEY_FUNC_FLAG | KEY_CORRECTION;
        case 0x1B:  /* ESC = sortie locale */
            return KEY_LOCAL_ESCAPE;
        case 0x01:  return KEY_FUNC_FLAG | KEY_ANNULATION;   /* CTRL+A */
        case 0x03:  return KEY_FUNC_FLAG | KEY_CONNEXION;    /* CTRL+C */
        case 0x05:  return KEY_FUNC_FLAG | KEY_REPETITION;   /* CTRL+E */
        case 0x07:  return KEY_FUNC_FLAG | KEY_GUIDE;        /* CTRL+G */
        case 0x0E:  return KEY_FUNC_FLAG | KEY_SUITE;        /* CTRL+N */
        case 0x12:  return KEY_FUNC_FLAG | KEY_RETOUR;       /* CTRL+R */
        case 0x13:  return KEY_FUNC_FLAG | KEY_SOMMAIRE;     /* CTRL+S */
        case 0x04:  return KEY_TOGGLE_RENDER;                /* CTRL+D */
        case 0x0C:  return KEY_LOCAL_CLEAR;                  /* CTRL+L */
        case 0x06:  return KEY_LOCAL_RESET;                  /* CTRL+F */
        case 0x0F:  return KEY_LOCAL_RECORD;                 /* CTRL+O */
        default:
            break;
    }

    if (ch >= 0x20 && ch < 0x7F) {
        return ch;
    }
    return KEY_NONE;    /* autres codes de controle : ignores */
}

unsigned char keyboard_scan(void)
{
    unsigned char ch;
    unsigned char arrow = 0;

    if (keyboard_inject) {
        ch = keyboard_inject;
        keyboard_inject = 0;
        return keyboard_translate(ch, 0);
    }
    if (queue_empty()) return KEY_NONE;
    ch = queue_read();
    if (ch == 0x01 || ch == 0x04 || ch == 0x13 || ch == 0x17) {
        arrow = arrow_down();
    }
    return keyboard_translate(ch, arrow);
}

/* ===================================================================
 *  Emission des codes Minitel selon les aiguillages PRO3
 * =================================================================== */

static void kbd_emit(vtx_context_t* ctx, unsigned char byte)
{
    if (ctx->aiguillages & AIG_KBD_TO_MDM) {
        serial_send(byte);
    }
    if (ctx->aiguillages & AIG_KBD_TO_SCR) {
        vtx_process(ctx, byte);
    }
}

void keyboard_process(vtx_context_t* ctx, unsigned char key)
{
    if (key == KEY_NONE) {
        return;
    }

    /* Fleches : actives uniquement en mode curseur (PRO3 START $59 $43),
     * comme sur un Minitel 1B reel. Sequences CSI CUB/CUF/CUU/CUD. */
    if (key == KEY_ARROW_LEFT || key == KEY_ARROW_RIGHT ||
        key == KEY_ARROW_UP || key == KEY_ARROW_DOWN) {
        if (ctx->kbd_cursor || s_extended) {
            kbd_emit(ctx, 0x1B);
            kbd_emit(ctx, 0x5B);
            switch (key) {
                case KEY_ARROW_LEFT:  kbd_emit(ctx, 0x44); break;
                case KEY_ARROW_RIGHT: kbd_emit(ctx, 0x43); break;
                case KEY_ARROW_UP:    kbd_emit(ctx, 0x41); break;
                default:              kbd_emit(ctx, 0x42); break;
            }
        }
        return;
    }

    /* Code C0 brut du clavier etendu (KEY_FUNC_FLAG | 0x20 | code) */
    if ((key & KEY_FUNC_FLAG) && (key & 0x60) == 0x20) {
        kbd_emit(ctx, key & 0x1F);
        return;
    }

    /* Touche fonction Minitel */
    if (key & KEY_FUNC_FLAG) {
        kbd_emit(ctx, SEP);
        kbd_emit(ctx, key & 0x7F);
        return;
    }

    /* Caractere ASCII normal (7 bits) */
    kbd_emit(ctx, key & 0x7F);
}
