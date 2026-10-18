/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lxmapkey.h - correspondance clavier PC -> boutons Game Boy (meme interface que lxmapkey.h de STECCY)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifndef LXMAPKEY_H
#define LXMAPKEY_H

#include <stdint.h>

extern volatile uint_fast8_t    lxmapkey_menu_enabled;          // toujours 0 : pas de menu dans le port Linux
extern volatile uint16_t        lxmapkey_menu_scancode;
extern void                     lxkeypress (uint16_t scancode);
extern void                     lxkeyrelease (uint16_t scancode);

#endif
