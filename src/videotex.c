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
    if (!s_ctx->dirty[row]) {
        s_ctx->dirty[row] = 1;
        s_ctx->dirty_min[row] = col_from;
        s_ctx->dirty_max[row] = col_to;
    } else {
        if (col_from < s_ctx->dirty_min[row]) s_ctx->dirty_min[row] = col_from;
        if (col_to   > s_ctx->dirty_max[row]) s_ctx->dirty_max[row] = col_to;
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
        ctx->dirty_min[r] = 0;
        ctx->dirty_max[r] = VTX_COLS - 1;
    }
}

/* ===================================================================
 *  Initialisation
 * =================================================================== */

void vtx_init(vtx_context_t* ctx)
{
    memset(ctx, 0, sizeof(vtx_context_t));
    reset_spans(ctx, 0);

    ctx->state = VTX_STATE_NORMAL;
    ctx->cur_x = 0;
    ctx->cur_y = 1;    /* Ligne 1 (ligne 0 = statut) */
    ctx->cur_visible = 1;
    ctx->charset = CHARSET_G0;
    ctx->fg_color = VTX_WHITE;
    ctx->bg_color = VTX_BLACK;
    ctx->attr_flags = 0;
    ctx->attr_size = SIZE_NORMAL;
    ctx->pending_bg = VTX_BLACK;
    ctx->pending_underline = 0;
    ctx->has_pending = 0;
    ctx->rolling_mode = 0;
    ctx->lowercase_mode = 0;
    ctx->terminal_mode = TERM_MODE_VIDEOTEX;
    /* Aiguillages defaut Minitel 1B: MODEM->ECRAN et CLAVIER->MODEM */
    ctx->aiguillages = AIG_MDM_TO_SCR | AIG_KBD_TO_MDM;
    ctx->kbd_extended = 0;
    ctx->kbd_cursor = 0;
    ctx->global_mask = 1;  /* defaut: cellules concealed cachees */
    g_global_mask = 1;     /* garder la copie renderer synchronisee */
    /* Minitel 2 : jeux de base associes a G0/G1, en-tete DRCS par defaut
     * G'0 (STUM 2 par. 2.2.2 et 2.3.2) ; les formes sont effacees (memset). */
    ctx->drcs_g0 = 0;
    ctx->drcs_g1 = 0;
    ctx->drcs_hdr_set = 0;
    vtx_current = ctx;

    vtx_clear_page(ctx);
    vtx_clear_status(ctx);
    ctx->full_refresh = 1;
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
    cell->fg = VTX_WHITE;
    cell->bg = VTX_BLACK;
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
    reset_cells(&ctx->screen[row][0], VTX_COLS);
    vtx_touch(ctx, row, 0, VTX_COLS - 1);
}

void vtx_clear_page(vtx_context_t* ctx)
{
    reset_cells(&ctx->screen[1][0], VTX_COLS * (VTX_ROWS - 1));

    /* Effacer le framebuffer HIRES d'un coup (8000 octets = $40)
     * au lieu de marquer dirty et re-rendre 1000 cellules vides */
    display_clear();
    memset(&ctx->dirty[1], 0, VTX_ROWS - 1);
    reset_spans(ctx, 1);

    ctx->cur_x = 0;
    ctx->cur_y = 1;
    ctx->charset = CHARSET_G0;
    ctx->fg_color = VTX_WHITE;
    ctx->bg_color = VTX_BLACK;
    ctx->attr_flags = 0;
    ctx->attr_size = SIZE_NORMAL;
    ctx->pending_bg = VTX_BLACK;
    ctx->has_pending = 0;
}

void vtx_clear_status(vtx_context_t* ctx)
{
    clear_row(ctx, 0);
}

void vtx_set_cursor(vtx_context_t* ctx, unsigned char row, unsigned char col)
{
    if (row < VTX_ROWS) {
        ctx->cur_y = row;
    }
    if (col < VTX_COLS) {
        ctx->cur_x = col;
    }
}

/* ===================================================================
 *  Ecriture d'un caractere a la position curseur
 * =================================================================== */

static void scroll_up(vtx_context_t* ctx);

/* ===================================================================
 *  Adressage rapide des cellules
 *
 *  &ctx->screen[row][col] fait calculer a cc65 row * 200 puis col * 5 :
 *  deux multiplications 16 bits par des constantes qui ne sont pas des
 *  puissances de deux, payees A CHAQUE CARACTERE recu (put_char etait
 *  mesure a ~3 950 cycles/caractere, cf. make bench-render). Deux tables
 *  d'offsets les remplacent par de simples lectures indexees.
 *
 *  39 * 5 = 195 : les offsets de colonne tiennent dans un octet.
 * =================================================================== */
