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

ti_context_t* ti_current;

/* Adressage des rangees sans multiplication par 160 (cc65 emet sinon une
 * multiplication 16 bits a chaque acces, tres couteuse en taille de code). */
static const unsigned int ti_row_off[TI_ROWS] = {
    0, 160, 320, 480, 640, 800, 960, 1120, 1280, 1440, 1600, 1760, 1920,
    2080, 2240, 2400, 2560, 2720, 2880, 3040, 3200, 3360, 3520, 3680, 3840
};
#define ROW(ctx, r) ((ti_cell_t*)((unsigned char*)(ctx)->screen + ti_row_off[(r)]))

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
    if (row < TI_ROWS) ctx->dirty[row] = 1;
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
    ctx->cols = cols;
    ctx->cur_x = 0;
    ctx->cur_y = 1;
    ctx->attr = 0;
    ctx->shift = 0;
    ctx->g0_set = TI_SET_US;
    ctx->g1_set = TI_SET_FR;
    ctx->roll = 1;
    ctx->insert = 0;
    ctx->cur_visible = 1;
    ctx->state = TI_STATE_NORMAL;
    ctx->csi_len = 0;
    ctx->csi_priv = 0;
    ctx->full_refresh = 1;
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
    ctx->full_refresh = 1;
}

static void scroll_down(ti_context_t* ctx)
{
    memmove((ROW(ctx, 2) + (0)), (ROW(ctx, 1) + (0)),
            sizeof(ti_cell_t) * TI_COLS * (TI_ROWS - 2));
    clear_row(ctx, 1);
    ctx->full_refresh = 1;
}

/* LF / VT / FF / IND (p. 166) : rangee 24 -> rangee 1 en mode page, rouleau
 * sinon. Colonne inchangee. */
static void line_feed(ti_context_t* ctx)
{
    if (ctx->cur_y < TI_ROWS - 1) {
        ++ctx->cur_y;
    } else if (ctx->roll) {
        scroll_up(ctx);
    } else {
        ctx->cur_y = 1;
    }
}

/* RI (p. 166) : rangee 1 -> rangee 24 en mode page, rouleau descendant sinon. */
static void reverse_index(ti_context_t* ctx)
{
    if (ctx->cur_y > 1) {
        --ctx->cur_y;
    } else if (ctx->roll) {
        scroll_down(ctx);
    } else {
        ctx->cur_y = TI_ROWS - 1;
    }
}

/* CR (p. 166) : "colonne 1 de la rangee suivante" ; NEL idem. */
static void new_line(ti_context_t* ctx)
{
    ctx->cur_x = 0;
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
    unsigned char attr = ctx->attr | TI_SET_ATTR(ctx->shift ? ctx->g1_set : ctx->g0_set);

    /* Auto-wrap differe (ISO 6429) : apres la 80e ecriture, cur_x vaut cols
     * (etat "en attente") ; c'est le caractere suivant qui passe a la ligne,
     * pas la 80e ecriture. Evite une rangee sautee quand le serveur envoie
     * un CR apres avoir rempli la 80e colonne. La STUM ne decrit pas ce cas
     * (comportement ISO 6429, coherent avec les CSI de deplacement qui
     * s'arretent au bord droit, p. 168). Tout deplacement explicite du
     * curseur le ramene a une valeur < cols et annule l'attente.  */
    if (ctx->cur_x >= ctx->cols) new_line(ctx);
    rowp = (ROW(ctx, ctx->cur_y) + (0));
    if (ctx->insert) {
        /* SM4 (p. 167) : decalage a droite, limite a la rangee, le dernier
         * caractere est perdu */
        memmove(&rowp[ctx->cur_x + 1], &rowp[ctx->cur_x],
                sizeof(ti_cell_t) * (ctx->cols - 1 - ctx->cur_x));
    }
    put_cell(ctx, ctx->cur_y, ctx->cur_x, ch, attr);
    ++ctx->cur_x;              /* peut atteindre cols : passage a la ligne differe */
}

/* Symbole d'erreur : pave plein avec les attributs courants (p. 169-170). */
static void put_error(ti_context_t* ctx)
{
    ctx->attr |= TI_ATTR_ERROR;
    put_char(ctx, 0x7F);
    ctx->attr &= (unsigned char)~TI_ATTR_ERROR;
}

/* ===================================================================
 *  Sequences de commande CSI (p. 167-169, STUM 2 p. 40)
 * =================================================================== */

