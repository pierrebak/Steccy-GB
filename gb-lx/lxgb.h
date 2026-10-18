/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lxgb.h - variables globales du port Linux de STECCY-GB (remplace z80.h pour lxkbd.c repris de STECCY)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifndef LXGB_H
#define LXGB_H

#include <stdint.h>
#include "gb.h"

extern volatile uint_fast8_t    steccy_exit;                    // 1 => quitter (F12, fermeture de fenetre)
extern volatile uint_fast8_t    lx_reset_request;               // 1 => reset demande (F5)

#endif
