/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lxdisplay.h - affichage de l'ecran Game Boy (meme role que lxdisplay.h de STECCY)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifndef LXDISPLAY_H
#define LXDISPLAY_H

#include <stdint.h>

extern unsigned int     zx_display_width;                       // noms conserves pour lxfb.c repris de STECCY
extern unsigned int     zx_display_height;
extern uint_fast8_t     lx_display_cached;                      // 0 => tout redessiner
extern const uint32_t   lx_palettes[3][4];
extern void             lxdisplay_init (unsigned int, unsigned int);
extern void             lxdisplay_update (void);                // affiche lx_frame[][]

#endif