/* Parametre numerique n (1-based dans csi_buf), 0 = absent. */
static unsigned char csi_param(const ti_context_t* ctx, unsigned char n)
{
    unsigned char i, cur = 1;
    unsigned int v = 0;
    for (i = 0; i < ctx->csi_len; ++i) {
        if (ctx->csi_buf[i] == ';') { ++cur; continue; }
        if (cur == n) {
            v = v * 10 + (ctx->csi_buf[i] - '0');
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
            clear_cells((ROW(ctx, ctx->cur_y) + (ctx->cur_x)), (unsigned int)ctx->cols - ctx->cur_x);
            ti_touch(ctx, ctx->cur_y);
            for (r = ctx->cur_y + 1; r < TI_ROWS; ++r) clear_row(ctx, r);
            break;
        case 1:     /* du debut de la page au curseur (inclus) */
            for (r = 1; r < ctx->cur_y; ++r) clear_row(ctx, r);
            clear_cells((ROW(ctx, ctx->cur_y) + (0)), (unsigned int)ctx->cur_x + 1);
            ti_touch(ctx, ctx->cur_y);
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
            clear_cells((ROW(ctx, ctx->cur_y) + (ctx->cur_x)), (unsigned int)ctx->cols - ctx->cur_x);
            break;
        case 1:
            clear_cells((ROW(ctx, ctx->cur_y) + (0)), (unsigned int)ctx->cur_x + 1);
            break;
        case 2:
            clear_cells((ROW(ctx, ctx->cur_y) + (0)), ctx->cols);
            break;
        default:
            return;
    }
    ti_touch(ctx, ctx->cur_y);
}

/* IL : insertion de n rangees a la rangee active (p. 167) */
static void insert_lines(ti_context_t* ctx, unsigned char n)
{
    unsigned char r;
    if (n > (unsigned char)(TI_ROWS - ctx->cur_y)) n = (unsigned char)(TI_ROWS - ctx->cur_y);
    for (r = TI_ROWS - 1; r >= ctx->cur_y + n; --r) {
        memcpy((ROW(ctx, r) + (0)), (ROW(ctx, r - n) + (0)), sizeof(ti_cell_t) * TI_COLS);
    }
    for (r = ctx->cur_y; r < ctx->cur_y + n; ++r) clear_row(ctx, r);
    ctx->cur_x = 0;
    ctx->full_refresh = 1;
}

/* DL : suppression de n rangees (p. 168) */
static void delete_lines(ti_context_t* ctx, unsigned char n)
{
    unsigned char r;
    if (n > (unsigned char)(TI_ROWS - ctx->cur_y)) n = (unsigned char)(TI_ROWS - ctx->cur_y);
    for (r = ctx->cur_y; r + n < TI_ROWS; ++r) {
        memcpy((ROW(ctx, r) + (0)), (ROW(ctx, r + n) + (0)), sizeof(ti_cell_t) * TI_COLS);
    }
    for (r = TI_ROWS - n; r < TI_ROWS; ++r) clear_row(ctx, r);
    ctx->cur_x = 0;
    ctx->full_refresh = 1;
}

/* DCH : suppression de n caracteres (p. 168) */
static void delete_chars(ti_context_t* ctx, unsigned char n)
{
    ti_cell_t* rowp = (ROW(ctx, ctx->cur_y) + (0));
    unsigned char rest = (unsigned char)(ctx->cols - ctx->cur_x);
    if (n > rest) n = rest;
    memmove(&rowp[ctx->cur_x], &rowp[ctx->cur_x + n], sizeof(ti_cell_t) * (rest - n));
    clear_cells(&rowp[ctx->cols - n], n);
    ti_touch(ctx, ctx->cur_y);
}

/* ICH : insertion de n positions effacees (p. 167, terminaux RTIC) */
static void insert_chars(ti_context_t* ctx, unsigned char n)
{
    ti_cell_t* rowp = (ROW(ctx, ctx->cur_y) + (0));
    unsigned char rest = (unsigned char)(ctx->cols - ctx->cur_x);
    if (n > rest) n = rest;
    memmove(&rowp[ctx->cur_x + n], &rowp[ctx->cur_x], sizeof(ti_cell_t) * (rest - n));
    clear_cells(&rowp[ctx->cur_x], n);
    ti_touch(ctx, ctx->cur_y);
}

/* CSI Ps m (p. 165) : filtre en 40 colonnes */
static void select_attr(ti_context_t* ctx, unsigned char ps)
{
    if (ctx->cols == 40) return;
    switch (ps) {
        case 0:  ctx->attr = 0; break;
        case 1:  ctx->attr |= TI_ATTR_BOLD; break;
        case 4:  ctx->attr |= TI_ATTR_UNDERLINE; break;
        case 5:  ctx->attr |= TI_ATTR_BLINK; break;
        case 7:  ctx->attr |= TI_ATTR_INVERSE; break;
        case 22: ctx->attr &= (unsigned char)~TI_ATTR_BOLD; break;
        case 24: ctx->attr &= (unsigned char)~TI_ATTR_UNDERLINE; break;
        case 25: ctx->attr &= (unsigned char)~TI_ATTR_BLINK; break;
        case 27: ctx->attr &= (unsigned char)~TI_ATTR_INVERSE; break;
        default: break;
    }
}

/* Reponse a CSI 6 n (STUM 2 par. 3.3/3.4) : CSI Pr ; Pc R, 1-based. */
static void report_cursor(const ti_context_t* ctx)
{
    unsigned char v;
    serial_send(0x1B); serial_send(0x5B);
    v = ctx->cur_y;
    if (v >= 10) serial_send((unsigned char)('0' + v / 10));
    serial_send((unsigned char)('0' + v % 10));
    serial_send(';');
    v = (ctx->cur_x < ctx->cols) ? (unsigned char)(ctx->cur_x + 1) : ctx->cols;
    if (v >= 10) serial_send((unsigned char)('0' + v / 10));
    serial_send((unsigned char)('0' + v % 10));
    serial_send('R');
    serial_tx_flush();
}

static void process_csi(ti_context_t* ctx, unsigned char byte)
{
    unsigned char p1, p2, n;

    if ((byte >= '0' && byte <= '9') || byte == ';') {
        if (ctx->csi_len < sizeof ctx->csi_buf) ctx->csi_buf[ctx->csi_len++] = byte;
        return;
    }
    if (byte == '?' || byte == '<') {       /* intermediaires STUM 2 */
        ctx->csi_priv = byte;
        return;
    }
    ctx->state = TI_STATE_NORMAL;
    p1 = csi_param(ctx, 1);
    p2 = csi_param(ctx, 2);
    n = p1 ? p1 : 1;

    if (ctx->csi_priv) {
        /* STUM 2 p. 40 : CSI 3/C 3/3 6/8 = 40 colonnes, CSI 3/F 3/3 6/C = 80
         * colonnes (ecran reinitialise, STUM 1B p. 161), CSI 3/C 3/4 6/8 = mode
         * page ; CSI 3/F 7/B = retour au standard Teletel mode Videotex
         * (STUM 1B p. 169). CSI 3/C 3/4 6/C (rouleau) : hypothese symetrique. */
        if (ctx->csi_priv == '<' && p1 == 3 && byte == 'h') {
            reset_screen(ctx, 40, 0); ctx->req_format = 1;
        } else if (ctx->csi_priv == '?' && p1 == 3 && byte == 'l') {
            reset_screen(ctx, 80, 0); ctx->req_format = 1;
        } else if (ctx->csi_priv == '<' && p1 == 4 && byte == 'h') {
            ctx->roll = 0;
        } else if (ctx->csi_priv == '<' && p1 == 4 && byte == 'l') {
            ctx->roll = 1;
        } else if (ctx->csi_priv == '<' && p1 == 1 && (byte == 'h' || byte == 'l')
                   && g_term_model == TERM_MINITEL_2) {
            /* STUM 2 par. 3.3 : CSI 3/C 3/1 6/8 extinction, 6/C allumage du
             * curseur (un Minitel 1B ne peut pas eteindre son curseur, p. 161) */
            ctx->cur_visible = (byte == 'l') ? 1 : 0;
        } else if (ctx->csi_priv == '?' && byte == '{' && ctx->csi_len == 0) {
            ctx->req_videotex = 1;
        }
        return;
    }

    switch (byte) {
        case 'A': ctx->cur_y = (ctx->cur_y > n) ? (unsigned char)(ctx->cur_y - n) : 1; break;
        case 'B': ctx->cur_y = (ctx->cur_y + n < TI_ROWS) ? (unsigned char)(ctx->cur_y + n) : TI_ROWS - 1; break;
        case 'C': ctx->cur_x = (ctx->cur_x + n < ctx->cols) ? (unsigned char)(ctx->cur_x + n) : (unsigned char)(ctx->cols - 1); break;
        case 'D': ctx->cur_x = (ctx->cur_x >= n) ? (unsigned char)(ctx->cur_x - n) : 0; break;
        case 'H':   /* CUP : rangee ; colonne, defaut (1,1) */
            ctx->cur_y = (p1 >= 1 && p1 < TI_ROWS) ? p1 : (p1 >= TI_ROWS ? TI_ROWS - 1 : 1);
            ctx->cur_x = (p2 >= 1) ? (unsigned char)((p2 <= ctx->cols) ? p2 - 1 : ctx->cols - 1) : 0;
            break;
        case 'J': erase_display(ctx, p1); break;
        case 'K': erase_line(ctx, p1); break;
        case '@': insert_chars(ctx, n); break;
        case 'L': insert_lines(ctx, n); break;
        case 'M': delete_lines(ctx, n); break;
        case 'P': delete_chars(ctx, n); break;
        case 'h': if (p1 == 4) ctx->insert = 1; break;      /* SM4 ; SM2 (clavier) sans effet ecran */
        case 'l': if (p1 == 4) ctx->insert = 0; break;
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
        if (ctx->state == TI_STATE_ESC_G0) ctx->g0_set = set; else ctx->g1_set = set;
    }
    ctx->state = TI_STATE_NORMAL;
}

static void process_esc(ti_context_t* ctx, unsigned char byte)
{
    ctx->state = TI_STATE_NORMAL;
    switch (byte) {
        case 0x5B:  /* CSI */
            ctx->state = TI_STATE_CSI;
            ctx->csi_len = 0;
            ctx->csi_priv = 0;
            break;
        case 0x44: line_feed(ctx); break;               /* IND */
        case 0x45: new_line(ctx); break;                /* NEL */
        case 0x4D: reverse_index(ctx); break;           /* RI */
        case 0x37:  /* ESC 7 : memorise position, attributs, jeu */
            ctx->sav_valid = 1; ctx->sav_x = ctx->cur_x; ctx->sav_y = ctx->cur_y;
            ctx->sav_attr = ctx->attr; ctx->sav_shift = ctx->shift;
            break;
        case 0x38:  /* ESC 8 : restitution (sinon (1,1), sans attribut, americain) */
            if (ctx->sav_valid) {
                ctx->cur_x = ctx->sav_x; ctx->cur_y = ctx->sav_y;
                ctx->attr = ctx->sav_attr; ctx->shift = ctx->sav_shift;
            } else {
                ctx->cur_x = 0; ctx->cur_y = 1; ctx->attr = 0; ctx->shift = 0;
            }
            if (ctx->cur_x >= ctx->cols) ctx->cur_x = ctx->cols - 1;
            break;
        case 0x63:  /* ESC c : etat initial, rangee 00 comprise, 80 colonnes */
            reset_screen(ctx, 80, 1);
            ctx->req_format = 1;
            break;
        case 0x28:  /* ESC 2/8 F : designation du jeu G0 (STUM 2 par. 3.2.2) */
            ctx->state = TI_STATE_ESC_G0;
            break;
        case 0x29:  /* ESC 2/9 F : designation du jeu G1 */
            ctx->state = TI_STATE_ESC_G1;
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
    if (!ctx->r0_active) {          /* nouvel acces : memoriser le contexte */
        ctx->r0_x = ctx->cur_x; ctx->r0_y = ctx->cur_y;
        ctx->r0_attr = ctx->attr; ctx->r0_shift = ctx->shift;
        ctx->r0_so = 0;
        ctx->r0_active = 1;
    }
    ctx->r0_col = (col < TI_COLS) ? col : TI_COLS - 1;
    ctx->state = TI_STATE_ROW0;
}

static void row0_leave(ti_context_t* ctx)
{
    ctx->cur_x = ctx->r0_x; ctx->cur_y = ctx->r0_y;
    ctx->attr = ctx->r0_attr; ctx->shift = ctx->r0_shift;
    ctx->r0_active = 0;
    ctx->state = TI_STATE_NORMAL;
}

static unsigned char r0_last;

static void row0_put(ti_context_t* ctx, unsigned char ch)
{
    if (ctx->r0_so) ch = ' ';                   /* semi-graphique -> espace */
    put_cell(ctx, 0, ctx->r0_col, ch, 0);
    r0_last = ch;
    if (ctx->r0_col < TI_COLS - 1) ++ctx->r0_col;
}

static void process_row0(ti_context_t* ctx, unsigned char byte)
{
    switch (ctx->state) {
        case TI_STATE_ROW0_ESC:                 /* attribut videotex avale */
            ctx->state = TI_STATE_ROW0;
            return;
        case TI_STATE_ROW0_SS2:                 /* accent avale, lettre de base affichee */
            ctx->state = TI_STATE_ROW0;
            if (byte >= 0x41 && byte <= 0x4F) return;
            if (byte >= 0x20) row0_put(ctx, byte);
            return;
        case TI_STATE_ROW0_REP: {
            unsigned char n = (byte >= 0x40) ? (unsigned char)(byte - 0x40) : 0;
            ctx->state = TI_STATE_ROW0;
            while (n-- > 0) row0_put(ctx, r0_last);
            return;
        }
        default:
            break;
    }
    if (byte >= 0x20 && byte < 0x7F) { row0_put(ctx, byte); return; }
    switch (byte) {
        case 0x0A: row0_leave(ctx); break;                              /* LF : retour */
        case 0x08: if (ctx->r0_col) --ctx->r0_col; break;               /* BS */
        case 0x09: if (ctx->r0_col < TI_COLS - 1) ++ctx->r0_col; break; /* HT */
        case 0x0D: ctx->r0_col = 0; break;                              /* CR */
        case 0x0E: ctx->r0_so = 1; break;
        case 0x0F: ctx->r0_so = 0; break;
        case 0x12: ctx->state = TI_STATE_ROW0_REP; break;
        case 0x19: ctx->state = TI_STATE_ROW0_SS2; break;
        case 0x1B: ctx->state = TI_STATE_ROW0_ESC; break;
        case 0x1F: ctx->state = TI_STATE_US; break;   /* nouvel acces (US 4/0 X/Y) */
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
    if (ctx->state >= TI_STATE_ROW0 && ctx->state <= TI_STATE_ROW0_REP) {
        process_row0(ctx, byte);
        return;
    }
    if (ctx->state == TI_STATE_US) {
        /* US 4/0 X/Y : acces en rangee 00 (X/Y de 4/1 a 7/F, p. 169) ; toute
         * autre valeur est filtree. Depuis la rangee 00, un nouvel acces
         * garde le contexte memorise des rangees 01-24. */
        if (byte == 0x40) { ctx->state = TI_STATE_US_COL; return; }
        ctx->state = ctx->r0_active ? TI_STATE_ROW0 : TI_STATE_NORMAL;
        return;
    }
    if (ctx->state == TI_STATE_US_COL) {
        if (byte >= 0x41) row0_enter(ctx, (unsigned char)(byte - 0x41));
        else ctx->state = ctx->r0_active ? TI_STATE_ROW0 : TI_STATE_NORMAL;
        return;
    }

    /* C0 : resynchronisation (p. 170) - NUL excepte */
    if (byte < 0x20) {
        if (byte == 0x00) return;
        if (ctx->state != TI_STATE_NORMAL) {
            /* CAN / SUB annulent la commande en cours ET affichent le pave */
            ctx->state = TI_STATE_NORMAL;
        }
        switch (byte) {
            case 0x07: ctx->req_beep = 1; break;
            case 0x08: if (ctx->cur_x) --ctx->cur_x; break;
            case 0x09: {
                unsigned char nx = (unsigned char)((ctx->cur_x & ~7) + 8);
                ctx->cur_x = (nx < ctx->cols) ? nx : (unsigned char)(ctx->cols - 1);
                break;
            }
            case 0x0A: case 0x0B: case 0x0C: line_feed(ctx); break;
            case 0x0D: new_line(ctx); break;
            case 0x0E: ctx->shift = 1; break;      /* SO : G1 */
            case 0x0F: ctx->shift = 0; break;      /* SI : G0 */
            case 0x18: case 0x1A: put_error(ctx); break;
            case 0x1B: ctx->state = TI_STATE_ESC; break;
            case 0x1F: ctx->state = TI_STATE_US; break;
            default: break;     /* XON/XOFF et autres : sans effet ecran */
        }
        return;
    }

    switch (ctx->state) {
        case TI_STATE_ESC: process_esc(ctx, byte); return;
        case TI_STATE_CSI: process_csi(ctx, byte); return;
        case TI_STATE_ESC_G0:
        case TI_STATE_ESC_G1: designate_set(ctx, byte); return;
        default: break;
    }
    if (byte == 0x7F) return;                   /* DEL : non visualisable */
    put_char(ctx, byte);
}
