#!/bin/sh
# run_emu.sh — lance NeoTel dans un emulateur avec le faux modem Hayes
#
#   tools/run_emu.sh neo|phos [options du faux modem]
#
# Le faux modem (tools/fake_modem.py) est cree sur un pty et expose au firmware
# comme modem USB CDC (NEO_CDC_TTY). Sans option, ATDT hote:port ouvre une
# VRAIE connexion TCP (serveurs Minitel d'Internet) ; avec --serve, une page de
# test locale est servie. Pour un vrai PicoWiFiModemUSB sur le PC :
#   NEO_CDC_TTY=/dev/ttyACM0 ~/Neo6502firmware/bin/neo build/neotel.bin@800 cold
set -u
cd "$(dirname "$0")/.."
PHOS=${PHOSPHONEO:-$HOME/Phosphoneo/build/phosphoneo}
NEO=${NEO_EMU:-$HOME/Neo6502firmware/bin/neo}
which=${1:-neo}; shift 2>/dev/null
rm -f build/pty.txt
python3 tools/fake_modem.py build/pty.txt "$@" &
mpid=$!
i=0; while [ ! -s build/pty.txt ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
export NEO_CDC_TTY=$(cat build/pty.txt)
if [ "$which" = phos ]; then
    "$PHOS" --sdl --scale 3 build/neotel.neo
else
    "$NEO" build/neotel.bin@800 cold
fi
kill $mpid 2>/dev/null
