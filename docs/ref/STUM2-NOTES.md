# STUM Minitel 2 — notes de lecture pour NeoTel

Source : **« Spécifications Techniques d'Utilisation du Minitel 2 »**, France
Télécom, Direction des Affaires Commerciales et Télématiques, © février 1991,
118 pages. Scan récupéré le 2026-09-17 sur
<https://wiki.labomedia.org/images/a/ad/STUM2.pdf> (23 Mo, sans couche
texte ; SHA-256 `7150aa3ce37298513928fd225110a078dcc96ea5dc7b8a9865f4dfd3f6f04d52`).
Le PDF est conservé localement dans `docs/ref/STUM2.pdf` (hors dépôt, trop
gros) ; `docs/ref/STUM2-ocr.txt` est l'OCR brut (tesseract, français) des
118 pages, utile pour la recherche plein texte mais **bruité sur les tableaux
d'octets** : tout ce qui suit a été relu sur les scans (numéro de page du
document entre parenthèses, page du scan = page du document + 8).

Les faits ci-dessous sont **vérifiés** dans la STUM 2 et remplacent les
« non vérifié » de `docs/MINITEL_1B_VS_2.md`.

## 1. Identification ROM (§3.1 p. 63, annexe 6.6 p. 103)

Réponse à PRO1 ENQROM : `SOH` + identifiant en ROM (3 caractères :
constructeur, type, version) + `EOT`. « La réponse à ENQ ROM n'est plus
prioritaire sur le flux comme elle l'était sur Minitel 1B. »

| Nom commercial | Constructeur | Identifiant ROM | Type STUM | DRCS | Retournement | Vitesses prise |
|---|---|---|---|---|---|---|
| Minitel 1 | TELIC / MATRA / TRT | Cb0-Cb5, Cc5, Cr0, Bc0, Br0-Br4 | 6/2, 6/3, 7/2 | non | selon | 300, 1200 |
| Minitel 1 couleur | TRT | Bs0 | 7/3 | non | oui | 300, 1200 |
| Minitel 10 | TELIC | Cd1-Cd6, Cf0-Cf2 | 6/4, 6/6 | non | — | 300, 1200 |
| **Minitel 1 bistandard (1B)** | TELIC / MATRA / RTIC | Cu2-Cu4, Cu5, Cu;, Cu:, Cu<, Bu0… | **7/5 = `u`** | non | oui | **300, 1200, 4800** |
| Minitel 10 bistandard | TELIC | Cw0, Cw1 | 7/7 | — | oui | 300, 1200, 4800 |
| **Minitel 2** | TELIC / PHILIPS | Cv1, Cv:, Cv; / Bv1-Bv4, Bv6-Bv9 | **7/6 = `v`** | **oui** | oui | **300, 1200, 4800, 9600** |
| Minitel 12 | TELIC / PHILIPS | Cz2-Cz5, Bz1… | 7/A | Philips | oui | 4800 / 9600 |
| Minitel 5 | MATRA | Ay0… | 7/9 | — | oui | 300, 1200, 4800 |

Lettres constructeur relevées : `A` Matra (Minitel 5), `B` TRT / Philips / RTIC, `C` Telic mais aussi Matra (Cr0, Cu4, Cu5) : la lettre n'identifie pas strictement le constructeur.
Les identifiants de NeoTel (`C` + `u`/`v` + version) sont donc conformes ;
un Minitel 2 Telic réel répond par exemple `SOH C v 1 EOT`.

Identification RAM 1 (§3.2) : `ENQ` → `SOH <champ ID saisi par l'usager> EOT`
(`SOH EOT` si vide). NeoTel n'a pas de champ ID : réponse `SOH EOT` serait
la réponse conforme d'un terminal vierge (non implémenté à ce jour).

## 2. Écran, mode Vidéotex (chapitre L'écran, p. 34-39)

- Deux formats : 40 et 80 colonnes ; résolution 320 × 250, matrice
  caractère **8 × 10** (p. 35).
- Jeux standard G0 / G1 / G2 identiques au 1B (annexes 3.6-3.9).
- Invocation : SO → G1, SI → G0, SS2 (1/9) + code → G2 (p. 34).
- Associations (§2.2.2 p. 35) :
  - `ESC 2/8 4/0` : jeu alphanumérique de base → G0
  - `ESC 2/8 2/0 4/2` : jeu DRCS **G'0** → G0
  - `ESC 2/9 6/3` : jeu semi-graphique de base → G1
  - `ESC 2/9 2/0 4/3` : jeu DRCS **G'1** → G1
  - Les jeux de base sont ré-associés à la mise sous tension, à la
    connexion/déconnexion, au reset Vidéotex, à l'accès en rangée 00.
    Toute modification d'association conserve les attributs actifs.
- Nouvelles séquences Vidéotex (§2.5 p. 39) : `CSI 3/6 6/E` demande de
  position curseur ; réponse `CSI Pr 3/B Pc 5/2` (Pr rangée, Pc colonne).

## 3. Téléchargement DRCS (§2.3 p. 35-39)

Sous-ensemble de CEPT T/TE.06.01 partie 4. Deux jeux de 96 caractères
(94 téléchargeables, 2/1 à 7/E ; 2/0 et 7/F restent espace et pavé).

**En-tête** (§2.3.2 p. 36) :
- G'0 : `US 2/3 2/0 2/0 2/0 4/2 4/9`
- G'1 : `US 2/3 2/0 2/0 2/0 4/3 4/9`
- En-tête incomplète ou erronée : ignorée, la précédente reste valide ;
  par défaut (après reset) c'est l'en-tête G'0 qui est prise.

