/**
 * @file terminal.h
 * @brief Profil du terminal emule : Minitel 1B ou Minitel 2
 *
 * Les deux modeles partagent le meme protocole Videotex 40 colonnes (STUM 1B),
 * decode par videotex.c. Ce module porte ce qui DIFFERE et qui est
 * modelisable ici :
 *
 *  - l'identification renvoyee a ENQ / PRO1 ENQROM ($7B) : SOH, constructeur,
 *    type, version, EOT. Octet de type : 'u' = Minitel 1B, 'v' = Minitel 2
 *    (valeurs de la bibliotheque Python "Minitel" de F. Bisson ; une autre
 *    source locale, telenet-workspace, donne 'x' pour le Minitel 2 : NON
 *    VERIFIE contre la STUM, voir docs/MINITEL_1B_VS_2.md). La reponse est
 *    DESACTIVEE par defaut, comme dans OricTel : les serveurs modernes
 *    (MiniPavi, PAVI) l'echoient comme une frappe.
 *  - les vitesses acceptees par PRO2 PROG ($6B) : 300/1200/4800 bauds sur le
 *    1B, plus 9600 sur le Minitel 2. La liaison physique (modem USB) n'en
 *    depend pas ; la vitesse "programmee" est memorisee et affichee.
 *
 * Ce qui n'est PAS emule (Minitel 2) : le mode teleinformatique 80 colonnes
 * et les jeux de caracteres redefinissables (DRCS), faute de specification
 * verifiee sous la main. Voir ROADMAP.
 */

#ifndef TERMINAL_H
#define TERMINAL_H

#define TERM_MINITEL_1B 0
#define TERM_MINITEL_2  1

/* Codes de vitesse PRO2 PROG (bits : 1 vvv vvv, emission = reception) */
#define SPEED_CODE_300  0x52
#define SPEED_CODE_1200 0x64
#define SPEED_CODE_4800 0x76
#define SPEED_CODE_9600 0x7F

/* Modele courant (TERM_MINITEL_*) et activation de la reponse d'identification */
extern unsigned char g_term_model;
extern unsigned char g_ident_enabled;

/** Selectionne le modele ; remet la vitesse programmee a 1200. */
void term_set_model(unsigned char model);

/** "Minitel 1B" / "Minitel 2" */
const char* term_model_name(void);

/** "1B" / "M2" (barre de statut) */
const char* term_model_short(void);

/** Octet de type de l'identification ('u' / 'v'). */
unsigned char term_ident_type(void);

/** Envoie l'identification (si g_ident_enabled) : SOH C type V EOT. */
void term_send_ident(void);

/** PRO2 PROG : memorise la vitesse si le modele l'accepte. Retourne 1 si
 *  acceptee, 0 sinon (code inconnu ou 9600 sur un 1B). */
unsigned char term_prog_speed(unsigned char code);

/** Vitesse programmee, en bauds (300, 1200, 4800, 9600). */
unsigned int term_speed(void);

#endif /* TERMINAL_H */
