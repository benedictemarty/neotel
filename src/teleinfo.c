/**
 * @file teleinfo.c
 * @brief Decodeur ISO 6429 de l'ecran 80 colonnes (voir teleinfo.h)
 *
 * Chaque regle porte la reference de la STUM 1B (partie 3, chapitre 2, page)
 * ou de la STUM 2 qui la fonde. Ce qui n'est pas ecrit dans les STUM est
 * signale "hypothese".
 */

#include <string.h>
#include "teleinfo.h"
#include "serial.h"
#include "terminal.h"
#include "font80.h"

/* Contexte unique de la cible (voir ctx.h) : -4,4 Ko de code. */
#include "ctx.h"
#ifdef __CC65__
#define CTX VTX_TI
#else
#define CTX (*ctx)
#endif

ti_context_t* ti_current;

/* Adressage des rangees sans multiplication par 160 (cc65 emet sinon une
 * multiplication 16 bits a chaque acces, tres couteuse en taille de code). */
static const unsigned int ti_row_off[TI_ROWS] = {
    0, 160, 320, 480, 640, 800, 960, 1120, 1280, 1440, 1600, 1760, 1920,
    2080, 2240, 2400, 2560, 2720, 2880, 3040, 3200, 3360, 3520, 3680, 3840
};
#define ROW(ctx, r) ((ti_cell_t*)((unsigned char*)CTX.screen + ti_row_off[(r)]))

/* ===================================================================
 *  Cellules
 * =================================================================== */

static void clear_cells(ti_cell_t* c, unsigned int n)
{
    unsigned int done;
    if (n == 0) return;
    c->ch = ' ';
    c->attr = 0;
    for (done = 1; done < n; ) {
        unsigned int k = n - done;
        if (k > done) k = done;
        memcpy(c + done, c, k * sizeof(ti_cell_t));
        done += k;
    }
}

void ti_touch(ti_context_t* ctx, unsigned char row)
{
    if (row < TI_ROWS) CTX.dirty[row] = 1;
}

static void clear_row(ti_context_t* ctx, unsigned char row)
{
    clear_cells((ROW(ctx, row) + (0)), TI_COLS);
    ti_touch(ctx, row);
}

/* Initialisation de l'ecran (STUM 1B p. 161 ; mode Mixte p. 106) : page
 * effacee, format cols, curseur (1,1) visible, rouleau, blanc sur noir sans
 * attribut, jeu americain. La rangee 00 n'est effacee que par ESC c (p. 169). */
static void reset_screen(ti_context_t* ctx, unsigned char cols, unsigned char row0_too)
{
    unsigned char r;
    for (r = row0_too ? 0 : 1; r < TI_ROWS; ++r) clear_row(ctx, r);
    CTX.cols = cols;
    CTX.cur_x = 0;
    CTX.cur_y = 1;
    CTX.attr = 0;
    CTX.shift = 0;
    CTX.g0_set = TI_SET_US;
    CTX.g1_set = TI_SET_FR;
    CTX.roll = 1;
    CTX.insert = 0;
    CTX.cur_visible = 1;
    CTX.state = TI_STATE_NORMAL;
    CTX.csi_len = 0;
    CTX.csi_priv = 0;
    CTX.full_refresh = 1;
}

void ti_init(ti_context_t* ctx)
{
    memset(ctx, 0, sizeof *ctx);
    reset_screen(ctx, TI_COLS, 1);
    ti_current = ctx;
}

/* ===================================================================
 *  Deplacements
 * =================================================================== */

/* Rouleau : les rangees 02-24 montent d'un cran, la 24 est effacee. */
static void scroll_up(ti_context_t* ctx)
{
    memmove((ROW(ctx, 1) + (0)), (ROW(ctx, 2) + (0)),
            sizeof(ti_cell_t) * TI_COLS * (TI_ROWS - 2));
    clear_row(ctx, TI_ROWS - 1);
    CTX.full_refresh = 1;
}

