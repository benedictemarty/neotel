# Manuel d'utilisation — NeoTel

## Matériel

- Un **Neo6502** avec le firmware Neo6502 **fork bmarty** (`~/Neo6502firmware`,
  groupe 14 USB CDC + routage UART 10,19) et un clavier USB.
- Un modem Hayes :
  - **PicoWiFiModemUSB** sur le port USB hôte (recommandé : c'est le montage
    d'OricTel, commandes `AT$SSID` / `AT$PASS` / `ATC1` / `AT&W` pour le
    Wi-Fi, `ATDT hôte:port` pour appeler un serveur) ;
  - ou tout modem AT sur l'UART de l'UEXT (115200 8N1). C'est le seul chemin
    avec le firmware amont (l'écran « Liaison série » le dit).

Le programme est `build/neotel.neo` (chargé et lancé à `$0800`) : depuis
NeoBASIC, `run "neotel.neo"` (comme AsteroNeo, non vérifié sur carte pour NeoTel).
En sortant (ESC au menu principal), NeoTel recharge NeoBASIC.

## Écrans

Tous les écrans locaux suivent la même charte (v0.9.1) : bandeau bleu en
haut avec le titre en double hauteur et une information à droite (version,
profil, numéro de page), filets mosaïques, items `[touche] libellé ....
valeur`, item courant sur fond bleu, pied de page avec les touches utiles.
Dans les menus, les **flèches** haut/bas déplacent l'item courant, `ENVOI`,
`Entrée`, `Suite` ou `→` le valident ; les **chiffres** agissent toujours
directement.

1. **Splash** : titre, version, auteur, licence. Une touche ou 5 s.
2. **Liaison série** : carte verte « modem USB CDC détecté » (API routée
   vers lui) ou jaune « UART UEXT ». Une touche.
3. **Menu principal** (la valeur courante de chaque réglage est affichée
   sur la ligne de l'item : dernier serveur, profil, aspect, dernier
   enregistrement…) :
   - `1` Modem AT → choix du serveur → connexion → session ;
   - `2` Config Wi-Fi du PicoWiFiModemUSB : scan (`AT$SCAN`), choix du
     réseau, mot de passe (masqué), association, attente d'IP, sauvegarde
     (`AT&W`) ;
   - `3` Terminal : **Minitel 1B** ↔ **Minitel 2** (voir
     [MINITEL_1B_VS_2.md](MINITEL_1B_VS_2.md)) ;
   - `4` Aspect : **couleur** ↔ **gris (1B mono)** ;
   - `5` Identification ENQROM : OFF (défaut) ↔ ON ;
   - `6` Relire un `.vdt` : NeoTel **liste les enregistrements** `neoNN.vdt`
     de la carte, chacun choisi par une lettre (`A`, `B`…) ; `ENVOI` rejoue
     le dernier, `ESC` annule. La touche `Suppr` (ou `Correction`) puis une
     lettre efface l'enregistrement correspondant. Sans enregistrement, on
     saisit un nom. La page
     est rejouée comme si le serveur l'envoyait (mode Mixte compris), sans
     liaison ; la barre de statut affiche le nom ; `ESC ESC` revient au menu ;
   - `7` Son : ON (défaut) ↔ OFF (BEL des serveurs et bip du splash) ;
   - `H` : **aide** en français ou en anglais (2 pages touche / action ;
     `Espace`/`→` page suivante, `P`/`←` précédente, `L` change la langue,
     `ESC` revient au menu) ;
   - `ESC` : retour à NeoBASIC.
4. **Serveur** : `1` PAVI 3617 (`pavi.3617.fr:3617`), `2` MiniPavi
   (`go.minipavi.fr:516`), `3` 3617.fr en WebSocket (`wss://3617.fr/ws`),
   `4` saisie libre `hôte:port` ou `ws://…` / `wss://…` (Envoi valide,
   Annulation/ESC annule). L'item courant à l'ouverture est le **dernier
   serveur utilisé** : `ENVOI` le recompose ; `ESC` revient au menu. Les cibles
   WebSocket sont relayées par le faux modem sur PC (module python3
   `websockets`) ; sur un PicoWiFiModemUSB réel, ce n'est **pas vérifié**
   (pas de commande AT WebSocket connue : le modem répondra sans doute
   `NO CARRIER`, écran « ÉCHEC DE CONNEXION »).

Les réglages (profil, aspect, identification, dernier serveur, son,
compteur d'enregistrement) sont sauvés
dans `neotel.cfg` sur la carte SD / clé USB à chaque changement et relus au
démarrage. Sans carte ou sans fichier : valeurs par défaut, sans message.
5. **Connexion** : `ATZ` (si pas de `OK` : `+++`/`ATH` pour raccrocher un
   modem resté en ligne, puis `ATZ` à nouveau), `ATI` jusqu'à
   « CONNECTED TO WIFI » (immédiat sur un modem sans Wi-Fi), `ATDT hôte:port`,
   attente de `CONNECT` (10 s). Sans `CONNECT` : écran **ÉCHEC DE CONNEXION**
   (`1` réessayer, `2` autre serveur, `3` entrer quand même, `ESC` menu).
6. **Session** : la page Videotex occupe les 225 lignes du haut ; la barre
   de statut en bas affiche `C`/`F` (données reçues il y a moins de 30 s),
   le serveur, le chrono `mm:ss`, le profil (`1B`/`M2`), la vitesse
   programmée, l'aspect (`COUL`/`GRIS`) et le rappel `F1` (`RE` rouge
   pendant un enregistrement).
7. **Perte de porteuse** : quand le modem émet une ligne `NO CARRIER` suivie
   de 4 s de silence : `1` reconnecter, `2` rester en local, `ESC` menu.

### Mode Mixte / Téléinformatique (80 colonnes)

Quand le serveur envoie `PRO2 MIXTE 1`, NeoTel passe en **80 × 25** (mode
vidéo Hercules 720 × 350 du firmware bmarty ; sur le firmware amont un message
« 80 colonnes: firmware sans mode 1 » s'affiche et l'écran reste en Videotex).
Le clavier devient **étendu** : `Entrée` = CR, `CTRL+lettre` = code de
contrôle, `Suppr` = BS, flèches = `CSI A/B/C/D` ; `F1-F9` restent les touches
Minitel (SEP), `ESC` reste la sortie locale (la question s'affiche sur la
rangée 00). Il n'y a pas de barre de statut en mode 1. Le retour au Videotex
se fait par le serveur (`PRO2 MIXTE 2`, `CSI ? {`) ou par `ESC ESC`.

## Touches en session

| Touche | Minitel | Code émis |
|---|---|---|
| `F1` / `CTRL+S` | Sommaire | SEP $46 |
| `F2` / `CTRL+A` / `Suppr` | Annulation | SEP $45 |
| `F3` / `←` | Retour | SEP $42 |
| `F4` / `CTRL+R` | Répétition | SEP $43 |
| `F5` / `CTRL+G` | Guide | SEP $44 |
| `F6` / `Retour arrière` | Correction | SEP $47 |
| `F7` / `→` | Suite | SEP $48 |
| `F8` / `Entrée` | Envoi | SEP $41 |
| `F9` / `CTRL+C` | Connexion/Fin | SEP $49 |
| Flèches en mode curseur (activé par le serveur, PRO3) | curseur | CSI A/B/C/D |
| lettres, chiffres, ponctuation | texte | ASCII 7 bits |

Touches locales (rien n'est envoyé) : `F10` / `CTRL+D` aspect couleur/gris,
`CTRL+L` effacer la page, `CTRL+F` réinitialiser la liaison série, `CTRL+O`
enregistrer / arrêter (voir ci-dessous), `ESC`
pose la question « quitter ? » dans la barre de statut : `ESC` à nouveau
raccroche (`+++`, `ATH`) et revient au menu, toute autre touche reprend.

Les flèches partagent leurs codes avec `CTRL+A/D/S/W` dans le firmware :
NeoTel regarde si une flèche est physiquement enfoncée au moment où il lit
la touche. En frappe très rapide, une flèche relâchée avant la lecture est
interprétée comme le `CTRL` correspondant.

L'écho local est **désactivé** par défaut (aiguillage clavier → écran OFF,
comme sur un Minitel 1B) : ce qu'on tape n'apparaît que si le serveur le
renvoie ou active l'écho (PRO3 SWITCH ON écran ← clavier).

## Enregistrer une page (`CTRL+O`)

En session, `CTRL+O` ouvre `neoNN.vdt` (NN = 01, 02… jusqu'à 99, puis 01)
sur la carte SD / clé USB et y copie **tel quel** tout ce que le serveur
envoie à partir de ce moment (pas d'instantané de l'écran : commencer
l'enregistrement avant de demander la page). `CTRL+O` de nouveau, `ESC ESC`
ou une perte de porteuse ferment le fichier. Sans carte : message
« Enregistrement impossible ». Le fichier est un flux Videotex brut,
relisible par le menu `6`, par `tests/host/render_page` ou par tout
lecteur `.vdt`.

## Sur PC (émulateurs)

```sh
make run                    # neo + faux modem (ATDT = vraie connexion TCP)
make run-phos               # Phosphoneo (fenêtre SDL x3)
make run MODEM=--serve      # page de test locale, sans réseau
NEO_CDC_TTY=/dev/ttyACM0 ~/Neo6502firmware/bin/neo build/neotel.bin@800 cold   # vrai Pico W sur le PC
```

`tools/fake_modem.py` accepte `--serve`, `--page FICHIER.vdt`, `--nc S`
(NO CARRIER après S secondes), `--echo-server`, `--guard S`, `--log F`.
