/**
 * @file videotex.c
 * @brief Decodeur protocole Videotex Teletel/Antiope
 *
 * Machine a etats complete pour interpreter le flux Videotex Minitel.
 * Reference: emulateur JS miedit (protocol.js, decoder.js, constant.js)
 */

#include <string.h>
#include "videotex.h"
#include "display.h"
#include "serial.h"
#include "terminal.h"

/* Mask global Videotex (defini dans main.c, lu par display.c).
 * 1 = cacher cellules ATTR_CONCEALED, 0 = les rendre visibles. */
extern unsigned char g_global_mask;

/* Contexte unique de la cible (voir ctx.h) : -2,8 Ko de code. */
#include "ctx.h"
#ifdef __CC65__
#define CTX  vtx
#define SCTX vtx
#else
#define CTX  (*ctx)
#define SCTX (*s_ctx)
#endif

/* ===================================================================
 *  Constantes du protocole (STUM 1B / CEPT) - evite les nombres magiques
 * =================================================================== */

/* Marqueurs de sequence protocole apres ESC (STUM 1B) :
 * PRO1 = ESC $39 + 1 octet, PRO2 = ESC $3A + 2, PRO3 = ESC $3B + 3. */
#define VTX_PRO1_MARK   0x39
#define VTX_PRO3_MARK   0x3B
#define VTX_PRO_BASE    0x38    /* pro_kind = marqueur - base (1/2/3) */

/* Decodage d'adresse curseur US (norme STUM p.91) : ligne/colonne sont
 * codees a partir de $40 ($40 = 0). */
#define VTX_ADDR_BASE   0x40

/* Codes accent du single-shift G2 (ESC $19 + code) : sous-ensemble CEPT
 * des diacritiques utilises par le Minitel. Plage valide $41..$4F. */
#define SS2_ACC_MIN     0x41
#define SS2_ACC_MAX     0x4F
#define SS2_ACC_GRAVE   0x41
#define SS2_ACC_ACUTE   0x42
#define SS2_ACC_CIRC    0x43
#define SS2_ACC_TREMA   0x48
#define SS2_ACC_CEDILLA 0x4B

/* ===================================================================
 *  Identification terminal (ENQ et PRO1 ENQROM)
 *  Reponse STUM 1B: SOH + constructeur + type + version + EOT
 * =================================================================== */

/* Reponse d'identification ENQ/ENQROM.
 *
 * La STUM 1B demande de repondre SOH + constructeur + type + version
 * + EOT, mais les serveurs modernes (MiniPavi/PAVI) ne CONSOMMENT pas
 * cette reponse : ils l'echoient comme une frappe utilisateur (verifie a
 * la trace serie par OricTel). miedit, l'emulateur de reference qui
 * fonctionne avec ces serveurs, ne repond a AUCUNE sequence PRO.
 * La reponse est donc desactivee par defaut (g_ident_enabled) ; les octets
 * dependent du modele emule (terminal.c : Minitel 1B ou Minitel 2). */
static void send_ident(void)
{
    term_send_ident();
}

/* ===================================================================
 *  Dirty spans
 * =================================================================== */

/* Declaree tot: s_ctx est defini plus bas, pres de put_char. */
static vtx_context_t* s_ctx;

/* Contexte du dernier vtx_init/vtx_process, pour le rendu des cellules
 * DRCS (display.c, display_asm.s) : les formes vivent dans le contexte. */
vtx_context_t* vtx_current;

/* Variante interne de vtx_touch travaillant sur s_ctx. Un argument de moins
 * (le pointeur, que cc65 empile via pushax) et plus aucun rechargement du
 * pointeur parametre. vtx_touch reste l'entree publique (display.c, main.c
 * l'appellent avec leur propre contexte). */
static void touch_here(unsigned char row,
                       unsigned char col_from, unsigned char col_to)
{
    if (row >= VTX_ROWS) {
        return;
    }
    if (!SCTX.dirty[row]) {
        SCTX.dirty[row] = 1;
        SCTX.dirty_min[row] = col_from;
        SCTX.dirty_max[row] = col_to;
    } else {
        if (col_from < SCTX.dirty_min[row]) SCTX.dirty_min[row] = col_from;
        if (col_to   > SCTX.dirty_max[row]) SCTX.dirty_max[row] = col_to;
    }
}

void vtx_touch(vtx_context_t* ctx, unsigned char row,
               unsigned char col_from, unsigned char col_to)
{
    vtx_context_t* saved = s_ctx;
    s_ctx = ctx;
    touch_here(row, col_from, col_to);
    s_ctx = saved;      /* appel externe: ne pas perturber le decodage en cours */
}

/* Retablit l'invariant "ligne propre = span plein" sur une plage de
 * lignes: un dirty[row]=1 pose sans vtx_touch rendra la ligne entiere. */
static void reset_spans(vtx_context_t* ctx, unsigned char from_row)
{
    unsigned char r;
    for (r = from_row; r < VTX_ROWS; ++r) {
        CTX.dirty_min[r] = 0;
        CTX.dirty_max[r] = VTX_COLS - 1;
    }
}

/* ===================================================================
 *  Initialisation
 * =================================================================== */

void vtx_init(vtx_context_t* ctx)
{
    memset(ctx, 0, sizeof(vtx_context_t));
    reset_spans(ctx, 0);

    CTX.state = VTX_STATE_NORMAL;
    CTX.cur_x = 0;
    CTX.cur_y = 1;    /* Ligne 1 (ligne 0 = statut) */
    CTX.cur_visible = 1;
    CTX.charset = CHARSET_G0;
    CTX.fg_color = VTX_WHITE;
    CTX.bg_color = VTX_BLACK;
    CTX.attr_flags = 0;
    CTX.attr_size = SIZE_NORMAL;
    CTX.pending_bg = VTX_BLACK;
    CTX.pending_underline = 0;
    CTX.has_pending = 0;
    CTX.rolling_mode = 0;
    CTX.lowercase_mode = 0;
    CTX.terminal_mode = TERM_MODE_VIDEOTEX;
    /* Aiguillages defaut Minitel 1B: MODEM->ECRAN et CLAVIER->MODEM */
    CTX.aiguillages = AIG_MDM_TO_SCR | AIG_KBD_TO_MDM;
    CTX.kbd_extended = 0;
    CTX.kbd_cursor = 0;
    CTX.global_mask = 1;  /* defaut: cellules concealed cachees */
    g_global_mask = 1;     /* garder la copie renderer synchronisee */
    /* Minitel 2 : jeux de base associes a G0/G1, en-tete DRCS par defaut
     * G'0 (STUM 2 par. 2.2.2 et 2.3.2) ; les formes sont effacees (memset). */
    CTX.drcs_g0 = 0;
    CTX.drcs_g1 = 0;
    CTX.drcs_hdr_set = 0;
    vtx_current = ctx;

    vtx_clear_page(ctx);
    vtx_clear_status(ctx);
    CTX.full_refresh = 1;
}

/* ===================================================================
 *  Gestion ecran
 * =================================================================== */

/* Remet une plage de cellules a l'etat "vide visible":
 * ch=' ' et fg=WHITE (fg=BLACK rendrait la cellule invisible,
 * encre noire sur fond noir).
 *
 * NeoTel : la boucle C d'origine coutait ~900 cycles par cellule sous cc65
 * (~0,9 M de cycles = 150 ms par effacement de page, mesure sous Phosphoneo).
 * On remplit la premiere cellule puis on la duplique par memcpy (assembleur
 * de la bibliotheque cc65) en doublant la taille a chaque tour : les zones
 * source [0, n) et destination [done, done+n) ne se recouvrent jamais
 * puisque n <= done. */
