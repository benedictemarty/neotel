# ROADMAP — NeoTel

## Vision
Un Minitel 1B (et 2) fidèle et agréable sur Neo6502, connecté aux serveurs
Minitel d'Internet, entièrement testé dans les émulateurs avant la carte.

## v0.1.0 — Sprint 1 [TERMINÉ, 2026-09-17]
- [x] Portage d'OricTel : décodeur Videotex, polices, modem AT, menus, UI.
- [x] Affichage 8 × 9 sur 320 × 240 (tampon de ligne + blitter), palette
      couleur / gris, ligne de statut, curseur, clignotement animé.
- [x] Course de cellules en assembleur (`blit_run`), ~10 ms par ligne.
- [x] Clavier USB : F1-F9 = touches Minitel, CTRL, flèches (HID), ESC.
- [x] Série : API UART routée vers le modem USB CDC (fork), UART UEXT sinon.
- [x] Profils Minitel 1B / Minitel 2 (identification, vitesses PRO2 PROG).
- [x] Tests : 379 assertions hôte, 8 scénarios cible (Phosphoneo + faux
      modem), preuve asm == C par capture, fumée dans `neo`, fuzzer.
- [x] Documentation complète (architecture, manuel, 1B vs 2, tests, agile).

## v0.2.0 — Minitel 2 : DRCS [TERMINÉ, 2026-09-17]
Préalable levé : la STUM Minitel 2 (France Télécom, 1991) est récupérée
(`docs/ref/STUM2-NOTES.md`, OCR `docs/ref/STUM2-ocr.txt`).
- [x] Vérifier les octets d'identification : `u` (7/5) = 1B, `v` (7/6) =
      Minitel 2, 9600 bauds réservé au Minitel 2 (annexe 6.6 p. 103).
- [x] Caractères redéfinissables DRCS G'0/G'1 (STUM 2 §2.3) : en-tête,
      transfert 14 octets × 6 bits, associations ESC 2/8|2/9 2/0 4/2|4/3,
      sortie sur US, SS2 en G'0 ; rendu 8 × 10 → 8 × 9 (rangées 9 et 10
      fusionnées) ; 33 tests hôte + page DRCS + capture cible == oracle.
- [x] Demande de position curseur `CSI 3/6 6/E` → `CSI Pr;Pc R` (§2.5).
- [x] Interruption / reprise du téléchargement par la rangée 00 (§2.3.4) — v0.2.1.
- [x] Double hauteur d'une forme G'0 (§2.3.6) : non applicable en 8 × 9,
      la règle générale s'y ramène (décision v0.2.1).

## v0.3 — Carte réelle et performance [PLANIFIÉ]
- [ ] Exécution sur Neo6502 physique + PicoWiFiModemUSB : session PAVI 3617
      de bout en bout, mesure de la latence des appels API, du débit CDC
      (RX 1 Ko du firmware, `tuh_task` à ~100 Hz : risque R16 de F-90) et du
      temps de rendu réel. Ajuster le budget de rendu si des octets se
      perdent à 115200 bauds.
- [x] Rendu : cellules vides déjà dans le tampon ignorées, `scan_dblh` en
      assembleur — page vide 70 ms, page de test 129 ms (`make bench`),
      v0.6.1.
- [ ] Rendu : une page dense reste à ~250 ms (1 500 cycles par cellule
      dessinée, dont 9 × 8 écritures) ; < 100 ms demanderait un dessin par
      table de nibbles ou un cache d'image par cellule.
- [x] Réglages persistants (profil, aspect, identification, dernier serveur)
      via l'API fichiers (3,3 / 3,2) sur la carte SD — v0.3.0.
- [x] Bip Videotex (BEL) par 8,7 plutôt que le bip système — v0.4.3.
- [x] Option pour couper le son (menu 7) — v0.6.2. Pas de jingle : le bip
      1 kHz du splash suffit (décision).

## v0.4.0 — Modes Mixte et Téléinformatique (80 colonnes) [TERMINÉ, 2026-09-17]
- [x] STUM 1B récupérée (transcription jbellue, `docs/ref/STUM1B-NOTES.md`).
- [x] Moteur 80 × 25 ISO 6429 (rangées 01-24) + rangée 00 Vidéotex réduit
      (`teleinfo.c`, 79 tests), rendu 1 bpp en mode Hercules du fork
      (`display80.c` + asm, capture == oracle), clavier étendu, PRO2 MIXTE
      1/2 avec acquittements SEP $70/$71, `CSI ? {`.