static void scroll_down(ti_context_t* ctx)
{
    memmove((ROW(ctx, 2) + (0)), (ROW(ctx, 1) + (0)),
            sizeof(ti_cell_t) * TI_COLS * (TI_ROWS - 2));
    clear_row(ctx, 1);
    CTX.full_refresh = 1;
}

/* LF / VT / FF / IND (p. 166) : rangee 24 -> rangee 1 en mode page, rouleau
 * sinon. Colonne inchangee. */
static void line_feed(ti_context_t* ctx)
{
    if (CTX.cur_y < TI_ROWS - 1) {
        ++CTX.cur_y;
    } else if (CTX.roll) {
        scroll_up(ctx);
    } else {
        CTX.cur_y = 1;
    }
}

/* RI (p. 166) : rangee 1 -> rangee 24 en mode page, rouleau descendant sinon. */
static void reverse_index(ti_context_t* ctx)
{
    if (CTX.cur_y > 1) {
        --CTX.cur_y;
    } else if (CTX.roll) {
        scroll_down(ctx);
    } else {
        CTX.cur_y = TI_ROWS - 1;
    }
}

/* CR (p. 166) : "colonne 1 de la rangee suivante" ; NEL idem. */
static void new_line(ti_context_t* ctx)
{
    CTX.cur_x = 0;
    line_feed(ctx);
}

/* ===================================================================
 *  Ecriture
 * =================================================================== */

static void put_cell(ti_context_t* ctx, unsigned char row, unsigned char col,
                     unsigned char ch, unsigned char attr)
{
    ti_cell_t* c = (ROW(ctx, row) + (col));
    c->ch = ch;
    c->attr = attr;
    ti_touch(ctx, row);
}

/* Caractere visualisable sur les rangees 01-24. */
static void put_char(ti_context_t* ctx, unsigned char ch)
{
    ti_cell_t* rowp;
    unsigned char attr = CTX.attr | TI_SET_ATTR(CTX.shift ? CTX.g1_set : CTX.g0_set);

    /* Auto-wrap differe (ISO 6429) : apres la 80e ecriture, cur_x vaut cols
     * (etat "en attente") ; c'est le caractere suivant qui passe a la ligne,
     * pas la 80e ecriture. Evite une rangee sautee quand le serveur envoie
     * un CR apres avoir rempli la 80e colonne. La STUM ne decrit pas ce cas
     * (comportement ISO 6429, coherent avec les CSI de deplacement qui
     * s'arretent au bord droit, p. 168). Tout deplacement explicite du
     * curseur le ramene a une valeur < cols et annule l'attente.  */
    if (CTX.cur_x >= CTX.cols) new_line(ctx);
    rowp = (ROW(ctx, CTX.cur_y) + (0));
    if (CTX.insert) {
        /* SM4 (p. 167) : decalage a droite, limite a la rangee, le dernier
         * caractere est perdu */
        memmove(&rowp[CTX.cur_x + 1], &rowp[CTX.cur_x],
                sizeof(ti_cell_t) * (CTX.cols - 1 - CTX.cur_x));
    }
    put_cell(ctx, CTX.cur_y, CTX.cur_x, ch, attr);
    ++CTX.cur_x;              /* peut atteindre cols : passage a la ligne differe */
}

/* Symbole d'erreur : pave plein avec les attributs courants (p. 169-170). */
static void put_error(ti_context_t* ctx)
{
    CTX.attr |= TI_ATTR_ERROR;
    put_char(ctx, 0x7F);
    CTX.attr &= (unsigned char)~TI_ATTR_ERROR;
}

/* ===================================================================
 *  Sequences de commande CSI (p. 167-169, STUM 2 p. 40)
 * =================================================================== */

/* Parametre numerique n (1-based dans csi_buf), 0 = absent. */
static unsigned char csi_param(const ti_context_t* ctx, unsigned char n)
{
    unsigned char i, cur = 1;
    unsigned int v = 0;
    for (i = 0; i < CTX.csi_len; ++i) {
        if (CTX.csi_buf[i] == ';') { ++cur; continue; }
        if (cur == n) {
            v = v * 10 + (CTX.csi_buf[i] - '0');
            if (v > 200) v = 200;
        }
    }
    return (unsigned char)v;
}