static void reset_cells(vtx_cell_t* cell, unsigned int count)
{
    unsigned int done;

    if (count == 0) return;
    cell->ch = ' ';
    cell->charset = CHARSET_G0;
    cell_set_colors(cell, VTX_WHITE, VTX_BLACK);
    cell->flags = 0;        /* size (bits 5-6) = SIZE_NORMAL */
    for (done = 1; done < count; ) {
        unsigned int n = count - done;
        if (n > done) n = done;
        memcpy(cell + done, cell, n * sizeof(vtx_cell_t));
        done += n;
    }
}

static void clear_row(vtx_context_t* ctx, unsigned char row)
{
    reset_cells(&CTX.screen[row][0], VTX_COLS);
    vtx_touch(ctx, row, 0, VTX_COLS - 1);
}

void vtx_clear_page(vtx_context_t* ctx)
{
    reset_cells(&CTX.screen[1][0], VTX_COLS * (VTX_ROWS - 1));

    /* Effacer le framebuffer HIRES d'un coup (8000 octets = $40)
     * au lieu de marquer dirty et re-rendre 1000 cellules vides */
    display_clear();
    memset(&CTX.dirty[1], 0, VTX_ROWS - 1);
    reset_spans(ctx, 1);

    CTX.cur_x = 0;
    CTX.cur_y = 1;
    CTX.charset = CHARSET_G0;
    CTX.fg_color = VTX_WHITE;
    CTX.bg_color = VTX_BLACK;
    CTX.attr_flags = 0;
    CTX.attr_size = SIZE_NORMAL;
    CTX.pending_bg = VTX_BLACK;
    CTX.has_pending = 0;
}

void vtx_clear_status(vtx_context_t* ctx)
{
    clear_row(ctx, 0);
}

void vtx_set_cursor(vtx_context_t* ctx, unsigned char row, unsigned char col)
{
    if (row < VTX_ROWS) {
        CTX.cur_y = row;
    }
    if (col < VTX_COLS) {
        CTX.cur_x = col;
    }
}

/* ===================================================================
 *  Ecriture d'un caractere a la position curseur
 * =================================================================== */

static void scroll_up(vtx_context_t* ctx);

/* ===================================================================
 *  Adressage rapide des cellules
 *
 *  &CTX.screen[row][col] fait calculer a cc65 row * 160 puis col * 4 :
 *  deux multiplications 16 bits par des constantes qui ne sont pas des
 *  puissances de deux, payees A CHAQUE CARACTERE recu (put_char etait
 *  mesure a ~3 950 cycles/caractere, cf. make bench-render). Deux tables
 *  d'offsets les remplacent par de simples lectures indexees.
 *
 *  39 * 4 = 156 : les offsets de colonne tiennent dans un octet.
 * =================================================================== */
static const unsigned int row_byte_offset[VTX_ROWS] = {
    0, 160, 320, 480, 640, 800, 960, 1120, 1280, 1440, 1600, 1760, 1920, 2080, 2240, 2400, 2560, 2720, 2880, 3040, 3200, 3360, 3520, 3680, 3840
};
static const unsigned char col_byte_offset[VTX_COLS] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 148, 152, 156
};

/* Cellule (row, col) sans multiplication. row < VTX_ROWS et col < VTX_COLS
 * sont des PRECONDITIONS : les appelants les verifient deja. */
#define CELL_AT(c, row, col)                                             \
    ((vtx_cell_t*)((unsigned char*)((c).screen)                         \
                   + row_byte_offset[(row)] + col_byte_offset[(col)]))

/* Contexte courant, memorise UNE FOIS par appel a vtx_process().
 *
 * cc65 recharge un pointeur PARAMETRE depuis sa pile logicielle a chaque
 * dereferencement : un `jsr ldptr1ysp` (~50 cycles) par acces. put_char en
 * comptait 38, soit ~1 900 des 3 077 cycles/caractere mesures. Passer par un
 * pointeur de portee fichier les ramene a ZERO (verifie sur l'assembleur
 * genere : jsr 66 -> 24, ldptr1ysp 38 -> 0).
 *
 * Sur : put_char() est statique et n'est atteignable QUE depuis vtx_process(),
 * qui affecte s_ctx en entree. Aucune reentrance (pas d'IRQ dans le decodeur).
 */
static void put_char(unsigned char ch, unsigned char cs)
{
    /* static: cc65 adresse une variable de portee fichier directement, alors
     * qu'un pointeur LOCAL vit dans sa pile logicielle et impose un
     * `jsr ldptr10sp` a chaque acces. Pas de reentrance ici. */
    static vtx_cell_t* cell;

    if (SCTX.cur_y >= VTX_ROWS || SCTX.cur_x >= VTX_COLS) {
        return;
    }

    /* Minitel 2 : jeu DRCS associe a G0 / G1 (STUM 2 par. 2.2.2). Les codes
     * 2/0 et 7/F restent ceux du jeu de base (par. 2.3.3.1, remarque). */
    if (ch != 0x20 && ch != 0x7F) {
        if (cs == CHARSET_G0 && SCTX.drcs_g0)      cs = CHARSET_DRCS0;
        else if (cs == CHARSET_G1 && SCTX.drcs_g1) cs = CHARSET_DRCS1;
    }

    /* Mode majuscule force (defaut Minitel 1B) : 'a'-'z' -> 'A'-'Z'.
     * Ne s'applique qu'au jeu G0 (alphanumerique). G1 mosaique et
     * G2 supplementaire ne sont pas affectes. */
    if (cs == CHARSET_G0 && !SCTX.lowercase_mode &&
        ch >= 'a' && ch <= 'z') {
        ch -= 32;
    }

    cell = CELL_AT(SCTX, SCTX.cur_y, SCTX.cur_x);
    cell->ch = ch;
    cell->charset = cs;
    cell_set_colors(cell, SCTX.fg_color, SCTX.bg_color);
    cell->flags = (unsigned char)(SCTX.attr_flags | (SCTX.attr_size << SIZE_SHIFT));

    /* Attributs de zone en attente (STUM 1B, codage des attributs definis
     * par zone) : un espace G0 est le delimiteur explicite, il valide tout
     * (fond, soulignement) ; un caractere semi-graphique (G1, ou DRCS
     * associe a G1) valide la couleur de fond seulement, les autres
     * attributs latents attendent le premier espace (v0.8.2 ; auparavant
     * les mosaiques gardaient l'ancien fond : cartes du POKER de 3617.fr). */
    if (SCTX.has_pending) {
        unsigned char delim = 0;
        if (cs == CHARSET_G0) {
            if (ch == 0x20) delim = 1;
        } else if (cs == CHARSET_G1 || cs == CHARSET_DRCS1) {
            delim = 2;                          /* semi-graphique : fond seul */
        }
        if (delim) {
            cell_set_bg(cell, SCTX.pending_bg);
            SCTX.bg_color = SCTX.pending_bg;
        }
        if (delim == 1) {
            if (SCTX.pending_underline) {
                cell->flags |= ATTR_UNDERLINE;
                SCTX.attr_flags |= ATTR_UNDERLINE;
            } else {
                SCTX.attr_flags &= ~ATTR_UNDERLINE;
            }
            SCTX.has_pending = 0;
        }
    }

    /* Marquer la plage modifiee: la cellule, +1 colonne en double
     * largeur/taille (moitie droite du glyphe) */
    {
        unsigned char span_end = SCTX.cur_x;
        if ((SCTX.attr_size == SIZE_DOUBLE_WIDTH ||
             SCTX.attr_size == SIZE_DOUBLE_SIZE) &&
            span_end < VTX_COLS - 1) {
            ++span_end;
        }
        touch_here(SCTX.cur_y, SCTX.cur_x, span_end);
        /* Double hauteur/taille: la moitie haute du glyphe est rendue
         * dans les lignes pixel de la ligne du dessus. Sans ce dirty,
         * un re-rendu isole de cur_y-1 ecraserait la moitie haute. */
        if ((SCTX.attr_size == SIZE_DOUBLE_HEIGHT ||
             SCTX.attr_size == SIZE_DOUBLE_SIZE) && SCTX.cur_y > 0) {
            touch_here(SCTX.cur_y - 1, SCTX.cur_x, span_end);
        }
    }
    SCTX.last_char = ch;
    SCTX.last_charset = cs;

    /* Avancer le curseur (2 colonnes pour double largeur/taille) */
    if (SCTX.attr_size == SIZE_DOUBLE_WIDTH ||
        SCTX.attr_size == SIZE_DOUBLE_SIZE) {
        SCTX.cur_x += 2;
    } else {
        SCTX.cur_x++;
    }
    if (SCTX.cur_x >= VTX_COLS) {
        SCTX.cur_x = 0;
        SCTX.cur_y++;
        if (SCTX.cur_y >= VTX_ROWS) {
            if (SCTX.rolling_mode) {
                scroll_up(s_ctx);
                SCTX.cur_y = VTX_ROWS - 1;
            } else {
                /* Mode page (defaut): retour en ligne 1 (pas 0 = status) */
                SCTX.cur_y = 1;
            }
        }
    }
}

