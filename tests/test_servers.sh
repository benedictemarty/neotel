#!/bin/sh
# test_servers.sh — fumee sur les VRAIS serveurs Minitel (reseau requis, hors `make test`)
#
#   make test-servers   : PAVI 3617, MiniPavi (relais TCP) puis 3617.fr en
#                         WebSocket (relais wss://) par le faux modem,
#                         (Phosphoneo va ~6 fois plus vite que le temps reel :
#                         1,5 G cycles = ~10 s reels), une page doit s'afficher et le
#                         serveur doit avoir envoye plus de 200 octets.
# SKIP si le serveur est injoignable.
set -u
cd "$(dirname "$0")/.."
PHOS=${PHOSPHONEO:-$HOME/Phosphoneo/build/phosphoneo}
OUT=tests/out; mkdir -p "$OUT"
[ -x "$PHOS" ] || { echo "SKIP: Phosphoneo absent"; exit 0; }
[ -f build/neotel.neo ] || { echo "FAIL: build/neotel.neo absent (make)"; exit 1; }
KI=$(grep " \._keyboard_inject\$" build/neotel.lbl | awk '{print $2}' | sed 's/^00//')
fail=0
srv() {   # $1 = nom, $2 = hote:port (test de joignabilite), $3 = touche serveur du menu (31 / 32 / 33)
    name=$1; hp=$2; key=$3
    if ! timeout 5 bash -c "echo > /dev/tcp/${hp%:*}/${hp#*:}" 2>/dev/null; then
        echo "SKIP $name ($hp injoignable)"; return
    fi
    rm -f build/pty.txt build/modem.log; rm -rf "$OUT/storage"; mkdir -p "$OUT/storage"
    python3 tools/fake_modem.py build/pty.txt --guard 1 --log build/modem.log &
    mpid=$!; i=0; while [ ! -s build/pty.txt ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
    NEO_CDC_TTY=$(cat build/pty.txt) timeout 600 "$PHOS" build/neotel.neo --storage "$OUT/storage" --cycles 1500000000 \
        --poke-at "9000000:$KI=20" --poke-at "12000000:$KI=20" --poke-at "15000000:$KI=31" --poke-at "18000000:$KI=$key" \
        --screenshot-at "1490000000:$OUT/srv_$name.ppm" >"$OUT/phos.log" 2>&1
    kill $mpid 2>/dev/null; wait $mpid 2>/dev/null
    rx=$(grep -o "<< [0-9]* octets serveur" build/modem.log | awk '{s+=$2} END{print s+0}')
    # ws : le relais passe par une socketpair, meme journal "<< N octets serveur"
    n=$(python3 -c "
d=open('$OUT/srv_$name.ppm','rb').read(); px=d.split(b'\n',3)[3]
print(sum(1 for i in range(0,len(px),3) if px[i] or px[i+1] or px[i+2]))" 2>/dev/null || echo 0)
    if [ "$rx" -gt 200 ] && [ "$n" -gt 2000 ]; then echo "PASS $name ($rx octets recus, $n pixels allumes)"
    else echo "FAIL $name ($rx octets, $n pixels, voir $OUT/srv_$name.ppm)"; fail=1; fi
}
srv pavi pavi.3617.fr:3617 31
srv minipavi go.minipavi.fr:516 32
# 3 = wss://3617.fr/ws (WebSocket relaye par le faux modem, module websockets)
if python3 -c "import websockets" 2>/dev/null; then srv ws3617 3617.fr:443 33; else echo "SKIP ws3617 (websockets absent)"; fi
[ $fail -eq 0 ] && echo "test_servers : OK" || echo "test_servers : ECHEC"
exit $fail