static void erase_display(ti_context_t* ctx, unsigned char ps)
{
    unsigned char r;
    switch (ps) {
        case 0:     /* du curseur (inclus) a la fin de la page */
            clear_cells((ROW(ctx, CTX.cur_y) + (CTX.cur_x)), (unsigned int)CTX.cols - CTX.cur_x);
            ti_touch(ctx, CTX.cur_y);
            for (r = CTX.cur_y + 1; r < TI_ROWS; ++r) clear_row(ctx, r);
            break;
        case 1:     /* du debut de la page au curseur (inclus) */
            for (r = 1; r < CTX.cur_y; ++r) clear_row(ctx, r);
            clear_cells((ROW(ctx, CTX.cur_y) + (0)), (unsigned int)CTX.cur_x + 1);
            ti_touch(ctx, CTX.cur_y);
            break;
        case 2:
            for (r = 1; r < TI_ROWS; ++r) clear_row(ctx, r);
            break;
        default:
            break;
    }
}

static void erase_line(ti_context_t* ctx, unsigned char ps)
{
    switch (ps) {
        case 0:
            clear_cells((ROW(ctx, CTX.cur_y) + (CTX.cur_x)), (unsigned int)CTX.cols - CTX.cur_x);
            break;
        case 1:
            clear_cells((ROW(ctx, CTX.cur_y) + (0)), (unsigned int)CTX.cur_x + 1);
            break;
        case 2:
            clear_cells((ROW(ctx, CTX.cur_y) + (0)), CTX.cols);
            break;
        default:
            return;
    }
    ti_touch(ctx, CTX.cur_y);
}

/* IL : insertion de n rangees a la rangee active (p. 167) */
static void insert_lines(ti_context_t* ctx, unsigned char n)
{
    unsigned char r;
    if (n > (unsigned char)(TI_ROWS - CTX.cur_y)) n = (unsigned char)(TI_ROWS - CTX.cur_y);
    for (r = TI_ROWS - 1; r >= CTX.cur_y + n; --r) {
        memcpy((ROW(ctx, r) + (0)), (ROW(ctx, r - n) + (0)), sizeof(ti_cell_t) * TI_COLS);
    }
    for (r = CTX.cur_y; r < CTX.cur_y + n; ++r) clear_row(ctx, r);
    CTX.cur_x = 0;
    CTX.full_refresh = 1;
}

/* DL : suppression de n rangees (p. 168) */
static void delete_lines(ti_context_t* ctx, unsigned char n)
{
    unsigned char r;
    if (n > (unsigned char)(TI_ROWS - CTX.cur_y)) n = (unsigned char)(TI_ROWS - CTX.cur_y);
    for (r = CTX.cur_y; r + n < TI_ROWS; ++r) {
        memcpy((ROW(ctx, r) + (0)), (ROW(ctx, r + n) + (0)), sizeof(ti_cell_t) * TI_COLS);
    }
    for (r = TI_ROWS - n; r < TI_ROWS; ++r) clear_row(ctx, r);
    CTX.cur_x = 0;
    CTX.full_refresh = 1;
}

/* DCH : suppression de n caracteres (p. 168) */
static void delete_chars(ti_context_t* ctx, unsigned char n)
{
    ti_cell_t* rowp = (ROW(ctx, CTX.cur_y) + (0));
    unsigned char rest = (unsigned char)(CTX.cols - CTX.cur_x);
    if (n > rest) n = rest;
    memmove(&rowp[CTX.cur_x], &rowp[CTX.cur_x + n], sizeof(ti_cell_t) * (rest - n));
    clear_cells(&rowp[CTX.cols - n], n);
    ti_touch(ctx, CTX.cur_y);
}

