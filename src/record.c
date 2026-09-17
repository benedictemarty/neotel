/**
 * @file record.c
 * @brief Enregistrement / relecture de flux Videotex (voir record.h)
 */

#include "record.h"
#include "neo.h"

#define NEO_G_FILE   3
#define F_OPEN       4
#define F_CLOSE      5
#define F_READ       8
#define F_WRITE      9
#define CH_REC       1          /* canal d'enregistrement */
#define CH_REPLAY    2          /* canal de relecture */
#define MODE_CREATE  3          /* creer / tronquer, lecture-ecriture */
#define MODE_READ    0

static unsigned char s_active;
static unsigned char s_buf[RECORD_CHUNK];
static unsigned char s_len;      /* octets dans le tampon */
static unsigned char s_pos;      /* relecture : prochain octet a rendre */
static unsigned char s_eof;      /* relecture : fin de fichier atteinte */
static unsigned char s_name[RECORD_NAME_MAX + 1];   /* prefixee par sa longueur */

static void set_name(const char* name)
{
    unsigned char n = 0;
    while (name[n] && n < RECORD_NAME_MAX) { s_name[n + 1] = (unsigned char)name[n]; ++n; }
    s_name[0] = n;
}

static unsigned char file_open(unsigned char ch, unsigned char mode)
{
    neo_wait();
    NEO_P[0] = ch;
    NEO_SET_ADDR(s_name);
    NEO_P[3] = mode;
    neo_call(NEO_G_FILE, F_OPEN);
    return NEO_ERR ? 0 : 1;
}

static void file_close(unsigned char ch)
{
    neo_wait();
    NEO_P[0] = ch;
    neo_call(NEO_G_FILE, F_CLOSE);
}

static void flush(void)
{
    if (!s_len) return;
    neo_wait();
    NEO_P[0] = CH_REC;
    NEO_SET_ADDR(s_buf);
    NEO_P[3] = s_len;
    NEO_P[4] = 0;
    neo_call(NEO_G_FILE, F_WRITE);
    s_len = 0;
}

void record_make_name(char* name, unsigned char index)
{
    unsigned char tens = 0;
    if (index == 0 || index > 99) index = 1;
    while (index >= 10) { index -= 10; ++tens; }
    name[0] = 'n'; name[1] = 'e'; name[2] = 'o';
    name[3] = (char)('0' + tens);
    name[4] = (char)('0' + index);
    name[5] = '.'; name[6] = 'v'; name[7] = 'd'; name[8] = 't'; name[9] = 0;
}

unsigned char record_start(const char* name)
{
    if (s_active) record_stop();
    set_name(name);
    s_len = 0;
    s_active = file_open(CH_REC, MODE_CREATE);
    return s_active;
}

void record_byte(unsigned char b)
{
    if (!s_active) return;
    s_buf[s_len++] = b;
    if (s_len >= RECORD_CHUNK) flush();
}

void record_stop(void)
{
    if (!s_active) return;
    flush();
    file_close(CH_REC);
    s_active = 0;
}

unsigned char record_active(void)
{
    return s_active;
}

unsigned char replay_open(const char* name)
{
    if (s_active) record_stop();
    set_name(name);
    s_len = 0;
    s_pos = 0;
    s_eof = 0;
    return file_open(CH_REPLAY, MODE_READ);
}

/* Recharge le tampon si vide ; 0 en fin de fichier (ou erreur), sans
 * relire le fichier ensuite. */
unsigned char replay_pending(void)
{
    if (s_pos < s_len) return 1;
    if (s_eof) return 0;
    neo_wait();
    NEO_P[0] = CH_REPLAY;
    NEO_SET_ADDR(s_buf);
    NEO_P[3] = RECORD_CHUNK;
    NEO_P[4] = 0;
    neo_call(NEO_G_FILE, F_READ);
    s_pos = 0;
    s_len = NEO_ERR ? 0 : NEO_P[3];    /* EOF = erreur, 0 octet */
    if (!s_len) s_eof = 1;
    return s_len ? 1 : 0;
}

unsigned char replay_next(void)
{
    return s_buf[s_pos++];
}

void replay_close(void)
{
    s_len = 0;
    file_close(CH_REPLAY);
}
