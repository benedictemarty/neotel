# Minitel 1B et Minitel 2 dans NeoTel

NeoTel propose deux **profils** (menu principal, touche `3`). Ce document
dit précisément ce qui change entre eux, sur quelle source cela s'appuie,
et ce qui n'est pas émulé. Règle du projet : rien d'inventé. Depuis le
2026-09-17 la **STUM Minitel 2** (France Télécom, février 1991) est dans le
projet (`docs/ref/STUM2-NOTES.md`, extraits relus sur les scans) : les points
ci-dessous sont vérifiés contre elle, sauf mention contraire.

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
| Octet de type de l'identification ENQROM | `u` (7/5) | `v` (7/6) | **Vérifié** : STUM 2 annexe 6.6 p. 103 (Minitel 1 bistandard = 7/5, Minitel 2 = 7/6). La table de `~/telenet-workspace` (`x`) est fausse. |
| Vitesses acceptées par PRO2 PROG (`ESC $3A $6B code`) | 300, 1200, 4800 | 300, 1200, 4800, **9600** | **Vérifié** : STUM 2 p. 103, note 3 (« 300, 1 200, 4 800, 9 600 programmable par le périphérique » pour le Minitel 2, note 2 sans 9600 pour le 1B). Codes `$52/$64/$76/$7F` : non relus dans la STUM 2 (chapitre prise), concordance de deux sources. |

L'identification (`SOH`, constructeur `C`, type, version `1`, `EOT`) est
**désactivée par défaut** (menu `5`), pour la raison relevée par OricTel :
les serveurs modernes (MiniPavi, PAVI) échoient la réponse comme une frappe
et `{tc` apparaît dans le champ de saisie. `C v 1` est l'identifiant d'un
Minitel 2 Telic première version (STUM 2 p. 103) ; `C u 1` n'est pas un
identifiant relevé tel quel (les 1B Telic vont de Cu2 à Cu<) mais respecte
le format. La STUM 2 précise que sur Minitel 2 la réponse « n'est plus
prioritaire sur le flux » (p. 63).

La vitesse programmée n'a pas d'effet physique : la liaison réelle est le
modem USB (ou l'UART UEXT à 115200). Elle est mémorisée et affichée dans
la barre de statut, ce qui permet de voir ce qu'un serveur demande.

## Ce qui n'est PAS (encore) émulé du Minitel 2

- **Caractères redéfinissables (DRCS)** : jeux G'0 / G'1 téléchargés par
  `US 2/3 2/0 2/0 2/0 4/2|4/3 4/9` puis `US 2/3 Y 3/0 <14 octets> 3/0 …`,
  associés par `ESC 2/8 2/0 4/2` / `ESC 2/9 2/0 4/3`, matrice 8 × 10 codée
  6 bits par octet. **Spécifié** (STUM 2 §2.3, détail dans
  `docs/ref/STUM2-NOTES.md`), implémentation planifiée (ROADMAP v0.3) ;
  la cellule 8 × 9 de NeoTel devra approximer la 10ᵉ rangée.
- **Mode téléinformatique 80 colonnes** : séquences de bascule connues
  (`CSI 3/C 3/3 6/8` 40 col., `CSI 3/F 3/3 6/C` 80 col.), mais le décodage
  lui-même renvoie à la STUM 1B (p. 105 et 161), non récupérée ; et l'écran
  320 px du Neo6502 ne donne que 4 px par colonne en mode 0 (mode Hercules
  du fork envisageable). Non implémenté.
- **Demande de position curseur** `CSI 3/6 6/E` (réponse `CSI Pr;Pc R`) :
  nouvelle séquence du Minitel 2, non implémentée (simple, à faire).
- **Retournement de modem**, prise péripherique à 9600 bauds : sans objet
  avec un modem Hayes sur USB.
- Les modes **MIXTE** (PRO2 $32 $7D) sont refusés silencieusement dans les
  deux profils, comme dans OricTel.

## Aspect « gris (1B mono) »

Ce n'est pas lié au profil : l'écran d'un Minitel 1B est monochrome, celui
d'un Minitel 2 aussi (les Minitel couleur sont d'autres modèles). L'aspect
gris rend les 8 couleurs Videotex en 8 niveaux de gris ordonnés par
luminance (Rec. 601), comme le faisait le dithering d'OricTel ; l'aspect
couleur montre les couleurs telles que le serveur les code.
