/**
 * @file videotex.h
 * @brief Decodeur protocole Videotex Teletel/Antiope pour Minitel 1B
 *
 * Machine a etats pour interpreter le flux d'octets Videotex recu
 * via la liaison serie. Reference: emulateur JS miedit/telenet.
 *
 * Codes de controle principaux:
 *   $08-$0B : deplacement curseur
 *   $0C     : effacement ecran
 *   $0D     : retour chariot
 *   $0E     : basculer vers G1 (mosaiques)
 *   $0F     : basculer vers G0 (alphanumerique)
 *   $12     : repetition caractere
 *   $1B     : ESC (sequences d'attributs)
 *   $1F     : US (positionnement curseur)
 */

#ifndef VIDEOTEX_H
#define VIDEOTEX_H

/* Dimensions ecran Minitel */
#define VTX_COLS    40
#define VTX_ROWS    25      /* ligne 0 = statut, lignes 1-24 = contenu */

/* Etats de la machine a etats */
#define VTX_STATE_NORMAL    0
#define VTX_STATE_ESC       1
#define VTX_STATE_CSI       2
#define VTX_STATE_US_ROW    3
#define VTX_STATE_US_COL    4
#define VTX_STATE_SS2       5
#define VTX_STATE_REP       6
#define VTX_STATE_PRO       7   /* Sequence PRO en cours (cf. pro_remaining) */
#define VTX_STATE_SS2_ACC   9   /* SS2 accent: attente du caractere base */
#define VTX_STATE_MASK_SP   10  /* Mask global: attend $20 apres ESC # */
#define VTX_STATE_MASK_END  11  /* Mask global: attend $58 (set) ou $5F (reset) */
#define VTX_STATE_SEP       12  /* SEP ($13) recu: consommer le code fonction */
/* Minitel 2 (STUM 2, chapitre L'ecran par. 2.2.2 et 2.3) */
#define VTX_STATE_DRCS_HDR  13  /* US 2/3 recu: en-tete ou debut de transfert */
#define VTX_STATE_DRCS_XFER 14  /* transfert de formes DRCS en cours */
#define VTX_STATE_ESC_G0SET 15  /* ESC 2/8 : association du jeu G0 */
#define VTX_STATE_ESC_G1SET 16  /* ESC 2/9 : association du jeu G1 */
#define VTX_STATE_ESC_G0SET2 17 /* ESC 2/8 2/0 recu : attend 4/2 */
#define VTX_STATE_ESC_G1SET2 18 /* ESC 2/9 2/0 recu : attend 4/3 */

/* Jeux de caracteres */
#define CHARSET_G0  0       /* Alphanumerique */
#define CHARSET_G1  1       /* Mosaiques semi-graphiques */
#define CHARSET_G2  2       /* Supplementaire (accents) */
#define CHARSET_DRCS0 3     /* Jeu telecharge G'0 (Minitel 2), associe a G0 */
#define CHARSET_DRCS1 4     /* Jeu telecharge G'1 (Minitel 2), associe a G1 */

/* Jeux DRCS (STUM 2 par. 2.3) : 94 formes (codes 2/1 a 7/E) de 8x10 pixels,
 * 1 octet par rangee (bit 7 = pixel de gauche). */
#define DRCS_FIRST  0x21
#define DRCS_LAST   0x7E
#define DRCS_COUNT  (DRCS_LAST - DRCS_FIRST + 1)
#define DRCS_ROWS   10
#define DRCS_BYTES  14      /* octets de 6 bits par forme */

/* Attributs de taille */
#define SIZE_NORMAL         0
#define SIZE_DOUBLE_HEIGHT  1
#define SIZE_DOUBLE_WIDTH   2
#define SIZE_DOUBLE_SIZE    3

/* Drapeaux d'attributs (bitfield) */
#define ATTR_FLASH      0x01
#define ATTR_CONCEALED  0x02
#define ATTR_INVERT     0x04
#define ATTR_UNDERLINE  0x08
#define ATTR_SEPARATED  0x10

/* Couleurs Minitel (identiques aux couleurs Oric, prefixe VTX_) */
#define VTX_BLACK       0
#define VTX_RED         1
#define VTX_GREEN       2
#define VTX_YELLOW      3
#define VTX_BLUE        4
#define VTX_MAGENTA     5
#define VTX_CYAN        6
#define VTX_WHITE       7

