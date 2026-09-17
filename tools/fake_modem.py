#!/usr/bin/env python3
# SPDX-License-Identifier: EUPL-1.2
"""fake_modem.py - modem Hayes logiciel sur un pseudo-terminal, pour NeoTel.

Expose au firmware (NEO_CDC_TTY / --cdc-tty) un modem qui se comporte comme
un PicoWiFiModemUSB :
  AT, ATZ, ATE0/1, ATH, AT&W, ATC1, AT$SSID=, AT$PASS=  -> OK
  ATI                 -> "WiFi status: CONNECTED TO WIFI" puis OK
  AT$SCAN             -> deux reseaux fictifs puis OK
  ATDT hote:port      -> vraie connexion TCP, "CONNECT", relais transparent ;
                         fermeture distante -> "NO CARRIER", retour en commande
  ATDT ws://... | wss://...
                      -> serveur Minitel en WebSocket (module python3
                         "websockets", sous-protocole "binary", messages texte
                         ramenes sur 7 bits comme le bridge d'OricTel) ; meme
                         relais, meme NO CARRIER. Ex. : ATDTws://3617.fr/ws
  +++ (garde 1 s)     -> OK, mode commande (la connexion reste ouverte, ATH la ferme)

Options :
  FICHIER_PTY          chemin du pty ecrit dans ce fichier (et affiche)
  --serve              ATDT ne sort pas sur le reseau : un serveur Videotex
                       integre envoie une page de test ("PAGE DE TEST NEOTEL")
  --page FICHIER       page Videotex (.vdt) a envoyer a la place de la page integree
  --nc SECONDES        en mode --serve : NO CARRIER SECONDES apres le CONNECT
  --delay SECONDES     en mode --serve : la page part SECONDES apres le CONNECT
  --on-rx              en mode --serve : la page part au premier octet envoye
                       par le terminal apres le CONNECT (independant de la
                       vitesse de l'emulateur : le test arme un enregistrement,
                       puis tape une touche)
  --echo-server        en mode --serve : renvoie a l'ecran ce que le terminal tape
  --guard SECONDES     garde de silence Hayes autour de "+++" (1 s ; les tests
                       sous emulateur, plus rapide que le temps reel, la reduisent)
  --log FICHIER        journal horodate des echanges
Ctrl-C pour quitter. Auteur : bmarty
"""
import os
import pty
import re
import select
import socket
import sys
import threading
import time

# Page de test integree : couvre curseur US, couleurs, double hauteur,
# mosaiques G1, souligne, inversion, clignotement, accent SS2, REP.
TEST_PAGE = (
    b"\x0c"                                   # FF : effacement
    b"\x1f\x41\x41" b"\x1b\x46PAGE DE TEST NEOTEL"   # ligne 1, cyan
    b"\x1f\x43\x41" b"\x1b\x4d\x1b\x43Double hauteur"  # ligne 3, DH jaune
    b"\x1f\x45\x41" b"\x1b\x47\x1b\x51 Fond rouge \x1b\x50"  # ligne 5 fond rouge
    b"\x1f\x47\x41" b"\x0e\x1b\x43\x21\x22\x23\x24\x25\x26\x27\x28\x0f"  # ligne 7 mosaiques
    b"\x1f\x49\x41" b"\x1b\x5a Souligne\x1b\x59 \x1b\x5dInverse\x1b\x5c \x1b\x48Flash\x1b\x49"
    b"\x1f\x4b\x41" b"Accents: \x19\x42e \x19\x41a \x19\x43o \x19\x4bc"
    b"\x1f\x4d\x41" b"Rep: A\x12\x49"        # A repete 9 fois
    b"\x1f\x58\x41" b"Bonjour"                # ligne 24
)

args = sys.argv[1:]
pty_file = None
log_file = None
serve = False
page_file = None
nc_after = None
echo_server = False
page_delay = 0.0
page_on_rx = False
guard = 1.0
while args:
    a = args.pop(0)
    if a == "--log":
        log_file = args.pop(0)
    elif a == "--serve":
        serve = True
    elif a == "--page":
        page_file = args.pop(0)
    elif a == "--nc":
        nc_after = float(args.pop(0))
    elif a == "--echo-server":
        echo_server = True
    elif a == "--delay":
        page_delay = float(args.pop(0))
    elif a == "--on-rx":
        page_on_rx = True
    elif a == "--guard":
        guard = float(args.pop(0))
    else:
        pty_file = a

logf = open(log_file, "a") if log_file else None
t0 = time.time()


def log(s):
    if logf:
        logf.write("%7.2f %s\n" % (time.time() - t0, s))
        logf.flush()


master, slave = pty.openpty()
path = os.ttyname(slave)
if pty_file:
    with open(pty_file, "w") as f:
        f.write(path)
print(path, flush=True)


def w(b):
    os.write(master, b)
    log("< %r" % b[:80])


def page_bytes():
    if page_file:
        with open(page_file, "rb") as f:
            return f.read()
    return TEST_PAGE


buf = b""
online = False          # mode donnees (apres CONNECT)
sock = None             # socket TCP en mode reseau
served = False          # connexion "servie" localement
page_pending = False    # page differee (--delay) a envoyer
echo = True             # ATE1
t_conn = None
sent_nc = False
last_rx = time.time()
plus_count = 0