/* ICH : insertion de n positions effacees (p. 167, terminaux RTIC) */
static void insert_chars(ti_context_t* ctx, unsigned char n)
{
    ti_cell_t* rowp = (ROW(ctx, CTX.cur_y) + (0));
    unsigned char rest = (unsigned char)(CTX.cols - CTX.cur_x);
    if (n > rest) n = rest;
    memmove(&rowp[CTX.cur_x + n], &rowp[CTX.cur_x], sizeof(ti_cell_t) * (rest - n));
    clear_cells(&rowp[CTX.cur_x], n);
    ti_touch(ctx, CTX.cur_y);
}

/* CSI Ps m (p. 165) : filtre en 40 colonnes */
static void select_attr(ti_context_t* ctx, unsigned char ps)
{
    if (CTX.cols == 40) return;
    switch (ps) {
        case 0:  CTX.attr = 0; break;
        case 1:  CTX.attr |= TI_ATTR_BOLD; break;
        case 4:  CTX.attr |= TI_ATTR_UNDERLINE; break;
        case 5:  CTX.attr |= TI_ATTR_BLINK; break;
        case 7:  CTX.attr |= TI_ATTR_INVERSE; break;
        case 22: CTX.attr &= (unsigned char)~TI_ATTR_BOLD; break;
        case 24: CTX.attr &= (unsigned char)~TI_ATTR_UNDERLINE; break;
        case 25: CTX.attr &= (unsigned char)~TI_ATTR_BLINK; break;
        case 27: CTX.attr &= (unsigned char)~TI_ATTR_INVERSE; break;
        default: break;
    }
}

/* Reponse a CSI 6 n (STUM 2 par. 3.3/3.4) : CSI Pr ; Pc R, 1-based. */
static void report_cursor(const ti_context_t* ctx)
{
    unsigned char v;
    serial_send(0x1B); serial_send(0x5B);
    v = CTX.cur_y;
    if (v >= 10) serial_send((unsigned char)('0' + v / 10));
    serial_send((unsigned char)('0' + v % 10));
    serial_send(';');
    v = (CTX.cur_x < CTX.cols) ? (unsigned char)(CTX.cur_x + 1) : CTX.cols;
    if (v >= 10) serial_send((unsigned char)('0' + v / 10));
    serial_send((unsigned char)('0' + v % 10));
    serial_send('R');
    serial_tx_flush();
}

