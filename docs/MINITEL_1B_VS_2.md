# Minitel 1B et Minitel 2 dans NeoTel

NeoTel propose deux **profils** (menu principal, touche `3`). Ce document
dit précisément ce qui change entre eux, sur quelle source cela s'appuie,
et ce qui n'est pas émulé. Règle du projet : rien d'inventé. Depuis le
2026-09-17 la **STUM Minitel 2** (France Télécom, février 1991) est dans le
projet (`docs/ref/STUM2-NOTES.md`, extraits relus sur les scans) : les points
ci-dessous sont vérifiés contre elle, sauf mention contraire.

## Mode Mixte / Téléinformatique (80 colonnes), depuis la v0.4.0

Commun aux deux profils (STUM 1B partie 2 p. 106 et partie 3 chapitre 2 ;
STUM 2 §3) : `PRO2 MIXTE 1` (`ESC 3/A 3/2 7/D`) passe l'écran en 80 × 25
ISO 6429 (mode vidéo Hercules du firmware bmarty, 720 × 350) avec
acquittement `SEP $70` ; `PRO2 MIXTE 2` ou `CSI ? {` reviennent au Videotex
(`SEP $71`). Rangée 00 en Videotex réduit, rangées 01-24 en ISO 6429 avec
les 4 attributs (clignotement, souligné, inverse, surintensité), jeux
américain / français (SO/SI), clavier étendu. Propre au **Minitel 2**
(STUM 2 §3.3/3.4, émulé dans les deux profils faute de raison de les
distinguer, sauf `CSI 6 n`, `CSI < 1 h|l` (curseur) et les jeux DEC /
complémentaire réservés au M2) : formats 40 / 80 colonnes (`CSI < 3 h` /
`CSI ? 3 l`), mode page / rouleau (`CSI < 4 h|l`). Sur le
firmware amont (pas de mode 1), le passage en mode Mixte est refusé avec
un message.

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
| **Jeux DRCS G'0 / G'1** (téléchargement `US 2/3 …`, associations `ESC 2/8|2/9 …`) | ignorés (ESC 2/8 = séquence inconnue, US 2/3 = adresse) | **émulés** (v0.2.0) | STUM 2 §2.2.2 et §2.3, exemple §2.3.5 comme vecteur de test. Forme 8 × 10 rendue en 8 × 9 (rangées 9 et 10 fusionnées). |
| `CSI 3/6 6/E` demande de position curseur | sans réponse | réponse `CSI Pr;Pc R` | STUM 2 §2.5 (Pr rangée, Pc colonne ; NeoTel : rangée 0-24, colonne 1-based comme `CSI H`). |
| `SS2` quand G1 est invoqué | traité (OricTel) | ignoré | STUM 2 §2.3.7. |
| Vitesses acceptées par PRO2 PROG (`ESC $3A $6B code`) | 300, 1200, 4800 | 300, 1200, 4800, **9600** | **Vérifié** : STUM 2 p. 103, note 3 (« 300, 1 200, 4 800, 9 600 programmable par le périphérique » pour le Minitel 2, note 2 sans 9600 pour le 1B). Codes `$52/$64/$76/$7F` : non relus dans la STUM 2 (chapitre prise), concordance de deux sources. |

L'identification (`SOH`, constructeur `C`, type, version `1`, `EOT`) est
**désactivée par défaut** (menu `5`), pour la raison relevée par OricTel :
les serveurs modernes (MiniPavi, PAVI) échoient la réponse comme une frappe
et `{tc` apparaît dans le champ de saisie. `C v 1` est l'identifiant d'un
Minitel 2 Telic première version (STUM 2 p. 103) ; `C u 1` n'est pas un
identifiant relevé tel quel (les 1B Telic vont de Cu2 à Cu<) mais respecte
le format. La STUM 2 précise que sur Minitel 2 la réponse « n'est plus
prioritaire sur le flux » (p. 63). Quand elle est activée, la réponse part
bien dès l'`ENQROM` d'ouverture de page (`ESC 9 {`), y compris lorsqu'il est
le tout premier octet reçu après le `CONNECT` (corrigé en v0.6.3 ; vérifié
sur PAVI, MiniPavi et 3617.fr).

La vitesse programmée n'a pas d'effet physique : la liaison réelle est le
modem USB (ou l'UART UEXT à 115200). Elle est mémorisée et affichée dans
la barre de statut, ce qui permet de voir ce qu'un serveur demande.

## Ce qui n'est PAS (encore) émulé du Minitel 2

- **DRCS, écarts connus** : la 10ᵉ rangée des formes est fusionnée (OU)
  avec la 9ᵉ (cellule 8 × 9) ; la double hauteur d'une forme G'0 suit la
  règle générale de NeoTel (rangées doublées), la « 1ʳᵉ ligne triplée » du
  §2.3.6 n'ayant pas de place en 18 lignes. L'interruption par la rangée 00
  (§2.3.4) est gérée depuis la v0.2.1.
- **Jeux DEC et complémentaire** (Minitel 2, STUM 2 annexes 3.12-3.13) :
  émulés depuis la v0.4.1 avec des approximations (traits de balayage DEC
  6/F-7/3 rendus par le trait médian, 6/1 du complémentaire par `+`, 7/4-7/8
  par des filets).
- **Retournement de modem**, prise péripherique à 9600 bauds : sans objet
  avec un modem Hayes sur USB.

## Aspect « gris (1B mono) »

Ce n'est pas lié au profil : l'écran d'un Minitel 1B est monochrome, celui
d'un Minitel 2 aussi (les Minitel couleur sont d'autres modèles). L'aspect
gris rend les 8 couleurs Videotex en 8 niveaux de gris ordonnés par
luminance (Rec. 601), comme le faisait le dithering d'OricTel ; l'aspect
couleur montre les couleurs telles que le serveur les code.
