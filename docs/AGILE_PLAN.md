# Plan agile — NeoTel

Méthode Scrum allégée (héritée d'OricTel) : sprints courts, chaque
incrément = code + tests + documentation + CHANGELOG, commit unique par
incrément, auteur `bmarty <bmarty@mailo.com>`.

## Rôles
- Product Owner : Bénédicte Marty.
- Équipe : développement assisté, revue par les tests (hôte + cible).

## Definition of Done
1. `make` sans avertissement bloquant, `make test` vert (hôte + cible).
2. Documentation à jour : README, docs/ARCHITECTURE, MANUEL, TESTS,
   MINITEL_1B_VS_2 si le comportement change, ROADMAP si le périmètre change.
3. CHANGELOG.md : entrée datée, versionnée.
4. Rien d'inventé : tout fait non vérifié est marqué comme tel.
5. Commit signé bmarty, sans mention d'IA.

## Sprint 1 — v0.1.0 (2026-09-16 → 2026-09-17) — TERMINÉ
Objectif : un Minitel 1B utilisable sur Neo6502 à partir d'OricTel, avec
profil Minitel 2, testé dans les émulateurs.

| User story | État |
|---|---|
| US-01 En tant qu'utilisateur, je vois une page Videotex 40 × 25 fidèle (G0/G1/G2, couleurs, attributs, doubles tailles) sur l'écran du Neo6502 | fait (display.c, display_asm.s, 46 tests + oracle asm == C) |
| US-02 Je me connecte à un serveur Minitel par un modem AT USB (PicoWiFiModemUSB) ou UART | fait (serial.c, at_modem.c, scénario session) |
| US-03 J'utilise les touches Minitel depuis un clavier USB (F1-F9, CTRL, flèches) | fait (keyboard.c, 50 tests) |
| US-04 Je choisis le profil Minitel 1B ou Minitel 2 | fait pour ce qui est modélisable (terminal.c, doc MINITEL_1B_VS_2) |
| US-05 Je configure le Wi-Fi du Pico depuis le Neo6502 | repris d'OricTel (non joué contre un vrai Pico) |
| US-06 Je suis prévenu d'une perte de porteuse et je peux me reconnecter | fait (scénario carrier) |
| US-07 Je quitte proprement (raccrochage, retour NeoBASIC) | fait (scénarios escape, exit) |
| US-08 Je peux choisir un rendu monochrome comme un vrai 1B | fait (aspect gris) |
| US-09 Le projet est testé sans matériel | fait (379 assertions hôte, 8 scénarios cible, fumée neo) |

Rétrospective : le premier moteur de rendu en C coûtait ~250 000 cycles par
ligne (41 ms) ; passage de la course de cellules en assembleur → ~60 000
(10 ms). Le faux modem doit tenir compte d'un émulateur plus rapide que le
temps réel (garde Hayes réglable). Les états de test doivent être posés
APRÈS le rendu d'un écran pour que le dump RAM le contienne.

## Sprint 2 — v0.2.0 (2026-09-17) — TERMINÉ
Objectif : Minitel 2 fidèle sur les DRCS, à partir de la STUM 2 récupérée.

| User story | État |
|---|---|
| US-10 En tant que PO, je dispose de la STUM Minitel 2 dans le projet, relue | fait (`docs/ref/STUM2-NOTES.md`, OCR) |
| US-11 Un serveur Minitel 2 peut télécharger des jeux DRCS et les afficher | fait (33 tests, page cible == oracle) |
| US-12 Le serveur peut demander la position du curseur (`CSI 6n`) | fait |

Rétrospective : un `.o` non recompilé après changement d'en-tête a corrompu
la BSS (dépendances d'en-têtes ajoutées aux Makefiles) ; `unsigned int`
n'a pas la même taille sur l'hôte et sur cc65 (structures partagées en
`unsigned char`/`unsigned short` seulement).

## Sprint 3 — v0.3 (à planifier)
- Validation sur carte Neo6502 réelle avec PicoWiFiModemUSB (US-13).
- Mesure de la latence API réelle (`--api-latency` de Phosphoneo) et
  budget de rendu sous flux 115200 (US-14).
- Rendu : sauter les cellules inchangées / lignes vides (US-15).
- Sauvegarde des réglages (profil, aspect, serveur) sur la carte SD par
  l'API fichiers (US-16).
- 80 colonnes Minitel 2 après récupération de la STUM 1B (US-17, conditionnelle).
