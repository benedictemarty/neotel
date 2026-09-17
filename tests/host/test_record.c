/**
 * @file test_record.c
 * @brief Tests hote de l'enregistrement / relecture .vdt (record.c) sur le
 *        disque en memoire de neo_stub.c : noms neoNN.vdt, ecriture par
 *        blocs de 64 octets, arret vidant le tampon, relecture octet par
 *        octet jusqu'a la fin, fichier absent, creation refusee, exclusion
 *        enregistrement/relecture, fermeture des canaux.
 */

#include <stdio.h>
#include <string.h>
#include "record.h"
#include "neo_stub.h"

static int run, pass;
#define CHECK(c, name) do { ++run; if (c) ++pass; else printf("FAIL : %s (ligne %d)\n", name, __LINE__); } while (0)

int main(void)
{
    char name[RECORD_NAME_MAX];
    unsigned char data[200];
    host_disk_file_t* f;
    int i, n;

    record_make_name(name, 1);   CHECK(strcmp(name, "neo01.vdt") == 0, "nom 1");
    record_make_name(name, 42);  CHECK(strcmp(name, "neo42.vdt") == 0, "nom 42");
    record_make_name(name, 99);  CHECK(strcmp(name, "neo99.vdt") == 0, "nom 99");
    record_make_name(name, 100); CHECK(strcmp(name, "neo01.vdt") == 0, "nom 100 : repart a 01");
    record_make_name(name, 0);   CHECK(strcmp(name, "neo01.vdt") == 0, "nom 0 : 01");

    /* Sans enregistrement : record_byte ne fait rien */
    CHECK(record_active() == 0, "inactif au depart");
    record_byte(0x41);
    CHECK(host_disk[0].name[0] == 0 && host_open_count == 0, "octet ignore sans enregistrement");

    /* Enregistrement de 200 octets : 3 blocs de 64 puis 8 au stop */
    for (i = 0; i < 200; ++i) data[i] = (unsigned char)i;
    CHECK(record_start("neo01.vdt") == 1 && record_active() == 1 && host_open_count == 1, "demarrage");
    for (i = 0; i < 100; ++i) record_byte(data[i]);
    f = host_disk_find("neo01.vdt");
    CHECK(f && f->len == 64, "ecriture par blocs de 64 : 100 octets -> 64 ecrits");
    for (; i < 200; ++i) record_byte(data[i]);
    CHECK(f->len == 192, "192 ecrits, 8 en tampon");
    record_stop();
    CHECK(record_active() == 0 && host_open_count == 0 && f->len == 200 &&
          memcmp(f->data, data, 200) == 0, "arret : tampon vide, fichier complet et ferme");
    record_stop();
    CHECK(host_open_count == 0, "double arret sans effet");

    /* Un second demarrage pendant un enregistrement ferme le premier */
    CHECK(record_start("neo02.vdt") == 1, "demarrage 2");
    record_byte(0x55);
    CHECK(record_start("neo03.vdt") == 1 && host_open_count == 1, "redemarrage ferme le precedent");
    CHECK(host_disk_find("neo02.vdt")->len == 1, "le precedent a ete vide");
    record_stop();

    /* Creation refusee */
    host_disk_ro = 1;
    CHECK(record_start("neo04.vdt") == 0 && record_active() == 0 && host_open_count == 0, "creation refusee");
    host_disk_ro = 0;

    /* Relecture */
    CHECK(replay_open("absent.vdt") == 0 && host_open_count == 0, "fichier absent");
    CHECK(replay_open("neo01.vdt") == 1 && host_open_count == 1, "ouverture relecture");
    n = 0;
    while (replay_pending() && n < 300) { CHECK(replay_next() == data[n], "octet relu"); ++n; }
    CHECK(n == 200, "200 octets relus puis fin");
    CHECK(replay_pending() == 0, "fin stable");
    replay_close();
    CHECK(host_open_count == 0, "fermeture relecture");

    /* Fichier vide : rien a relire */
    CHECK(record_start("neo05.vdt") == 1, "fichier vide");
    record_stop();
    CHECK(replay_open("neo05.vdt") == 1 && replay_pending() == 0, "vide : pas d'octet");
    replay_close();

    /* Relecture pendant un enregistrement : l'enregistrement est arrete */
    CHECK(record_start("neo06.vdt") == 1, "enregistrement 6");
    record_byte(1); record_byte(2);
    CHECK(replay_open("neo01.vdt") == 1 && record_active() == 0 &&
          host_disk_find("neo06.vdt")->len == 2, "relecture arrete l'enregistrement (tampon partage)");
    replay_close();

    printf("test_record : %d/%d\n", pass, run);
    return pass == run ? 0 : 1;
}