/* ===================================================================
 *  Deplacement curseur
 *
 *  Zone d'accueil (STUM 1B) : « lors d'un changement de zone (LF, VT, BS,
 *  HT, CSI, [US]), l'ecriture s'effectue avec les attributs serie de la
 *  zone d'accueil tant qu'un delimiteur explicite ne permet pas la prise
 *  en compte des attributs serie latents ». Le Minitel ne relit pas sa
 *  memoire de page ; NeoTel, lui, connait la cellule d'arrivee : la
 *  couleur de fond courante devient celle de la zone ou arrive le curseur
 *  (v0.8.2 ; auparavant fond noir force apres US, d'ou un caractere ecrit
 *  sur une zone blanche qui perdait son fond).
 * =================================================================== */

static void adopt_zone_bg(void)
{
    SCTX.bg_color = cell_bg(CELL_AT(SCTX, SCTX.cur_y, SCTX.cur_x));
}

static void cursor_left(vtx_context_t* ctx)
{
    if (CTX.cur_x > 0) {
        CTX.cur_x--;
    } else if (CTX.cur_y > 1) {
        CTX.cur_x = VTX_COLS - 1;
        CTX.cur_y--;
    }
    adopt_zone_bg();
}

static void cursor_right(vtx_context_t* ctx)
{
    CTX.cur_x++;
    if (CTX.cur_x >= VTX_COLS) {
        CTX.cur_x = 0;
        CTX.cur_y++;
        if (CTX.cur_y >= VTX_ROWS) {
            CTX.cur_y = VTX_ROWS - 1;
        }
    }
    adopt_zone_bg();
}

static void cursor_up(vtx_context_t* ctx)
{
    if (CTX.cur_y > 1) {
        CTX.cur_y--;
    }
    adopt_zone_bg();
}

static void cursor_down(vtx_context_t* ctx)
{
    if (CTX.cur_y < VTX_ROWS - 1) {
        CTX.cur_y++;
    } else if (CTX.rolling_mode) {
        /* Mode rouleau: scroll d'une ligne, curseur reste en bas */
        scroll_up(ctx);
    }
    /* Mode page: pas de scroll, curseur reste en ligne 24 */
    adopt_zone_bg();
}

static void scroll_up(vtx_context_t* ctx)
{
    /* Decale les lignes 2..VTX_ROWS-1 vers 1..VTX_ROWS-2.
     * La ligne 0 (statut) est preservee. La derniere ligne est effacee.
     * Cout: ~5.5 Ko de memmove + full_refresh (~80ms a 1 MHz). */
    memmove(&CTX.screen[1][0], &CTX.screen[2][0],
            sizeof(vtx_cell_t) * VTX_COLS * (VTX_ROWS - 2));
    clear_row(ctx, VTX_ROWS - 1);
    CTX.full_refresh = 1;
}

/* ===================================================================
 *  Effacement partiel
 * =================================================================== */

static void clear_eol(vtx_context_t* ctx)
{
    reset_cells(&CTX.screen[CTX.cur_y][CTX.cur_x],
                VTX_COLS - CTX.cur_x);
    vtx_touch(ctx, CTX.cur_y, CTX.cur_x, VTX_COLS - 1);
}

static void clear_eos(vtx_context_t* ctx)
{
    unsigned char r;
    clear_eol(ctx);
    for (r = CTX.cur_y + 1; r < VTX_ROWS; ++r) {
        clear_row(ctx, r);
    }
}

/* ===================================================================
 *  Traitement ESC sequences
 * =================================================================== */