- [x] STUM 2 : `CSI 6 n`, 40 / 80 colonnes, page / rouleau.
- [x] Jeux DEC / complémentaire (annexes 3.12-3.13) et extinction du curseur
      `CSI < 1 h|l` — v0.4.1.
- [x] Fumée sur les vrais serveurs (`make test-servers`) — v0.4.1.
- [x] 40 colonnes avec pixels doublés — v0.4.2.
- [x] Passage à la ligne 80ᵉ colonne : auto-wrap **différé** ISO 6429
      (v0.7.2), plus une « hypothèse » — cohérent avec les CSI qui s'arrêtent
      au bord droit (STUM p. 168). Non vérifié sur Minitel physique.
- [x] Traits de balayage DEC rendus exactement (5 hauteurs) — v0.7.3.

## v0.5.0 — Enregistrement et relecture [TERMINÉ, 2026-09-17]
- [x] `CTRL+O` : enregistrement du flux reçu dans `neoNN.vdt` (API fichiers),
      menu `6` : relecture par la boucle de session. 225 tests hôte, 2
      scénarios cible.
- [x] RAM : cache G1 supprimé (mosaïques calculées à la volée, v0.5.1) ;
      marge BSS ~1,6 Ko (pile C à 256 o, tampons Wi-Fi dans `vtx.drcs`).

## v0.9.2 — Cellule 4 octets [TERMINÉ, 2026-09-18]
- [x] Encre + fond dans un octet (−1 000 o), contexte 80 col. sur drcs +
      screen ; marge BSS 1 255 o.

## v0.9.1 — Charte des menus [TERMINÉ, 2026-09-18]
- [x] Bandeau titre double hauteur, filets mosaïques, item courant sur fond
      bleu, valeurs des réglages en ligne, navigation flèches + ENVOI ;
      splash, menu, serveur, liaison série, aide sur la même charte.
- [ ] Reste sur l'ancienne présentation : Config WiFi, liste des
      enregistrements, pages d'échec de connexion / perte de porteuse.

## v0.9.0 — Rendu par demi-rangées [TERMINÉ, 2026-09-18]
- [x] Tampon de rangée 2 880 → 1 440 o, deux passes, un seul bloc asm ;
      marge BSS ~1,8 Ko. Banc : +11 % (texte) à +33 % (page vide).

## v0.8.2 — Attributs de zone STUM [TERMINÉ, 2026-09-18]
- [x] Semi-graphiques délimiteurs de couleur de fond ; zone d'accueil après
      déplacement (POKER 3617 conforme) ; RAM regagnée (76 o de marge).

## v0.8.1 — Raccourcis clavier [TERMINÉ, 2026-09-17]
- [x] `Suppr` Annulation, `CTRL+R` Répétition, `←` Retour, `→` Suite (hors
      mode curseur) ; premiers essais sur carte réelle (clavier corrigé en 0.8.0).

## v0.8.0 — Aide bilingue et RAM [TERMINÉ, 2026-09-17]
- [x] Écran d'aide FR/EN (menu `H`), langue mémorisée.
- [x] Cellule Vidéotex 6→5 octets (taille dans `flags`) : ~1,1 Ko de RAM.

## v0.7.0 — Gestion des enregistrements [TERMINÉ, 2026-09-17]
- [x] Menu `6` : énumération des `neoNN.vdt` (API répertoire 3,17-3,19),
      choix par lettre ; correction du tampon capacité de 3,18.
- [x] Suppression (Suppr + lettre, API 3,13) — v0.7.1 ; pile C réduite à
      96 o (usage mesuré 33) pour tenir en RAM.

## v0.6.0 — Serveurs WebSocket [TERMINÉ, 2026-09-17]
- [x] `ATDT ws://` relayé par le faux modem (PC), serveur 3 = `wss://3617.fr/ws`,
      scénario cible + serveur réel.
- [ ] Sur carte réelle : le PicoWiFiModemUSB n'a pas de WebSocket connu
      (non vérifié) ; il faudrait une commande AT dédiée dans son firmware
      ou un relais sur PC (`ws_page_server.py` montre le côté serveur).

## Idées
- Mode « réponse ENQROM » automatique par serveur.
