# CHANGELOG — NeoTel

Toutes les modifications notables sont consignées ici (format Keep a
Changelog, versions SemVer). Auteur : bmarty <bmarty@mailo.com>.

## [0.7.2] — 2026-09-17

### Modifié
- **Passage à la ligne différé (auto-wrap ISO 6429)** en 80 colonnes
  (`teleinfo.c`) : après la 80ᵉ écriture d'une rangée, le curseur reste au
  bord droit (`cur_x = cols`, état en attente) ; c'est le **caractère
  suivant** qui passe à la ligne, non la 80ᵉ écriture. Une rangée remplie
  puis un `CR` ne saute donc plus une rangée (l'auto-wrap immédiat avançait
  déjà d'une rangée). Tout déplacement explicite du curseur ramène `cur_x`
  à une valeur `< cols` et annule l'attente ; le curseur n'est pas dessiné
  pendant l'attente (colonne 80 hors écran), la demande de position (`CSI
  6n`) borne la colonne à 80. La STUM 1B/2 ne décrit pas ce cas ; ce
  comportement est celui de l'ISO 6429 (mode que ce moteur implémente),
  cohérent avec les `CSI` de déplacement qui « s'arrêtent au bord droit »
  (p. 168) — l'ancien code le marquait « hypothèse ». Non vérifié sur un
  Minitel physique.
- `test_teleinfo` : 4 cas d'auto-wrap différé (rangée pleine + CR sans
  rangée sautée, 81ᵉ caractère, `CSI D`). Référence visuelle `mixte.ppm`
  régénérée (le rendu 80 colonnes ne saute plus la rangée après la règle).



### Ajouté
- **Suppression d'un enregistrement** depuis le menu `6` : la touche `Suppr`
  (ou `Correction`) arme l'effacement, la lettre choisit le fichier
  `neoNN.vdt` à supprimer (API `3,13`), la liste se rafraîchit ; `ESC`
  annule l'armement. Geste en deux temps (action destructive).
  `record_delete()` dans `record.c` ; `test_record` +4 (233), stub hôte
  `3,13`, scénario cible **recdel**.

### Modifié
- Pile C ramenée de 256 à **96 octets** (`$FBA0-$FBFF`) : usage réel mesuré
  sur les dumps RAM (session **et** relecture) = 33 octets ; libère 160
  octets pour la BSS (le menu de relecture était à ~140 octets du plafond).
  `PASS stack` vérifie `$FBA0-$FBCF` vierge (marge >= 48 o).
- Le rafraîchissement de la liste après suppression passe par une boucle
  (pas de récursion : supprimer en série ne pouvait plus empiler).



### Ajouté
- **Liste des enregistrements** dans le menu `6` : au lieu de saisir un nom,
  NeoTel énumère les `neoNN.vdt` de la carte (API répertoire 3,17 / 3,18 /
  3,19) et les affiche triés, chacun choisi par une lettre (`A`..`L`) ;
  `ENVOI` rejoue le dernier, `ESC` renonce. Sans enregistrement, on retombe
  sur la saisie libre. `record_list()` dans `record.c` ; le stub hôte gagne
  l'énumération par répertoire ; `test_record` +3 (229 assertions) ;
  scénario cible **reclist** (trois `.vdt` déposés, la lettre `B` rejoue le
  second).

### Corrigé
- **Tampon de l'API 3,18 (Read Directory)** : son premier octet doit contenir
  la **capacité** en entrée (`DSPSetStdString` écrit `min(capacité, longueur)`)
  — il était mis à 0, donc le nom revenait vide et la liste paraissait
  toujours absente. Corrigé (`RECORD_CHUNK - 1`) ; le stub hôte respecte
  désormais aussi cette capacité (il l'ignorait, masquant le bug).
- `NEO_SET_ADDR0` (pointeur en P0,P1) : les fonctions répertoire prennent le
  nom en P0,P1, pas en P1,P2 comme l'ouverture de fichier.



### Corrigé
- **Identification manquée en début de page** : l'octet reçu juste après le
  `CONNECT` était passé à un décodeur Videotex remis à zéro avant l'entrée
  en session ; un `ESC` de tête (donc l'`ENQROM` `ESC 9 {` que MiniPavi et
  3617.fr envoient à l'ouverture) était perdu et le terminal ne
  s'identifiait pas, même identification activée (menu `5`). Cet octet est
  désormais gardé (`s_first_byte`) et rejoué comme premier octet de session
  (enregistrement compris). Vérifié sur les serveurs réels : PAVI 3617 et
  MiniPavi (TCP), 3617.fr (WebSocket) reçoivent `SOH C u 1 EOT`.

### Ajouté
- Scénario cible **enqrom** : page précédée de `ESC 9 {`
  (`tests/page_enqrom.vdt`), identification activée par les réglages ; le
  faux modem doit voir la réponse `\x01Cu1\x04` et la page est décodée.



### Ajouté
- Menu `7 - Son: ON/OFF` : coupe les bips (BEL Videotex et 80 colonnes,
  bip du splash) ; réglage `sound` dans `neotel.cfg` (**version 3** ; les
  fichiers v1 et v2 restent acceptés, son actif par défaut). Un bip de
  confirmation à la remise en marche. +2 tests `test_display`, +2
  `test_settings` ; le stub compte aussi les tonalités 8,7.



### Ajouté
- `make bench` : banc de rendu sur cible (`tests/emu/t_bench.c`,
  `tests/bench.sh`, `tools/vdt2c.py`) — cycles d'une page complète (vide,
  960 lettres, 960 mosaïques, page de test) lus entre deux marqueurs 5,37
  dans le journal API de Phosphoneo.

### Modifié
- **Rendu Videotex 2 à 3 fois plus rapide** (profil par trace Phosphoneo) :
  - `blit_run` mémorise par colonne (`rb_state`, 40 o) qu'une cellule vide
    de fond B est déjà dans le tampon de ligne : une cellule vide identique
    n'est pas redessinée (72 écritures). Le C invalide la colonne quand il
    écrit lui-même (`blit_cell9`, barre curseur) et tout au `display_init()`
    (le mode 1 partage le tampon). L'oracle hôte n'a pas ce cache : les
    tests capture == oracle prouvent qu'il ne change rien à l'image ;
  - l'espace ne lit plus son glyphe ;
  - les moitiés hautes des doubles hauteurs de la rangée du dessous sont
    cherchées en assembleur (`scan_dblh`) : la boucle C coûtait 7 800
    cycles par rangée (indexation ×6 de chaque cellule).
  Mesures (`make bench`, 6,25 MHz) : page vide 236 → 70 ms, page de test
  273 → 129 ms, 960 lettres 318 → 248 ms, 960 mosaïques 312 → 245 ms.



### Ajouté
- **Serveurs WebSocket** : `ATDT ws://…` / `wss://…` est relayé par
  `tools/fake_modem.py` (module python3 `websockets`, sous-protocole
  `binary`, messages texte ramenés sur 7 bits comme le bridge d'OricTel ;
  deux fils et une `socketpair`, la boucle principale voit une connexion TCP
  ordinaire : `NO CARRIER` à la fermeture distante, `ATH` ferme le
  WebSocket). Serveur prédéfini `3` = `wss://3617.fr/ws` (vérifié : même
  page d'accueil que PAVI 3617, `ws://` y répond 308 vers `wss://`), saisie
  libre passée en `4` (accepte `hôte:port` ou `ws://`). **Non vérifié sur un
  PicoWiFiModemUSB réel** (son firmware AT ne documente pas de WebSocket).
- `tools/ws_page_server.py` : serveur Minitel WebSocket minimal (port libre
  écrit dans un fichier, envoie une page `.vdt` à la connexion, journal,
  `--echo`).
- Tests : scénario cible **ws** (réglages préremplis avec
  `ws://127.0.0.1:PORT`, menu 1 + ENVOI, page reçue par WebSocket ; SKIP sans
  `websockets`) ; `make test-servers` joue aussi `wss://3617.fr/ws`
  (**PASS** : 667 octets, page Télétel affichée).



### Modifié
- **Cache G1 supprimé** (1 152 octets de BSS) : les mosaïques sont calculées
  à la volée, en C (`mosaic_pattern` dans `g1_pattern`) et en assembleur
  (`display_asm.s`, table `g1_tbl` de 8 octets : ligne de pixels selon les
  deux blocs d'une rangée, contigu / séparé). Rendu identique (capture ==
  oracle sur les 4 pages de référence). Marge BSS : ~1,6 Ko.
- `tests/run.sh` : capture de la page à 38 M cycles au lieu de 39 M, au
  milieu d'une phase de clignotement (période 3,1 M cycles) plutôt qu'à
  0,5 M d'une frontière ; `tests/ref/page.ppm` régénérée (autre phase,
  même rendu — seules les autres phases diffèrent, vérifié contre l'oracle).



### Ajouté
- **Enregistrement des pages reçues** (`src/record.c`) : `CTRL+O` en session
  ouvre `neoNN.vdt` (NN = 01..99, compteur `rec_index` dans `neotel.cfg`)
  par l'API fichiers (3,4 création, 3,9 écriture par blocs de 64 octets,
  3,5 fermeture) et y copie tel quel tout octet reçu du serveur ; `CTRL+O`
  de nouveau, ESC ESC ou une perte de porteuse arrêtent. Indicateur `RE`
  (rouge, inversé) en bas à droite de la barre de statut. Format = flux
  Videotex brut, le même que les pages `tests/*.vdt`.
- **Relecture** : menu `6 - Relire neoNN.vdt` (ENVOI = dernier enregistré,
  ou un nom saisi). Le fichier est rejoué par la boucle de session elle-même
  (source = `record.c` au lieu de la liaison ; mode Mixte compris), la barre
  de statut affiche le nom ; ESC ESC revient au menu sans `+++`/`ATH`.
- Tests : `tests/host/test_record.c` (225 assertions : noms, blocs de 64,
  vidage à l'arrêt, relecture jusqu'à la fin, fichier absent, création
  refusée, exclusion enregistrement/relecture, canaux fermés) sur un disque
  en mémoire ajouté à `neo_stub.c` (3,4/5/8/9 par canal, `neo_host_ptr`
  pour les adresses 16 bits) ; scénarios cible **record** (`neo01.vdt` ==
  page reçue, octet pour octet) et **replay** (menu 6 → page décodée) ;
  `tools/fake_modem.py --on-rx` (page envoyée au premier octet tapé, pour
  armer l'enregistrement avant, quelle que soit la vitesse de l'émulateur)
  et `--delay S`.
- `NEO_SET_ADDR(p)` dans `neo.h` (adresse 16 bits dans P1-2 ; sur l'hôte,
  pointeur réel via `neo_host_ptr`).

### Modifié
- `neotel.cfg` version 2 (+ `rec_index`) ; un fichier version 1 est accepté
  (compteur à 0), > 99 remis à 0 (+2 tests).
- RAM : les tampons de la page Wi-Fi (312 o) logent dans `vtx.drcs`, remis
  à zéro par `vtx_init()` avant chaque session ; pile C ramenée à **256
  octets** (`$FB00-$FBFF`, usage mesuré 34 o) ; `PASS stack` vérifie
  `$FB00-$FB7F` vierge. Marge BSS : ~330 octets (`grep BSS build/neotel.map`).
- `tests/run.sh` : les valeurs de `--dump-ram-when` sont en **hexadécimal**
  (`ST_EXIT=0A`, `ST_HUNGUP=0C`, `ST_REPLAY_END=0F`) — les anciennes
  constantes décimales 10/12 n'étaient utilisées par aucun scénario.



### Modifié
- Pile C ramenée de 1 Ko à **512 octets** (`$FA00-$FBFF`) : avec
  `--static-locals` elle ne porte que les paramètres ; usage mesuré sur les
  dumps RAM des tests = 34 octets. La RAM programme monte à `$F9FF` (marge
  BSS : ~1,4 Ko). `tests/run.sh` vérifie à chaque exécution que la moitié
  basse de la pile reste vierge (`PASS stack`).
- `BEL` (Videotex et 80 colonnes) et bip du splash : tonalité 1 kHz / 100 ms
  / carré / 60 % par 8,7 au lieu du jingle système 8,3 (choix de NeoTel, la
  STUM ne fixe ni fréquence ni durée). Vérifié dans le WAV de Phosphoneo.

## [0.4.2] — 2026-09-17

### Ajouté
- `tests/host/fuzz_teleinfo.c` : fuzzer libFuzzer + ASan du décodeur 80
  colonnes et du rendu (118 000 cas / 30 s sans défaut). `fuzz_videotex`
  rejoué (1 M de cas) après DRCS et mode Mixte.
- `docs/ARCHITECTURE.md` : carte mémoire mise à jour (v0.4.2).
- Format 40 colonnes du mode Mixte (STUM 2 `CSI < 3 h`) : **pixels doublés**
  (cellules de 18 pixels = deux créneaux de 9 bits), en assembleur et dans
  l'oracle hôte ; curseur sur 18 pixels. Scénario cible `mixte40` (capture ==
  oracle, référence `tests/ref/mixte40.ppm`), page `tests/page_mixte40.vdt`.

## [0.4.1] — 2026-09-17

### Ajouté
- Mode Mixte, Minitel 2 : jeux **complémentaire** (STUM 2 annexe 3.12,
  accents, ¼ ½ ¾, flèches, Œ œ ß…) et **DEC** (annexe 3.13, filets
  ┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼ ─ │, ≤ ≥ ≠) désignés par `ESC 2/8|2/9 3/3` et `3/0` ;
  `ESC 2/8|2/9 4/2` (américain) et `5/2` (français) dans les deux profils.
  Le jeu est porté par la cellule (bits 4 et 6 de l'attribut). 57 glyphes de
  plus dans `font80.c` (tables relues sur les scans p. 92-93). Approximations
  documentées : traits de balayage DEC 6/F-7/3 = trait médian, 6/1 = +.
- `CSI < 1 h` / `CSI < 1 l` : extinction / allumage du curseur (STUM 2 §3.3,
  Minitel 2 seulement).
- `make test-servers` : fumée sur les vrais serveurs **PAVI 3617** et
  **MiniPavi** par le faux modem (relais TCP) — réseau requis, hors
  `make test` ; Phosphoneo va ~6 fois plus vite que le temps réel, d'où
  1,5 G cycles par session. Les deux pages d'accueil s'affichent.
- `test_teleinfo` : 87 assertions ; scénario `mixte` en profil Minitel 2 avec
  une ligne DEC / complémentaire.

### Corrigé
- `display80_asm.s` : l'adresse du glyphe était calculée par une table de
  mots indexée par `index * 2` sur 8 bits — faux au-delà du glyphe 127
  (visible dès l'ajout des jeux). Calcul 16 bits `index * 14`.
- `tests/run.sh` : l'oracle `render_page` est recompilé à chaque exécution
  (un oracle périmé donnait un faux FAIL).

## [0.4.0] — 2026-09-17

Mode Mixte / standard Téléinformatique : écran **80 colonnes ISO 6429**
(STUM 1B partie 3 chapitre 2, STUM 2 §3), sur le mode vidéo Hercules du
firmware bmarty (720 × 350, 1 bpp, cellules 9 × 14).

### Ajouté
- `src/teleinfo.c/h` : décodeur ISO 6429 des rangées 01-24 + rangée 00 en
  Videotex réduit (STUM 1B p. 160-170) : C0 (BS HT LF VT FF CR SO SI BEL CAN
  SUB NUL, resynchronisation), ESC D/E/M/7/8/c, CSI A B C D H J K @ L M P,
  SM4/RM4 (insertion), CSI Ps m (surintensité, souligné, clignotant, inverse
  et leurs négations, un seul paramètre interprété comme sur Telic/Matra),
  rouleau / page, filtrage des séquences inconnues, `US 4/0 X/Y`… `LF`
  (rangée 00 : attributs Videotex avalés, semi-graphiques → espaces, SS2,
  REP), `CSI ? {` (retour Videotex, SEP $71), STUM 2 : `CSI 6 n` (Minitel 2),
  `CSI < 3 h` / `CSI ? 3 l` (40 / 80 colonnes, écran réinitialisé),
  `CSI < 4 h|l` (page / rouleau, `l` = hypothèse symétrique). Hypothèse
  signalée : passage à la rangée suivante après la 80ᵉ colonne.
- `src/display80.c/h` + `src/asm/display80_asm.s` : rendu 1 bpp dans le
  tampon de rangée (14 × 90 octets, partagé avec le mode 0) puis blit ;
  attributs (inverse 9 pixels, souligné 14ᵉ ligne, gras double frappe,
  clignotement, curseur tiret), rangée 00 sans attribut, format 40 colonnes
  (pas de 18 px, glyphe non doublé). Composition en assembleur : ~67 000
  cycles par rangée (11 ms) contre ~400 000 en C. Mode 1 absent (firmware
  amont) : PRO2 MIXTE 1 est refusé avec un message, sans planter.
- `src/font80.c/h` + `tools/gen_font80.py` : police 8 × 14 (Lat15-VGA14,
  console-setup, domaine public), 96 ASCII + jeu français NF Z 62-010 (STUM 1B
  tableau 4 relu sur `docs/ref/stum1b-img/p171.svg`) + pavé d'erreur.
- `videotex.c` : PRO2 MIXTE 1 (`ESC 3/A 3/2 7/D`) passe en mode Mixte et
  acquitte `SEP $70` (STUM 1B p. 3347) — OricTel le refusait ; PRO2 MIXTE 2
  acquitte `SEP $71` et revient (page effacée).
- `main.c` : bascule d'écran au même octet que la commande (`session_byte`),
  filtre Protocole en mode Mixte (`ESC 3/9-3/B` + 1-3 octets → décodeur
  Videotex : aiguillages, retour), question ESC sur la rangée 00, sortie de
  session et perte de porteuse depuis le mode Mixte, pas de barre de statut
  en mode 1. Le contexte 80 colonnes (4 Ko) est logé dans `vtx.screen`.
- `keyboard.c` : clavier étendu (STUM 1B p. 3387) en mode Mixte : CTRL+lettre
  = C0, Entrée = CR, Suppr = BS, flèches = CSI sans mode curseur ; F1-F10 et
  ESC inchangés.
- `neo_gfx` : `gfx_set_mode` (5,9), `gfx_blit_ex` (pas et offset libres).
- Tests : `test_teleinfo` (79 assertions : décodeur + pixels du tampon),
  `test_keyboard` +11 (clavier étendu), `test_videotex` mis à jour (MIXTE
  acquitté) ; page `tests/page_mixte.vdt` ; scénarios cible `mixte` (capture
  720 × 350 **identique** à l'oracle hôte `render_page --mixte`, asm == C),
  `mixte-ref`, `mixte-exit` (ESC ESC en 80 colonnes → raccrochage, mode 0).
- `docs/ref/STUM1B-NOTES.md`, `stum1b-img/` (tableaux des jeux).

### Corrigé
- Un `.o` de test (`tests/emu/*.c`) est construit à la main ; `t_blit1` et
  `t_d80` servent à isoler le blitter et le rendu en mode 1.

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
