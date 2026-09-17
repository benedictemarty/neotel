# Architecture technique — NeoTel

## Vue d'ensemble

NeoTel est un programme 65C02 (cc65) chargé à `$0800` sur le Neo6502. Il ne
touche aucun périphérique directement : écran, clavier, liaison série, son
et temps passent par l'**API du firmware** (bloc de contrôle `$FF00-$FF0B`,
exécutée par le RP2040). Le protocole Minitel (Videotex, STUM 1B) et la
logique de session sont ceux d'OricTel.

```
+----------------------------------------------------------------------+
| main.c        cycle menus -> connexion AT -> session (ESC = retour)  |
|   ui.c        helpers de menus (ecriture page, saisie bornee)        |
|   at_modem.c  ATZ / ATI / ATDT, +++ ATH, veille "NO CARRIER"         |
|   terminal.c  profil Minitel 1B / Minitel 2 (ident, vitesses)        |
|   settings.c  neotel.cfg sur la carte (3,2 / 3,3) : profil, aspect...|
|   teleinfo.c  ecran 80 col. ISO 6429 (mode Mixte / Teleinformatique) |
|   display80.c + display80_asm.s  rendu 1 bpp 720x350 (mode video 1) |
|   font80.c    police 8x14 ASCII + jeu francais NF Z 62-010            |
|   videotex.c  machine a etats Videotex -> screen[25][40] + dirty     |
|   fonts.c     G0 (ASCII + accents), G2 (CEPT), 6x8                   |
|   display.c   lignes sales -> tampon de ligne 320x9 -> blitter       |
|   display_asm.s  blit_run / blit_cell9 : cellules -> pixels          |
|   keyboard.c  file clavier du firmware -> touches Minitel            |
|   serial.c    UART API (10,15-10,18), routage CDC (10,19)            |
|   neo_gfx.c   2,12 / 2,19 / 5,1 / 5,3 / 5,32 / 12,3                   |
|   neo_time.c  timer 100 Hz (1,1), bip (8,3) ; neo_delay.s           |
|   crt0.s      pile, BSS, main(), retour NeoBASIC (1,3)               |
+----------------------------------------------------------------------+
        | bloc $FF00 (groupe / fonction / erreur / P0-P7)
+----------------------------------------------------------------------+
| RP2040 : firmware Neo6502 (fork bmarty)                              |
|   console, graphisme 320x240x256, blitter, clavier USB, timer,       |
|   UART UEXT, USB CDC (groupe 14, F-90), routage UART -> CDC (F-93)   |
+----------------------------------------------------------------------+
        | USB CDC                                    | UART UEXT
   PicoWiFiModemUSB (modem Hayes Wi-Fi)          modem AT quelconque
        | TCP
   serveur Minitel
```

## Carte mémoire (cfg/neo6502.cfg)

```
$0000-$00FF  page zero (cc65 + 17 octets de display_asm.s)
$0100-$01FF  pile 65C02
$0800-       STARTUP, CODE (~22 Ko), RODATA (polices, tables), DATA
             BSS : contexte Videotex (8 Ko dont 1 880 o de DRCS), tampon de ligne (2 880 o),
                   cache G1 (1 152 o), page Wi-Fi, etc.  Fin ~ $9E00
$F800-$FBFF  pile C cc65 (1 Ko)
$FC00-$FFFF  noyau 6502 du firmware ; $FF00-$FF0F bloc de contrôle API
```

`build/neotel.map` donne les tailles exactes, `build/neotel.lbl` les
symboles (les tests cible y lisent `_keyboard_inject`, `_g_dbg_state`,
`_g_vtx_bytes`, `_g_dbg_hangups`, `_vtx`). Les dumps RAM sont lus avec
`offsetof(vtx_context_t, screen)` calculé sur l'hôte : la structure ne doit
contenir que des `unsigned char` / `unsigned short` (même taille sur cc65).

## Affichage (display.c, display_asm.s)