static const unsigned int row_byte_offset[VTX_ROWS] = {
    0, 200, 400, 600, 800, 1000, 1200, 1400, 1600, 1800, 2000, 2200, 2400, 2600, 2800, 3000, 3200, 3400, 3600, 3800, 4000, 4200, 4400, 4600, 4800
};
static const unsigned char col_byte_offset[VTX_COLS] = {
    0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100, 105, 110, 115, 120, 125, 130, 135, 140, 145, 150, 155, 160, 165, 170, 175, 180, 185, 190, 195
};

/* Cellule (row, col) sans multiplication. row < VTX_ROWS et col < VTX_COLS
 * sont des PRECONDITIONS : les appelants les verifient deja. */
#define CELL_AT(ctx, row, col)                                          \
    ((vtx_cell_t*)((unsigned char*)((ctx)->screen)                       \
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

    if (s_ctx->cur_y >= VTX_ROWS || s_ctx->cur_x >= VTX_COLS) {
        return;
    }

    /* Minitel 2 : jeu DRCS associe a G0 / G1 (STUM 2 par. 2.2.2). Les codes
     * 2/0 et 7/F restent ceux du jeu de base (par. 2.3.3.1, remarque). */
    if (ch != 0x20 && ch != 0x7F) {
        if (cs == CHARSET_G0 && s_ctx->drcs_g0)      cs = CHARSET_DRCS0;
        else if (cs == CHARSET_G1 && s_ctx->drcs_g1) cs = CHARSET_DRCS1;
    }

    /* Mode majuscule force (defaut Minitel 1B) : 'a'-'z' -> 'A'-'Z'.
     * Ne s'applique qu'au jeu G0 (alphanumerique). G1 mosaique et
     * G2 supplementaire ne sont pas affectes. */
    if (cs == CHARSET_G0 && !s_ctx->lowercase_mode &&
        ch >= 'a' && ch <= 'z') {
        ch -= 32;
    }

    cell = CELL_AT(s_ctx, s_ctx->cur_y, s_ctx->cur_x);
    cell->ch = ch;
    cell->charset = cs;
    cell->fg = s_ctx->fg_color;
    cell->bg = s_ctx->bg_color;
    cell->flags = (unsigned char)(s_ctx->attr_flags | (s_ctx->attr_size << SIZE_SHIFT));

    /* Appliquer les attributs en attente sur un delimiteur (espace G0) */
    if (ch == 0x20 && cs == CHARSET_G0 && s_ctx->has_pending) {
        cell->bg = s_ctx->pending_bg;
        if (s_ctx->pending_underline) {
            cell->flags |= ATTR_UNDERLINE;
        }
        s_ctx->bg_color = s_ctx->pending_bg;
        if (s_ctx->pending_underline) {
            s_ctx->attr_flags |= ATTR_UNDERLINE;
        } else {
            s_ctx->attr_flags &= ~ATTR_UNDERLINE;
        }
        s_ctx->has_pending = 0;
    }

    /* Marquer la plage modifiee: la cellule, +1 colonne en double
     * largeur/taille (moitie droite du glyphe) */
    {
        unsigned char span_end = s_ctx->cur_x;
        if ((s_ctx->attr_size == SIZE_DOUBLE_WIDTH ||
             s_ctx->attr_size == SIZE_DOUBLE_SIZE) &&
            span_end < VTX_COLS - 1) {
            ++span_end;
        }
        touch_here(s_ctx->cur_y, s_ctx->cur_x, span_end);
        /* Double hauteur/taille: la moitie haute du glyphe est rendue
         * dans les lignes pixel de la ligne du dessus. Sans ce dirty,
         * un re-rendu isole de cur_y-1 ecraserait la moitie haute. */
        if ((s_ctx->attr_size == SIZE_DOUBLE_HEIGHT ||
             s_ctx->attr_size == SIZE_DOUBLE_SIZE) && s_ctx->cur_y > 0) {
            touch_here(s_ctx->cur_y - 1, s_ctx->cur_x, span_end);
        }
    }
    s_ctx->last_char = ch;
    s_ctx->last_charset = cs;

    /* Avancer le curseur (2 colonnes pour double largeur/taille) */
    if (s_ctx->attr_size == SIZE_DOUBLE_WIDTH ||
        s_ctx->attr_size == SIZE_DOUBLE_SIZE) {
        s_ctx->cur_x += 2;
    } else {
        s_ctx->cur_x++;
    }
    if (s_ctx->cur_x >= VTX_COLS) {
        s_ctx->cur_x = 0;
        s_ctx->cur_y++;
        if (s_ctx->cur_y >= VTX_ROWS) {
            if (s_ctx->rolling_mode) {
                scroll_up(s_ctx);
                s_ctx->cur_y = VTX_ROWS - 1;
            } else {
                /* Mode page (defaut): retour en ligne 1 (pas 0 = status) */
                s_ctx->cur_y = 1;
            }
        }
    }
}

/* ===================================================================
 *  Deplacement curseur
 * =================================================================== */

static void cursor_left(vtx_context_t* ctx)
{
    if (ctx->cur_x > 0) {
        ctx->cur_x--;
    } else if (ctx->cur_y > 1) {
        ctx->cur_x = VTX_COLS - 1;
        ctx->cur_y--;
    }
}

static void cursor_right(vtx_context_t* ctx)
{
    ctx->cur_x++;
    if (ctx->cur_x >= VTX_COLS) {
        ctx->cur_x = 0;
        ctx->cur_y++;
        if (ctx->cur_y >= VTX_ROWS) {
            ctx->cur_y = VTX_ROWS - 1;
        }
    }
}

static void cursor_up(vtx_context_t* ctx)
{
    if (ctx->cur_y > 1) {
        ctx->cur_y--;
    }
}

static void cursor_down(vtx_context_t* ctx)
{
    if (ctx->cur_y < VTX_ROWS - 1) {
        ctx->cur_y++;
    } else if (ctx->rolling_mode) {
        /* Mode rouleau: scroll d'une ligne, curseur reste en bas */
        scroll_up(ctx);
    }
    /* Mode page: pas de scroll, curseur reste en ligne 24 */
}

static void scroll_up(vtx_context_t* ctx)
{
    /* Decale les lignes 2..VTX_ROWS-1 vers 1..VTX_ROWS-2.
     * La ligne 0 (statut) est preservee. La derniere ligne est effacee.
     * Cout: ~5.5 Ko de memmove + full_refresh (~80ms a 1 MHz). */
    memmove(&ctx->screen[1][0], &ctx->screen[2][0],
            sizeof(vtx_cell_t) * VTX_COLS * (VTX_ROWS - 2));
    clear_row(ctx, VTX_ROWS - 1);
    ctx->full_refresh = 1;
}

/* ===================================================================
 *  Effacement partiel
 * =================================================================== */

static void clear_eol(vtx_context_t* ctx)
{
    reset_cells(&ctx->screen[ctx->cur_y][ctx->cur_x],
                VTX_COLS - ctx->cur_x);
    vtx_touch(ctx, ctx->cur_y, ctx->cur_x, VTX_COLS - 1);
}

static void clear_eos(vtx_context_t* ctx)
{
    unsigned char r;
    clear_eol(ctx);
    for (r = ctx->cur_y + 1; r < VTX_ROWS; ++r) {
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
        ctx->fg_color = byte - 0x40;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* Flash on/off: ESC $48/$49 */
    if (byte == 0x48) {
        ctx->attr_flags |= ATTR_FLASH;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }
    if (byte == 0x49) {
        ctx->attr_flags &= ~ATTR_FLASH;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* Taille: ESC $4C-$4F */
    if (byte >= 0x4C && byte <= 0x4F) {
        ctx->attr_size = byte - 0x4C;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* Couleur fond: ESC $50-$57 */
    if (byte >= 0x50 && byte <= 0x57) {
        /* Fond = attribut serie, mis en attente */
        ctx->pending_bg = byte - 0x50;
        ctx->has_pending = 1;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* Masquage: ESC $58 */
    if (byte == 0x58) {
        ctx->attr_flags |= ATTR_CONCEALED;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* Soulignement (G0) / Mosaique separee (G1): ESC $59=off, $5A=on */
    if (byte == 0x59) {
        if (ctx->charset == CHARSET_G1) {
            /* Clear separated mosaic mode */
            ctx->attr_flags &= ~ATTR_SEPARATED;
        } else {
            ctx->pending_underline = 0;
            ctx->has_pending = 1;
        }
        ctx->state = VTX_STATE_NORMAL;
        return;
    }
    if (byte == 0x5A) {
        if (ctx->charset == CHARSET_G1) {
            /* Set separated mosaic mode */
            ctx->attr_flags |= ATTR_SEPARATED;
        } else {
            ctx->pending_underline = 1;
            ctx->has_pending = 1;
        }
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* Mask global: ESC $23 $20 $58/$5F (set/reset)
     * Reference: miedit (constant.js mask-global) */
    if (byte == 0x23) {
        ctx->state = VTX_STATE_MASK_SP;
        return;
    }

    /* CSI: ESC $5B */
    if (byte == 0x5B) {
        ctx->state = VTX_STATE_CSI;
        ctx->csi_len = 0;
        return;
    }

    /* Inversion: ESC $5C=OFF (fond normal), $5D=ON (fond inverse)
     * Ref: telenet emulateur.js, miedit directStream */
    if (byte == 0x5C) {
        ctx->attr_flags &= ~ATTR_INVERT;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }
    if (byte == 0x5D) {
        ctx->attr_flags |= ATTR_INVERT;
        ctx->state = VTX_STATE_NORMAL;
        return;
    }

    /* PRO1: ESC $39 + 1 octet (commande)
     * PRO2: ESC $3A + 2 octets (commande + parametre)
     * PRO3: ESC $3B + 3 octets (commande + 2 parametres)
     * Reference: STUM 1B (specification technique du Minitel) */
    if (byte >= VTX_PRO1_MARK && byte <= VTX_PRO3_MARK) {
        ctx->state = VTX_STATE_PRO;
        ctx->pro_kind = byte - VTX_PRO_BASE;  /* $39->1, $3A->2, $3B->3 */
        ctx->pro_idx = 0;
        return;
    }

    /* SS2 (G2 single shift): ESC $19 */
    if (byte == 0x19) {
        ctx->state = VTX_STATE_SS2;
        return;
    }

    /* Minitel 2 : association des jeux (STUM 2 par. 2.2.2).
     *   ESC 2/8 4/0 : jeu alphanumerique de base -> G0
     *   ESC 2/8 2/0 4/2 : jeu DRCS G'0 -> G0
     *   ESC 2/9 6/3 : jeu semi-graphique de base -> G1
     *   ESC 2/9 2/0 4/3 : jeu DRCS G'1 -> G1
     * Les attributs actifs sont conserves. Un Minitel 1B ignore ESC 2/8. */
    if (g_term_model == TERM_MINITEL_2 && (byte == 0x28 || byte == 0x29)) {
        ctx->state = (byte == 0x28) ? VTX_STATE_ESC_G0SET : VTX_STATE_ESC_G1SET;
        return;
    }

    /* Non reconnu: ignorer et revenir a NORMAL */
    ctx->state = VTX_STATE_NORMAL;
}

/* Suite de ESC 2/8 / ESC 2/9 (Minitel 2). Toute autre valeur : sequence
 * ignoree, retour a NORMAL sans effet. */
static void process_charset_assoc(vtx_context_t* ctx, unsigned char byte)
{
    switch (ctx->state) {
        case VTX_STATE_ESC_G0SET:
            if (byte == 0x40)      { ctx->drcs_g0 = 0; break; }
            else if (byte == 0x20) { ctx->state = VTX_STATE_ESC_G0SET2; return; }
            break;
        case VTX_STATE_ESC_G0SET2:
            if (byte == 0x42) ctx->drcs_g0 = 1;
            break;
        case VTX_STATE_ESC_G1SET:
            if (byte == 0x63)      { ctx->drcs_g1 = 0; break; }
            else if (byte == 0x20) { ctx->state = VTX_STATE_ESC_G1SET2; return; }
            break;
        default: /* VTX_STATE_ESC_G1SET2 */
            if (byte == 0x43) ctx->drcs_g1 = 1;
            break;
    }
    ctx->state = VTX_STATE_NORMAL;
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
    if (!ctx->drcs_started) return;
    ctx->drcs_started = 0;
    /* Bits en attente d'une rangee incomplete : le reste est du fond */
    if (ctx->drcs_nbits && ctx->drcs_nrow < DRCS_ROWS) {
        ctx->drcs_form[ctx->drcs_nrow] =
            (unsigned char)(ctx->drcs_acc << (8 - ctx->drcs_nbits));
    }
    if (ctx->drcs_code >= DRCS_FIRST && ctx->drcs_code <= DRCS_LAST) {
        memcpy(&ctx->drcs[ctx->drcs_hdr_set][ctx->drcs_code - DRCS_FIRST][0],
               ctx->drcs_form, DRCS_ROWS);
    }
}

static void drcs_form_begin(vtx_context_t* ctx)
{
    ctx->drcs_started = 1;
    ctx->drcs_nbyte = 0;
    ctx->drcs_nrow = 0;
    ctx->drcs_nbits = 0;
    ctx->drcs_acc = 0;
    memset(ctx->drcs_form, 0, DRCS_ROWS);
}

/* Un octet de donnees : 6 bits (b5..b0), rangees de 8 pixels remplies de
 * gauche a droite et de haut en bas, les bits excedentaires passant a la
 * rangee suivante ; au-dela de 14 octets, filtre (par. 2.3.3.2). */
static void drcs_data(vtx_context_t* ctx, unsigned char six)
{
    if (!ctx->drcs_started || ctx->drcs_nbyte >= DRCS_BYTES) return;
    ++ctx->drcs_nbyte;
    ctx->drcs_acc = (unsigned short)((ctx->drcs_acc << 6) | (six & 0x3F));
    ctx->drcs_nbits += 6;
    while (ctx->drcs_nbits >= 8) {
        ctx->drcs_nbits -= 8;
        if (ctx->drcs_nrow < DRCS_ROWS) {
            ctx->drcs_form[ctx->drcs_nrow++] =
                (unsigned char)(ctx->drcs_acc >> ctx->drcs_nbits);
        }
        ctx->drcs_acc &= (unsigned short)((1u << ctx->drcs_nbits) - 1);
    }
}

/* Octet suivant US 2/3. Retourne 1 si consomme ; 0 si l'octet doit etre
 * traite normalement (resynchronisation C0 en cours d'en-tete). */
static unsigned char drcs_header(vtx_context_t* ctx, unsigned char byte)
{
    static const unsigned char hdr[4] = { 0x20, 0x20, 0x42, 0x49 };
    unsigned char i = ctx->drcs_hdr_idx;

    if (i == 0) {
        if (byte == 0x20) {                     /* en-tete */
            ctx->drcs_hdr_idx = 1;
            return 1;
        }
        if (byte >= DRCS_FIRST && byte <= DRCS_LAST) {   /* transfert : Y */
            ctx->drcs_code = byte;
            ctx->drcs_started = 0;
            ctx->state = VTX_STATE_DRCS_XFER;
            return 1;
        }
        ctx->state = VTX_STATE_NORMAL;          /* erronee : ignoree */
        return (byte < 0x20) ? 0 : 1;
    }
    if (byte == 0x1F) {                         /* US : rangee 00 ? (US_COL) */
        ctx->drcs_suspended = 2;
        ctx->state = VTX_STATE_US_ROW;
        return 1;
    }
    if (byte < 0x20) {                          /* C0 : resynchronisation */
        ctx->state = VTX_STATE_NORMAL;
        return 0;
    }
    /* i = 1..4 : 2/0 2/0 (4/2|4/3) 4/9 */
    if ((i == 3 && (byte == 0x42 || byte == 0x43)) ||
        (i != 3 && byte == hdr[i - 1])) {
        if (i == 3) ctx->drcs_code = (byte == 0x43) ? 1 : 0;   /* candidat */
        if (i == 4) {
            ctx->drcs_hdr_set = ctx->drcs_code;  /* en-tete complete */
            ctx->state = VTX_STATE_NORMAL;
        } else {
            ctx->drcs_hdr_idx = i + 1;
        }
        return 1;
    }
    /* Syntaxe erronee : l'en-tete precedente reste valide (le jeu candidat
     * n'a pas ete retenu). */
    ctx->state = VTX_STATE_NORMAL;
    return 1;
}

/* Octet en cours de transfert. Retourne 1 si consomme. */
static unsigned char drcs_xfer(vtx_context_t* ctx, unsigned char byte)
{
    if (byte == 0x1F) {
        /* US : sortie du telechargement, sauf si le US est un acces en
         * rangee 00 (decide en US_COL) : le transfert est alors suspendu et
         * reprend sur le LF qui quitte la rangee 00 (par. 2.3.4). */
        ctx->drcs_suspended = 1;
        ctx->state = VTX_STATE_US_ROW;
        return 1;
    }
    if (byte == 0x00) return 1;                 /* NUL : rien */
    if (byte == 0x30) {                         /* B1 : delimiteur de forme */
        if (ctx->drcs_started) {
            drcs_form_store(ctx);
            ++ctx->drcs_code;
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
    return &ctx->drcs[set ? 1 : 0][ch - DRCS_FIRST][0];
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
        if (ctx->csi_len < sizeof(ctx->csi_buf) - 1) {
            ctx->csi_buf[ctx->csi_len++] = byte;
        }
        return;
    }

    /* Terminer: parser param1[;param2] */
    ctx->csi_buf[ctx->csi_len] = 0;
    param = 0;
    param2 = 0;
    {
        unsigned char i;
        /* Accumulation BORNEE a 64 : au-dela ca n'a aucun sens (ecran 40x25)
         * et un parametre a 3+ chiffres deborderait l'unsigned char (ex.
         * "999A" -> 231) -> mouvement curseur faux ou boucle excessive. Le
         * calcul intermediaire passe par unsigned int pour ne jamais wrapper. */
        for (i = 0; i < ctx->csi_len && ctx->csi_buf[i] != ';'; ++i) {
            unsigned int t = (unsigned int)param * 10 + (ctx->csi_buf[i] - '0');
            param = (t > 64) ? 64 : (unsigned char)t;
        }
        if (i < ctx->csi_len && ctx->csi_buf[i] == ';') {
            for (++i; i < ctx->csi_len && ctx->csi_buf[i] != ';'; ++i) {
                unsigned int t = (unsigned int)param2 * 10 + (ctx->csi_buf[i] - '0');
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
            break;
        case 'J':   /* ED - effacer ecran */
            if (param == 2 || ctx->csi_len == 0) {
                vtx_clear_page(ctx);
            } else {
                clear_eos(ctx);
            }
            break;
        case 'K':   /* EL - effacer ligne */
            clear_eol(ctx);
            break;
        case 'h':   /* Mode set (curseur visible, etc.) */
            ctx->cur_visible = 1;
            break;
        case 'l':   /* Mode reset (curseur invisible) */
            ctx->cur_visible = 0;
            break;
        case 'n':   /* Minitel 2 (STUM 2 par. 2.5) : CSI 3/6 6/E = demande de
                     * position curseur ; reponse CSI Pr 3/B Pc 5/2. Pr = rangee
                     * (0-24) et Pc = colonne 1-based, comme CSI H les lit. */
            if (param == 6 && g_term_model == TERM_MINITEL_2) {
                unsigned char v;
                serial_send(0x1B);
                serial_send(0x5B);
                v = ctx->cur_y;
                if (v >= 10) serial_send((unsigned char)('0' + v / 10));
                serial_send((unsigned char)('0' + v % 10));
                serial_send(0x3B);
                v = (unsigned char)(ctx->cur_x + 1);
                if (v >= 10) serial_send((unsigned char)('0' + v / 10));
                serial_send((unsigned char)('0' + v % 10));
                serial_send(0x52);
                serial_tx_flush();
            }
            break;
        default:
            break;
    }

    ctx->state = VTX_STATE_NORMAL;
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
    if (ctx->pro_kind == 1) {
        switch (ctx->pro_buf[0]) {
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
    if (ctx->pro_kind == 2) {
        unsigned char on;

        /* PRO2 + $72 + module = demande de status d'un module.
         * Format reponse: ESC ($1B) + $3B + $73 + module + status byte
         * (= PRO3 + $73 + ...). Reference: STUM 1B + eMinitel.
         * Les modules courants sont $59 (KEYBOARD_IN) et $51 (KEYBOARD_OUT).
         * Le status byte reflete l'etat du clavier: bits 6-7 fixes par
         * convention, plus quelques flags optionnels (minuscules etc.). */
        if (ctx->pro_buf[0] == 0x72) {
            unsigned char target = ctx->pro_buf[1];
            if (target == 0x59 || target == 0x51) {
                unsigned char status = 0xC0;  /* bits 6-7 fixes (cf. eMinitel) */
                if (ctx->lowercase_mode) status |= 0x02;
                if (ctx->rolling_mode)   status |= 0x04;
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
        if (ctx->pro_buf[0] == 0x32) {
            if (ctx->pro_buf[1] == 0x7E) {       /* MODE VIDEOTEX */
                ctx->terminal_mode = TERM_MODE_VIDEOTEX;
                /* ACK: SEP ($13) + $71 (videotex confirme) */
                serial_send(0x13);
                serial_send(0x71);
                serial_tx_flush();
            } else if (ctx->pro_buf[1] == 0x7D) { /* MODE MIXTE */
                /* PRO2 MIXTE 1 (STUM 1B partie 2 chap. 6 par. 12.2) : passage
                 * au standard Teletel mode Mixte (80 colonnes, ISO 6429),
                 * acquitte par SEP 0x70. main.c bascule l'ecran (teleinfo.c)
                 * quand terminal_mode passe a TERM_MODE_MIXED. */
                ctx->terminal_mode = TERM_MODE_MIXED;
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
        if (ctx->pro_buf[0] == 0x6B) {
            term_prog_speed(ctx->pro_buf[1]);
            return;
        }

        /* PRO2 + $69/$6A = START/STOP d'un mode (rolling, lowercase, ...) */
        if (ctx->pro_buf[0] == 0x69)      on = 1;  /* START */
        else if (ctx->pro_buf[0] == 0x6A) on = 0;  /* STOP */
        else return;
        switch (ctx->pro_buf[1]) {
            case 0x43:  /* ROLLING */
                ctx->rolling_mode = on;
                break;
            case 0x45:  /* LOWERCASE */
                ctx->lowercase_mode = on;
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
    if (ctx->pro_kind == 3) {
        unsigned char on;
        unsigned char dest, src, mask;
        /* PRO3 START/STOP module: $69/$6A + $59 (KEYBOARD_IN) + sub-cmd
         *   sub-cmd $41 = clavier etendu (touches alt)
         *   sub-cmd $43 = clavier curseur (fleches actives)
         * Reference: miedit pro3Start/Stop -> startKeyboardFunction. */
        if ((ctx->pro_buf[0] == 0x69 || ctx->pro_buf[0] == 0x6A)
            && ctx->pro_buf[1] == 0x59) {
            unsigned char on2 = (ctx->pro_buf[0] == 0x69);
            switch (ctx->pro_buf[2]) {
                case 0x41: ctx->kbd_extended = on2; break;
                case 0x43: ctx->kbd_cursor   = on2; break;
                default: break;
            }
            return;
        }

        if (ctx->pro_buf[0] == 0x61)      on = 1;  /* SWITCH ON */
        else if (ctx->pro_buf[0] == 0x60) on = 0;  /* SWITCH OFF */
        else return;  /* autres START/STOP: ignore */

        dest = ctx->pro_buf[1];
        src  = ctx->pro_buf[2];
        mask = 0;
        if (dest == 0x58 && src == 0x51) mask = AIG_KBD_TO_SCR;
        else if (dest == 0x58 && src == 0x59) mask = AIG_MDM_TO_SCR;
        else if (dest == 0x59 && src == 0x51) mask = AIG_KBD_TO_MDM;
        else if (dest == 0x59 && src == 0x58) mask = AIG_SCR_TO_MDM;
        else return;  /* couple non gere */

        if (on) ctx->aiguillages |= mask;
        else    ctx->aiguillages &= ~mask;

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
                if (ctx->aiguillages & AIG_KBD_TO_SCR) status |= 0x02;
                if (ctx->aiguillages & AIG_MDM_TO_SCR) status |= 0x04;
            } else if (dest == 0x59) {  /* dest = MODEM */
                if (ctx->aiguillages & AIG_SCR_TO_MDM) status |= 0x01;
                if (ctx->aiguillages & AIG_KBD_TO_MDM) status |= 0x02;
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
    if (ctx->state == VTX_STATE_DRCS_XFER) {
        if (drcs_xfer(ctx, byte)) return;
    } else if (ctx->state == VTX_STATE_DRCS_HDR) {
        if (drcs_header(ctx, byte)) return;
        /* C0 : l'en-tete est abandonnee, l'octet est traite normalement */
    }

    /* Re-sync: un ESC ($1B) recu en milieu de sequence multi-octets
     * abandonne l'etat courant et redemarre une nouvelle sequence ESC.
     * $1B n'est jamais une valeur legitime dans les payloads US/SS2/PRO/CSI
     * (US: $40+ligne, SS2: $20-$7F, CSI param: '0'-'9'/';'), donc le voir
     * signifie qu'on a perdu le sync et qu'une nouvelle commande arrive. */
    if (byte == 0x1B && ctx->state != VTX_STATE_NORMAL
                    && ctx->state != VTX_STATE_ESC) {
        ctx->state = VTX_STATE_ESC;
        return;
    }

    switch (ctx->state) {

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
            ctx->drcs_hdr_idx = 0;
            ctx->state = VTX_STATE_DRCS_HDR;
            return;
        }
        /* Valider la plage en amont: un octet < $40 sous-deborderait
         * l'unsigned char (vtx_set_cursor reclampe en US_COL, mais on rejette
         * proprement plutot que de s'appuyer sur ce filet). */
        ctx->us_row = (byte >= VTX_ADDR_BASE) ? (byte - VTX_ADDR_BASE) : 0;
        ctx->state = VTX_STATE_US_COL;
        return;

    case VTX_STATE_US_COL:
        /* Col = byte - $41. Si byte=$40, col=0 (pas -1).
         * Le Minitel utilise $40 pour col 0. */
        vtx_set_cursor(ctx, ctx->us_row,
                        (byte > VTX_ADDR_BASE) ? (byte - (VTX_ADDR_BASE + 1)) : 0);
        /* US reset tous les attributs (norme STUM p.91)
         * Ref: telenet emulateur.js lignes 785-795 */
        ctx->charset = CHARSET_G0;      /* modeG1 = false */
        ctx->fg_color = VTX_WHITE;      /* fgColor = 7 */
        ctx->bg_color = VTX_BLACK;      /* bgColor = 0 */
        ctx->attr_flags = 0;            /* souligne, inversion, clignotement = false */
        ctx->attr_size = SIZE_NORMAL;   /* taille = 0 */
        ctx->has_pending = 0;
        /* Minitel 2 : un acces en rangee 00 reassocie les jeux de base a
         * G0 et G1 (STUM 2 par. 2.2.2). */
        if (ctx->us_row == 0) {
            ctx->drcs_g0 = 0;
            ctx->drcs_g1 = 0;
            /* Telechargement DRCS interrompu par la rangee 00 : il reste
             * suspendu (reprise sur LF, abandon sur FF/RS, par. 2.3.4) */
        } else if (ctx->drcs_suspended) {
            /* US vers une autre rangee : sortie du telechargement en
             * completant la forme en cours (par. 2.3.3.3) */
            if (ctx->drcs_suspended == 1) drcs_form_store(ctx);
            ctx->drcs_suspended = 0;
        }
        ctx->state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_SS2:
        /* Single shift G2: diacritiques ou caractere G2 standalone */
        if (byte >= SS2_ACC_MIN && byte <= SS2_ACC_MAX) {
            /* Code accent: sauver et attendre le caractere base */
            ctx->ss2_accent = byte;
            ctx->state = VTX_STATE_SS2_ACC;
            return;
        }
        /* Caractere G2 standalone (ex: $23=livre, $30=degre) */
        put_char(byte, CHARSET_G2);
        ctx->state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_SS2_ACC:
        /* Caractere base apres un code accent.
         * Combiner accent + base pour un glyphe accentue.
         * Les glyphes sont dans font_g2_extra[9..20].
         * On utilise CHARSET_G2 avec un code interne $80+. */
        {
            unsigned char acc_ch = 0;
            /* Mapper (accent, base) -> code interne G2 accentue */
            switch (ctx->ss2_accent) {
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
            if (ctx->drcs_g0 && ctx->charset == CHARSET_G0) {
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
        ctx->state = VTX_STATE_NORMAL;
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
                put_char(ctx->last_char, ctx->last_charset);
            }
        }
        ctx->state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_MASK_SP:
        /* Mask global: attend $20 (espace) apres ESC #. */
        ctx->state = (byte == 0x20) ? VTX_STATE_MASK_END : VTX_STATE_NORMAL;
        return;

    case VTX_STATE_MASK_END:
        /* Mask global: $58 = masquer (cacher concealed),
         *              $5F = demasquer (rendre concealed visible). */
        if (byte == 0x58) {
            ctx->global_mask = 1;
            g_global_mask = 1;
        } else if (byte == 0x5F) {
            ctx->global_mask = 0;
            g_global_mask = 0;
        }
        ctx->full_refresh = 1;
        ctx->state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_SEP:
        /* Code fonction apres SEP: consomme, aucune action. */
        ctx->state = VTX_STATE_NORMAL;
        return;

    case VTX_STATE_PRO:
        /* Accumule un octet de payload PRO. */
        if (ctx->pro_idx < 3) {
            ctx->pro_buf[ctx->pro_idx++] = byte;
        }
        if (ctx->pro_idx >= ctx->pro_kind) {
            dispatch_pro(ctx);
            ctx->state = VTX_STATE_NORMAL;
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
                if (ctx->drcs_suspended && ctx->cur_y == 0) {
                    /* Sortie de la rangee 00 par LF : le telechargement DRCS
                     * reprend ou il en etait (STUM 2 par. 2.3.4) */
                    cursor_down(ctx);
                    ctx->state = (ctx->drcs_suspended == 1)
                                 ? VTX_STATE_DRCS_XFER : VTX_STATE_DRCS_HDR;
                    ctx->drcs_suspended = 0;
                    break;
                }
                cursor_down(ctx);
                break;
            case 0x0B:  /* VT - curseur haut */
                cursor_up(ctx);
                break;
            case 0x0C:  /* FF - effacer ecran + home */
                if (ctx->drcs_suspended) {
                    /* Sortie de la rangee 00 par FF : sortie du telechargement
                     * SANS completer la forme en cours (par. 2.3.4) */
                    ctx->drcs_started = 0;
                    ctx->drcs_suspended = 0;
                }
                vtx_clear_page(ctx);
                break;
            case 0x0D:  /* CR - retour chariot */
                ctx->cur_x = 0;
                break;
            case 0x0E:  /* SO - basculer G1 (mosaiques) */
                ctx->charset = CHARSET_G1;
                break;
            case 0x0F:  /* SI - basculer G0 (alphanumerique) */
                ctx->charset = CHARSET_G0;
                break;
            case 0x11:  /* DC1/CON - curseur visible */
                ctx->cur_visible = 1;
                break;
            case 0x12:  /* REP - repetition */
                ctx->state = VTX_STATE_REP;
                break;
            case 0x13:  /* SEP - separateur (touches fonction Minitel) */
                /* Le prochain octet est le code fonction ($41-$49).
                 * En reception, on le consomme sans agir (c'est le
                 * serveur qui envoie). Etat dedie: passer par le
                 * mecanisme PRO declencherait dispatch_pro (un SEP
                 * suivi de $7B repondrait ENQROM a tort). */
                ctx->state = VTX_STATE_SEP;
                break;
            case 0x14:  /* DC4/COFF - curseur invisible */
                ctx->cur_visible = 0;
                break;
            case 0x16:  /* SS2 - single shift G2 (accents) */
            case 0x19:  /* SS2 - single shift G2 (variante) */
                /* Minitel 2 : SS2 ignore si le jeu invoque est G1/G'1
                 * (STUM 2 par. 2.3.7). */
                if (g_term_model == TERM_MINITEL_2 && ctx->charset == CHARSET_G1) {
                    break;
                }
                ctx->state = VTX_STATE_SS2;
                break;
            case 0x18:  /* CAN - effacer jusqu'a fin de ligne */
                clear_eol(ctx);
                break;
            case 0x1A:  /* SUB - substitution (affiche espace) */
                put_char(' ', ctx->charset);
                break;
            case 0x1B:  /* ESC */
                ctx->state = VTX_STATE_ESC;
                break;
            case 0x1E:  /* RS - home (curseur en 1,0) */
                if (ctx->drcs_suspended) {          /* idem FF */
                    ctx->drcs_started = 0;
                    ctx->drcs_suspended = 0;
                }
                ctx->cur_x = 0;
                ctx->cur_y = 1;
                break;
            case 0x1F:  /* US - positionnement curseur */
                ctx->state = VTX_STATE_US_ROW;
                break;
            default:
                break;
        }
        return;
    }

    /* Caracteres affichables ($20-$7F) */
    put_char(byte, ctx->charset);
}
