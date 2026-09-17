/**
 * @file neo_time.h
 * @brief Base de temps et delais sur le Neo6502
 *
 * Le timer systeme 100 Hz de l'API (1,1) remplace le Timer 2 du VIA
 * d'OricTel : une lecture donne directement des tics de 10 ms, sans ISR.
 * Les delais courts (sondage AT de 2 ms) sont des boucles calibrees sur le
 * 65C02 a 6,25 MHz (neo_delay_ms, assembleur).
 */

#ifndef NEO_TIME_H
#define NEO_TIME_H

#ifndef __CC65__
#define __fastcall__
#endif

/** Timer 100 Hz de l'API (1,1), 32 bits. */
unsigned long neo_timer(void);

/** Arme le compteur de tics (a l'entree en session). */
void tick_reset(void);

/** Nombre de tics de 10 ms ecoules depuis l'appel precedent (sature a 255). */
unsigned char tick_10ms(void);

/** Delai actif d'environ ms millisecondes (6,25 MHz). */
void __fastcall__ neo_delay_ms(unsigned int ms);

/** Bip Videotex (BEL) : tonalite 8,7 sur le canal 0. Choix de NeoTel (la
 *  STUM ne specifie ni frequence ni duree) : 1 kHz, 100 ms, carre, volume 60 %. */
void neo_beep(void);

#endif /* NEO_TIME_H */
