/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lxcart.h - ROM / sauvegarde / ini sous Linux
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifndef LXCART_H
#define LXCART_H

#include <stdint.h>
#include "gb.h"

extern uint8_t              lx_frame[GB_LCD_HEIGHT][GB_LCD_WIDTH];
extern void                 lx_load_ini (void);
extern int                  lx_load_rom (const char * name);
extern void                 lx_save_ram (int force);

#endif
