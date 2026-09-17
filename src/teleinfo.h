/**
 * @file teleinfo.h
 * @brief Ecran 80 colonnes : standard Teletel mode Mixte et standard
 *        Teleinformatique (ISO 6429), Minitel 1B et Minitel 2
 *
 * Reference : STUM 1B partie 3 chapitre 2 (L'ecran, p. 160-170) pour le
 * decodage des rangees 01 a 24, partie 2 chapitre 1 par. 2 (mode Mixte,
 * p. 106) pour la rangee 00 ; STUM 2 par. 3.3 / 3.4 pour les sequences
 * ajoutees (position curseur, format 40/80, mode page). Voir
 * docs/ref/STUM1B-NOTES.md et docs/ref/STUM2-NOTES.md.
 *
 * Modele : 25 rangees de 80 cellules (rangee 00 + rangees 01-24), une
 * cellule = code ASCII (0x20-0x7F) + attributs. Le jeu (americain / francais)
 * est fige dans la cellule a l'ecriture (SO/SI). Le decodeur est portable et
 * teste sur l'hote ; le rendu est dans display80.c (mode Hercules 720x350).
 */

#ifndef TELEINFO_H
#define TELEINFO_H

#define TI_COLS     80
#define TI_ROWS     25          /* rangee 00 + 24 rangees ISO 6429 */

/* Attributs par cellule (STUM 1B p. 160 ; CSI Ps m p. 165) */
#define TI_ATTR_BOLD      0x01  /* surintensite (1 / 22) */
#define TI_ATTR_UNDERLINE 0x02  /* souligne (4 / 24) */
#define TI_ATTR_BLINK     0x04  /* clignotant (5 / 25) */
#define TI_ATTR_INVERSE   0x08  /* inversion de fond (7 / 27) */
#define TI_ATTR_FRENCH    0x10  /* cellule ecrite dans le jeu francais (SO) */
#define TI_ATTR_ERROR     0x20  /* symbole d'erreur (pave plein, CAN / SUB) */

/* Etats du decodeur */
#define TI_STATE_NORMAL   0
#define TI_STATE_ESC      1
#define TI_STATE_CSI      2
#define TI_STATE_ROW0     3     /* rangee 00 (US 4/0 X/Y) jusqu'au LF */
#define TI_STATE_ROW0_ESC 4     /* rangee 00 : ESC + 1 octet d'attribut videotex, avale */
#define TI_STATE_ROW0_SS2 5     /* rangee 00 : SS2 + accent, avale */
#define TI_STATE_ROW0_REP 6     /* rangee 00 : REP + nombre */
#define TI_STATE_US       7     /* US recu (attente de 4/0) */
#define TI_STATE_US_COL   8     /* US 4/0 recu : colonne */

typedef struct {
    unsigned char ch;           /* code 0x20-0x7F */
    unsigned char attr;         /* TI_ATTR_* */
} ti_cell_t;

typedef struct {
    ti_cell_t screen[TI_ROWS][TI_COLS];
    unsigned char dirty[TI_ROWS];
    unsigned char full_refresh;

    unsigned char cur_x;        /* 0..79 */
    unsigned char cur_y;        /* 1..24 (0 = rangee 00 quand row0 actif) */
    unsigned char attr;         /* attributs courants (TI_ATTR_BOLD..INVERSE) */
    unsigned char french;       /* 1 = jeu francais (SO), 0 = americain (SI) */
    unsigned char roll;         /* 1 = mode rouleau (defaut), 0 = mode page */
    unsigned char cols;         /* 80 (defaut) ou 40 (STUM 2 CSI 3/C 3/3 6/8) */
    unsigned char insert;       /* SM4 : insertion de caracteres */
    unsigned char cur_visible;  /* 1 : tiret clignotant (toujours sur 1B) */
    unsigned char blink_phase;

    unsigned char state;
    unsigned char csi_buf[8];
    unsigned char csi_len;
    unsigned char csi_priv;     /* caractere intermediaire ('?' ou '<'), 0 sinon */

    /* Contexte ESC 7 / ESC 8 */
    unsigned char sav_valid, sav_x, sav_y, sav_attr, sav_french;

    /* Rangee 00 : position/attributs/jeu restitues au LF */
    unsigned char r0_active;    /* 1 = en rangee 00 (contexte memorise) */
    unsigned char r0_x, r0_y, r0_attr, r0_french, r0_so;
    unsigned char r0_col;       /* colonne courante en rangee 00 */

    /* Demandes vers l'hote (main.c les lit et les remet a 0) */
    unsigned char req_videotex; /* CSI ? { : retour au mode Videotex */
    unsigned char req_beep;     /* BEL */
    unsigned char req_format;   /* format change (40/80) : reinitialiser le rendu */
} ti_context_t;

/** Initialise l'ecran 80 colonnes (passage en mode Mixte : page effacee,
 *  80 colonnes, curseur en (1,1), rouleau, jeu americain). */
void ti_init(ti_context_t* ctx);

/** Traite un octet recu (7 bits). */
void ti_process(ti_context_t* ctx, unsigned char byte);

/** Marque une rangee a re-rendre. */
void ti_touch(ti_context_t* ctx, unsigned char row);

/** Contexte du dernier ti_process (rendu). */
extern ti_context_t* ti_current;

#endif /* TELEINFO_H */
