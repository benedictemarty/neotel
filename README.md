# NeoTel — terminal Minitel 1B / Minitel 2 pour Neo6502

NeoTel transforme un **Neo6502** (Olimex, W65C02S à 6,25 MHz + RP2040) en
terminal **Minitel 1B** (ou, au choix, **Minitel 2**) capable de se connecter
aux serveurs Minitel encore en service sur Internet (PAVI 3617, MiniPavi…)
à travers un modem Hayes : un **PicoWiFiModemUSB** branché sur le port USB
hôte du Neo6502, ou tout modem AT sur l'UART de l'UEXT.

C'est le portage d'[OricTel](../orictel) (même auteur, Oric 1/Atmos) : le
décodeur Videotex, les polices G0/G1/G2, la machine à états modem AT et les
menus sont repris ; l'affichage, le clavier, la liaison série et la base de
temps sont réécrits sur l'API du Neo6502.

```
Serveur Minitel (pavi.3617.fr:3617, go.minipavi.fr:516, …)
        │  TCP
PicoWiFiModemUSB (modem Hayes, Wi-Fi)      ── ou modem AT sur l'UART UEXT
        │  USB CDC
RP2040 du Neo6502 (firmware bmarty : API UART 10,15-10,18 routée vers le CDC)
        │  bloc $FF00
NeoTel sur le 65C02 : Videotex → cellules 40x25 → tampon de ligne → blitter → VRAM
```

## État

**v0.5.0 — sprints 1 à 5 livrés.** Minitel 1B complet (Videotex 40 colonnes :
G0/G1/G2, couleurs, attributs, doubles tailles, PRO1/2/3, aiguillages),
profil Minitel 2 (identification, vitesses PRO2 PROG jusqu'à 9600, **jeux
DRCS téléchargeables** conformes à la STUM 2, demande de position curseur), réglages persistants sur la carte, **mode Mixte / Téléinformatique 80 colonnes** (ISO 6429, mode Hercules du fork), **enregistrement des pages reçues (`CTRL+O`, `.vdt`) et relecture** (menu `6`), menus,
connexion AT, page de configuration Wi-Fi du Pico, perte de porteuse,
retour à NeoBASIC. Vérifié dans les émulateurs Phosphoneo et `neo` avec un
faux modem, et **sur les vrais serveurs PAVI 3617 et MiniPavi** (`make
test-servers`) ; **non exécuté sur une carte Neo6502 physique** (aucune
disponible) — voir [ROADMAP.md](ROADMAP.md).

Les écarts connus (DRCS, 80 colonnes, jeux DEC du Minitel 2) sont détaillés dans
[docs/MINITEL_1B_VS_2.md](docs/MINITEL_1B_VS_2.md).

## Prérequis

- cc65 ≥ 2.19 (`cl65`, `ca65`, `ld65`), Python 3.8+, gcc (tests hôte),
  pillow facultatif (conversion des captures), `websockets` facultatif
  (serveurs `ws://` via le faux modem).
- Firmware Neo6502 **fork bmarty** (`~/Neo6502firmware`) pour le modem USB
  (groupe 14 CDC + routage 10,19). Sur le firmware amont, seule l'UART de
  l'UEXT est utilisable (détecté et affiché à l'écran « Liaison série »).
- Émulateurs : [Phosphoneo](../Phosphoneo) (oracle des tests) et/ou
  `~/Neo6502firmware/bin/neo`.

## Construire, lancer, tester

```sh
make                   # build/neotel.neo (+ neotel.bin brut chargé à $0800)
make run               # émulateur neo + faux modem (ATDT hôte:port = vraie connexion TCP)
make run-phos          # idem sous Phosphoneo (SDL)
make run MODEM=--serve # page de test locale, sans réseau
make test              # tests hôte (gcc) + tests cible (Phosphoneo headless)
make test-host         # 767 assertions : décodeur, modem AT, UI, clavier, affichage, profil/série, enregistrement
make test-emu          # menus → session, page/DRCS/80 col. cible == oracle hôte, ESC ESC, NO CARRIER, réglages, enregistrement/relecture, WebSocket, sortie, neo
make test-servers      # fumée sur les vrais serveurs PAVI 3617 / MiniPavi / wss://3617.fr (réseau)
make ref               # régénère tests/ref/*.ppm après un changement visuel voulu
make bench             # cycles / ms d'une page complète sur cible (Phosphoneo)
```

Sur le Neo6502 : copier `build/neotel.neo` sur la carte SD/clé USB et
`run "neotel.neo"` depuis NeoBASIC (comme AsteroNeo).
Avec un vrai PicoWiFiModemUSB sur le PC : `NEO_CDC_TTY=/dev/ttyACM0 make run`.

## Utilisation en bref

Splash → écran « Liaison série » (modem USB détecté ou UART UEXT) → menu :
`1` Modem AT (puis choix du serveur, ATZ/ATI/ATDT, session), `2` Config
Wi-Fi du Pico, `3` terminal Minitel 1B / Minitel 2, `4` aspect couleur /
gris (rendu monochrome du 1B), `5` identification ENQROM on/off, `ESC`
retour à NeoBASIC.

En session : `F1` Sommaire, `F2` Annulation, `F3` Retour, `F4` Répétition,
`F5` Guide, `F6` Correction, `F7` Suite, `F8`/`Entrée` Envoi, `F9`
Connexion/Fin, `F10`/`CTRL+D` aspect, `CTRL+L` effacer, `CTRL+F` reset série,
`ESC` puis `ESC` raccroche et revient au menu. Détails :
[docs/MANUEL_UTILISATION.md](docs/MANUEL_UTILISATION.md).

## Documentation

[CLAUDE.md](CLAUDE.md) (règles du projet) · [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) ·
[docs/MANUEL_UTILISATION.md](docs/MANUEL_UTILISATION.md) ·
[docs/MINITEL_1B_VS_2.md](docs/MINITEL_1B_VS_2.md) · [docs/TESTS.md](docs/TESTS.md) ·
[docs/AGILE_PLAN.md](docs/AGILE_PLAN.md) · [ROADMAP.md](ROADMAP.md) ·
[CHANGELOG.md](CHANGELOG.md)

## Licence

EUPL-1.2, © Bénédicte Marty (bmarty@mailo.com). Code repris d'OricTel
(même licence, même auteur). Le format `.neo`, l'API et les emplacements
mémoire viennent du firmware Neo6502 (Paul Robson et contributeurs, MIT).