static void process_esc(vtx_context_t* ctx, unsigned char byte)
{
    /* Couleur encre: ESC $40-$47 */
    if (byte >= 0x40 && byte <= 0x47) {
        CTX.fg_color = byte - 0x40;
        CTX.state = VTX_STATE_NORMAL;
        return;
    }

    /* Flash on/off: ESC $48/$49 */
    if (byte == 0x48) {
        CTX.attr_flags |= ATTR_FLASH;
        CTX.state = VTX_STATE_NORMAL;
        return;
    }
    if (byte == 0x49) {
        CTX.attr_flags &= ~ATTR_FLASH;
        CTX.state = VTX_STATE_NORMAL;
        return;
    }

    /* Taille: ESC $4C-$4F */
    if (byte >= 0x4C && byte <= 0x4F) {
        CTX.attr_size = byte - 0x4C;
        CTX.state = VTX_STATE_NORMAL;
        return;
    }

    /* Couleur fond: ESC $50-$57 */
    if (byte >= 0x50 && byte <= 0x57) {
        /* Fond = attribut serie, mis en attente */
        CTX.pending_bg = byte - 0x50;
        CTX.has_pending = 1;
        CTX.state = VTX_STATE_NORMAL;
        return;
    }

    /* Masquage: ESC $58 */
    if (byte == 0x58) {
        CTX.attr_flags |= ATTR_CONCEALED;
        CTX.state = VTX_STATE_NORMAL;
        return;
    }

    /* Soulignement (G0) / Mosaique separee (G1): ESC $59=off, $5A=on */
    if (byte == 0x59) {
        if (CTX.charset == CHARSET_G1) {
            /* Clear separated mosaic mode */
            CTX.attr_flags &= ~ATTR_SEPARATED;
        } else {
            CTX.pending_underline = 0;
            CTX.has_pending = 1;
        }
        CTX.state = VTX_STATE_NORMAL;
        return;
    }
    if (byte == 0x5A) {
        if (CTX.charset == CHARSET_G1) {
            /* Set separated mosaic mode */
            CTX.attr_flags |= ATTR_SEPARATED;
        } else {
            CTX.pending_underline = 1;
            CTX.has_pending = 1;
        }
        CTX.state = VTX_STATE_NORMAL;
        return;
    }

    /* Mask global: ESC $23 $20 $58/$5F (set/reset)
     * Reference: miedit (constant.js mask-global) */
    if (byte == 0x23) {
        CTX.state = VTX_STATE_MASK_SP;
        return;
    }

    /* CSI: ESC $5B */
    if (byte == 0x5B) {
        CTX.state = VTX_STATE_CSI;
        CTX.csi_len = 0;
        return;
    }

    /* Inversion: ESC $5C=OFF (fond normal), $5D=ON (fond inverse)
     * Ref: telenet emulateur.js, miedit directStream */
    if (byte == 0x5C) {
        CTX.attr_flags &= ~ATTR_INVERT;
        CTX.state = VTX_STATE_NORMAL;
        return;
    }
    if (byte == 0x5D) {
        CTX.attr_flags |= ATTR_INVERT;
        CTX.state = VTX_STATE_NORMAL;
        return;
    }

    /* PRO1: ESC $39 + 1 octet (commande)
     * PRO2: ESC $3A + 2 octets (commande + parametre)
     * PRO3: ESC $3B + 3 octets (commande + 2 parametres)
     * Reference: STUM 1B (specification technique du Minitel) */
    if (byte >= VTX_PRO1_MARK && byte <= VTX_PRO3_MARK) {
        CTX.state = VTX_STATE_PRO;
        CTX.pro_kind = byte - VTX_PRO_BASE;  /* $39->1, $3A->2, $3B->3 */
        CTX.pro_idx = 0;
        return;
    }

    /* SS2 (G2 single shift): ESC $19 */
    if (byte == 0x19) {
        CTX.state = VTX_STATE_SS2;
        return;
    }

    /* Minitel 2 : association des jeux (STUM 2 par. 2.2.2).
     *   ESC 2/8 4/0 : jeu alphanumerique de base -> G0
     *   ESC 2/8 2/0 4/2 : jeu DRCS G'0 -> G0
     *   ESC 2/9 6/3 : jeu semi-graphique de base -> G1
     *   ESC 2/9 2/0 4/3 : jeu DRCS G'1 -> G1
     * Les attributs actifs sont conserves. Un Minitel 1B ignore ESC 2/8. */
    if (g_term_model == TERM_MINITEL_2 && (byte == 0x28 || byte == 0x29)) {
        CTX.state = (byte == 0x28) ? VTX_STATE_ESC_G0SET : VTX_STATE_ESC_G1SET;
        return;
    }

    /* Non reconnu: ignorer et revenir a NORMAL */
    CTX.state = VTX_STATE_NORMAL;
}

/* Suite de ESC 2/8 / ESC 2/9 (Minitel 2). Toute autre valeur : sequence
 * ignoree, retour a NORMAL sans effet. */
static void process_charset_assoc(vtx_context_t* ctx, unsigned char byte)
{
    switch (CTX.state) {
        case VTX_STATE_ESC_G0SET:
            if (byte == 0x40)      { CTX.drcs_g0 = 0; break; }
            else if (byte == 0x20) { CTX.state = VTX_STATE_ESC_G0SET2; return; }
            break;
        case VTX_STATE_ESC_G0SET2:
            if (byte == 0x42) CTX.drcs_g0 = 1;
            break;
        case VTX_STATE_ESC_G1SET:
            if (byte == 0x63)      { CTX.drcs_g1 = 0; break; }
            else if (byte == 0x20) { CTX.state = VTX_STATE_ESC_G1SET2; return; }
            break;
        default: /* VTX_STATE_ESC_G1SET2 */
            if (byte == 0x43) CTX.drcs_g1 = 1;
            break;
    }
    CTX.state = VTX_STATE_NORMAL;
}

/* ===================================================================
 *  Telechargement DRCS (Minitel 2, STUM 2 par. 2.3)
 *
 *  En-tete  : US 2/3 2/0 2/0 2/0 4/2|4/3 4/9  (jeu G'0 | G'1)
 *  Transfert: US 2/3 Y  puis, pour chaque forme, B1 (3/0) + 14 octets de
 *             6 bits (colonnes 4 a 7) ; Y = code de la premiere forme,
 *             les suivantes occupent Y+1, Y+2...
 *  Sortie   : tout US (la forme en cours est completee en fond, le US est
 *             interprete normalement).
 * =================================================================== */

/* Range la forme en cours (completee de rangees vides) si son code est
 * telechargeable (2/1..7/E), sinon l'ignore. */
static void drcs_form_store(vtx_context_t* ctx)
{
    if (!CTX.drcs_started) return;
    CTX.drcs_started = 0;
    /* Bits en attente d'une rangee incomplete : le reste est du fond */
    if (CTX.drcs_nbits && CTX.drcs_nrow < DRCS_ROWS) {
        CTX.drcs_form[CTX.drcs_nrow] =
            (unsigned char)(CTX.drcs_acc << (8 - CTX.drcs_nbits));
    }
    if (CTX.drcs_code >= DRCS_FIRST && CTX.drcs_code <= DRCS_LAST) {
        memcpy(&CTX.drcs[CTX.drcs_hdr_set][CTX.drcs_code - DRCS_FIRST][0],
               CTX.drcs_form, DRCS_ROWS);
    }
}

static void drcs_form_begin(vtx_context_t* ctx)
{
    CTX.drcs_started = 1;
    CTX.drcs_nbyte = 0;
    CTX.drcs_nrow = 0;
    CTX.drcs_nbits = 0;
    CTX.drcs_acc = 0;
    memset(CTX.drcs_form, 0, DRCS_ROWS);
}

/* Un octet de donnees : 6 bits (b5..b0), rangees de 8 pixels remplies de
 * gauche a droite et de haut en bas, les bits excedentaires passant a la
 * rangee suivante ; au-dela de 14 octets, filtre (par. 2.3.3.2). */
static void drcs_data(vtx_context_t* ctx, unsigned char six)
{
    if (!CTX.drcs_started || CTX.drcs_nbyte >= DRCS_BYTES) return;
    ++CTX.drcs_nbyte;
    CTX.drcs_acc = (unsigned short)((CTX.drcs_acc << 6) | (six & 0x3F));
    CTX.drcs_nbits += 6;
    while (CTX.drcs_nbits >= 8) {
        CTX.drcs_nbits -= 8;
        if (CTX.drcs_nrow < DRCS_ROWS) {
            CTX.drcs_form[CTX.drcs_nrow++] =
                (unsigned char)(CTX.drcs_acc >> CTX.drcs_nbits);
        }
        CTX.drcs_acc &= (unsigned short)((1u << CTX.drcs_nbits) - 1);
    }
}

/* Octet suivant US 2/3. Retourne 1 si consomme ; 0 si l'octet doit etre
 * traite normalement (resynchronisation C0 en cours d'en-tete). */
