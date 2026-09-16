# CHANGELOG — NeoTel

Toutes les modifications notables sont consignées ici (format Keep a
Changelog, versions SemVer). Auteur : bmarty <bmarty@mailo.com>.

## [Non publié]

### Ajouté
- `docs/ref/STUM2-NOTES.md` : extraits relus de la STUM Minitel 2 (France
  Télécom, février 1991, scan de wiki.labomedia.org, SHA-256 consignée) :
  identification ROM (annexe 6.6), DRCS (§2.3), associations de jeux,
  nouvelles séquences Vidéotex / mixte / téléinformatique, vitesses.
  `docs/ref/STUM2-ocr.txt` : OCR des 118 pages (recherche plein texte). Le
  PDF (23 Mo) reste local, hors dépôt.

### Modifié
- `docs/MINITEL_1B_VS_2.md` : les octets de type `u`/`v` et le 9600 bauds
  du Minitel 2 passent de « non vérifié » à **vérifié** ; DRCS désormais
  spécifié et planifié (ROADMAP v0.3), séquences 80 colonnes relevées.
- `src/terminal.h` : références STUM 2 dans les commentaires.

## [0.1.0] — 2026-09-17

Première version : portage d'OricTel (Oric 1/Atmos) sur le Neo6502.

### Ajouté
- `src/videotex.c/h`, `fonts.c/h`, `at_modem.c/h`, `ui.c/h` repris
  d'OricTel (EUPL-1.2, même auteur) ; `videotex.c` appelle le profil
  terminal pour l'identification et PRO2 PROG, et compte les octets décodés
  (`g_vtx_bytes`) pour les tests cible.
- `src/display.c` + `src/asm/display_asm.s` : rendu 40 × 25 cellules de
  8 × 9 pixels dans un tampon de ligne, copié en VRAM par le blitter (12,3) ;
  mosaïques G1 8 × 9 en cache, doubles tailles, curseur, clignotement animé
  (`display_blink_toggle`), ligne de statut, palettes couleur / gris (1B mono).
  `blit_run` en assembleur (~60 000 cycles par ligne, contre ~250 000 en C).
- `src/keyboard.c/h` : file clavier du firmware, F1-F10 par hotkeys (2,4),
  flèches distinguées de CTRL+A/D/S/W par l'état HID (1,2), `keyboard_inject`
  pour les tests.
- `src/serial.c/h` : API UART (groupe 10) avec routage AUTO vers le modem
  USB CDC (fork bmarty, F-90/F-93), présence CDC par 14,1.
- `src/neo_gfx.c/h`, `src/neo_time.c/h`, `src/asm/neo_delay.s`, `src/neo.h`,
  `src/asm/crt0.s`, `cfg/neo6502.cfg` : couche Neo6502.
- `src/terminal.c/h` : profils Minitel 1B / Minitel 2 (identification
  désactivée par défaut, vitesses PRO2 PROG, 9600 réservé au Minitel 2).
- `src/main.c` : splash, écran « Liaison série », menu (modem, Wi-Fi,
  profil, aspect, identification, ESC → NeoBASIC), serveurs, connexion AT,
  session, ESC ESC, perte de porteuse ; états `g_dbg_state` pour les tests.
- `tools/fake_modem.py` : modem Hayes sur pty (vraies sockets TCP ou page
  de test locale, NO CARRIER programmable, garde réglable) ;
  `tools/run_emu.sh`, `tools/mkneo.py`.
- Tests hôte (`tests/host/`, 379 assertions) avec `neo_stub.c` (Neo6502
  logiciel) : décodeur, modem AT, UI, clavier, affichage, profil / série ;
  `render_page` (oracle) ; fuzzer libFuzzer.
- Tests cible (`tests/run.sh`, Phosphoneo + faux modem) : session, ATDT,
  capture == oracle hôte (asm == C), référence, ESC ESC, NO CARRIER, sortie
  NeoBASIC, fumée `neo`.
- Documentation : README, CLAUDE.md, docs/ARCHITECTURE, MANUEL_UTILISATION,
  MINITEL_1B_VS_2, TESTS, AGILE_PLAN, ROADMAP, LICENSE (EUPL-1.2).

### Non vérifié
- Aucune exécution sur une carte Neo6502 physique (voir ROADMAP v0.2).
- Octets de type de l'identification ENQROM (`u`/`v`), constructeur `C`.
