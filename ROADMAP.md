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

## v0.3 — Minitel 2 [CONDITIONNEL]
Préalable : obtenir la STUM Minitel 2 (ou relever les séquences sur un
Minitel 2 réel). Sans cela, rien n'est implémenté (règle « rien d'inventé »).
- [ ] Caractères redéfinissables (DRCS G0'/G1').
- [ ] Mode téléinformatique 80 colonnes (mode Hercules 720 × 350 du fork ?).
- [ ] Vérifier les octets d'identification (`u`/`v` vs `x`).

## Idées
- Enregistrement des pages reçues (.vdt) sur la carte SD, relecture.
- Bridge WebSocket (serveurs `ws://`) : réutiliser `orictel_bridge.py`
  côté PC, ou une commande `AT` dédiée du Pico.
- Mode « réponse ENQROM » automatique par serveur.
