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
- [ ] Rendu : ignorer les cellules inchangées (comparaison avec un cache
      de la ligne précédente), rendu d'une page complète < 100 ms.
- [x] Réglages persistants (profil, aspect, identification, dernier serveur)
      via l'API fichiers (3,3 / 3,2) sur la carte SD — v0.3.0.
- [ ] Bip Videotex (BEL) par 8,7 plutôt que le bip système.
- [ ] Jingle du splash (son du RP2040), option pour le couper.

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
- [ ] Écarts : 40 colonnes sans doublement des pixels ; passage à la ligne
      après la 80ᵉ colonne = hypothèse ; traits de balayage DEC approximés.

## Idées
- Enregistrement des pages reçues (.vdt) sur la carte SD, relecture.
- Bridge WebSocket (serveurs `ws://`) : réutiliser `orictel_bridge.py`
  côté PC, ou une commande `AT` dédiée du Pico.
- Mode « réponse ENQROM » automatique par serveur.