static void process_csi(ti_context_t* ctx, unsigned char byte)
{
    unsigned char p1, p2, n;

    if ((byte >= '0' && byte <= '9') || byte == ';') {
        if (CTX.csi_len < sizeof CTX.csi_buf) CTX.csi_buf[CTX.csi_len++] = byte;
        return;
    }
    if (byte == '?' || byte == '<') {       /* intermediaires STUM 2 */
        CTX.csi_priv = byte;
        return;
    }
    CTX.state = TI_STATE_NORMAL;
    p1 = csi_param(ctx, 1);
    p2 = csi_param(ctx, 2);
    n = p1 ? p1 : 1;

    if (CTX.csi_priv) {
        /* STUM 2 p. 40 : CSI 3/C 3/3 6/8 = 40 colonnes, CSI 3/F 3/3 6/C = 80
         * colonnes (ecran reinitialise, STUM 1B p. 161), CSI 3/C 3/4 6/8 = mode
         * page ; CSI 3/F 7/B = retour au standard Teletel mode Videotex
         * (STUM 1B p. 169). CSI 3/C 3/4 6/C (rouleau) : hypothese symetrique. */
        if (CTX.csi_priv == '<' && p1 == 3 && byte == 'h') {
            reset_screen(ctx, 40, 0); CTX.req_format = 1;
        } else if (CTX.csi_priv == '?' && p1 == 3 && byte == 'l') {
            reset_screen(ctx, 80, 0); CTX.req_format = 1;
        } else if (CTX.csi_priv == '<' && p1 == 4 && byte == 'h') {
            CTX.roll = 0;
        } else if (CTX.csi_priv == '<' && p1 == 4 && byte == 'l') {
            CTX.roll = 1;
        } else if (CTX.csi_priv == '<' && p1 == 1 && (byte == 'h' || byte == 'l')
                   && g_term_model == TERM_MINITEL_2) {
            /* STUM 2 par. 3.3 : CSI 3/C 3/1 6/8 extinction, 6/C allumage du
             * curseur (un Minitel 1B ne peut pas eteindre son curseur, p. 161) */
            CTX.cur_visible = (byte == 'l') ? 1 : 0;
        } else if (CTX.csi_priv == '?' && byte == '{' && CTX.csi_len == 0) {
            CTX.req_videotex = 1;
        }
        return;
    }

    switch (byte) {
        case 'A': CTX.cur_y = (CTX.cur_y > n) ? (unsigned char)(CTX.cur_y - n) : 1; break;
        case 'B': CTX.cur_y = (CTX.cur_y + n < TI_ROWS) ? (unsigned char)(CTX.cur_y + n) : TI_ROWS - 1; break;
        case 'C': CTX.cur_x = (CTX.cur_x + n < CTX.cols) ? (unsigned char)(CTX.cur_x + n) : (unsigned char)(CTX.cols - 1); break;
        case 'D': CTX.cur_x = (CTX.cur_x >= n) ? (unsigned char)(CTX.cur_x - n) : 0; break;
        case 'H':   /* CUP : rangee ; colonne, defaut (1,1) */
            CTX.cur_y = (p1 >= 1 && p1 < TI_ROWS) ? p1 : (p1 >= TI_ROWS ? TI_ROWS - 1 : 1);
            CTX.cur_x = (p2 >= 1) ? (unsigned char)((p2 <= CTX.cols) ? p2 - 1 : CTX.cols - 1) : 0;
            break;
        case 'J': erase_display(ctx, p1); break;
        case 'K': erase_line(ctx, p1); break;
        case '@': insert_chars(ctx, n); break;
        case 'L': insert_lines(ctx, n); break;
        case 'M': delete_lines(ctx, n); break;
        case 'P': delete_chars(ctx, n); break;
        case 'h': if (p1 == 4) CTX.insert = 1; break;      /* SM4 ; SM2 (clavier) sans effet ecran */
        case 'l': if (p1 == 4) CTX.insert = 0; break;
        case 'm': select_attr(ctx, p1); break;
        case 'n': if (p1 == 6 && g_term_model == TERM_MINITEL_2) report_cursor(ctx); break;
        case 'i': break;    /* MC : copie d'ecran, pas d'imprimante */
        default:  break;    /* filtre (p. 169) */
    }
}

/* ===================================================================
 *  Sequences ESC (p. 166, 168-169)
 * =================================================================== */

/* Suite de ESC 2/8 / ESC 2/9 (STUM 2 par. 3.2.2) : 4/2 americain, 5/2
 * francais, 3/0 DEC et 3/3 complementaire (ces deux derniers : Minitel 2).
 * Autre valeur : sequence ISO 2022 non definie, filtree (STUM 1B p. 169). */
static void designate_set(ti_context_t* ctx, unsigned char byte)
{
    unsigned char set = 0xFF;
    switch (byte) {
        case 0x42: set = TI_SET_US; break;
        case 0x52: set = TI_SET_FR; break;
        case 0x30: if (g_term_model == TERM_MINITEL_2) set = TI_SET_DEC; break;
        case 0x33: if (g_term_model == TERM_MINITEL_2) set = TI_SET_COMP; break;
        default: break;
    }
    if (set != 0xFF) {
        if (CTX.state == TI_STATE_ESC_G0) CTX.g0_set = set; else CTX.g1_set = set;
    }
    CTX.state = TI_STATE_NORMAL;
}

