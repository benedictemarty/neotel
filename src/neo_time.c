/**
 * @file neo_time.c
 * @brief Base de temps 100 Hz du Neo6502 (voir neo_time.h)
 */

#include "neo_time.h"
#include "neo.h"

static unsigned long s_prev;

unsigned long neo_timer(void)
{
    neo_wait();
    neo_call(NEO_G_SYSTEM, NEO_F_TIMER);
    return (unsigned long)NEO_P[0]
         | ((unsigned long)NEO_P[1] << 8)
         | ((unsigned long)NEO_P[2] << 16)
         | ((unsigned long)NEO_P[3] << 24);
}

void tick_reset(void)
{
    s_prev = neo_timer();
}

unsigned char tick_10ms(void)
{
    unsigned long now = neo_timer();
    unsigned long d = now - s_prev;
    if (d == 0) return 0;
    if (d > 255) d = 255;
    s_prev = now;
    return (unsigned char)d;
}

void neo_beep(void)
{
    neo_wait();
    neo_call(NEO_G_SOUND, NEO_F_SND_BEEP);
}