/* Structure d'une cellule ecran.
 * ATTENTION: l'ordre et la taille des champs (6 octets, offsets
 * 0=ch 1=charset 2=fg 3=bg 4=flags 5=size) sont exploites par le
 * moteur assembleur display_asm.s (blit_run). */
typedef struct {
    unsigned char ch;           /* Code caractere */
    unsigned char charset;      /* CHARSET_G0, G1, G2 */
    unsigned char fg;           /* Couleur encre (0-7) */
    unsigned char bg;           /* Couleur fond (0-7) */
    unsigned char flags;        /* ATTR_FLASH | ATTR_CONCEALED | ... */
    unsigned char size;         /* SIZE_NORMAL, DOUBLE_HEIGHT, etc. */
} vtx_cell_t;

/* Contexte du decodeur Videotex */
typedef struct {
    /* Machine a etats */
    unsigned char state;

    /* Position curseur */
    unsigned char cur_x;        /* Colonne (0-39) */
    unsigned char cur_y;        /* Ligne (0-24) */
    unsigned char cur_visible;

    /* Jeu de caracteres courant */
    unsigned char charset;      /* G0 ou G1 actif */

    /* Attributs courants */
    unsigned char fg_color;
    unsigned char bg_color;
    unsigned char attr_flags;
    unsigned char attr_size;

    /* Attributs en attente (serial attributes Videotex) */
    unsigned char pending_bg;
    unsigned char pending_underline;
    unsigned char has_pending;

    /* Buffer parametres CSI */
    unsigned char csi_buf[8];
    unsigned char csi_len;

    /* Ligne memorisee pour US */
    unsigned char us_row;

    /* Caractere precedent (pour REP) */
    unsigned char last_char;
    unsigned char last_charset;

    /* Code accent SS2 en cours ($41=grave, $42=aigu, $43=circ, $48=trema, $4B=cedille) */
    unsigned char ss2_accent;

    /* Modes terminal (PRO2). 0 = etat par defaut Minitel 1B. */
    unsigned char rolling_mode;    /* 1 = scroll en bas d'ecran, 0 = mode page */
    unsigned char lowercase_mode;  /* 1 = minuscules autorisees, 0 = forcer majuscule */
    unsigned char terminal_mode;   /* 0 = VIDEOTEX (defaut), 1 = MIXED (non gere) */
#define TERM_MODE_VIDEOTEX  0
#define TERM_MODE_MIXED     1

    /* Aiguillages PRO3 (SWITCH ON/OFF entre modules).
     * Le Minitel a 4 modules: ECRAN ($58), CLAVIER ($51), MODEM ($59), PRISE ($50).
     * Bit 0 = source CLAVIER vers destination ECRAN (echo local, le defaut sur 1B
     * est OFF: ce qu'on tape n'est pas affiche sans retour serveur).
     * Bit 1 = source MODEM vers destination ECRAN (defaut ON sur 1B).
     * Bit 2 = source CLAVIER vers destination MODEM (defaut ON).
     * Bit 3 = source ECRAN vers destination MODEM (rare, defaut OFF). */
    unsigned char aiguillages;
#define AIG_KBD_TO_SCR  0x01
#define AIG_MDM_TO_SCR  0x02
#define AIG_KBD_TO_MDM  0x04
#define AIG_SCR_TO_MDM  0x08

    /* Modes clavier (PRO3 START/STOP). Tous OFF par defaut sur Minitel 1B. */
    unsigned char kbd_extended;  /* 1 = clavier etendu actif (touches alt) */
    unsigned char kbd_cursor;    /* 1 = touches curseur (fleches) actives */

    /* Mask global (ESC # $20 $58/$5F). 1 = cellules ATTR_CONCEALED cachees
     * (defaut), 0 = demasquees (rendues visibles malgre ATTR_CONCEALED).
     * Permet a l'utilisateur de reveler le texte cache via une commande
     * serveur. */
    unsigned char global_mask;

    /* Sequence PRO en cours.
     * pro_kind: 1, 2 ou 3 (nombre total d'octets attendus apres ESC $39/$3A/$3B)
     * pro_idx: index du prochain octet a recevoir (0..pro_kind-1)
     * pro_buf: octets PRO accumules pour dispatch a la fin (ENQROM, AIGUILLAGE...)
     * Quand pro_idx == pro_kind, la sequence est complete. */
    unsigned char pro_kind;
    unsigned char pro_idx;
    unsigned char pro_buf[3];

    /* --- Minitel 2 : jeux DRCS (STUM 2 par. 2.3) --- */
    unsigned char drcs_g0;          /* 1 = G'0 associe a G0 (ESC 2/8 2/0 4/2) */
    unsigned char drcs_g1;          /* 1 = G'1 associe a G1 (ESC 2/9 2/0 4/3) */
    unsigned char drcs_hdr_set;     /* en-tete valide : 0 = G'0 (defaut), 1 = G'1 */
    unsigned char drcs_hdr_idx;     /* octets d'en-tete recus apres US 2/3 */
    unsigned char drcs_code;        /* code de la forme en cours de transfert */
    unsigned char drcs_started;     /* 1 = un B1 a ete recu (forme en cours) */
    unsigned char drcs_nbyte;       /* octets recus pour la forme (0..14) */
    unsigned char drcs_nrow;        /* rangees completes (0..10) */
    unsigned char drcs_nbits;       /* bits en attente dans drcs_acc */
    unsigned short drcs_acc;        /* accumulateur de bits (6 par octet) ; short :
                                     * meme taille sur cc65 et sur l'hote (offsets
                                     * identiques pour les dumps RAM des tests) */
    unsigned char drcs_form[DRCS_ROWS];
    unsigned char drcs[2][DRCS_COUNT][DRCS_ROWS];

    /* Buffer ecran (40x25 cellules) */
    vtx_cell_t screen[VTX_ROWS][VTX_COLS];

    /* Dirty flags + plage de colonnes modifiees par ligne.
     * Invariant: quand dirty[row]==0, le span est (0, VTX_COLS-1)
     * (plein). Un code externe peut donc poser dirty[row]=1 sans
     * toucher au span: la ligne entiere sera rendue. Le retrecissement
     * passe par vtx_touch(). */
    unsigned char dirty[VTX_ROWS];      /* 1 = ligne modifiee, a re-rendre */
    unsigned char dirty_min[VTX_ROWS];  /* 1ere colonne modifiee */
    unsigned char dirty_max[VTX_ROWS];  /* derniere colonne modifiee */
    unsigned char full_refresh;         /* 1 = tout redessiner */
    unsigned char blink_phase;          /* 0 ou 1, bascule toutes les ~500ms */
} vtx_context_t;

