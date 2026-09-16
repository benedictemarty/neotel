# Minitel 1B et Minitel 2 dans NeoTel

NeoTel propose deux **profils** (menu principal, touche `3`). Ce document
dit précisément ce qui change entre eux, sur quelle source cela s'appuie,
et ce qui n'est pas émulé. Règle du projet : rien d'inventé ; ce qui n'a pas
pu être vérifié contre la STUM (Spécifications Techniques d'Utilisation du
Minitel) est marqué **non vérifié**.

## Ce que les deux profils partagent

Le protocole **Videotex 40 colonnes** (STUM 1B) décodé par `videotex.c` :
jeux G0/G1/G2, couleurs, attributs parallèles et série, doubles tailles,
positionnement US/CSI, REP, SEP, PRO1/PRO2/PRO3 (rouleau, minuscules,
aiguillages, état clavier, modes VIDEOTEX/MIXTE), masque global. Le Minitel
2 est rétro-compatible avec le 1B sur tout cela : un serveur Minitel ne fait
pas la différence, et NeoTel non plus.

## Ce qui diffère dans NeoTel

| Aspect | Minitel 1B | Minitel 2 | Source / statut |
|---|---|---|---|
| Nom (menu, statut) | `Minitel 1B` / `1B` | `Minitel 2` / `M2` | — |
| Octet de type de l'identification ENQROM | `u` ($75) | `v` ($76) | Bibliothèque Python *Minitel* (F. Bisson) ; **non vérifié** contre la STUM. Une autre source locale (`~/telenet-workspace`) donne `x` ($78) pour le Minitel 2 : les deux tables divergent, aucune n'a été confirmée. |
| Vitesses acceptées par PRO2 PROG (`ESC $3A $6B code`) | 300, 1200, 4800 | 300, 1200, 4800, **9600** | Codes `$52/$64/$76/$7F` : bibliothèque *Minitel* et `~/telenet-workspace/telenet/constants.py` concordent. Le 9600 réservé au Minitel 2 : documentation courante (non vérifié dans la STUM 2). |

L'identification (`SOH`, constructeur `C`, type, version `1`, `EOT`) est
**désactivée par défaut** (menu `5`), pour la raison relevée par OricTel :
les serveurs modernes (MiniPavi, PAVI) échoient la réponse comme une frappe
et `{tc` apparaît dans le champ de saisie. Le constructeur `C` et la version
`1` sont des valeurs de commodité, **non vérifiées**.

La vitesse programmée n'a pas d'effet physique : la liaison réelle est le
modem USB (ou l'UART UEXT à 115200). Elle est mémorisée et affichée dans
la barre de statut, ce qui permet de voir ce qu'un serveur demande.

## Ce qui n'est PAS émulé du Minitel 2

- **Mode téléinformatique 80 colonnes** (VT-like) : l'écran 320 pixels du
  Neo6502 ne donne que 4 pixels par colonne en mode 0 ; le mode Hercules
  du fork (720 × 350, 80 × 25) serait la voie, mais le jeu de commandes de
  ce mode n'est pas documenté ici. Non vérifié, non implémenté.
- **Caractères redéfinissables (DRCS)** : le Minitel 2 accepte des jeux G0'
  et G1' téléchargés par le serveur. La séquence exacte (US $23 …, format
  des 14 sextets par caractère) n'a pas pu être vérifiée sur une source
  fiable : non implémenté plutôt qu'implémenté de mémoire.
- **Retournement de modem**, prise péripherique à 9600 bauds : sans objet
  avec un modem Hayes sur USB.
- Les modes **MIXTE** (PRO2 $32 $7D) sont refusés silencieusement dans les
  deux profils, comme dans OricTel.

Ces points sont dans le [ROADMAP](../ROADMAP.md) ; ils demandent la STUM
Minitel 2 (ou un Minitel 2 réel pour relever les séquences) avant toute
implémentation.

## Aspect « gris (1B mono) »

Ce n'est pas lié au profil : l'écran d'un Minitel 1B est monochrome, celui
d'un Minitel 2 aussi (les Minitel couleur sont d'autres modèles). L'aspect
gris rend les 8 couleurs Videotex en 8 niveaux de gris ordonnés par
luminance (Rec. 601), comme le faisait le dithering d'OricTel ; l'aspect
couleur montre les couleurs telles que le serveur les code.
