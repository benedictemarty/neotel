#!/bin/sh
# run.sh — tests cible (headless) de NeoTel
#
#   tests/run.sh check   joue les scenarios sous Phosphoneo avec le faux modem
#                        (tools/fake_modem.py sur pty, expose en modem USB CDC)
#                        et verifie : parcours des menus jusqu'a la session,
#                        page decodee en RAM, capture de la page IDENTIQUE au
#                        rendu C de l'hote (oracle : display_asm.s == display.c),
#                        ESC ESC -> raccrochage (+++ / ATH) et retour au menu,
#                        NO CARRIER -> ecran de perte de porteuse, ESC au menu
#                        -> retour a NeoBASIC, puis fumee dans l'emulateur neo.
#   tests/run.sh ref     regenere les captures de reference (tests/ref/*.ppm)
#
# Les touches sont injectees par --poke-at sur keyboard_inject (adresse lue
# dans build/neotel.lbl), les etats par --dump-ram-when sur g_dbg_state.
set -u
cd "$(dirname "$0")/.."
PHOS=${PHOSPHONEO:-$HOME/Phosphoneo/build/phosphoneo}
NEO=${NEO_EMU:-$HOME/Neo6502firmware/bin/neo}
OUT=tests/out; REF=tests/ref
mkdir -p "$OUT" "$REF"
mode=${1:-check}
fail=0

[ -x "$PHOS" ] || { echo "SKIP: Phosphoneo absent ($PHOS)"; exit 0; }
[ -f build/neotel.neo ] || { echo "FAIL: build/neotel.neo absent (make)"; exit 1; }
[ -x tests/host/render_page ] || make -C tests/host render_page >/dev/null

sym() { grep " \._$1\$" build/neotel.lbl | awk '{print $2}' | sed 's/^00//'; }
KI=$(sym keyboard_inject); ST=$(sym g_dbg_state); VTX=$(sym vtx); NB=$(sym g_vtx_bytes); HU=$(sym g_dbg_hangups)
[ -n "$KI" ] && [ -n "$ST" ] && [ -n "$VTX" ] && [ -n "$NB" ] && [ -n "$HU" ] || { echo "FAIL: symboles absents de build/neotel.lbl"; exit 1; }
SCREEN_OFF=$(tests/host/render_page --screen-offset)
PAGE_LEN=$(printf '%X' $(wc -c < tests/page_test.vdt))    # octets de la page (hexa, < 256)

# Etats de g_dbg_state (main.c)
ST_MENU=3; ST_SESSION=6; ST_CARRIER=8; ST_EXIT=10; ST_HUNGUP=12

# Touches : splash (espace), interface (espace), menu '1', serveur '1'
KEYS="--poke-at 9000000:$KI=20 --poke-at 12000000:$KI=20 --poke-at 15000000:$KI=31 --poke-at 18000000:$KI=31"