static unsigned char drcs_header(vtx_context_t* ctx, unsigned char byte)
{
    static const unsigned char hdr[4] = { 0x20, 0x20, 0x42, 0x49 };
    unsigned char i = CTX.drcs_hdr_idx;

    if (i == 0) {
        if (byte == 0x20) {                     /* en-tete */
            CTX.drcs_hdr_idx = 1;
            return 1;
        }
        if (byte >= DRCS_FIRST && byte <= DRCS_LAST) {   /* transfert : Y */
            CTX.drcs_code = byte;
            CTX.drcs_started = 0;
            CTX.state = VTX_STATE_DRCS_XFER;
            return 1;
        }
        CTX.state = VTX_STATE_NORMAL;          /* erronee : ignoree */
        return (byte < 0x20) ? 0 : 1;
    }
    if (byte == 0x1F) {                         /* US : rangee 00 ? (US_COL) */
        CTX.drcs_suspended = 2;
        CTX.state = VTX_STATE_US_ROW;
        return 1;
    }
    if (byte < 0x20) {                          /* C0 : resynchronisation */
        CTX.state = VTX_STATE_NORMAL;
        return 0;
    }
    /* i = 1..4 : 2/0 2/0 (4/2|4/3) 4/9 */
    if ((i == 3 && (byte == 0x42 || byte == 0x43)) ||
        (i != 3 && byte == hdr[i - 1])) {
        if (i == 3) CTX.drcs_code = (byte == 0x43) ? 1 : 0;   /* candidat */
        if (i == 4) {
            CTX.drcs_hdr_set = CTX.drcs_code;  /* en-tete complete */
            CTX.state = VTX_STATE_NORMAL;
        } else {
            CTX.drcs_hdr_idx = i + 1;
        }
        return 1;
    }
    /* Syntaxe erronee : l'en-tete precedente reste valide (le jeu candidat
     * n'a pas ete retenu). */
    CTX.state = VTX_STATE_NORMAL;
    return 1;
}

/* Octet en cours de transfert. Retourne 1 si consomme. */
static unsigned char drcs_xfer(vtx_context_t* ctx, unsigned char byte)
{
    if (byte == 0x1F) {
        /* US : sortie du telechargement, sauf si le US est un acces en
         * rangee 00 (decide en US_COL) : le transfert est alors suspendu et
         * reprend sur le LF qui quitte la rangee 00 (par. 2.3.4). */
        CTX.drcs_suspended = 1;
        CTX.state = VTX_STATE_US_ROW;
        return 1;
    }
    if (byte == 0x00) return 1;                 /* NUL : rien */
    if (byte == 0x30) {                         /* B1 : delimiteur de forme */
        if (CTX.drcs_started) {
            drcs_form_store(ctx);
            ++CTX.drcs_code;
        }
        drcs_form_begin(ctx);
        return 1;
    }
    if (byte >= 0x40) {                         /* colonnes 4 a 7 : donnees */
        drcs_data(ctx, byte);
    } else {
        /* C0 (sauf US, NUL) et colonnes 2-3 (sauf B1) : pixels en fond,
         * sans resynchronisation (par. 2.3.3.2). */
        drcs_data(ctx, 0);
    }
    return 1;
}

const unsigned char* vtx_drcs_form(const vtx_context_t* ctx,
                                   unsigned char set, unsigned char ch)
{
    if (ch < DRCS_FIRST || ch > DRCS_LAST) return 0;
    return &CTX.drcs[set ? 1 : 0][ch - DRCS_FIRST][0];
}

/* ===================================================================
 *  Traitement CSI sequences (ESC [ params commande)
 * =================================================================== */

static void process_csi(vtx_context_t* ctx, unsigned char byte)
{
    unsigned char param;
    unsigned char param2;

    /* Accumuler les parametres (chiffres et ;) */
    if ((byte >= '0' && byte <= '9') || byte == ';') {
        if (CTX.csi_len < sizeof(CTX.csi_buf) - 1) {
            CTX.csi_buf[CTX.csi_len++] = byte;
        }
        return;
    }

    /* Terminer: parser param1[;param2] */
    CTX.csi_buf[CTX.csi_len] = 0;
    param = 0;
    param2 = 0;
    {
        unsigned char i;
        /* Accumulation BORNEE a 64 : au-dela ca n'a aucun sens (ecran 40x25)
         * et un parametre a 3+ chiffres deborderait l'unsigned char (ex.
         * "999A" -> 231) -> mouvement curseur faux ou boucle excessive. Le
         * calcul intermediaire passe par unsigned int pour ne jamais wrapper. */
        for (i = 0; i < CTX.csi_len && CTX.csi_buf[i] != ';'; ++i) {
            unsigned int t = (unsigned int)param * 10 + (CTX.csi_buf[i] - '0');
            param = (t > 64) ? 64 : (unsigned char)t;
        }
        if (i < CTX.csi_len && CTX.csi_buf[i] == ';') {
            for (++i; i < CTX.csi_len && CTX.csi_buf[i] != ';'; ++i) {
                unsigned int t = (unsigned int)param2 * 10 + (CTX.csi_buf[i] - '0');
                param2 = (t > 64) ? 64 : (unsigned char)t;
            }
        }
    }
    if (param == 0) param = 1;

    switch (byte) {
        case 'A':   /* CUU - curseur haut */
            while (param-- > 0) cursor_up(ctx);
            break;
        case 'B':   /* CUD - curseur bas */
            while (param-- > 0) cursor_down(ctx);
            break;
        case 'C':   /* CUF - curseur droite */
            while (param-- > 0) cursor_right(ctx);
            break;
        case 'D':   /* CUB - curseur gauche */
            while (param-- > 0) cursor_left(ctx);
            break;
        case 'H':   /* CUP - position curseur (row;col, 1-based).
                     * Sans parametre: home (1,0). row mappe direct sur
                     * la grille vtx (ligne 0 = statut, inaccessible:
                     * param=0 est force a 1 plus haut). col 1-based ->
                     * 0-based; vtx_set_cursor clampe row/col invalides. */
            vtx_set_cursor(ctx, param, (param2 > 0) ? param2 - 1 : 0);
            adopt_zone_bg();
            break;
        case 'J':   /* ED - effacer ecran */
            if (param == 2 || CTX.csi_len == 0) {
                vtx_clear_page(ctx);
            } else {
                clear_eos(ctx);
            }
            break;
        case 'K':   /* EL - effacer ligne */
            clear_eol(ctx);
            break;
        case 'h':   /* Mode set (curseur visible, etc.) */
            CTX.cur_visible = 1;
            break;
        case 'l':   /* Mode reset (curseur invisible) */
            CTX.cur_visible = 0;
            break;
        case 'n':   /* Minitel 2 (STUM 2 par. 2.5) : CSI 3/6 6/E = demande de
                     * position curseur ; reponse CSI Pr 3/B Pc 5/2. Pr = rangee
                     * (0-24) et Pc = colonne 1-based, comme CSI H les lit. */
            if (param == 6 && g_term_model == TERM_MINITEL_2) {
                unsigned char v;
                serial_send(0x1B);
                serial_send(0x5B);
                v = CTX.cur_y;
                if (v >= 10) serial_send((unsigned char)('0' + v / 10));
                serial_send((unsigned char)('0' + v % 10));
                serial_send(0x3B);
                v = (unsigned char)(CTX.cur_x + 1);
                if (v >= 10) serial_send((unsigned char)('0' + v / 10));
                serial_send((unsigned char)('0' + v % 10));
                serial_send(0x52);
                serial_tx_flush();
            }
            break;
        default:
            break;
    }

    CTX.state = VTX_STATE_NORMAL;
}