/**
 * Initialise le contexte Videotex.
 */
void vtx_init(vtx_context_t* ctx);

/**
 * Traite un octet recu du flux Videotex.
 * @param ctx Contexte du decodeur
 * @param byte Octet Videotex (7 bits)
 */
void vtx_process(vtx_context_t* ctx, unsigned char byte);

/**
 * Efface l'ecran (contenu, lignes 1-24).
 */
void vtx_clear_page(vtx_context_t* ctx);

/**
 * Efface la ligne de statut (ligne 0).
 */
void vtx_clear_status(vtx_context_t* ctx);

/**
 * Positionne le curseur.
 */
void vtx_set_cursor(vtx_context_t* ctx, unsigned char row, unsigned char col);

/**
 * Marque une plage de colonnes [col_from, col_to] modifiee sur une
 * ligne (etend le span dirty existant le cas echeant).
 */
void vtx_touch(vtx_context_t* ctx, unsigned char row,
               unsigned char col_from, unsigned char col_to);

/**
 * Forme DRCS (10 rangees) du code ch dans le jeu set (0 = G'0, 1 = G'1) ;
 * NULL si le code n'est pas telechargeable (2/0, 7/F, hors plage).
 */
const unsigned char* vtx_drcs_form(const vtx_context_t* ctx,
                                   unsigned char set, unsigned char ch);

/**
 * Contexte du dernier vtx_process (pour le rendu des cellules DRCS par
 * display.c / display_asm.s : les formes vivent dans le contexte).
 */
extern vtx_context_t* vtx_current;

#endif /* VIDEOTEX_H */
