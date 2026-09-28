# Plan agile — NeoTel

Méthode Scrum allégée (héritée d'OricTel) : sprints courts, chaque
incrément = code + tests + documentation + CHANGELOG, commit unique par
incrément, auteur `bmarty`.

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

## Sprint 3 — v0.3.0 (2026-09-17) — TERMINÉ
| User story | État |
|---|---|
| US-16 Mes réglages (profil, aspect, identification, dernier serveur) sont conservés d'un lancement à l'autre | fait (`settings.c`, 7 tests hôte, 2 scénarios cible) |
| US-18 Le projet est publié (dépôt distant) | fait (`github.com/benedictemarty/neotel`, public comme AsteroNeo/OricTel) |

## Sprint 4 — v0.4.0 (2026-09-17) — TERMINÉ
| User story | État |
|---|---|
| US-17 Un serveur peut passer NeoTel en mode Mixte / Téléinformatique 80 colonnes (ISO 6429) et revenir | fait (teleinfo.c, display80 + asm, 79 tests, capture == oracle) |
| US-19 Le clavier suit le mode (étendu en Mixte) | fait (+11 tests) |

Rétrospective : le premier rendu C coûtait 400 000 cycles par rangée (rangées
≥ 14 jamais atteintes avant la capture — diagnostiqué comme un « bug » avant
de mesurer) ; l'assembleur ramène à 67 000. La RAM a débordé de 671 octets :
le contexte 80 colonnes partage désormais la mémoire de l'écran Videotex.

## Sprint 5 — v0.5.0 (2026-09-17) — TERMINÉ
| User story | État |
|---|---|
| US-20 Je peux enregistrer une page reçue sur la carte (`CTRL+O`) et la relire hors ligne (menu `6`) | fait (`record.c`, 225 tests hôte, scénarios `record` / `replay`) |

Rétrospective : la première version dupliquait la boucle de session pour la
relecture (+790 o de code) et faisait déborder la RAM de 504 octets ; la
relecture passe finalement par la boucle de session (source commutée), les
tampons Wi-Fi partagent `vtx.drcs` et la pile C descend à 256 octets. Les
valeurs de `--dump-ram-when` sont en hexadécimal : « 15 » valait 0x15.

## Sprint 6 — v0.6.0 (2026-09-17) — TERMINÉ
| User story | État |
|---|---|
| US-21 Je peux joindre un serveur Minitel WebSocket (`ws://`) depuis NeoTel | fait côté PC (faux modem, serveur 3 `wss://3617.fr/ws`, scénario `ws`, serveur réel) ; non vérifié sur un Pico réel |

## Sprint 6b — v0.6.1 (2026-09-17) — TERMINÉ
| User story | État |
|---|---|
| US-15 Le rendu saute les cellules vides déjà affichées ; page complète nettement plus rapide | fait (`rb_state`, `scan_dblh`, `make bench` : page vide 236 → 70 ms, page de test 273 → 129 ms) |

Rétrospective : le profil par trace a montré que la moitié du temps de la
page vide était dans deux boucles C (indexation ×6 des cellules) et dans le
chargement du glyphe de l'espace, pas dans le dessin lui-même.

## Sprint 7 — v0.7.0-0.7.3 (2026-09-17) — TERMINÉ
| User story | État |
|---|---|
| US-22 Je vois la liste de mes enregistrements et j'en rejoue / supprime un | fait (menu 6, API 3,17-3,19, `Suppr` + lettre) |
| US-23 Le mode 80 colonnes respecte le passage à la ligne différé et les traits DEC | fait (v0.7.2, v0.7.3) |

## Sprint 8 — v0.8.0-0.8.2 (2026-09-17/18) — TERMINÉ
| User story | État |
|---|---|
| US-13 NeoTel tourne sur la carte réelle avec le clavier USB | fait : premier essai sur carte → clavier muet, API 2,2 lue à l'envers depuis la v0.1 ; scénario `realkeys` (vraie file du firmware) |
| US-24 Une aide bilingue est disponible depuis le menu | fait (`H`, FR/EN mémorisé, `neotel.cfg` v4) |
| US-25 Les raccourcis suivent l'usage Minitel (Suppr Annulation, ←/→ Retour/Suite, CTRL+R Répétition) | fait (v0.8.1) |
| US-26 Les pages réelles s'affichent comme sur un Minitel (POKER 3617) | fait : semi-graphiques délimiteurs de fond, zone d'accueil (v0.8.2, scénario `poker`) |

Rétrospective : les tests injectaient les touches en court-circuitant la
file du firmware ; un bug présent depuis la v0.1 n'a été vu qu'à la première
frappe sur carte. Règle : toute couche « matérielle » a un scénario qui passe
par le vrai chemin du firmware. La RAM est arrivée à 76 o de marge.

## Sprint 9 — v0.9.0-0.9.5 (2026-09-18 → 2026-09-20) — TERMINÉ
| User story | État |
|---|---|
| US-27 Dégager de la RAM sans perdre le rendu pixel-exact | fait (v0.9.0 : tampon de demi-rangée, −1 440 o, marge ~1,8 Ko ; banc +11 à +33 %) |
| US-29 Nouvelle économie de RAM pour finir la charte | fait (v0.9.2 : cellule 4 octets, −1 000 o ; marge 1 255 o) |
| US-28 Les menus, le splash, la liaison série et l'aide ont une charte moderne (bandeaux, cadres, inverse, flèches) | fait (v0.9.1 : `ui_header/item/footer/nav`, `test_ui` 33, scénario `menunav`) ; v0.9.3 : WiFi, liste des enregistrements, pages d'échec) |

Complément v0.9.4-0.9.5 (retours du PO sur carte) : `_` jointif, bandeau
de liaison USB/UEXT, F10 = aide, environnement NeoDOS.

Rétrospective : la RAM a dicté chaque choix (aide sans sauvegarde de page,
tampons logés dans `vtx`) ; le coût venait surtout du code généré par cc65
pour les accès par pointeur, pas des données.

## Sprint 10 — v0.9.6-0.9.8 (2026-09-28) — EN COURS
| User story | État |
|---|---|
| US-30 Le modem USB CDC fonctionne sur la carte réelle | fait (validé par le PO sur carte, 2026-09-28) |
| US-31 Réduire la taille du programme en RAM | fait (v0.9.6 : contextes uniques `ctx.h`, −8,4 Ko de code ; marge 1 110 o → ~9,3 Ko ; banc −4 à −12 % de cycles) |
| US-32 Optimiser la réception série | fait (v0.9.6 : un appel API par octet au lieu de deux ; `serial_tx_*` sans code sur la cible) |
| US-33 Au retour de l'aide F10 en session, je retrouve ma page | fait (v0.9.7 : `session_help` sauve l'état du décodeur et les rangées 1-24, 3 873 o ; scénario `help-session` : page restaurée == oracle) |
| US-34 Les messages de la barre de statut (CTRL+O, CTRL+F) restent lisibles | fait (v0.9.8 : `status_message`, maintien 2-3 s par `g_status_hold` ; scénario `status-msg`) |
| US-14 Latence API réelle et budget de rendu sous flux 115200 | à faire (`--api-latency` de Phosphoneo) |