# Lance Phosphoneo avec le faux modem ; $1 = options du modem, reste = phosphoneo
run() {
    mopts=$1; shift
    rm -f build/pty.txt build/modem.log
    python3 tools/fake_modem.py build/pty.txt $mopts --log build/modem.log &
    mpid=$!
    i=0; while [ ! -s build/pty.txt ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
    NEO_CDC_TTY=$(cat build/pty.txt) timeout 900 "$PHOS" build/neotel.neo "$@" >"$OUT/phos.log" 2>&1
    rc=$?
    kill $mpid 2>/dev/null; wait $mpid 2>/dev/null
    return $rc
}

# Texte present dans le buffer ecran Videotex d'un dump RAM ?
page_has() {   # $1 = dump, $2 = texte
    python3 - "$1" "$2" "$VTX" "$SCREEN_OFF" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read(); text = sys.argv[2]
base = int(sys.argv[3], 16) + int(sys.argv[4])
rows = [''.join(chr(d[base + (r * 40 + c) * 6]) for c in range(40)) for r in range(25)]
sys.exit(0 if any(text in row for row in rows) else 1)
EOF
}

# --- 1. menus -> session, page decodee en RAM ------------------------------
run "--serve --page tests/page_test.vdt" --cycles 60000000 $KEYS \
    --dump-ram-when "$NB:$PAGE_LEN:$OUT/session.bin"
if [ -f "$OUT/session.bin" ] && page_has "$OUT/session.bin" "PAGE DE TEST NEOTEL"; then
    echo "PASS session (menus -> ATZ/ATDT -> page decodee)"
else
    echo "FAIL session (voir $OUT/phos.log, build/modem.log)"; fail=1
fi
grep -q "commande b'ATDTpavi.3617.fr:3617'" build/modem.log && echo "PASS ATDT (serveur 1 compose)" || { echo "FAIL ATDT"; fail=1; }

# --- 2. capture de la page == rendu hote (asm == C) ------------------------
tests/host/render_page tests/page_test.vdt "$OUT/gold0.ppm" "$OUT/gold1.ppm"
run "--serve --page tests/page_test.vdt" --cycles 40000000 $KEYS \
    --screenshot-at "39000000:$OUT/page.ppm"
python3 - "$OUT/page.ppm" "$OUT/gold0.ppm" "$OUT/gold1.ppm" <<'EOF'
import sys
def load(p):
    d = open(p, 'rb').read()
    parts = d.split(b'\n', 3)
    return parts[3]
cap = load(sys.argv[1]); g0 = load(sys.argv[2]); g1 = load(sys.argv[3])
n = 320 * 225 * 3          # zone page seulement (le statut porte l'horloge)
if cap[:n] == g0[:n] or cap[:n] == g1[:n]:
    sys.exit(0)
diff = sum(1 for i in range(0, n, 3) if cap[i:i+3] != g0[i:i+3])
print("pixels differents (phase 0) :", diff)
sys.exit(1)
EOF
if [ $? -eq 0 ]; then echo "PASS page (capture cible == oracle hote, asm == C)"
else echo "FAIL page (capture != oracle, voir $OUT/page.ppm et $OUT/gold0.ppm)"; fail=1; fi
if [ "$mode" = ref ]; then cp "$OUT/page.ppm" "$REF/page.ppm"; echo "REF  page"
elif [ -f "$REF/page.ppm" ] && cmp -s "$OUT/page.ppm" "$REF/page.ppm"; then echo "PASS page-ref (identique a tests/ref/page.ppm)"
elif [ -f "$REF/page.ppm" ]; then echo "FAIL page-ref (capture != tests/ref/page.ppm)"; fail=1; fi

# --- 3. ESC ESC en session -> raccrochage, retour au menu ------------------
# NB : Phosphoneo va plus vite que le temps reel ; la garde de silence Hayes
# du faux modem est reduite (--guard 0.02) pour qu'il voie le "+++".
run "--serve --page tests/page_test.vdt --guard 0.02" --cycles 120000000 $KEYS \
    --poke-at "40000000:$KI=1B" --poke-at "46000000:$KI=1B" \
    --dump-ram-when "$HU:1:$OUT/hungup.bin"
if [ -f "$OUT/hungup.bin" ] && grep -q "+++" build/modem.log && grep -q "commande b'ATH'" build/modem.log; then
    echo "PASS escape (ESC ESC : +++ puis ATH emis, retour au menu)"
else
    echo "FAIL escape (pas de +++/ATH dans build/modem.log, ou etat non atteint)"; fail=1
fi

# --- 4. perte de porteuse -> ecran dedie -----------------------------------
run "--serve --page tests/page_test.vdt --nc 1" --cycles 250000000 $KEYS \
    --dump-ram-when "$ST:$ST_CARRIER:$OUT/carrier.bin"
if [ -f "$OUT/carrier.bin" ] && page_has "$OUT/carrier.bin" "PERTE DE PORTEUSE"; then
    echo "PASS carrier (NO CARRIER confirme par le silence -> ecran)"
else
    echo "FAIL carrier"; fail=1
fi

# --- 5. ESC au menu -> sortie vers NeoBASIC --------------------------------
run "--serve" --cycles 80000000 --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" \
    --poke-at "15000000:$KI=1B" --type-keys '40000000:PRINT 6*7\n' \
    --screenshot-text "$OUT/exit.txt"
if grep -q "^42" "$OUT/exit.txt" 2>/dev/null; then echo "PASS exit (NeoBASIC repond apres ESC : 6*7 = 42)"
else echo "FAIL exit (NeoBASIC ne repond pas, voir $OUT/exit.txt)"; fail=1; fi

# --- 6. fumee dans l'emulateur officiel neo --------------------------------
if [ -x "$NEO" ]; then
    rm -f build/pty.txt
    python3 tools/fake_modem.py build/pty.txt --serve &
    mpid=$!; sleep 1
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy NEO_CDC_TTY=$(cat build/pty.txt) timeout 120 "$NEO" \
        build/neotel.bin@800 cold cycles:20000000 "shot:19000000:$OUT/neo.ppm" >"$OUT/neo.log" 2>&1
    kill $mpid 2>/dev/null
    n=$(python3 -c "
d=open('$OUT/neo.ppm','rb').read(); px=d.split(b'\n',3)[3]
print(sum(1 for i in range(0,len(px),3) if px[i] or px[i+1] or px[i+2]))" 2>/dev/null || echo 0)
    if [ "$n" -gt 500 ]; then echo "PASS neo (splash affiche, pixels allumes : $n)"
    else echo "FAIL neo (pixels allumes : $n)"; fail=1; fi
else
    echo "SKIP neo absent"
fi

[ $fail -eq 0 ] && echo "run.sh : OK" || echo "run.sh : ECHEC"
exit $fail
