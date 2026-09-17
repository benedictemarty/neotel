#!/usr/bin/env python3
# SPDX-License-Identifier: EUPL-1.2
"""ws_page_server.py - serveur Minitel WebSocket minimal (tests de NeoTel).

    ws_page_server.py FICHIER_PORT PAGE.vdt [--echo] [--log FICHIER]

Ecoute en WebSocket sur 127.0.0.1 (port libre, ecrit dans FICHIER_PORT),
envoie PAGE.vdt en un message binaire a chaque connexion, puis journalise
ce que le terminal envoie (--echo : le renvoie). Sert de bout distant a
`fake_modem.py` pour `ATDT ws://127.0.0.1:PORT`. Auteur : bmarty.
"""
import asyncio
import sys
import time

import websockets

args = sys.argv[1:]
port_file = args.pop(0)
page = open(args.pop(0), "rb").read()
echo = False
log_file = None
while args:
    a = args.pop(0)
    if a == "--echo":
        echo = True
    elif a == "--log":
        log_file = args.pop(0)
t0 = time.time()


def log(s):
    line = "%7.2f %s\n" % (time.time() - t0, s)
    if log_file:
        with open(log_file, "a") as f:
            f.write(line)


async def handler(ws):
    log("connexion %s" % (ws.remote_address,))
    await ws.send(page)
    log("page envoyee (%d octets)" % len(page))
    try:
        async for msg in ws:
            log("> %r" % (msg[:80],))
            if echo:
                await ws.send(msg)
    except websockets.ConnectionClosed:
        pass
    log("fermeture")


async def main():
    async with websockets.serve(handler, "127.0.0.1", 0, subprotocols=["binary"]) as server:
        port = list(server.sockets)[0].getsockname()[1]
        with open(port_file, "w") as f:
            f.write("%d\n" % port)
        log("ecoute sur %d" % port)
        await asyncio.Future()


try:
    asyncio.run(main())
except KeyboardInterrupt:
    pass