- **Géométrie** : 40 × 25 cellules de **8 × 9 pixels** = 320 × 225, en haut
  de l'écran 320 × 240 ; une **ligne de statut** de 9 pixels en y = 230.
  Les glyphes G0/G2 d'OricTel (6 × 8, bits 5-0) sont centrés (colonnes
  1-6, lignes 0-7) ; la ligne 8 porte le souligné. Les mosaïques G1 sont
  générées en natif 8 × 9 (blocs de 4 × 3 ; séparées : 3 × 2 + interstice
  droite/bas ; `$60` = trait plein sur la ligne 0, comme OricTel d'après
  une capture de Minitel réel) et mises en cache (128 motifs × 2 modes).
- **Couleurs** : 1 octet par pixel = index de palette. NeoTel programme les
  index 0-7 (5,32) selon l'**aspect** : couleurs Videotex, ou 8 niveaux de
  gris dans l'ordre de luminance Rec. 601 (noir < bleu < rouge < magenta <
  vert < cyan < jaune < blanc) pour l'écran monochrome d'un Minitel 1B.
  Changer d'aspect = 8 écritures de palette, aucun re-rendu.
- **Rendu** : les lignes sales (`dirty[]`, plage `dirty_min/max` héritées
  d'OricTel) sont dessinées dans un **tampon de ligne** en RAM 6502
  (9 × 320 octets) puis copiées dans la VRAM par le **blitter** (12,3,
  copie rectangulaire, pas 320, page `$80/$81`). Une ligne = un appel API.
  Budget : une ligne par `display_render()`, la boucle principale rappelle
  tant qu'il reste des lignes sales et que rien n'attend (série, clavier).
- **`blit_run`** (assembleur) rend une course de cellules de taille
  normale : lecture de la cellule (6 octets), inversion, masquage,
  clignotement, glyphe G0 (adresse calculée), G1 (cache), G2 (appel C
  `font_get_g2`), souligné, puis 9 lignes : fond (8 `sta abs,x`) et encre
  sur les bits à 1. X = `col*8` déborde 255 à partir de la colonne 32 :
  deux copies du bloc (base et base+256). ~60 000 cycles par ligne de 40
  cellules (mesuré sous Phosphoneo, `--api-log 12`), soit ~10 ms ; une page
  complète ~250 ms. Les doubles tailles restent en C (`render_cell_into_row`
  + `blit_cell9`).
- **Doubles hauteurs** : comme sur le Minitel, la cellule occupe sa ligne
  (moitié basse) et la ligne du dessus (moitié haute). Le rendu d'une ligne
  r superpose donc les moitiés hautes des cellules double hauteur de r+1 ;
  `put_char` salit r-1 quand il écrit une double hauteur en r.
- **Curseur** : barre d'encre sur la ligne 8 de la cellule, redessinée avec
  la cellule (l'ancienne position est salie quand il bouge ou clignote).
- **Preuve asm == C** : `tests/host/render_page` rend la page de test avec
  le chemin C (hôte) ; `tests/run.sh` exige que la capture Phosphoneo
  (chemin assembleur) soit identique pixel pour pixel (zone page).

## Écran 80 colonnes (teleinfo.c, display80.c, display80_asm.s)

- Contexte `ti_context_t` : 25 × 80 cellules (code + attributs, 4 Ko),
  **logé dans `vtx.screen`** (6 Ko, inutilisé en mode Mixte ; la RAM ne
  permet pas les deux écrans côte à côte). Garde-fou `sizeof` à la
  compilation dans `main.c`.
- Mode vidéo 1 du fork (F-52) : 720 × 350, 1 bpp MSB à gauche, 90 octets
  par ligne, cellules 9 × 14. Une rangée est composée dans `display_rowbuf`
  (14 × 90 octets) par `blit80_row` (asm) : octet `b = col + col/8`,
  décalage `s = col & 7`, octet haut `g >> s`, bas `g << (8-s)` par chaînes
  de LSR/ASL déroulées à point d'entrée patché, 9ᵉ pixel `$80 >> s` ; puis
  blit (12,3) à l'offset `row × 1260`. Mesuré : ~67 000 cycles par rangée.
  Le C de `display80.c` (même algorithme) ne sert que d'oracle sur l'hôte.
- Bascule : `session_byte` route chaque octet au décodeur du mode courant
  et change d'écran **au même octet** ; en mode Mixte les séquences
  Protocole (`ESC 3/9-3/B` + n) vont au décodeur Videotex (`mixte_byte`).

## Clavier (keyboard.c)

Lecture de la file du firmware (2,1 / 2,2). Le firmware donne l'ASCII, les
touches de contrôle en `$01-$1F`, et **les flèches avec les mêmes codes que
CTRL+A/D/S/W** (`CC_LEFT` = 1, `CC_RIGHT` = 4, `CC_DOWN` = $13, `CC_UP` =
$17) : NeoTel les distingue par l'état physique de la touche (1,2 sur les
codes HID `$4F-$52`). Les touches **F1-F10** sont programmées par 2,4 pour
émettre un octet privé `$81-$8A`. `keyboard_inject` est une case mémoire
lue avant la file : les tests cible y déposent des touches (`--poke-at`).

## Liaison série (serial.c)

Groupe 10 : 10,19 routage (AUTO = modem USB CDC si présent, sinon UART
UEXT), 10,15 format (115200 8N1, ignoré par les modems USB), 10,16 écriture,
10,18 disponibilité, 10,17 lecture (erreur 1 si rien). 14,1 sert seulement
à afficher la présence d'un modem USB (`serial_cdc_status` : présent,
absent, ou non supporté sur le firmware amont où l'appel lève le drapeau
d'erreur).
L'émission est immédiate (tampon TX du firmware) : `serial_tx_pump/flush`
sont vides mais conservés pour garder `videotex.c` / `at_modem.c` intacts.

## Base de temps (neo_time.c, neo_delay.s)

Timer 100 Hz de l'API (1,1) : `tick_10ms()` rend les tics écoulés (chrono de
session, confirmation de perte de porteuse 4 s, indicateur C/F 30 s,
clignotement 500 ms). Les délais courts du dialogue AT (`AT_POLL_MS` = 2)
sont des boucles calibrées à 6,25 MHz (`neo_delay_ms`).

