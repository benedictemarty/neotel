/**
 * @file keyboard.h
 * @brief Clavier USB du Neo6502 avec mapping Minitel
 *
 * Lit la file clavier du firmware (API 2,1 : ASCII, touches de controle en
 * codes $01-$1F) et traduit en codes Videotex pour le Minitel.
 *
 * Touches fonction Minitel (F1-F9, dans l'ordre de la colonne de touches
 * d'un Minitel 1B, plus Connexion/Fin) :
 *   F1 = Sommaire    (SEP $46)      F6 = Correction  (SEP $47)
 *   F2 = Annulation  (SEP $45)      F7 = Suite       (SEP $48)
 *   F3 = Retour      (SEP $42)      F8 = Envoi       (SEP $41)
 *   F4 = Repetition  (SEP $43)      F9 = Connexion/Fin (SEP $49)
 *   F5 = Guide       (SEP $44)      F10 = aspect couleur / gris (local)
 * Les F-touches sont programmees par l'API 2,4 (hotkeys) pour emettre un
 * octet prive $81-$8A dans la file clavier.
 *
 * Raccourcis (v0.8.1) : Entree = Envoi, Retour arriere = Correction,
 * Suppr / CTRL+A Annulation, fleche gauche Retour, fleche droite Suite
 * (hors mode curseur PRO3, ou elles emettent CSI), CTRL+R Repetition,
 * CTRL+G Guide, CTRL+S Sommaire, CTRL+C Connexion/Fin, CTRL+L effacer la
 * page, CTRL+F reinitialiser la liaison serie, CTRL+D aspect, CTRL+O
 * enregistrer / arreter (.vdt), ESC = sortie locale. Les fleches arrivent avec les memes codes que CTRL+A/D/S/W
 * (console du firmware) : elles sont distinguees par l'etat de la touche
 * HID (API 1,2).
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "videotex.h"

/* Codes separateur Minitel */
#define SEP         0x13    /* Separateur (prefixe touches fonction) */

/* Codes touches fonction Minitel */
#define KEY_ENVOI       0x41
#define KEY_RETOUR      0x42
#define KEY_REPETITION  0x43
#define KEY_GUIDE       0x44
#define KEY_ANNULATION  0x45
#define KEY_SOMMAIRE    0x46
#define KEY_CORRECTION  0x47
#define KEY_SUITE       0x48
#define KEY_CONNEXION   0x49

/* Resultat du scan clavier */
#define KEY_NONE          0x00  /* Aucune touche */
#define KEY_FUNC_FLAG     0x80  /* Bit 7 = touche fonction Minitel */
#define KEY_TOGGLE_RENDER 0xFE  /* CTRL+D / F10 = basculer l'aspect couleur/gris */
#define KEY_LOCAL_CLEAR   0xFD  /* CTRL+L = effacer ecran local */
#define KEY_LOCAL_RESET   0xFC  /* CTRL+F = reinitialiser la liaison serie */
#define KEY_ARROW_LEFT    0xFB  /* Fleche gauche (mode curseur PRO3) */
#define KEY_ARROW_RIGHT   0xFA  /* Fleche droite (mode curseur PRO3) */
#define KEY_LOCAL_ESCAPE  0xF9  /* ESC = sortie (quitter la session / retour menu) */
#define KEY_ARROW_UP      0xF8  /* Fleche haut (mode curseur PRO3) */
#define KEY_ARROW_DOWN    0xF7  /* Fleche bas (mode curseur PRO3) */
#define KEY_LOCAL_RECORD  0xF6  /* CTRL+O = enregistrer / arreter (.vdt sur la carte) */

/* Octets prives emis par les hotkeys F1-F10 (2,4) */
#define KEY_HOTKEY_BASE   0x81

/**
 * Initialise le module clavier : programme les hotkeys F1-F10.
 */
void keyboard_init(void);

/**
 * Indique si une touche attend d'etre lue (sans la consommer).
 */
unsigned char keyboard_pending(void);

/**
 * Vide la file clavier (frappes accumulees pendant une attente AT).
 */
void keyboard_flush(void);

/**
 * Lit la file clavier et retourne la touche traduite.
 * @return Code ASCII (7 bits) ou KEY_FUNC_FLAG | code_fonction,
 *         un code KEY_LOCAL_*, ou KEY_NONE si aucune touche.
 */
unsigned char keyboard_scan(void);

/**
 * Traite une touche et emet les codes Minitel selon les aiguillages
 * PRO3 du contexte : vers le modem si CLAVIER->MODEM est actif (defaut),
 * en echo local (decodeur Videotex) si CLAVIER->ECRAN est actif.
 * Les fleches ne sont emises qu'en mode curseur (PRO3 START $59 $43).
 * @param ctx Contexte Videotex (aiguillages, kbd_cursor)
 * @param key Code retourne par keyboard_scan()
 */
void keyboard_process(vtx_context_t* ctx, unsigned char key);

/**
 * Clavier "etendu" du mode Mixte / Teleinformatique (STUM 1B p. 3387) :
 * CTRL+lettre emet le code C0 (0x01-0x1A), Entree = CR, Retour arriere = BS,
 * TAB = HT, fleches = CSI A/B/C/D sans condition. F1-F10 et ESC inchanges.
 */
void keyboard_set_extended(unsigned char on);
unsigned char keyboard_extended(void);

/**
 * Traduction pure d'un octet de la file clavier du firmware en code
 * keyboard_scan (exposee pour les tests hote). arrow_hid = code HID de la
 * fleche enfoncee (HID_LEFT/RIGHT/UP/DOWN) ou 0.
 */
unsigned char keyboard_translate(unsigned char ch, unsigned char arrow_hid);

/**
 * Injection de touche pour les tests cible : un octet non nul depose ici
 * (Phosphoneo --poke-at) est lu par keyboard_scan avant la file clavier.
 */
extern unsigned char keyboard_inject;

#endif /* KEYBOARD_H */
