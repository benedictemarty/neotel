# STUM Minitel 1B — notes de lecture pour NeoTel

Source : **« Spécifications Techniques d'Utilisation du Minitel 1B »**,
Ministère des PTT / Télétel, 1986. Récupérée le 2026-09-17 sous la forme de la
**transcription HTML de jbellue** (<https://github.com/jbellue/stum1b>,
<https://jbellue.github.io/stum1b/> : OCR relu et corrigé par son auteur,
hexadécimal noté `0x..` au lieu de `x/y`) : `docs/ref/STUM1B.html` (original)
et `docs/ref/STUM1B.txt` (texte extrait, ~400 Ko, recherche plein texte).
Le scan original existe sur archive.org
(<https://archive.org/details/specifications-techniques-utilisation-minitel-1b>,
58 Mo + PDF texte 4,4 Mo) mais le téléchargement direct renvoyait 404 ce
jour : **non récupéré**. La transcription est une source secondaire ; pour un
point litigieux, revérifier sur le scan.

## Ce qui concerne NeoTel

- **Mode Vidéotex** (partie 2, chapitre 1) : c'est le décodeur d'OricTel,
  repris tel quel dans `videotex.c` (219 tests). Non relu ici ligne à ligne.
- **Mode Mixte** (partie 2, chapitre 1 « Mode Mixte », p. 106) : le Minitel 1B
  aussi affiche **80 colonnes** en mode Mixte (25 rangées × 80, décodage ISO
  6429 sur les rangées 01-24, rangée 00 en Vidéotex sans attributs, jeux
  américain / français, 4 attributs par caractère : clignotement,
  soulignage, inversion, surintensité ; passage au mode Mixte par
  PRO2 `0x32 0x7D`, retour Vidéotex par `0x32 0x7E`). Contrairement à ce que
  disait `MINITEL_1B_VS_2.md` v0.1, le 80 colonnes n'est donc pas propre au
  Minitel 2 ; le Minitel 2 ajoute des jeux (DEC, complémentaire) et des
  séquences (§3.3/3.4 de la STUM 2).
- **Standard Téléinformatique** (partie 3, p. 154 sq.) : « le décodage écran
  est identique à celui du standard Télétel mode Mixte » ; pas de Protocole,
  aiguillages fixes ; passage par `Fnct T + A/F` (clavier) ou par le
  serveur (partie 2, chap. 6, §12.2) ; retour Vidéotex par `CSI 0x3F 0x7B`.
- Pour NeoTel, mode Mixte et Téléinformatique = **un seul moteur 80
  colonnes ISO 6429** à écrire (rendu 80 × 25 impossible en mode 0 du
  Neo6502 : 4 px par colonne ; mode Hercules 720 × 350 du fork à étudier).
  Reste en ROADMAP v0.4, conditionnel.
