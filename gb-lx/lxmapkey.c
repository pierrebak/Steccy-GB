/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lxmapkey.c - clavier PC -> boutons Game Boy
 *
 * Les scancodes sont ceux de STECCY (scancodes.h, jeu AT "set 1") : ils designent une POSITION physique,
 * donc la correspondance marche quelle que soit la disposition (AZERTY, QWERTY, QWERTZ) :
 *
 *   Fleches / pave numerique 8 4 6 2  -> croix directionnelle
 *   touche a droite de Shift gauche     -> B   (W en AZERTY, Z en QWERTY, Y en QWERTZ)
 *   touche suivante                     -> A   (X)
 *   Espace, pave 0                      -> A
 *   Entree                              -> START
 *   Retour arriere, Shift droit         -> SELECT
 *   F2 palette, F3 turbo, F5 reset, F12 / Echap quitter (F3 et F12 comme dans STECCY)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include "lxgb.h"
#include "lxmapkey.h"
#include "scancodes.h"

volatile uint_fast8_t       lxmapkey_menu_enabled  = 0;
volatile uint16_t           lxmapkey_menu_scancode = 0;
volatile uint_fast8_t       steccy_exit            = 0;
volatile uint_fast8_t       lx_reset_request       = 0;

static uint8_t
scancode_to_button (uint16_t scancode)
{
    switch (scancode)
    {
        case SCANCODE_L_ARROW:   case SCANCODE_L_ARROW_EXT:   case SCANCODE_KEYPAD_4:   return GB_BTN_LEFT;
        case SCANCODE_R_ARROW:   case SCANCODE_R_ARROW_EXT:   case SCANCODE_KEYPAD_6:   return GB_BTN_RIGHT;
        case SCANCODE_U_ARROW:   case SCANCODE_U_ARROW_EXT:   case SCANCODE_KEYPAD_8:   return GB_BTN_UP;
        case SCANCODE_D_ARROW:   case SCANCODE_D_ARROW_EXT:   case SCANCODE_KEYPAD_2:
        case SCANCODE_KEYPAD_5:                                                         return GB_BTN_DOWN;   // 5 = bas, comme STECCY
        case SCANCODE_KEYPAD_7:                                                         return GB_BTN_UP | GB_BTN_LEFT;
        case SCANCODE_KEYPAD_9:                                                         return GB_BTN_UP | GB_BTN_RIGHT;
        case SCANCODE_KEYPAD_1:                                                         return GB_BTN_DOWN | GB_BTN_LEFT;
        case SCANCODE_KEYPAD_3:                                                         return GB_BTN_DOWN | GB_BTN_RIGHT;
        case SCANCODE_X:         case SCANCODE_SPACE:         case SCANCODE_KEYPAD_0:   return GB_BTN_A;
        case SCANCODE_Y:                                                                return GB_BTN_B;
        case SCANCODE_ENTER:                                                            return GB_BTN_START;
        case SCANCODE_BACKSPACE: case SCANCODE_RSHIFT:                                  return GB_BTN_SELECT;
        default:                                                                        return 0;
    }
}

void
lxkeypress (uint16_t scancode)
{
    uint8_t b = scancode_to_button (scancode);

    if (b)
    {
        gb_press_button (b);
        return;
    }

    switch (scancode)
    {
        case SCANCODE_F2:   gb_settings.palette = (uint8_t) ((gb_settings.palette + 1) % 3);   break;
        case SCANCODE_F3:   gb_settings.turbo_mode = ! gb_settings.turbo_mode;                break;
        case SCANCODE_F5:   lx_reset_request = 1;                                             break;
        case SCANCODE_ESC:
        case SCANCODE_F12:  steccy_exit = 1;                                                  break;
        default:                                                                              break;
    }
}

void
lxkeyrelease (uint16_t scancode)
{
    uint8_t b = scancode_to_button (scancode);

    if (b)
    {
        gb_release_button (b);
    }
}
