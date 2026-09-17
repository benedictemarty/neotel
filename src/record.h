/**
 * @file record.h
 * @brief Enregistrement du flux recu (.vdt) sur la carte et relecture
 *
 * CTRL+O en session enregistre les octets recus du serveur, tels quels, dans
 * un fichier "neoNN.vdt" (NN = compteur des reglages) par l'API fichiers
 * (3,4 ouverture, 3,9 ecriture par blocs de 64 octets, 3,5 fermeture) ;
 * le nom est neoNN.vdt, NN = 01..99 (le compteur repart a 01 apres 99) ;
 * CTRL+O de nouveau arrete. Le menu "Relire" rejoue un fichier dans le
 * decodeur, sans liaison, comme si le serveur l'envoyait. Le format est le
 * flux Videotex brut (meme format que les pages .vdt de tests/).
 *
 * Un enregistrement demarre au milieu d'une page : la relecture commence la
 * ou l'enregistrement a commence (pas d'instantane de l'ecran).
 */

#ifndef RECORD_H
#define RECORD_H

#define RECORD_NAME_MAX 16      /* "neo99.vdt" + saisie libre */
#define RECORD_CHUNK    64

/** Demarre l'enregistrement dans le fichier name (chaine C). 1 si ouvert. */
unsigned char record_start(const char* name);

/** Ajoute un octet (tampon de 64, ecrit quand plein). */
void record_byte(unsigned char b);

/** Vide le tampon et ferme. */
void record_stop(void);

/** 1 si un enregistrement est en cours. */
unsigned char record_active(void);

/** Compose "neoNN.vdt" dans name (>= RECORD_NAME_MAX). */
void record_make_name(char* name, unsigned char index);

/** Enumere les enregistrements "neoNN.vdt" du repertoire courant (3,17-3,19),
 *  ecrit leurs numeros NN (1..99) tries dans idx[0..max-1] ; retourne le
 *  nombre trouve (au plus max). N'ouvre rien d'autre en meme temps. */
unsigned char record_list(unsigned char* idx, unsigned char max);

/** Efface l'enregistrement neoNN.vdt (index 1..99) par 3,13. 1 si efface. */
unsigned char record_delete(unsigned char index);

/** Relecture : ouverture (1 si ok), puis replay_pending() != 0 tant qu'un
 *  octet reste (lecture par blocs de 64 dans le tampon partage), replay_next()
 *  le rend ; fermeture. Exclusif de l'enregistrement (meme tampon). */
unsigned char replay_open(const char* name);
unsigned char replay_pending(void);
unsigned char replay_next(void);
void replay_close(void);

#endif /* RECORD_H */
