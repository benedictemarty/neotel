#!/bin/sh
# emu_run.sh — lance NeoTel sous Phosphoneo avec le faux modem (mode --serve)
#   usage : tests/emu_run.sh CYCLES [options phosphoneo...]
# Variables : KI (adresse keyboard_inject), ST (adresse g_dbg_state) exportées
# pour les scripts appelants via tests/lbl.sh.
set -u
cd "$(dirname "$0")/.."
PHOS=${PHOSPHONEO:-$HOME/Phosphoneo/build/phosphoneo}
MODEM_OPTS=${MODEM_OPTS:---serve}
cycles=$1; shift
rm -f build/pty.txt
python3 tools/fake_modem.py build/pty.txt $MODEM_OPTS --log build/modem.log &
mpid=$!
i=0; while [ ! -s build/pty.txt ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
NEO_CDC_TTY=$(cat build/pty.txt) timeout 600 "$PHOS" build/neotel.neo --cycles "$cycles" "$@"
rc=$?
kill $mpid 2>/dev/null
exit $rc
