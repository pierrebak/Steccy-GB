/*-------------------------------------------------------------------------------------------------------------------------------------------
 * keypress.h - EN-TETE DE COMPATIBILITE STECCY-GB
 *
 * Dans STECCY, keypress() convertit un scancode PS/2 en touche de la matrice ZX. Ici la fonction est
 * fournie par gbinput.c et convertit le scancode en bouton Game Boy. Les pilotes ps2key.c et
 * usb_hid_host.c de STECCY l'appellent sans le savoir.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifndef KEYPRESS_H
#define KEYPRESS_H

#include <stdint.h>

extern void                     keypress (uint16_t scancode);

#endif
