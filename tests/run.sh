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
make -C tests/host render_page >/dev/null 2>&1 || { echo "FAIL: render_page (oracle hote) ne compile pas"; exit 1; }

sym() { grep " \._$1\$" build/neotel.lbl | awk '{print $2}' | sed 's/^00//'; }
KI=$(sym keyboard_inject); ST=$(sym g_dbg_state); VTX=$(sym vtx); NB=$(sym g_vtx_bytes); HU=$(sym g_dbg_hangups); TM=$(sym g_term_model)
[ -n "$KI" ] && [ -n "$ST" ] && [ -n "$VTX" ] && [ -n "$NB" ] && [ -n "$HU" ] || { echo "FAIL: symboles absents de build/neotel.lbl"; exit 1; }
SCREEN_OFF=$(tests/host/render_page --screen-offset)
PAGE_LEN=$(printf '%X' $(wc -c < tests/page_test.vdt))    # octets de la page (hexa, < 256)

# Etats de g_dbg_state (main.c)
# (valeurs en hexadecimal : Phosphoneo lit ADDR:VAL en hexa)
ST_MENU=3; ST_SESSION=6; ST_CARRIER=8; ST_EXIT=0A; ST_HUNGUP=0C; ST_REPLAY_END=0F

# Touches : splash (espace), interface (espace), menu '1', serveur '1'
KEYS="--poke-at 9000000:$KI=20 --poke-at 12000000:$KI=20 --poke-at 15000000:$KI=31 --poke-at 18000000:$KI=31"