## Profil terminal (terminal.c)

Voir [MINITEL_1B_VS_2.md](MINITEL_1B_VS_2.md). Identification (SOH,
constructeur, type `u`/`v`, version, EOT) désactivée par défaut ; PRO2
PROG `$6B` mémorise la vitesse si le modèle l'accepte (9600 = Minitel 2).

## Protocole Videotex

Machine à états de `videotex.c`, inchangée par rapport à OricTel (voir
`docs/ARCHITECTURE.md` d'OricTel pour le diagramme) : C0, ESC (couleurs,
tailles, flash, masquage, souligné/séparé, inversion, CSI, PRO1/2/3, SS2
accents), US, REP, SEP, masque global, modes rouleau/minuscules,
aiguillages, ACK PRO3. Ajouts NeoTel : `g_vtx_bytes` (compteur pour les
tests), PRO2 PROG → `term_prog_speed`, identification → `term_send_ident`,
et, dans le profil Minitel 2 (STUM 2, `docs/ref/STUM2-NOTES.md`) : jeux
DRCS G'0/G'1 (états `DRCS_HDR`/`DRCS_XFER` traités AVANT la resynchronisation
ESC, formes 8 × 10 dans le contexte, associations `ESC 2/8|2/9`, cellules
`CHARSET_DRCS0/1`), `CSI 6n`. Le rendu d'une cellule DRCS passe par
`drcs_pattern9` (display.c, rangées 9 et 10 fusionnées) que `blit_run`
appelle comme `font_get_g2`.

## Tests

Voir [TESTS.md](TESTS.md).
