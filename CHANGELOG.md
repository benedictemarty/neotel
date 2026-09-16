# CHANGELOG — NeoTel

Toutes les modifications notables sont consignées ici (format Keep a
Changelog, versions SemVer). Auteur : bmarty <bmarty@mailo.com>.

## [0.3.0] — 2026-09-17

### Ajouté
- `src/settings.c/h` : réglages persistants dans `neotel.cfg` (carte SD /
  clé USB, API fichiers 3,2 / 3,3) : profil terminal, aspect, identification,
  dernier serveur (index ou saisie libre). Chargés au démarrage, sauvés à
  chaque changement au menu et à chaque choix de serveur ; au menu Serveur,
  `ENVOI` reprend le dernier serveur. Fichier absent ou corrompu : défauts
  et valeurs bornées, sans message. Dépôt distant `github.com/benedictemarty/neotel`.
- Tests : `test_settings` (7 assertions, fichier en mémoire dans `neo_stub`),
  scénarios cible `settings-save` / `settings-load` (stockage Phosphoneo
  `--storage`, un répertoire neuf par scénario, `neotel.cfg` relu au second
  démarrage). `storage/` ignoré par git.

## [0.2.1] — 2026-09-17

### Ajouté
- DRCS : interruption d'un téléchargement (en-tête ou transfert) par un
  accès en rangée 00 et reprise sur le `LF` qui la quitte ; sortie par `FF`
  ou `RS` sans compléter la forme ; sortie par `US` vers une autre rangée en
  la complétant (STUM 2 §2.3.4). 7 assertions de plus dans `test_drcs`.
- `docs/ref/STUM1B-NOTES.md`, `docs/ref/STUM1B.html` / `.txt` : STUM
  Minitel 1B (transcription jbellue). Constat : le mode Mixte 80 colonnes
  existe déjà sur le 1B (partie 2, p. 106) ; le scan archive.org n'a pas pu
  être téléchargé (404).
- `test_drcs` vérifie la disposition de `vtx_context_t` (offset pair de
  `drcs_acc`, offset de `screen` = somme des champs) : garde-fou contre le
  bourrage gcc qui casserait les dumps RAM des tests cible.

### Décidé
- Double hauteur d'une forme G'0 (STUM 2 §2.3.6 : 1ʳᵉ ligne triplée, les
  autres doublées sauf la 10ᵉ) : en 8 × 9 (18 lignes pour 20), la règle se
  ramène à « rangées 1-9 doublées, 10ᵉ fusionnée à la 9ᵉ », c'est-à-dire la
  règle générale de NeoTel ; pas de code spécifique, écart documenté.

## [0.2.0] — 2026-09-17

Minitel 2 : jeux de caractères téléchargeables (DRCS), conformes à la STUM 2.

### Ajouté
- `videotex.c/h` : téléchargement DRCS (STUM 2 §2.3) — en-tête
  `US 2/3 2/0 2/0 2/0 4/2|4/3 4/9`, transfert `US 2/3 Y` + `B1` + 14 octets
  de 6 bits par forme (8 × 10), formes successives, B1 anticipé, excédent
  filtré, C0 / colonnes 2-3 = fond sans resynchronisation, sortie sur US,
  code > 7/E ignoré ; associations `ESC 2/8 4/0`, `ESC 2/8 2/0 4/2`,
  `ESC 2/9 6/3`, `ESC 2/9 2/0 4/3` (§2.2.2) ; 2/0 et 7/F restent au jeu de
  base ; accès en rangée 00 = jeux de base ; `SS2 <accent> X` en G'0 =
  forme X, SS2 ignoré si G1 invoqué (§2.3.7) ; jeux effacés par `vtx_init`
  (connexion). Nouveaux jeux de cellule `CHARSET_DRCS0/1`. 1 880 octets de
  BSS dans le contexte. Actif dans le profil **Minitel 2** seulement.
- `CSI 3/6 6/E` (demande de position curseur, STUM 2 §2.5) : réponse
  `CSI Pr;Pc R` (Pr rangée 0-24, Pc colonne 1-based comme `CSI H`), Minitel 2.
- `display.c` / `display_asm.s` : rendu des cellules DRCS (`drcs_pattern9`,
  appelé aussi par `blit_run`) ; la forme 8 × 10 devient 8 × 9 en fusionnant
  (OU) les rangées 9 et 10 ; le lignage n'a pas d'effet en G'1 (§2.3.6).
- Tests : `tests/host/test_drcs.c` (33 assertions, vecteur = exemple du
  §2.3.5 de la STUM 2), page `tests/page_drcs.vdt`, scénario cible `drcs`
  (profil Minitel 2 par le menu, capture == oracle hôte `render_page --m2`),
  référence `tests/ref/drcs.ppm`.
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
- `vtx_context_t.drcs_acc` en `unsigned short` : même taille sur cc65 et
  sur l'hôte, les dumps RAM des tests lisant `offsetof(screen)` côté hôte.

### Corrigé
- Makefiles : tout `.o` dépend des en-têtes. Sans cela, `main.o` compilé
  avec l'ancienne taille de `vtx_context_t` faisait chevaucher `vtx` et les
  statiques de `videotex.o` (boucle sans fin dans `reset_cells`).

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