# Lance Phosphoneo avec le faux modem ; $1 = options du modem, reste = phosphoneo
# Stockage (carte SD emulee) : repertoire neuf a chaque scenario, sauf si
# STORAGE_KEEP=1 (scenario des reglages persistants).
STORAGE=$OUT/storage
run() {
    mopts=$1; shift
    rm -f build/pty.txt build/modem.log
    [ "${STORAGE_KEEP:-0}" = 1 ] || rm -rf "$STORAGE"
    mkdir -p "$STORAGE"
    python3 tools/fake_modem.py build/pty.txt $mopts --log build/modem.log &
    mpid=$!
    i=0; while [ ! -s build/pty.txt ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
    NEO_CDC_TTY=$(cat build/pty.txt) timeout 900 "$PHOS" build/neotel.neo --storage "$STORAGE" "$@" >"$OUT/phos.log" 2>&1
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
rows = [''.join(chr(d[base + (r * 40 + c) * 5]) for c in range(40)) for r in range(25)]
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
    --screenshot-at "38000000:$OUT/page.ppm"   # milieu d une phase de clignotement (3,1 M cycles)
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

# --- 2b. profil Minitel 2 : page DRCS (STUM 2 par. 2.3) == oracle hote -----
tests/host/render_page --m2 tests/page_drcs.vdt "$OUT/drcs_gold0.ppm" "$OUT/drcs_gold1.ppm"
run "--serve --page tests/page_drcs.vdt" --cycles 44000000 \
    --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" --poke-at "15000000:$KI=33" \
    --poke-at "18000000:$KI=31" --poke-at "21000000:$KI=31" \
    --screenshot-at "43000000:$OUT/drcs.ppm"
python3 - "$OUT/drcs.ppm" "$OUT/drcs_gold0.ppm" "$OUT/drcs_gold1.ppm" <<'EOF2'
import sys
def load(p):
    return open(p, 'rb').read().split(b'\n', 3)[3]
cap = load(sys.argv[1]); g0 = load(sys.argv[2]); g1 = load(sys.argv[3])
n = 320 * 225 * 3
sys.exit(0 if (cap[:n] == g0[:n] or cap[:n] == g1[:n]) else 1)
EOF2
if [ $? -eq 0 ]; then echo "PASS drcs (Minitel 2 : formes telechargees, capture == oracle hote)"
else echo "FAIL drcs (capture != oracle, voir $OUT/drcs.ppm et $OUT/drcs_gold0.ppm)"; fail=1; fi
if [ "$mode" = ref ]; then cp "$OUT/drcs.ppm" "$REF/drcs.ppm"; echo "REF  drcs"
elif [ -f "$REF/drcs.ppm" ] && cmp -s "$OUT/drcs.ppm" "$REF/drcs.ppm"; then echo "PASS drcs-ref"
elif [ -f "$REF/drcs.ppm" ]; then echo "FAIL drcs-ref (capture != tests/ref/drcs.ppm)"; fail=1; fi

# --- 2c. mode Mixte 80 colonnes (STUM 1B partie 3, profil Minitel 2) == oracle
#         hote (jeux DEC et complementaire de la STUM 2 inclus) -------------
# PRO2 MIXTE 1 dans le flux : passage en mode video 1 (720x350), ecran ISO
# 6429 compose en assembleur ; comparaison bit a bit avec le rendu C de l'hote.
tests/host/render_page --m2 --mixte tests/page_mixte.vdt "$OUT/mixte_gold0.ppm" "$OUT/mixte_gold1.ppm"
run "--serve --page tests/page_mixte.vdt" --cycles 60000000 \
    --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" --poke-at "15000000:$KI=33" \
    --poke-at "18000000:$KI=31" --poke-at "21000000:$KI=31" \
    --screenshot-at "59000000:$OUT/mixte.ppm"
python3 - "$OUT/mixte.ppm" "$OUT/mixte_gold0.ppm" "$OUT/mixte_gold1.ppm" <<'EOF2'
import sys
def load(p):
    d = open(p, 'rb').read().split(b'\n', 3)
    return d[1], d[3]
(sz, cap), (_, g0), (_, g1) = load(sys.argv[1]), load(sys.argv[2]), load(sys.argv[3])
if sz != b'720 350': print("taille de capture", sz); sys.exit(1)
sys.exit(0 if (cap == g0 or cap == g1) else 1)
EOF2
if [ $? -eq 0 ]; then echo "PASS mixte (80 colonnes : capture 720x350 == oracle hote, asm == C)"
else echo "FAIL mixte (capture != oracle, voir $OUT/mixte.ppm et $OUT/mixte_gold0.ppm)"; fail=1; fi
if [ "$mode" = ref ]; then cp "$OUT/mixte.ppm" "$REF/mixte.ppm"; echo "REF  mixte"
elif [ -f "$REF/mixte.ppm" ] && cmp -s "$OUT/mixte.ppm" "$REF/mixte.ppm"; then echo "PASS mixte-ref"
elif [ -f "$REF/mixte.ppm" ]; then echo "FAIL mixte-ref (capture != tests/ref/mixte.ppm)"; fail=1; fi

# --- 2c'. format 40 colonnes du mode Mixte (STUM 2, pixels doubles) --------
tests/host/render_page --m2 --mixte tests/page_mixte40.vdt "$OUT/mixte40_gold0.ppm" "$OUT/mixte40_gold1.ppm"
run "--serve --page tests/page_mixte40.vdt" --cycles 50000000 \
    --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" --poke-at "15000000:$KI=33" \
    --poke-at "18000000:$KI=31" --poke-at "21000000:$KI=31" \
    --screenshot-at "49000000:$OUT/mixte40.ppm"
python3 - "$OUT/mixte40.ppm" "$OUT/mixte40_gold0.ppm" "$OUT/mixte40_gold1.ppm" <<'EOF2'
import sys
def load(p):
    d = open(p, 'rb').read().split(b'\n', 3); return d[3]
cap, g0, g1 = load(sys.argv[1]), load(sys.argv[2]), load(sys.argv[3])
sys.exit(0 if (cap == g0 or cap == g1) else 1)
EOF2
if [ $? -eq 0 ]; then echo "PASS mixte40 (40 colonnes : capture == oracle hote)"
else echo "FAIL mixte40 (voir $OUT/mixte40.ppm et $OUT/mixte40_gold0.ppm)"; fail=1; fi
if [ "$mode" = ref ]; then cp "$OUT/mixte40.ppm" "$REF/mixte40.ppm"; echo "REF  mixte40"
elif [ -f "$REF/mixte40.ppm" ] && cmp -s "$OUT/mixte40.ppm" "$REF/mixte40.ppm"; then echo "PASS mixte40-ref"
elif [ -f "$REF/mixte40.ppm" ]; then echo "FAIL mixte40-ref"; fail=1; fi

# --- 2c''. jeu special DEC : les 5 traits de balayage a hauteurs distinctes
tests/host/render_page --m2 --mixte tests/page_dec.vdt "$OUT/dec_gold0.ppm" "$OUT/dec_gold1.ppm"
run "--serve --page tests/page_dec.vdt" --cycles 60000000 \
    --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" --poke-at "15000000:$KI=33" \
    --poke-at "18000000:$KI=31" --poke-at "21000000:$KI=31" \
    --screenshot-at "59000000:$OUT/dec.ppm"
python3 - "$OUT/dec.ppm" "$OUT/dec_gold0.ppm" "$OUT/dec_gold1.ppm" <<'EOF2'
import sys
def load(p):
    d = open(p, 'rb').read().split(b'\n', 3)
    return d[1], d[3]
(sz, cap), (_, g0), (_, g1) = load(sys.argv[1]), load(sys.argv[2]), load(sys.argv[3])
sys.exit(0 if (sz == b'720 350' and (cap == g0 or cap == g1)) else 1)
EOF2
if [ $? -eq 0 ]; then echo "PASS dec (traits de balayage DEC : capture == oracle hote)"
else echo "FAIL dec (capture != oracle, voir $OUT/dec.ppm)"; fail=1; fi

# --- 2d. mode Mixte : ESC ESC quitte, retour au mode video 0 et au menu -----
run "--serve --page tests/page_mixte.vdt --guard 0.02" --cycles 120000000 $KEYS \
    --poke-at "45000000:$KI=1B" --poke-at "52000000:$KI=1B" \
    --screenshot-at "119000000:$OUT/mixte_exit.ppm" --dump-ram-when "$HU:1:$OUT/mixte_hungup.bin"
if [ -f "$OUT/mixte_hungup.bin" ] && grep -q "commande b'ATH'" build/modem.log; then
    echo "PASS mixte-exit (ESC ESC en 80 colonnes : raccrochage, retour au menu)"
else
    echo "FAIL mixte-exit"; fail=1
fi
# Pile C ($FBC0-$FBFF, 64 o ; usage mesure 33) : la moitie basse ($FBC0-$FBCF)
# doit rester vierge (marge >= 16 octets)
if python3 -c "
import sys; d=open('$OUT/mixte_hungup.bin','rb').read(); sys.exit(0 if not any(d[0xFBC0:0xFBD0]) else 1)"; then
    echo "PASS stack (pile C : au moins 16 octets de marge)"
else
    echo "FAIL stack (pile C descendue sous \$FBD0)"; fail=1
fi

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

# --- 4b. reglages persistants : profil Minitel 2 sauve puis recharge -------
rm -rf "$STORAGE"
run "--serve" --cycles 20000000 --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" \
    --poke-at "15000000:$KI=33"
if [ -f "$STORAGE/neotel.cfg" ] && [ "$(od -An -tu1 -j3 -N1 "$STORAGE/neotel.cfg" | tr -d ' ')" = 1 ]; then
    echo "PASS settings-save (neotel.cfg ecrit, profil Minitel 2)"
else
    echo "FAIL settings-save ($STORAGE/neotel.cfg)"; fail=1
fi
STORAGE_KEEP=1 run "--serve" --cycles 20000000 --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" \
    --dump-ram-when "$ST:$ST_MENU:$OUT/settings.bin"
if [ -f "$OUT/settings.bin" ] && [ "$(od -An -tu1 -j$((0x$TM)) -N1 "$OUT/settings.bin" | tr -d ' ')" = 1 ]; then
    echo "PASS settings-load (profil Minitel 2 restaure au demarrage)"
else
    echo "FAIL settings-load"; fail=1
fi

# --- 4c. enregistrement (CTRL+O) puis relecture (menu 6) d'une page .vdt ----
# La page ne part qu'au premier octet tape (--on-rx) : CTRL+O arme
# l'enregistrement, ENVOI declenche la page, CTRL+O arrete ; le fichier
# neo01.vdt doit etre la copie exacte de la page.
rm -rf "$STORAGE"
run "--serve --page tests/page_test.vdt --on-rx --guard 0.02" --cycles 120000000 $KEYS \
    --poke-at "30000000:$KI=0F" --poke-at "40000000:$KI=0D" --poke-at "100000000:$KI=0F"
if [ -f "$STORAGE/neo01.vdt" ] && cmp -s "$STORAGE/neo01.vdt" tests/page_test.vdt; then
    echo "PASS record (CTRL+O : neo01.vdt == page recue)"
else
    echo "FAIL record ($STORAGE/neo01.vdt ; voir build/modem.log)"; fail=1
fi
# Relecture sur le meme stockage : menu '6', ENVOI (= dernier enregistrement) ;
# g_dbg_state passe a ST_REPLAY_END quand tout le fichier a ete rejoue.
rm -f "$OUT/replay.bin"
STORAGE_KEEP=1 run "--serve" --cycles 60000000 --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" \
    --poke-at "15000000:$KI=36" --poke-at "18000000:$KI=0D" \
    --dump-ram-when "$ST:$ST_REPLAY_END:$OUT/replay.bin"
if [ -f "$OUT/replay.bin" ] && page_has "$OUT/replay.bin" "PAGE DE TEST NEOTEL"; then
    echo "PASS replay (menu 6 : neo01.vdt rejoue, page decodee)"
else
    echo "FAIL replay (voir $OUT/phos.log)"; fail=1
fi

# --- 4d. serveur WebSocket : ATDT ws://127.0.0.1:PORT relaye par le faux modem
# (module python3 "websockets" ; SKIP sinon). Le dernier serveur des reglages
# est prerempli avec l'URL : menu 1 puis ENVOI compose.
if python3 -c "import websockets" 2>/dev/null; then
    rm -rf "$STORAGE"; mkdir -p "$STORAGE"; rm -f build/wsport.txt build/ws.log
    python3 tools/ws_page_server.py build/wsport.txt tests/page_test.vdt --log build/ws.log &
    wspid=$!
    i=0; while [ ! -s build/wsport.txt ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
    python3 - "$STORAGE/neotel.cfg" "ws://127.0.0.1:$(cat build/wsport.txt)" <<'EOF2'
import sys, struct
srv = sys.argv[2].encode().ljust(40, b'\0')
open(sys.argv[1], 'wb').write(b'NT' + bytes([2, 0, 0, 0, 255]) + srv + bytes([0]))
EOF2
    STORAGE_KEEP=1 run "" --cycles 120000000 --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" \
        --poke-at "15000000:$KI=31" --poke-at "18000000:$KI=0D" \
        --dump-ram-when "$NB:$PAGE_LEN:$OUT/ws.bin"
    kill $wspid 2>/dev/null; wait $wspid 2>/dev/null
    if [ -f "$OUT/ws.bin" ] && page_has "$OUT/ws.bin" "PAGE DE TEST NEOTEL" && grep -q "WebSocket ws://127.0.0.1" build/modem.log; then
        echo "PASS ws (ATDT ws:// : page recue par WebSocket)"
    else
        echo "FAIL ws (voir build/modem.log, build/ws.log)"; fail=1
    fi
else
    echo "SKIP ws (module python3 websockets absent)"
fi

# --- 4e. ENQROM en tete de page (MiniPavi, 3617.fr) : l'ESC recu juste apres
# CONNECT ne doit pas etre perdu ; identification ON par les reglages -> le
# modem doit voir la reponse SOH C u 1 EOT.
rm -rf "$STORAGE"; mkdir -p "$STORAGE"
python3 -c "
open('$STORAGE/neotel.cfg','wb').write(b'NT'+bytes([3,0,0,1,0])+bytes(40)+bytes([0,1]))"
STORAGE_KEEP=1 run "--serve --page tests/page_enqrom.vdt" --cycles 60000000 $KEYS \
    --dump-ram-when "$NB:$(printf '%X' $(wc -c < tests/page_enqrom.vdt)):$OUT/enqrom.bin"
if [ -f "$OUT/enqrom.bin" ] && page_has "$OUT/enqrom.bin" "PAGE DE TEST NEOTEL" && grep -q "> b'\\\\x01Cu1\\\\x04'" build/modem.log; then
    echo "PASS enqrom (ESC 9 { en tete de page : identification envoyee, page decodee)"
else
    echo "FAIL enqrom (voir build/modem.log)"; fail=1
fi

# --- 4f. liste des enregistrements (menu 6) : 3,17-3,19 sur cible ----------
# Trois .vdt deposes ; le menu doit les lister tries et la lettre B rejouer
# le second (page DRCS -> "G1 BASE").
rm -rf "$STORAGE"; mkdir -p "$STORAGE"
cp tests/page_test.vdt "$STORAGE/neo01.vdt"
cp tests/page_drcs.vdt "$STORAGE/neo02.vdt"
cp tests/page_test.vdt "$STORAGE/neo07.vdt"
python3 -c "open('$STORAGE/neotel.cfg','wb').write(b'NT'+bytes([3,0,0,0,0])+bytes(40)+bytes([7,1]))"
STORAGE_KEEP=1 run "--serve" --cycles 70000000 --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" \
    --poke-at "18000000:$KI=36" --poke-at "32000000:$KI=42" \
    --dump-ram-when "$ST:$ST_REPLAY_END:$OUT/rlist.bin"
if [ -f "$OUT/rlist.bin" ] && page_has "$OUT/rlist.bin" "G1 BASE"; then
    echo "PASS reclist (menu 6 liste neoNN.vdt, lettre B rejoue neo02)"
else
    echo "FAIL reclist (voir $OUT/phos.log)"; fail=1
fi

# --- 4g. suppression d'un enregistrement (Suppr + lettre) depuis le menu 6 --
rm -rf "$STORAGE"; mkdir -p "$STORAGE"
cp tests/page_test.vdt "$STORAGE/neo01.vdt"
cp tests/page_drcs.vdt "$STORAGE/neo02.vdt"
cp tests/page_test.vdt "$STORAGE/neo07.vdt"
python3 -c "open('$STORAGE/neotel.cfg','wb').write(b'NT'+bytes([3,0,0,0,0])+bytes(40)+bytes([7,1]))"
STORAGE_KEEP=1 run "--serve" --cycles 55000000 --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" \
    --poke-at "18000000:$KI=36" --poke-at "30000000:$KI=08" --poke-at "34000000:$KI=42" \
    --poke-at "50000000:$KI=1B"
if [ ! -f "$STORAGE/neo02.vdt" ] && [ -f "$STORAGE/neo01.vdt" ] && [ -f "$STORAGE/neo07.vdt" ]; then
    echo "PASS recdel (Suppr + B efface neo02, neo01/neo07 conserves)"
else
    echo "FAIL recdel (voir $STORAGE)"; fail=1
fi

# --- 4h. ecran d'aide bilingue (menu H) : "AIDE NEOTEL" affiche, bascule EN --
rm -rf "$STORAGE"; mkdir -p "$STORAGE"
run "--serve" --cycles 40000000 --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" \
    --poke-at "18000000:$KI=48" \
    --dump-ram-when "$ST:10:$OUT/help.bin"
if [ -f "$OUT/help.bin" ] && page_has "$OUT/help.bin" "AIDE NEOTEL"; then
    echo "PASS help (menu H : aide affichee, francais)"
else
    echo "FAIL help (voir $OUT/phos.log)"; fail=1
fi
# 'L' bascule la langue et l'ecrit dans neotel.cfg (offset 49 = apres sound) ;
# on laisse l'emulation aller au bout (le dump ST_HELP s'arreterait avant le L).
run "--serve" --cycles 45000000 --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" \
    --poke-at "18000000:$KI=48" --poke-at "26000000:$KI=4C" \
    --screenshot-at "34000000:$OUT/help_en.ppm"
if [ -f "$STORAGE/neotel.cfg" ] \
   && [ "$(od -An -tu1 -j49 -N1 "$STORAGE/neotel.cfg" | tr -d ' ')" = 1 ]; then
    echo "PASS help-lang (L : langue anglaise memorisee dans neotel.cfg)"
else
    echo "FAIL help-lang ($STORAGE/neotel.cfg)"; fail=1
fi

# --- 4i. vraie file clavier du firmware (--type-keys, pas keyboard_inject) --
# Splash passe par injection, puis l'ecran de la liaison serie par une frappe
# REELLE (Phosphoneo n'accepte qu'un --type-keys) : l'etat menu doit etre
# atteint. Couvre le sens de l'API 2,2 ($FF = touche disponible), inverse
# jusqu'en v0.8.0 (clavier muet sur carte reelle).
run "--serve" --cycles 30000000 --poke-at "9000000:$KI=20" --type-keys '14000000: ' \
    --dump-ram-when "$ST:$ST_MENU:$OUT/realkeys.bin"
if [ -f "$OUT/realkeys.bin" ]; then
    echo "PASS realkeys (frappes par la file clavier du firmware : menu atteint)"
else
    echo "FAIL realkeys (menu jamais atteint avec --type-keys, voir $OUT/phos.log)"; fail=1
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