/* ===================================================================
 *  Dispatch d'une sequence PRO complete (PRO1/PRO2/PRO3)
 *
 *  Appele quand pro_idx == pro_kind. pro_buf[0..pro_kind-1] contient
 *  les octets de la sequence. Reagit aux commandes connues, ignore
 *  les autres en preservant le sync.
 * =================================================================== */

static void dispatch_pro(vtx_context_t* ctx)
{
    /* PRO1: 1 octet de commande */
    if (CTX.pro_kind == 1) {
        switch (CTX.pro_buf[0]) {
            case 0x7B:  /* ENQROM - identification du Minitel.
                         * Meme reponse que ENQ ($05). */
                send_ident();
                break;
            default:
                /* SEP fonction et autres PRO1 inconnus: ignore */
                break;
        }
        return;
    }
    /* PRO2: 2 octets = action ($69=START / $6A=STOP) + cible.
     * Cibles connues:
     *   $43 = mode rouleau (scroll en bas d'ecran)
     *   $45 = mode minuscules (autoriser 'a'-'z' au lieu de forcer majuscule)
     * Reference: STUM 1B + miedit (constant.js) + eMinitel (Functionalities.cpp) */
    if (CTX.pro_kind == 2) {
        unsigned char on;

        /* PRO2 + $72 + module = demande de status d'un module.
         * Format reponse: ESC ($1B) + $3B + $73 + module + status byte
         * (= PRO3 + $73 + ...). Reference: STUM 1B + eMinitel.
         * Les modules courants sont $59 (KEYBOARD_IN) et $51 (KEYBOARD_OUT).
         * Le status byte reflete l'etat du clavier: bits 6-7 fixes par
         * convention, plus quelques flags optionnels (minuscules etc.). */
        if (CTX.pro_buf[0] == 0x72) {
            unsigned char target = CTX.pro_buf[1];
            if (target == 0x59 || target == 0x51) {
                unsigned char status = 0xC0;  /* bits 6-7 fixes (cf. eMinitel) */
                if (CTX.lowercase_mode) status |= 0x02;
                if (CTX.rolling_mode)   status |= 0x04;
                serial_send(0x1B);
                serial_send(0x3B);
                serial_send(0x73);
                serial_send(target);
                serial_send(status);
                serial_tx_flush();  /* reponse protocole: partir d'un bloc */
            }
            return;
        }

        /* PRO2 + $32 = changement de mode protocole (VIDEOTEX/MIXED).
         * Reference: STUM 1B, eMinitel PRO2_MODE_VIDEOTEX/MIXED. */
        if (CTX.pro_buf[0] == 0x32) {
            if (CTX.pro_buf[1] == 0x7E) {       /* MODE VIDEOTEX */
                CTX.terminal_mode = TERM_MODE_VIDEOTEX;
                /* ACK: SEP ($13) + $71 (videotex confirme) */
                serial_send(0x13);
                serial_send(0x71);
                serial_tx_flush();
            } else if (CTX.pro_buf[1] == 0x7D) { /* MODE MIXTE */
                /* PRO2 MIXTE 1 (STUM 1B partie 2 chap. 6 par. 12.2) : passage
                 * au standard Teletel mode Mixte (80 colonnes, ISO 6429),
                 * acquitte par SEP 0x70. main.c bascule l'ecran (teleinfo.c)
                 * quand terminal_mode passe a TERM_MODE_MIXED. */
                CTX.terminal_mode = TERM_MODE_MIXED;
                serial_send(0x13);
                serial_send(0x70);
                serial_tx_flush();
            }
            return;
        }

        /* PRO2 + $6B (PROG) + code de vitesse : changement de vitesse de la
         * prise peripherique / du modem. La liaison de NeoTel (modem USB) n'en
         * depend pas : la vitesse est memorisee si le modele l'accepte
         * (9600 bauds : Minitel 2 seulement), sans reponse. */
        if (CTX.pro_buf[0] == 0x6B) {
            term_prog_speed(CTX.pro_buf[1]);
            return;
        }

        /* PRO2 + $69/$6A = START/STOP d'un mode (rolling, lowercase, ...) */
        if (CTX.pro_buf[0] == 0x69)      on = 1;  /* START */
        else if (CTX.pro_buf[0] == 0x6A) on = 0;  /* STOP */
        else return;
        switch (CTX.pro_buf[1]) {
            case 0x43:  /* ROLLING */
                CTX.rolling_mode = on;
                break;
            case 0x45:  /* LOWERCASE */
                CTX.lowercase_mode = on;
                break;
            default:
                /* Autres cibles PRO2 (PCE, etc.) : TODO */
                break;
        }
        return;
    }
    /* PRO3: 3 octets = action + cible/source.
     *   $60 SWITCH OFF + dest + source: rompt le lien source -> dest
     *   $61 SWITCH ON  + dest + source: etablit le lien source -> dest
     *   $69 START + xx + yy: active un module (TODO)
     *   $6A STOP + xx + yy: desactive un module (TODO)
     *
     * Codes module STUM 1B:
     *   $50 = PRISE peripherique
     *   $51 = CLAVIER
     *   $58 = ECRAN
     *   $59 = MODEM
     *
     * Reference: STUM 1B + miedit (constant.js pro3SwitchOn/Off)
     *            + eMinitel (Functionalities.cpp __func_PRO3) */
    if (CTX.pro_kind == 3) {
        unsigned char on;
        unsigned char dest, src, mask;
        /* PRO3 START/STOP module: $69/$6A + $59 (KEYBOARD_IN) + sub-cmd
         *   sub-cmd $41 = clavier etendu (touches alt)
         *   sub-cmd $43 = clavier curseur (fleches actives)
         * Reference: miedit pro3Start/Stop -> startKeyboardFunction. */
        if ((CTX.pro_buf[0] == 0x69 || CTX.pro_buf[0] == 0x6A)
            && CTX.pro_buf[1] == 0x59) {
            unsigned char on2 = (CTX.pro_buf[0] == 0x69);
            switch (CTX.pro_buf[2]) {
                case 0x41: CTX.kbd_extended = on2; break;
                case 0x43: CTX.kbd_cursor   = on2; break;
                default: break;
            }
            return;
        }

        if (CTX.pro_buf[0] == 0x61)      on = 1;  /* SWITCH ON */
        else if (CTX.pro_buf[0] == 0x60) on = 0;  /* SWITCH OFF */
        else return;  /* autres START/STOP: ignore */

        dest = CTX.pro_buf[1];
        src  = CTX.pro_buf[2];
        mask = 0;
        if (dest == 0x58 && src == 0x51) mask = AIG_KBD_TO_SCR;
        else if (dest == 0x58 && src == 0x59) mask = AIG_MDM_TO_SCR;
        else if (dest == 0x59 && src == 0x51) mask = AIG_KBD_TO_MDM;
        else if (dest == 0x59 && src == 0x58) mask = AIG_SCR_TO_MDM;
        else return;  /* couple non gere */

        if (on) CTX.aiguillages |= mask;
        else    CTX.aiguillages &= ~mask;

        /* ACK PRO3 SWITCH: ESC $3B $63 + dest + status_byte (5 octets).
         * Le status byte resume tous les liens "X -> dest" connus, avec
         * convention bit (cf. eMinitel __func_PRO3):
         *   bit 0 = ECRAN  -> dest
         *   bit 1 = CLAVIER -> dest
         *   bit 2 = MODEM  -> dest
         *   bit 3 = PRISE/DIN -> dest (non gere, toujours 0)
         *   bit 6 = $40 fixe (convention) */
        {
            unsigned char status = 0x40;
            if (dest == 0x58) {  /* dest = ECRAN */
                if (CTX.aiguillages & AIG_KBD_TO_SCR) status |= 0x02;
                if (CTX.aiguillages & AIG_MDM_TO_SCR) status |= 0x04;
            } else if (dest == 0x59) {  /* dest = MODEM */
                if (CTX.aiguillages & AIG_SCR_TO_MDM) status |= 0x01;
                if (CTX.aiguillages & AIG_KBD_TO_MDM) status |= 0x02;
            }
            serial_send(0x1B);
            serial_send(0x3B);
            serial_send(0x63);
            serial_send(dest);
            serial_send(status);
            serial_tx_flush();  /* reponse protocole: partir d'un bloc */
        }
    }
}

