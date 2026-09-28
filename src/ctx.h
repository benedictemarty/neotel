/**
 * @file ctx.h
 * @brief Contextes uniques de la cible : acces absolus au lieu de pointeurs
 *
 * Sur le Neo6502 il n'existe qu'un contexte Videotex, `vtx` (main.c), et un
 * contexte 80 colonnes, `ti`, loge a adresse fixe au debut de vtx.drcs
 * (inutilise en mode Mixte, voir main.c). cc65 traduit chaque `ctx->champ`
 * par un rechargement du pointeur puis un acces indirect indexe ; un champ
 * d'une variable globale est un simple acces absolu. Les modules ecrivent
 * donc CTX.champ : `vtx` / `VTX_TI` sur la cible, `(*ctx)` sur l'hote, ou les
 * tests instancient plusieurs contextes. Gain v0.9.6 : ~7,5 Ko de code.
 *
 * Consequence : sur la cible, tout appelant doit passer &vtx (ou &ti) ; le
 * parametre `ctx` n'y est plus lu (avertissement cc65 coupe ci-dessous).
 */

#ifndef CTX_H
#define CTX_H

#ifdef __CC65__
#include "videotex.h"
#include "teleinfo.h"
extern vtx_context_t vtx;
#define VTX_TI (*(ti_context_t*)&vtx.drcs[0][0][0])
#pragma warn (unused-param, off)
#endif

#endif /* CTX_H */
