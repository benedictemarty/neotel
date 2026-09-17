#!/bin/sh
# bench.sh — cout du rendu d'une page complete sur cible (Phosphoneo, cycles
# 65C02 a 6,25 MHz) : lit les marqueurs 5,37 de build/t_bench.neo.
set -u
cd "$(dirname "$0")/.."
PHOS=${PHOSPHONEO:-$HOME/Phosphoneo/build/phosphoneo}
[ -x "$PHOS" ] || { echo "SKIP: Phosphoneo absent"; exit 0; }
"$PHOS" build/t_bench.neo --cycles 20000000 --api-log build/bench.log:5 >/dev/null 2>&1
grep "fn=37" build/bench.log | awk -F'[ =]' '{print $2}' | paste - - | awk '
BEGIN { n["1"]="page vide"; n["2"]="960 lettres"; n["3"]="960 mosaiques"; n["4"]="page de test" }
{ i++; c = $2 - $1; printf "%-14s %9d cycles  %6.1f ms  (%d / rangee)\n", n[i], c, c / 6250, c / 25 }'
