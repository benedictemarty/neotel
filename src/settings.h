/**
 * @file settings.h
 * @brief Reglages persistants de NeoTel sur la carte SD / cle USB
 *
 * Fichier "neotel.cfg" dans le repertoire courant du firmware (API fichiers
 * 3,2 Load File / 3,3 Store File) : profil terminal, aspect, identification,
 * dernier serveur (index ou saisie libre). Charge au demarrage, sauve a
 * chaque changement dans les menus. Sans fichier (premier lancement,
 * emulateur sans stockage) : valeurs par defaut, aucune erreur affichee.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#define SETTINGS_MAGIC0 'N'
#define SETTINGS_MAGIC1 'T'
#define SETTINGS_VERSION 4
#define SETTINGS_SERVER_MAX 40

typedef struct {
    unsigned char magic0, magic1;   /* 'N' 'T' */
    unsigned char version;          /* SETTINGS_VERSION */
    unsigned char model;            /* TERM_MINITEL_* */
    unsigned char look;             /* DISPLAY_LOOK_* */
    unsigned char ident;            /* g_ident_enabled */
    unsigned char server_idx;       /* 0..NUM_SERVERS-1, 255 = saisie libre */
    char          server[SETTINGS_SERVER_MAX];   /* saisie libre (nul-terminee) */
    unsigned char rec_index;        /* dernier enregistrement neoNN.vdt (0 = aucun) */
    unsigned char sound;            /* 1 = bips (BEL, splash) actifs (v3) */
    unsigned char lang;             /* 0 = francais, 1 = anglais (v4) */
} settings_t;

extern settings_t g_settings;

/** Charge neotel.cfg dans g_settings. Retourne 1 si un fichier valide a ete
 *  lu, 0 sinon (g_settings prend alors les valeurs par defaut). */
unsigned char settings_load(void);

/** Ecrit g_settings dans neotel.cfg. Retourne 1 si l'ecriture a reussi. */
unsigned char settings_save(void);

#endif /* SETTINGS_H */
