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

## v0.2 — Carte réelle et performance [PLANIFIÉ]
- [ ] Exécution sur Neo6502 physique + PicoWiFiModemUSB : session PAVI 3617
      de bout en bout, mesure de la latence des appels API, du débit CDC
      (RX 1 Ko du firmware, `tuh_task` à ~100 Hz : risque R16 de F-90) et du
      temps de rendu réel. Ajuster le budget de rendu si des octets se
      perdent à 115200 bauds.
- [ ] Rendu : ignorer les cellules inchangées (comparaison avec un cache
      de la ligne précédente), rendu d'une page complète < 100 ms.
- [ ] Réglages persistants (profil, aspect, dernier serveur) via l'API
      fichiers (3,3 / 3,2) sur la carte SD.
- [ ] Bip Videotex (BEL) par 8,7 plutôt que le bip système.
- [ ] Jingle du splash (son du RP2040), option pour le couper.

## v0.3 — Minitel 2 [PLANIFIÉ]
Préalable levé le 2026-09-17 : la STUM Minitel 2 (France Télécom, 1991) est
récupérée (`docs/ref/STUM2-NOTES.md`, OCR `docs/ref/STUM2-ocr.txt`).
- [x] Vérifier les octets d'identification : `u` (7/5) = 1B, `v` (7/6) =
      Minitel 2, 9600 bauds réservé au Minitel 2 (annexe 6.6 p. 103).
- [ ] Caractères redéfinissables DRCS G'0/G'1 (STUM 2 §2.3) : en-tête,
      transfert 14 octets × 6 bits, associations ESC 2/8|2/9 2/0 4/2|4/3,
      sortie sur US, SS2 en G'0 ; rendu 8 × 10 → 8 × 9 (règle à choisir,
      documentée) ; tests hôte + page DRCS dans le faux modem.
- [ ] Demande de position curseur `CSI 3/6 6/E` → `CSI Pr;Pc R` (§2.5).
- [ ] Mode téléinformatique 80 colonnes : récupérer la STUM 1B (décodage
      p. 105 / 161), puis étudier le mode Hercules 720 × 350 du fork.

## Idées
- Enregistrement des pages reçues (.vdt) sur la carte SD, relecture.
- Bridge WebSocket (serveurs `ws://`) : réutiliser `orictel_bridge.py`
  côté PC, ou une commande `AT` dédiée du Pico.
- Mode « réponse ENQROM » automatique par serveur.