/* ===================================================================
 *  Traitement principal - point d'entree par octet
 * =================================================================== */

/* Compteur d'octets decodes (modulo 256), pour les tests cible : Phosphoneo
 * --dump-ram-when s'arme sur sa valeur une fois une page complete recue. */
unsigned char g_vtx_bytes;

void vtx_process(vtx_context_t* ctx, unsigned char byte)
{
    /* Contexte courant pour put_char (voir s_ctx). */
    s_ctx = ctx;
    vtx_current = ctx;
    ++g_vtx_bytes;

    /* Masquer bit 7 (7 bits Videotex) */
    byte &= 0x7F;

    /* Telechargement DRCS (Minitel 2) : AVANT la resynchronisation ESC, un
     * C0 en cours de forme est une donnee (STUM 2 par. 2.3.3.2). */
    if (CTX.state == VTX_STATE_DRCS_XFER) {
        if (drcs_xfer(ctx, byte)) return;
    } else if (CTX.state == VTX_STATE_DRCS_HDR) {
        if (drcs_header(ctx, byte)) return;
        /* C0 : l'en-tete est abandonnee, l'octet est traite normalement */
    }

    /* Re-sync: un ESC ($1B) recu en milieu de sequence multi-octets
     * abandonne l'etat courant et redemarre une nouvelle sequence ESC.
     * $1B n'est jamais une valeur legitime dans les payloads US/SS2/PRO/CSI
     * (US: $40+ligne, SS2: $20-$7F, CSI param: '0'-'9'/';'), donc le voir
     * signifie qu'on a perdu le sync et qu'une nouvelle commande arrive. */
    if (byte == 0x1B && CTX.state != VTX_STATE_NORMAL
                    && CTX.state != VTX_STATE_ESC) {
        CTX.state = VTX_STATE_ESC;
        return;
    }

    switch (CTX.state) {

    case VTX_STATE_ESC:
        process_esc(ctx, byte);
        return;

    case VTX_STATE_CSI:
        process_csi(ctx, byte);
        return;

    case VTX_STATE_ESC_G0SET:
    case VTX_STATE_ESC_G0SET2:
    case VTX_STATE_ESC_G1SET:
    case VTX_STATE_ESC_G1SET2:
        process_charset_assoc(ctx, byte);
        return;

    case VTX_STATE_US_ROW:
        /* Minitel 2 : US 2/3 ouvre une en-tete ou un transfert DRCS */
        if (byte == 0x23 && g_term_model == TERM_MINITEL_2) {
            CTX.drcs_hdr_idx = 0;
            CTX.state = VTX_STATE_DRCS_HDR;
            return;
        }
        /* Valider la plage en amont: un octet < $40 sous-deborderait
         * l'unsigned char (vtx_set_cursor reclampe en US_COL, mais on rejette
         * proprement plutot que de s'appuyer sur ce filet). */
        CTX.us_row = (byte >= VTX_ADDR_BASE) ? (byte - VTX_ADDR_BASE) : 0;
        CTX.state = VTX_STATE_US_COL;
        return;

    case VTX_STATE_US_COL:
        /* Col = byte - $41. Si byte=$40, col=0 (pas -1).
         * Le Minitel utilise $40 pour col 0. */
        vtx_set_cursor(ctx, CTX.us_row,
                        (byte > VTX_ADDR_BASE) ? (byte - (VTX_ADDR_BASE + 1)) : 0);
        /* US reset tous les attributs (norme STUM p.91)
         * Ref: telenet emulateur.js lignes 785-795 */
        CTX.charset = CHARSET_G0;      /* modeG1 = false */
        CTX.fg_color = VTX_WHITE;      /* fgColor = 7 */
        CTX.attr_flags = 0;            /* souligne, inversion, clignotement = false */
        CTX.attr_size = SIZE_NORMAL;   /* taille = 0 */
        CTX.has_pending = 0;
        adopt_zone_bg();                /* fond : celui de la zone d'accueil */
        /* Minitel 2 : un acces en rangee 00 reassocie les jeux de base a
         * G0 et G1 (STUM 2 par. 2.2.2). */
        if (CTX.us_row == 0) {
            CTX.drcs_g0 = 0;
            CTX.drcs_g1 = 0;
            /* Telechargement DRCS interrompu par la rangee 00 : il reste
             * suspendu (reprise sur LF, abandon sur FF/RS, par. 2.3.4) */
        } else if (CTX.drcs_suspended) {
            /* US vers une autre rangee : sortie du telechargement en
             * completant la forme en cours (par. 2.3.3.3) */
            if (CTX.drcs_suspended == 1) drcs_form_store(ctx);
            CTX.drcs_suspended = 0;
        }
        CTX.state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_SS2:
        /* Single shift G2: diacritiques ou caractere G2 standalone */
        if (byte >= SS2_ACC_MIN && byte <= SS2_ACC_MAX) {
            /* Code accent: sauver et attendre le caractere base */
            CTX.ss2_accent = byte;
            CTX.state = VTX_STATE_SS2_ACC;
            return;
        }
        /* Caractere G2 standalone (ex: $23=livre, $30=degre) */
        put_char(byte, CHARSET_G2);
        CTX.state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_SS2_ACC:
        /* Caractere base apres un code accent.
         * Combiner accent + base pour un glyphe accentue.
         * Les glyphes sont dans font_g2_extra[9..20].
         * On utilise CHARSET_G2 avec un code interne $80+. */
        {
            unsigned char acc_ch = 0;
            /* Mapper (accent, base) -> code interne G2 accentue */
            switch (CTX.ss2_accent) {
                case SS2_ACC_ACUTE: /* aigu */
                    if (byte == 0x65)      acc_ch = 0x80; /* e' */
                    else if (byte == 0x45) acc_ch = 0x8E; /* E' */
                    break;
                case SS2_ACC_GRAVE: /* grave */
                    if (byte == 0x65)      acc_ch = 0x81; /* e` */
                    else if (byte == 0x61) acc_ch = 0x83; /* a` */
                    else if (byte == 0x75) acc_ch = 0x84; /* u` */
                    else if (byte == 0x41) acc_ch = 0x8D; /* A` */
                    else if (byte == 0x45) acc_ch = 0x8F; /* E` */
                    break;
                case SS2_ACC_CIRC: /* circonflexe */
                    if (byte == 0x65)      acc_ch = 0x82; /* e^ */
                    else if (byte == 0x61) acc_ch = 0x86; /* a^ */
                    else if (byte == 0x69) acc_ch = 0x87; /* i^ */
                    else if (byte == 0x6F) acc_ch = 0x88; /* o^ */
                    else if (byte == 0x75) acc_ch = 0x89; /* u^ */
                    else if (byte == 0x41) acc_ch = 0x93; /* A^ */
                    else if (byte == 0x45) acc_ch = 0x90; /* E^ */
                    break;
                case SS2_ACC_TREMA: /* trema */
                    if (byte == 0x65)      acc_ch = 0x8A; /* e" */
                    else if (byte == 0x69) acc_ch = 0x8B; /* i" */
                    else if (byte == 0x75) acc_ch = 0x8C; /* u" */
                    else if (byte == 0x45) acc_ch = 0x91; /* E" */
                    break;
                case SS2_ACC_CEDILLA: /* cedille */
                    if (byte == 0x63)      acc_ch = 0x85; /* c, */
                    else if (byte == 0x43) acc_ch = 0x92; /* C, */
                    break;
            }
            if (CTX.drcs_g0 && CTX.charset == CHARSET_G0) {
                /* Minitel 2, G'0 actif : SS2 <accent> X affiche la forme X
                 * du jeu DRCS (STUM 2 par. 2.3.7) ; put_char fait le mapping. */
                put_char(byte, CHARSET_G0);
            } else if (acc_ch) {
                put_char(acc_ch, CHARSET_G2);
            } else {
                /* Combinaison inconnue: afficher la lettre de base */
                put_char(byte, CHARSET_G0);
            }
        }
        CTX.state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_REP:
        /* Repeter le dernier caractere N fois */
        {
            /* Octet < $40 -> 0 repetition (rejet), au lieu d'un sous-debordement
             * unsigned char rattrape ensuite par le plafond a 40. */
            unsigned char count = (byte >= VTX_ADDR_BASE)
                                  ? (byte - VTX_ADDR_BASE) : 0;
            if (count > 40) count = 40;
            while (count-- > 0) {
                put_char(CTX.last_char, CTX.last_charset);
            }
        }
        CTX.state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_MASK_SP:
        /* Mask global: attend $20 (espace) apres ESC #. */
        CTX.state = (byte == 0x20) ? VTX_STATE_MASK_END : VTX_STATE_NORMAL;
        return;

    case VTX_STATE_MASK_END:
        /* Mask global: $58 = masquer (cacher concealed),
         *              $5F = demasquer (rendre concealed visible). */
        if (byte == 0x58) {
            CTX.global_mask = 1;
            g_global_mask = 1;
        } else if (byte == 0x5F) {
            CTX.global_mask = 0;
            g_global_mask = 0;
        }
        CTX.full_refresh = 1;
        CTX.state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_SEP:
        /* Code fonction apres SEP: consomme, aucune action. */
        CTX.state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_PRO:
        /* Accumule un octet de payload PRO. */
        if (CTX.pro_idx < 3) {
            CTX.pro_buf[CTX.pro_idx++] = byte;
        }
        if (CTX.pro_idx >= CTX.pro_kind) {
            dispatch_pro(ctx);
            CTX.state = VTX_STATE_NORMAL;
        }
        return;

    default:
        break;
    }

    /* --- Etat NORMAL --- */

    /* Codes de controle C0 ($00-$1F) */
    if (byte < 0x20) {
        switch (byte) {
            case 0x05:  /* ENQ - identification terminal */
                send_ident();
                break;
            case 0x07:  /* BEL - bip */
                display_beep();
                break;
            case 0x08:  /* BS - curseur gauche */
                cursor_left(ctx);
                break;
            case 0x09:  /* HT - curseur droite */
                cursor_right(ctx);
                break;
            case 0x0A:  /* LF - curseur bas */
                if (CTX.drcs_suspended && CTX.cur_y == 0) {
                    /* Sortie de la rangee 00 par LF : le telechargement DRCS
                     * reprend ou il en etait (STUM 2 par. 2.3.4) */
                    cursor_down(ctx);
                    CTX.state = (CTX.drcs_suspended == 1)
                                 ? VTX_STATE_DRCS_XFER : VTX_STATE_DRCS_HDR;
                    CTX.drcs_suspended = 0;
                    break;
                }
                cursor_down(ctx);
                break;
            case 0x0B:  /* VT - curseur haut */
                cursor_up(ctx);
                break;
            case 0x0C:  /* FF - effacer ecran + home */
                if (CTX.drcs_suspended) {
                    /* Sortie de la rangee 00 par FF : sortie du telechargement
                     * SANS completer la forme en cours (par. 2.3.4) */
                    CTX.drcs_started = 0;
                    CTX.drcs_suspended = 0;
                }
                vtx_clear_page(ctx);
                break;
            case 0x0D:  /* CR - retour chariot */
                CTX.cur_x = 0;
                break;
            case 0x0E:  /* SO - basculer G1 (mosaiques) */
                CTX.charset = CHARSET_G1;
                break;
            case 0x0F:  /* SI - basculer G0 (alphanumerique) */
                CTX.charset = CHARSET_G0;
                break;
            case 0x11:  /* DC1/CON - curseur visible */
                CTX.cur_visible = 1;
                break;
            case 0x12:  /* REP - repetition */
                CTX.state = VTX_STATE_REP;
                break;
            case 0x13:  /* SEP - separateur (touches fonction Minitel) */
                /* Le prochain octet est le code fonction ($41-$49).
                 * En reception, on le consomme sans agir (c'est le
                 * serveur qui envoie). Etat dedie: passer par le
                 * mecanisme PRO declencherait dispatch_pro (un SEP
                 * suivi de $7B repondrait ENQROM a tort). */
                CTX.state = VTX_STATE_SEP;
                break;
            case 0x14:  /* DC4/COFF - curseur invisible */
                CTX.cur_visible = 0;
                break;
            case 0x16:  /* SS2 - single shift G2 (accents) */
            case 0x19:  /* SS2 - single shift G2 (variante) */
                /* Minitel 2 : SS2 ignore si le jeu invoque est G1/G'1
                 * (STUM 2 par. 2.3.7). */
                if (g_term_model == TERM_MINITEL_2 && CTX.charset == CHARSET_G1) {
                    break;
                }
                CTX.state = VTX_STATE_SS2;
                break;
            case 0x18:  /* CAN - effacer jusqu'a fin de ligne */
                clear_eol(ctx);
                break;
            case 0x1A:  /* SUB : symbole d'erreur, « un pave remplissant
                         * l'emplacement ... avec les attributs courants »,
                         * en code comme hors code (STUM 1B, § 2-2-1-2-8
                         * vers p. 99, et § 1-5-1-3 « Le coupleur », vers
                         * p. 48). Pave G0 $7F (g0_joint)
                         * : plein meme en disjoint, jamais remplace par un
                         * DRCS. Jusqu'en v0.9.9 : un espace. */
                put_char(0x7F, CHARSET_G0);
                break;
            case 0x1B:  /* ESC */
                CTX.state = VTX_STATE_ESC;
                break;
            case 0x1E:  /* RS - home (curseur en 1,0) */
                if (CTX.drcs_suspended) {          /* idem FF */
                    CTX.drcs_started = 0;
                    CTX.drcs_suspended = 0;
                }
                CTX.cur_x = 0;
                CTX.cur_y = 1;
                break;
            case 0x1F:  /* US - positionnement curseur */
                CTX.state = VTX_STATE_US_ROW;
                break;
            default:
                break;
        }
        return;
    }

    /* Caracteres affichables ($20-$7F) */
    put_char(byte, CTX.charset);
}