**Transfert de formes** (§2.3.3 p. 36-37) : `US 2/3 Y <données>` avec Y =
code de la première forme (2/1 à 7/E). `<données>` = suite de formes,
chacune précédée du **délimiteur B1 = 3/0** (y compris la première) ; les
formes suivantes occupent les emplacements suivants (Y+1, Y+2…).

**Codage d'une forme** : 14 octets pris dans les colonnes 4 à 7 (4/0 à
7/F), **6 bits utiles par octet (b5..b0)**, remplissage rangée par rangée
à partir du coin supérieur gauche, les bits excédentaires d'un octet
passant à la rangée suivante : 10 rangées × 8 pixels = 80 bits = 13 octets
+ 2 bits ; **les 4 bits de poids faible du 14ᵉ octet sont ignorés**.
Bit à 1 = couleur de caractère, 0 = couleur de fond. Exemple p. 38 (forme
en 5/3 de G'1) : `4/4 4/3 6/0 5/0 4/4 4/1 4/0 6/8 5/1 4/4 5/0 6/8 4/4 4/X`.
Rangée 1 = b5..b0 de l'octet 1 puis b5, b4 de l'octet 2 ; rangée 2 =
b3..b0 de l'octet 2 puis b5..b2 de l'octet 3 ; etc.

Règles :
- B1 reçu avant la fin d'une forme : le reste est rempli en fond.
- Octets excédentaires (0/1 à 7/F, sauf US) après les 14 : filtrés.
- Forme à un code > 7/E : ignorée.
- Pendant une forme, un C0 (sauf US et NUL) ou un code des colonnes 2-3
  (sauf B1) remplit les pixels correspondants en fond (pas de resync).
- **Sortie du téléchargement** (§2.3.3.3 p. 37) : réception de `US X`
  (toute commande US autre que les accès rangée 00) : la forme en cours
  est complétée en fond, X est interprété normalement. Aussi sur mise
  sous tension, connexion/déconnexion, reset Vidéotex, accès rangée 00.
- Rangée 00 (§2.3.4 p. 38) : `US 4/0 X/Y` ou `US 3/0 3/0` interrompent,
  `LF` reprend ; sortie de rangée 00 par FF/RS = sortie sans compléter.

**Attributs** (§2.3.6 p. 38-39) : même attributs que G0/G1 ; en G'1 le
lignage n'a pas d'effet visuel ; double hauteur d'un caractère G'0 : 1ʳᵉ
ligne triplée, les autres doublées sauf la dernière (soulignement).
**SS2** (§2.3.7 p. 39) : si G'0 est actif, `SS2 <accent> X` affiche le
caractère X du jeu DRCS ; si G1/G'1 est invoqué, SS2 est ignoré.

## 4. Modes mixte et téléinformatique (chapitre L'écran §3, p. 39-41)

- Jeux : alphanumérique **américain** (défaut G0) et **français** (défaut
  G1), complémentaire, spécial graphique DEC (annexes 3.12, 3.13).
- Associations G0 : `ESC 2/8 4/2` américain, `ESC 2/8 5/2` français,
  `ESC 2/8 3/0` DEC, `ESC 2/8 3/3` complémentaire ; idem `ESC 2/9 …` pour G1.
- Mode mixte : `CSI 3/6 6/E` position curseur, `CSI 3/C 3/1 6/8` curseur
  éteint, `CSI 3/C 3/1 6/C` curseur allumé.
- Téléinformatique : idem plus `CSI 3/C 3/3 6/8` passage en **40 colonnes**,
  `CSI 3/F 3/3 6/C` passage en **80 colonnes**, `CSI 3/C 3/4 6/8` mode page
  (suite p. 41 : mode rouleau, etc. — à relire avant implémentation).
- Décodage lui-même : renvoi à la STUM 1B (p. 105 §2 mixte, p. 161 §4
  téléinformatique) — **la STUM 1B n'est pas dans le dépôt**.

## 5. Protocole (chapitre Le protocole Vidéotex, p. 58-64 ; séquences p. 100-103)

- Séquences supplémentaires à partir des versions Cv; et Bv8 (p. 103) :
  `PRO3 START VEILLE 1/B 3/B 6/9 5/8 4/1`, `PRO3 STOP VEILLE … 6/A …`,
  `PRO2 STATUS ECRAN 1/B 3/A 7/2 5/8`, réponse `PRO3 REP STATUS ECRAN
  1/B 3/B 7/3 5/8 <status>`.
- Vitesses prise péripherique du Minitel 2 : 300, 1200, 4800, 9600
  « programmable par le périphérique » (note 3 p. 103).

## 6. Ce que cela change pour NeoTel

| Sujet | Avant (v0.1.0) | Après lecture |
|---|---|---|
| Type ENQROM `u` / `v` | non vérifié | **vérifié** (7/5 et 7/6) |
| Constructeur `C` | commodité | **Telic = C**, cohérent |
| 9600 réservé au Minitel 2 | doc courante | **vérifié** (note 3) |
| DRCS | non implémenté faute de spec | **spécifié**, implémentable (§3 ci-dessus) |
| 80 colonnes | non implémenté | séquences CSI connues ; décodage téléinformatique dans la STUM 1B (à récupérer) |
| `CSI 3/6 6/E` position curseur | absent | à implémenter (réponse `CSI Pr;Pc R`) |