static void process_esc(ti_context_t* ctx, unsigned char byte)
{
    CTX.state = TI_STATE_NORMAL;
    switch (byte) {
        case 0x5B:  /* CSI */
            CTX.state = TI_STATE_CSI;
            CTX.csi_len = 0;
            CTX.csi_priv = 0;
            break;
        case 0x44: line_feed(ctx); break;               /* IND */
        case 0x45: new_line(ctx); break;                /* NEL */
        case 0x4D: reverse_index(ctx); break;           /* RI */
        case 0x37:  /* ESC 7 : memorise position, attributs, jeu */
            CTX.sav_valid = 1; CTX.sav_x = CTX.cur_x; CTX.sav_y = CTX.cur_y;
            CTX.sav_attr = CTX.attr; CTX.sav_shift = CTX.shift;
            break;
        case 0x38:  /* ESC 8 : restitution (sinon (1,1), sans attribut, americain) */
            if (CTX.sav_valid) {
                CTX.cur_x = CTX.sav_x; CTX.cur_y = CTX.sav_y;
                CTX.attr = CTX.sav_attr; CTX.shift = CTX.sav_shift;
            } else {
                CTX.cur_x = 0; CTX.cur_y = 1; CTX.attr = 0; CTX.shift = 0;
            }
            if (CTX.cur_x >= CTX.cols) CTX.cur_x = CTX.cols - 1;
            break;
        case 0x63:  /* ESC c : etat initial, rangee 00 comprise, 80 colonnes */
            reset_screen(ctx, 80, 1);
            CTX.req_format = 1;
            break;
        case 0x28:  /* ESC 2/8 F : designation du jeu G0 (STUM 2 par. 3.2.2) */
            CTX.state = TI_STATE_ESC_G0;
            break;
        case 0x29:  /* ESC 2/9 F : designation du jeu G1 */
            CTX.state = TI_STATE_ESC_G1;
            break;
        default:    /* ESC Fs / Fe inconnus : filtres */
            break;
    }
}

/* ===================================================================
 *  Rangee 00 (p. 169 ; mode Mixte p. 106-107) : codage Videotex reduit,
 *  sans attribut, semi-graphiques remplaces par des espaces.
 * =================================================================== */

static void row0_enter(ti_context_t* ctx, unsigned char col)
{
    if (!CTX.r0_active) {          /* nouvel acces : memoriser le contexte */
        CTX.r0_x = CTX.cur_x; CTX.r0_y = CTX.cur_y;
        CTX.r0_attr = CTX.attr; CTX.r0_shift = CTX.shift;
        CTX.r0_so = 0;
        CTX.r0_active = 1;
    }
    CTX.r0_col = (col < TI_COLS) ? col : TI_COLS - 1;
    CTX.state = TI_STATE_ROW0;
}

static void row0_leave(ti_context_t* ctx)
{
    CTX.cur_x = CTX.r0_x; CTX.cur_y = CTX.r0_y;
    CTX.attr = CTX.r0_attr; CTX.shift = CTX.r0_shift;
    CTX.r0_active = 0;
    CTX.state = TI_STATE_NORMAL;
}

static unsigned char r0_last;

static void row0_put(ti_context_t* ctx, unsigned char ch)
{
    if (CTX.r0_so) ch = ' ';                   /* semi-graphique -> espace */
    put_cell(ctx, 0, CTX.r0_col, ch, 0);
    r0_last = ch;
    if (CTX.r0_col < TI_COLS - 1) ++CTX.r0_col;
}