def ws_open(url):
    """Ouvre url en WebSocket et rend une socket locale (socketpair) que la
    boucle principale traite exactement comme une connexion TCP : deux fils
    relaient socket <-> WebSocket ; la fermeture distante ferme la socket
    (-> NO CARRIER), la fermeture locale (ATH) ferme le WebSocket."""
    from websockets.sync.client import connect     # ImportError si absent
    ws = connect(url, subprotocols=["binary"], max_size=65536, open_timeout=10)
    a, b = socket.socketpair()

    def ws_to_sock():
        try:
            for msg in ws:
                if isinstance(msg, str):
                    msg = bytes(ord(c) & 0x7F for c in msg)
                if msg:
                    b.sendall(msg)
        except Exception:
            pass
        try:
            b.shutdown(socket.SHUT_WR)
        except OSError:
            pass

    def sock_to_ws():
        try:
            while True:
                data = b.recv(1024)
                if not data:
                    break
                ws.send(data)
        except Exception:
            pass
        try:
            ws.close()
        except Exception:
            pass
        try:
            b.close()
        except OSError:
            pass

    threading.Thread(target=ws_to_sock, daemon=True).start()
    threading.Thread(target=sock_to_ws, daemon=True).start()
    return a


def hangup(notify):
    global online, sock, served, t_conn, sent_nc
    if sock:
        try:
            sock.close()
        except OSError:
            pass
        sock = None
    online = False
    served = False
    t_conn = None
    sent_nc = False
    if notify:
        w(b"\r\nNO CARRIER\r\n")


def command(line):
    global echo, online, sock, served, t_conn, sent_nc, page_pending
    u = line.upper()
    log("commande %r" % line)
    if u in (b"AT", b"ATZ", b"AT&W", b"ATC1") or u.startswith(b"AT$SSID=") or u.startswith(b"AT$PASS="):
        w(b"\r\nOK\r\n")
    elif u == b"ATE0":
        echo = False
        w(b"\r\nOK\r\n")
    elif u == b"ATE1":
        echo = True
        w(b"\r\nOK\r\n")
    elif u == b"ATI":
        w(b"\r\nNeoTel fake modem 1.0 (PicoWiFiModemUSB)\r\nWiFi status: CONNECTED TO WIFI\r\nOK\r\n")
    elif u == b"AT$SCAN":
        w(b"\r\n0 ReseauTest\tS\r\n1 Ouvert\tO\r\nOK\r\n")
    elif u == b"ATH":
        hangup(False)
        w(b"\r\nOK\r\n")
    elif u.startswith(b"ATDT") or u.startswith(b"ATD"):
        target = line[4:] if u.startswith(b"ATDT") else line[3:]
        target = target.strip().decode("latin-1")
        if serve:
            served = True
            online = True
            t_conn = time.time()
            sent_nc = False
            w(b"\r\nCONNECT\r\n")
            if page_delay > 0 or page_on_rx:
                page_pending = True
            else:
                w(page_bytes())
                log("serveur integre : page envoyee")
        elif target.startswith("ws://") or target.startswith("wss://"):
            try:
                s = ws_open(target)
                s.setblocking(False)
                sock = s
                online = True
                t_conn = time.time()
                w(b"\r\nCONNECT\r\n")
                log("WebSocket %s ouvert" % target)
            except Exception as e:      # ImportError, OSError, erreur WS
                log("ATD %s : %s" % (target, e))
                w(b"\r\nNO CARRIER\r\n")
        else:
            host, _, port = target.rpartition(":")
            try:
                s = socket.create_connection((host, int(port)), timeout=10)
                s.setblocking(False)
                sock = s
                online = True
                t_conn = time.time()
                w(b"\r\nCONNECT\r\n")
                log("TCP %s:%s ouvert" % (host, port))
            except (OSError, ValueError) as e:
                log("ATD %s : %s" % (target, e))
                w(b"\r\nNO CARRIER\r\n")
    else:
        w(b"\r\nERROR\r\n")


try:
    while True:
        fds = [master] + ([sock] if sock else [])
        r, _, _ = select.select(fds, [], [], min(0.2, guard / 2))
        now = time.time()

        if sock and sock in r:
            try:
                chunk = sock.recv(4096)
            except OSError:
                chunk = b""
            if not chunk:
                log("serveur : fermeture")
                hangup(True)
            elif online:
                os.write(master, chunk)
                log("<< %d octets serveur %r" % (len(chunk), chunk[:60]))

        if served and page_pending and not page_on_rx and t_conn and now - t_conn >= page_delay:
            page_pending = False
            w(page_bytes())
            log("serveur integre : page envoyee (apres %.2f s)" % page_delay)

        if served and nc_after is not None and not sent_nc and t_conn and now - t_conn > nc_after:
            sent_nc = True
            hangup(True)

        if master not in r:
            # garde de silence apres "+++"
            if online and plus_count >= 3 and now - last_rx > guard:
                plus_count = 0
                online = False
                w(b"\r\nOK\r\n")
                log("+++ : mode commande")
            continue

        try:
            data = os.read(master, 4096)
        except OSError:
            break
        if not data:
            continue
        last_rx = now
        log("> %r" % data[:80])

        if online:
            # detection de "+++" (sans CR) ; le reste part au serveur
            if data == b"+" or data == b"++" or data == b"+++":
                plus_count += len(data)
                if plus_count > 3:
                    plus_count = 0
                continue
            plus_count = 0
            if served and page_pending and page_on_rx:
                page_pending = False
                w(page_bytes())
                log("serveur integre : page envoyee (apres reception de %r)" % data[:8])
            if sock:
                try:
                    sock.sendall(data)
                except OSError as e:
                    log("send : %s" % e)
                    hangup(True)
            elif served and echo_server:
                os.write(master, data)
            continue

        if echo:
            os.write(master, data)
        buf += data
        while b"\r" in buf or b"\n" in buf:
            m = re.search(rb"[\r\n]", buf)
            line, buf = buf[:m.start()], buf[m.end():]
            line = line.strip()
            if line:
                command(line)
except KeyboardInterrupt:
    pass
