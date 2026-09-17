/**
 * @file neo.h
 * @brief Acces a l'API du Neo6502 (bloc de controle $FF00) pour NeoTel
 *
 * Protocole : ecrire les parametres ($FF04..$FF0B), la fonction ($FF01),
 * puis le groupe ($FF00) ; le RP2040 execute et remet $FF00 a 0. Une erreur
 * est signalee dans $FF02. Toute ecriture de parametres doit attendre que
 * la commande precedente soit terminee (neo_wait).
 *
 * Reference : ~/Neo6502firmware/bin/api-listing.md (fork bmarty : groupe 14
 * USB CDC et routage UART 10,19 ; absents du firmware amont, ou ils levent
 * simplement le drapeau d'erreur).
 */

#ifndef NEO_H
#define NEO_H

#ifdef TEST_HOST
/* Sur l'hote, le bloc $FF00 est un tableau fourni par le harnais. */
extern unsigned char neo_regs[16];
#define NEO_CMD   (neo_regs[0])
#define NEO_FN    (neo_regs[1])
#define NEO_ERR   (neo_regs[2])
#define NEO_P     (neo_regs + 4)
void neo_host_dispatch(void);      /* le harnais "execute" la commande */
extern unsigned char* neo_host_ptr; /* adresse hote derriere P1-2 (fichiers) */
#define NEO_SET_ADDR(p)  do { NEO_P[1] = 0; NEO_P[2] = 0; neo_host_ptr = (unsigned char*)(p); } while (0)
#define NEO_SET_ADDR0(p) do { NEO_P[0] = 0; NEO_P[1] = 0; neo_host_ptr = (unsigned char*)(p); } while (0)
#define neo_wait()        ((void)0)
#define neo_call(g, f)    do { NEO_FN = (f); NEO_CMD = (g); neo_host_dispatch(); } while (0)
#else
#define NEO_CMD   (*(volatile unsigned char *)0xFF00)
#define NEO_FN    (*(volatile unsigned char *)0xFF01)
#define NEO_ERR   (*(volatile unsigned char *)0xFF02)
#define NEO_P     ((volatile unsigned char *)0xFF04)   /* P0..P7 */
#define neo_wait()        do { while (NEO_CMD) ; } while (0)
#define neo_call(g, f)    do { NEO_FN = (f); NEO_CMD = (g); neo_wait(); } while (0)
/* Adresse 16 bits d'un tampon dans P1-2 */
#define NEO_SET_ADDR(p)  do { NEO_P[1] = (unsigned char)((unsigned int)(p) & 0xFF); \
                              NEO_P[2] = (unsigned char)((unsigned int)(p) >> 8); } while (0)
#define NEO_SET_ADDR0(p) do { NEO_P[0] = (unsigned char)((unsigned int)(p) & 0xFF); \
                              NEO_P[1] = (unsigned char)((unsigned int)(p) >> 8); } while (0)
#endif

/* Groupes */
#define NEO_G_SYSTEM    1
#define NEO_G_CONSOLE   2
#define NEO_G_GRAPHICS  5
#define NEO_G_SOUND     8
#define NEO_G_UEXT      10
#define NEO_G_BLITTER   12
#define NEO_G_CDC       14

/* Systeme */
#define NEO_F_TIMER        1    /* P0..3 = timer 100 Hz */
#define NEO_F_KEY_STATUS   2    /* P0 = code HID -> P0 = etat, P1 = modificateurs */
#define NEO_F_VERSION      11   /* P0..2 = majeur.mineur.patch */

/* Console */
#define NEO_F_READ_CHAR    1    /* P0 = touche ASCII (0 = rien) */
#define NEO_F_CON_STATUS   2    /* -> P0 = $FF si une touche attend, 0 si file vide */
#define NEO_F_DEF_HOTKEY   4    /* P0 = 1..10, P2-3 = chaine prefixee */
#define NEO_F_CLEAR_SCREEN 12
#define NEO_F_CURSOR_SHOW  19   /* P0 = visible */

/* Graphique */
#define NEO_F_GFX_DEFAULTS 1    /* P0 and, P1 or, P2 solid, P3 taille, P4 flip */
#define NEO_F_DRAW_RECT    3
#define NEO_F_SET_PALETTE  32   /* P0 index, P1-3 RGB */
#define NEO_F_FRAME_COUNT  37

/* Son */
#define NEO_F_SND_BEEP     3
#define NEO_F_SND_QUEUE_EXT 7   /* P0 canal, P1-2 Hz, P3-4 duree cs, P5-6 glissando, P7 type, P8 ($FF0C) volume % */

/* UExt / UART (routage AUTO -> modem USB CDC si present) */
#define NEO_F_UART_FORMAT  15   /* P0-3 bauds, P4 protocole (0 = 8N1) */
#define NEO_F_UART_WRITE   16   /* P0 = octet */
#define NEO_F_UART_READ    17   /* -> P0 ; erreur 1 si rien */
#define NEO_F_UART_AVAIL   18   /* -> P0 != 0 si octet disponible */
#define NEO_F_UART_ROUTE   19   /* P0 = 0 UEXT, 1 CDC, 2 AUTO -> P0 = mode */

/* Blitter */
#define NEO_F_BLIT_COPY    2    /* P0:P1-2 src, P3:P4-5 dst, P6-7 taille */
#define NEO_F_BLIT_COMPLEX 3    /* P0 action, P1-2 rect src, P3-4 rect dst */

/* USB CDC (fork) */
#define NEO_F_CDC_STATUS   1    /* P7 = peripherique -> P0 connecte, P1-2 octets */

/* Codes HID des fleches (kbdcodes.h du firmware) */
#define HID_RIGHT  0x4F
#define HID_LEFT   0x50
#define HID_DOWN   0x51
#define HID_UP     0x52

#endif /* NEO_H */