static void process_row0(ti_context_t* ctx, unsigned char byte)
{
    switch (CTX.state) {
        case TI_STATE_ROW0_ESC:                 /* attribut videotex avale */
            CTX.state = TI_STATE_ROW0;
            return;
        case TI_STATE_ROW0_SS2:                 /* accent avale, lettre de base affichee */
            CTX.state = TI_STATE_ROW0;
            if (byte >= 0x41 && byte <= 0x4F) return;
            if (byte >= 0x20) row0_put(ctx, byte);
            return;
        case TI_STATE_ROW0_REP: {
            unsigned char n = (byte >= 0x40) ? (unsigned char)(byte - 0x40) : 0;
            CTX.state = TI_STATE_ROW0;
            while (n-- > 0) row0_put(ctx, r0_last);
            return;
        }
        default:
            break;
    }
    if (byte >= 0x20 && byte < 0x7F) { row0_put(ctx, byte); return; }
    switch (byte) {
        case 0x0A: row0_leave(ctx); break;                              /* LF : retour */
        case 0x08: if (CTX.r0_col) --CTX.r0_col; break;               /* BS */
        case 0x09: if (CTX.r0_col < TI_COLS - 1) ++CTX.r0_col; break; /* HT */
        case 0x0D: CTX.r0_col = 0; break;                              /* CR */
        case 0x0E: CTX.r0_so = 1; break;
        case 0x0F: CTX.r0_so = 0; break;
        case 0x12: CTX.state = TI_STATE_ROW0_REP; break;
        case 0x19: CTX.state = TI_STATE_ROW0_SS2; break;
        case 0x1B: CTX.state = TI_STATE_ROW0_ESC; break;
        case 0x1F: CTX.state = TI_STATE_US; break;   /* nouvel acces (US 4/0 X/Y) */
        default: break;
    }
}

/* ===================================================================
 *  Point d'entree
 * =================================================================== */

void ti_process(ti_context_t* ctx, unsigned char byte)
{
    ti_current = ctx;
    byte &= 0x7F;

    /* Rangee 00 */
    if (CTX.state >= TI_STATE_ROW0 && CTX.state <= TI_STATE_ROW0_REP) {
        process_row0(ctx, byte);
        return;
    }
    if (CTX.state == TI_STATE_US) {
        /* US 4/0 X/Y : acces en rangee 00 (X/Y de 4/1 a 7/F, p. 169) ; toute
         * autre valeur est filtree. Depuis la rangee 00, un nouvel acces
         * garde le contexte memorise des rangees 01-24. */
        if (byte == 0x40) { CTX.state = TI_STATE_US_COL; return; }
        CTX.state = CTX.r0_active ? TI_STATE_ROW0 : TI_STATE_NORMAL;
        return;
    }
    if (CTX.state == TI_STATE_US_COL) {
        if (byte >= 0x41) row0_enter(ctx, (unsigned char)(byte - 0x41));
        else CTX.state = CTX.r0_active ? TI_STATE_ROW0 : TI_STATE_NORMAL;
        return;
    }

    /* C0 : resynchronisation (p. 170) - NUL excepte */
    if (byte < 0x20) {
        if (byte == 0x00) return;
        if (CTX.state != TI_STATE_NORMAL) {
            /* CAN / SUB annulent la commande en cours ET affichent le pave */
            CTX.state = TI_STATE_NORMAL;
        }
        switch (byte) {
            case 0x07: CTX.req_beep = 1; break;
            case 0x08: if (CTX.cur_x) --CTX.cur_x; break;
            case 0x09: {
                unsigned char nx = (unsigned char)((CTX.cur_x & ~7) + 8);
                CTX.cur_x = (nx < CTX.cols) ? nx : (unsigned char)(CTX.cols - 1);
                break;
            }
            case 0x0A: case 0x0B: case 0x0C: line_feed(ctx); break;
            case 0x0D: new_line(ctx); break;
            case 0x0E: CTX.shift = 1; break;      /* SO : G1 */
            case 0x0F: CTX.shift = 0; break;      /* SI : G0 */
            case 0x18: case 0x1A: put_error(ctx); break;
            case 0x1B: CTX.state = TI_STATE_ESC; break;
            case 0x1F: CTX.state = TI_STATE_US; break;
            default: break;     /* XON/XOFF et autres : sans effet ecran */
        }
        return;
    }

    switch (CTX.state) {
        case TI_STATE_ESC: process_esc(ctx, byte); return;
        case TI_STATE_CSI: process_csi(ctx, byte); return;
        case TI_STATE_ESC_G0:
        case TI_STATE_ESC_G1: designate_set(ctx, byte); return;
        default: break;
    }
    if (byte == 0x7F) return;                   /* DEL : non visualisable */
    put_char(ctx, byte);
}
