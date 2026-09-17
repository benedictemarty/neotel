/**
 * @file help.h
 * @brief Ecran d'aide bilingue (francais / anglais), inspire de Neo6502Poker.
 *
 * Plusieurs pages affichees dans l'ecran Videotex 40 colonnes ; SUITE/Espace
 * page suivante, RETOUR/P precedente, la touche "L" bascule la langue
 * (memorisee dans les reglages), ESC / SOMMAIRE revient a l'appelant (qui
 * redessine son ecran).
 */
#ifndef HELP_H
#define HELP_H
#include "videotex.h"
void help_show(vtx_context_t* ctx);
#endif
