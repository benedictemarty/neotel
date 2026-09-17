# Stratégie de tests — NeoTel

Principe hérité d'OricTel et de Phosphoneo : **exactitude déclarée, pas
supposée**. Chaque couche a un test qui la falsifierait ; `make test` doit
être vert avant tout commit.

## Tests hôte (`make test-host`, gcc, `tests/host/`)

Compilés avec `-DTEST_HOST` contre **`neo_stub.c`**, un Neo6502 logiciel :
bloc `$FF00` dispatché en C (timer, clavier scripté, état HID, UART en
mémoire, présence CDC, fichier `neotel.cfg` et disque en mémoire par
canal), VRAM 320 × 240, palette, compteurs de blits.

| Test | Assertions | Couvre |
|---|---|---|
| `test_videotex` | 221 | décodeur Videotex (repris d'OricTel tel quel) |
| `test_atmodem` | 28 | machine à états AT (repris d'OricTel) |
| `test_ui` | 11 | helpers de menus, bornes de saisie (repris) |
| `test_keyboard` | 61 | traduction firmware → Minitel, flèches vs CTRL, hotkeys F1-F10, injection, émission SEP/CSI, aiguillages |
| `test_display` | 48 | géométrie 8 × 9, glyphes centrés, mosaïques (blocs, séparées, `$60`), inversion, souligné, masquage, flash, doubles largeur/hauteur/taille, clip colonne 39, budget 1 ligne, plages dirty, `full_refresh`, curseur, statut, palettes, G2, `display_clear`, bip soumis au réglage son |
| `test_drcs` | 40 | DRCS Minitel 2 (STUM 2 §2.2-2.5) : en-têtes, transfert (vecteur = exemple §2.3.5), B1 anticipé, excédent, C0 = fond, sortie US, associations, mapping 2/0 et 7/F, rangée 00, SS2, profil 1B, `CSI 6n`, rendu 8 × 9, effacement par `vtx_init` |
| `test_settings` | 11 | `neotel.cfg` : défauts, aller-retour, magie/version (v1 et v2 acceptées), bornes, écriture refusée |
| `test_record` | 233 | enregistrement / relecture `.vdt` (`record.c`) sur le disque en mémoire du stub (3,4/5/8/9 par canal, énumération 3,17/3,18/3,19, suppression 3,13) : noms `neoNN.vdt`, blocs de 64, vidage à l'arrêt, relecture octet par octet, fichier absent / vide, création refusée, exclusion enregistrement/relecture, `record_list` (tri, noms non conformes ignorés), `record_delete` |
| `test_teleinfo` | 89 | écran 80 colonnes : décodeur ISO 6429 (STUM 1B p. 160-170, STUM 2 §3), rangée 00, formats, rendu 1 bpp (pixels, attributs, curseur, 40 colonnes), mode 1 refusé |
| `test_terminal` | 25 | profils 1B/M2, identification, PRO2 PROG, intégration décodeur, `serial.c` (routage, format, RX/TX, CDC absent, firmware amont) |

`render_page` (même Makefile) est l'**oracle** : il rend une page `.vdt`
avec le chemin C de `display.c` et écrit deux PPM (phases de clignotement).

`make -C tests/host fuzz_videotex` construit le fuzzer libFuzzer d'OricTel
(clang) : ~780 000 exécutions / 10 s sans plantage sur la v0.1.0, 1 M / 20 s
en v0.4.2 (DRCS compris). `fuzz_teleinfo` (v0.4.2) fuzze le décodeur 80
colonnes puis compose les 25 rangées : 118 000 exécutions / 30 s sans
plantage ni débordement (ASan).

## Tests cible (`make test-emu`, `tests/run.sh`)

Phosphoneo headless (65C02 cycle-exact, API du vrai firmware liée) + faux
modem Hayes sur pty (`tools/fake_modem.py`, exposé via `NEO_CDC_TTY`).
Touches injectées par `--poke-at` dans `keyboard_inject`, états attendus
par `--dump-ram-when` sur `g_dbg_state` / `g_vtx_bytes` / `g_dbg_hangups`,
adresses lues dans `build/neotel.lbl`.

1. **session** : splash → liaison → menu `1` → serveur `1` → ATZ/ATI/ATDT →
   page décodée (« PAGE DE TEST NEOTEL » lu dans `vtx.screen` du dump RAM).
2. **ATDT** : le journal du modem contient `ATDTpavi.3617.fr:3617`.
3. **page** : la capture de la page (rendu **assembleur** sur cible, à 38 M
   cycles = milieu d'une phase de clignotement de 3,1 M cycles) est
   **identique pixel pour pixel** au rendu **C** de l'hôte (`render_page`,
   l'une des deux phases de clignotement). C'est la preuve que
   `display_asm.s` et `display.c` font la même chose.
4. **page-ref** : la capture est identique à `tests/ref/page.ppm`
   (`make ref` après un changement visuel voulu et inspecté).
4c. **mixte** / **mixte-ref** / **mixte-exit** : `PRO2 MIXTE 1` dans la page,
   capture 720 × 350 identique à `render_page --mixte` (asm == C) et à
   `tests/ref/mixte.ppm` ; ESC ESC en 80 colonnes raccroche et revient au
   mode vidéo 0.
4d. **mixte40** / **mixte40-ref** : format 40 colonnes (pixels doublés).
4b. **drcs** / **drcs-ref** : profil Minitel 2 choisi au menu (`3`), page
   `tests/page_drcs.vdt` (formes G'1 dont l'exemple de la STUM, formes G'0,
   souligné, retour au jeu de base) : capture == `render_page --m2`, et
   identique à `tests/ref/drcs.ppm`.
5. **escape** : ESC ESC en session → `+++` puis `ATH` émis, retour au menu
   (garde Hayes du faux modem réduite : l'émulateur va plus vite que le
   temps réel).
6. **carrier** : `NO CARRIER` 1 s après la page → écran « PERTE DE
   PORTEUSE » après confirmation par le silence.
6b. **settings-save / settings-load** : `3` au menu écrit `neotel.cfg`
   (profil = 1) dans le stockage du scénario ; relancé sur le même stockage,
   NeoTel démarre en Minitel 2 (`g_term_model` lu dans le dump RAM).
6c. **stack** : sur le dump de `mixte-exit`, `$FBA0-$FBCF` (moitié basse de
   la pile C de 96 octets) doit être vierge : marge d'au moins 48 octets.
   (Usage réel mesuré sur les chemins session et relecture : 33 octets.)
6d. **record / replay** : le faux modem (`--on-rx`) n'envoie la page qu'au
   premier octet tapé ; `CTRL+O` (0x0F injecté), ENVOI, `CTRL+O` : le
   fichier `neo01.vdt` du stockage est la copie exacte de
   `tests/page_test.vdt`. Puis, sur le même stockage, menu `6` + ENVOI :
   dump RAM quand `g_dbg_state` = `ST_REPLAY_END` (0x0F, fichier rejoué),
   la page doit y être décodée. NB : `--dump-ram-when` et `--poke-at`
   lisent les valeurs en **hexadécimal**.
6e. **ws** : `tools/ws_page_server.py` sert la page de test en WebSocket
   sur un port libre ; `neotel.cfg` est prérempli avec
   `ws://127.0.0.1:PORT` comme dernier serveur ; menu `1` + ENVOI → le faux
   modem (sans `--serve`) ouvre le WebSocket, la page est décodée. SKIP sans
   le module `websockets`.
6f. **reclist** : trois `.vdt` déposés sur le stockage ; menu `6` les liste
   (3,17/3,18/3,19), la lettre `B` rejoue le second (page DRCS, `G1 BASE`).
6g. **recdel** : menu `6`, `Suppr` puis `B` efface `neo02.vdt` ; `neo01`
   et `neo07` restent sur le stockage.
7. **exit** : ESC au menu → NeoBASIC répond (`PRINT 6*7` → `42`).
8. **neo** : fumée dans l'émulateur officiel `neo` (splash affiché).

Durée : ~9 s. Les scénarios sont déterministes côté 65C02 ; seul le faux
modem est asynchrone (il répond en quelques ms, bien avant les timeouts).

## Banc de rendu (`make bench`)

`tests/emu/t_bench.c` (programme cible sans `main.c`, page de test intégrée
par `tools/vdt2c.py`) rend quatre pages complètes en encadrant chaque rendu
de deux appels 5,37 ; `tests/bench.sh` lit les cycles dans `--api-log` et
affiche ms et cycles par rangée. Profil détaillé : `--trace` de Phosphoneo
agrégé par symbole de `build/t_bench.lbl` (méthode de la v0.6.1).

## Serveurs réels (`make test-servers`, hors `make test`)

PAVI 3617 et MiniPavi en TCP, puis `wss://3617.fr/ws` en WebSocket (serveur
`3`) : > 200 octets reçus et une page allumée.

`tests/test_servers.sh` : session complète sur **PAVI 3617** puis **MiniPavi**
par le faux modem (relais TCP réel), 1,5 G cycles chacune (Phosphoneo va
~6 fois plus vite que le temps réel : ~10 s), succès si > 200 octets reçus
et une page allumée. SKIP si le serveur est injoignable. Vérifié le
2026-09-17 : les deux pages d'accueil s'affichent correctement.

## Ce qui n'est pas testé

- **La carte réelle** : aucun Neo6502 physique n'était disponible. Les
  risques spécifiques (latence réelle des appels API, débit du CDC, tenue
  du tampon RX de 1 Ko à haut débit, comportement d'un vrai
  PicoWiFiModemUSB) sont listés dans le ROADMAP.
- Le mode Wi-Fi (`AT$SCAN`…) n'est joué que par le faux modem (réponses
  fictives), pas contre un Pico.
