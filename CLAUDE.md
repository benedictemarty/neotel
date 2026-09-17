# CLAUDE.md — NeoTel

Terminal Minitel 1B / Minitel 2 pour Neo6502, portage d'OricTel (`~/orictel`).
Lire README.md, docs/ARCHITECTURE.md, ROADMAP.md et CHANGELOG.md avant
toute modification.

## Règles du projet
- Méthode agile : chaque modification = code + tests + CHANGELOG + docs à
  jour (README / docs / ROADMAP), puis commit `bmarty <bmarty@mailo.com>`,
  message en français, **jamais de Co-Authored-By ni de mention d'IA**.
- `make test` (hôte + cible) doit passer avant tout commit. Un changement
  visuel voulu se valide par `make ref` après inspection de
  `tests/out/page.ppm`.
- **Rien d'inventé** : ce qui n'est pas vérifié (STUM, carte réelle) est
  écrit « non vérifié » (docs/MINITEL_1B_VS_2.md, ROADMAP.md).
- Cible = firmware Neo6502 **fork bmarty** (`~/Neo6502firmware`) pour le
  modem USB ; le firmware amont doit continuer de fonctionner (UART UEXT).
- Ne pas modifier `videotex.c` / `at_modem.c` / `fonts.c` sans reporter la
  raison (ils sont partagés de fait avec OricTel : garder les tests repris).

## Chaîne d'outils
- cc65 2.19 (`cl65 -t none --cpu 65c02`), `-Wc --disable-opt,OptStackOps`
  obligatoire (bug cc65 documenté dans AsteroNeo).
- Phosphoneo (`~/Phosphoneo/build/phosphoneo`) : oracle déterministe
  (`--cycles`, `--poke-at`, `--dump-ram-when`, `--screenshot-at`, `--api-log`,
  `NEO_CDC_TTY`). `neo` (`~/Neo6502firmware/bin/neo`) : jeu et fumée.
- Faux modem : `tools/fake_modem.py` (pty, Hayes, TCP réel ou `--serve`,
  `--on-rx`, `--delay`, `--nc`, `--guard`).

## Pièges connus
- Les flèches arrivent avec les codes de CTRL+A/D/S/W : `keyboard.c` lit
  l'état HID (1,2) pour trancher.
- `10,13` (bloc UART) bloque jusqu'à 5 s : n'utiliser que 10,17/10,18.
- `BLTSimpleCopy` (12,2) fait un `printf` dans le firmware : utiliser 12,3.
- Le blitter n'a pas de contrôle de fin de destination : rester dans
  320 × 240 (`gfx_blit`).
- Un état de test (`g_dbg_state`) doit être posé APRÈS le rendu de l'écran,
  sinon le dump RAM ne contient pas la page.
- Phosphoneo va plus vite que le temps réel : garde Hayes du faux modem
  réduite dans les tests (`--guard 0.02`).
- `keyboard_flush` ne purge pas `keyboard_inject` (touche de test).
- `--poke-at` et `--dump-ram-when` de Phosphoneo lisent les valeurs en
  **hexadécimal** (« 15 » = 0x15) ; `--dump-ram-when` arrête l'émulation à
  la première occurrence (ne pas le combiner avec une action ultérieure).
- Sa vitesse dépend de la machine hôte (jusqu'à ~100× le temps réel à
  vide) : un test ne doit jamais dépendre d'un délai mural du faux modem
  (`--on-rx` déclenche la page sur un octet tapé). Le clignotement change
  de phase tous les 3,1 M cycles : capturer au milieu d'une phase.
- RAM : ~1,6 Ko de marge BSS en v0.5.1. Les gros tampons transitoires se
  logent dans `vtx` (écran 80 col. dans `vtx.screen`, page Wi-Fi dans
  `vtx.drcs`) ; toute division en C tire `mod.o`/`shelp.o` de la libc.
