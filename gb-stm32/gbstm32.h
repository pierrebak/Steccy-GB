/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbstm32.h - port STM32F407 de STECCY-GB
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifndef GBSTM32_H
#define GBSTM32_H

#include <stdint.h>

#define GBKEY_UP                1                               // touches de navigation du menu
#define GBKEY_DOWN              2
#define GBKEY_PGUP              3
#define GBKEY_PGDN              4
#define GBKEY_OK                5
#define GBKEY_BACK              6

extern volatile uint32_t        uptime;                         // main.c : unites de 20 us
extern volatile uint_fast8_t    update_display;                 // main.c (inutilise ici, garde pour compatibilite)
extern volatile uint_fast8_t    gbstm32_reset_request;
extern volatile uint_fast8_t    gbstm32_display_request;
extern volatile uint_fast8_t    gbinput_menu_request;

extern void                     gbinput_poll (uint_fast8_t in_menu);
extern uint16_t                 gbinput_get_key (void);
extern int                      gbmenu (char * selected, int size, uint_fast8_t rom_loaded);
extern void                     gb_stm32 (void);

#define GBMENU_RESUME           0
#define GBMENU_LOAD_ROM         1
#define GBMENU_RESET            2
#define GBMENU_SAVE             3

#endif
