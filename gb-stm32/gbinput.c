/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbinput.c - entrees STM32 de STECCY-GB : clavier PS/2 ou USB (pilotes STECCY), manette Wii (Nunchuk / Gamepad)
 *
 * keypress() est appelee par les pilotes STECCY ps2key.c (depuis l'interruption) et usb_hid_host.c.
 * Elle ne touche PAS directement au coeur : elle met a jour un masque volatile que la boucle principale
 * transmet au coeur une fois par image (evite une course sur le registre IF).
 *
 * Clavier (scancodes PS/2 = positions physiques, donc valable en AZERTY) :
 *   fleches / pave 8 4 6 2 -> croix    X -> A    W (QWERTY Z) -> B    Espace, pave 0 -> A
 *   Entree -> START        Retour arriere, Shift droit -> SELECT
 *   TAB ou Echap -> menu   F1 orientation, F2 ordre RGB, F3 turbo (comme STECCY)   F5 -> reset
 *
 * Wii Gamepad : croix / stick gauche -> croix, A et X -> A, B et Y -> B, + -> START, - -> SELECT, HOME -> menu
 * Wii Nunchuk : stick -> croix, Z -> A, C -> B, Z+C maintenus -> START
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <stdint.h>
#include "z80.h"
#include "keypress.h"
#include "zxkbd.h"
#include "ps2key.h"
#include "wii-gamepad.h"
#include "gb.h"
#include "gbstm32.h"

Z80_SETTINGS                    z80_settings = { KEYBOARD_PS2 };    // lu par ps2key.c de STECCY

static volatile uint8_t         kbd_buttons;                        // boutons GB tenus au clavier
static volatile uint8_t         wii_buttons;                        // boutons GB tenus a la manette
volatile uint16_t               gbinput_last_key;                   // dernier code "menu" (GBKEY_xxx), 0 = aucun
volatile uint_fast8_t           gbinput_menu_request;               // 1 => ouvrir le menu

void
zxkbd_emulate_ps2keys (void)                                        // appele par ps2key.c seulement si KEYBOARD_ZX
{
}

static uint8_t
scancode_to_button (uint16_t sc)
{
    switch (sc)
    {
        case SCANCODE_L_ARROW: case SCANCODE_KP_4:                  return GB_BTN_LEFT;
        case SCANCODE_R_ARROW: case SCANCODE_KP_6:                  return GB_BTN_RIGHT;
        case SCANCODE_U_ARROW: case SCANCODE_KP_8:                  return GB_BTN_UP;
        case SCANCODE_D_ARROW: case SCANCODE_KP_2: case SCANCODE_KP_5: return GB_BTN_DOWN;
        case SCANCODE_X: case SCANCODE_SPACE: case SCANCODE_KP_0:   return GB_BTN_A;
        case SCANCODE_Z:                                            return GB_BTN_B;    // position QWERTY Z = AZERTY W
        case SCANCODE_ENTER: case SCANCODE_KP_ENTER:                return GB_BTN_START;
        case SCANCODE_BSP: case SCANCODE_RSHFT:                     return GB_BTN_SELECT;
        default:                                                    return 0;
    }
}

static uint16_t
scancode_to_menukey (uint16_t sc)
{
    switch (sc)
    {
        case SCANCODE_U_ARROW: case SCANCODE_KP_8:                  return GBKEY_UP;
        case SCANCODE_D_ARROW: case SCANCODE_KP_2:                  return GBKEY_DOWN;
        case SCANCODE_L_ARROW: case SCANCODE_PG_UP:                 return GBKEY_PGUP;
        case SCANCODE_R_ARROW: case SCANCODE_PG_DN:                 return GBKEY_PGDN;
        case SCANCODE_ENTER: case SCANCODE_KP_ENTER: case SCANCODE_SPACE: return GBKEY_OK;
        case SCANCODE_ESC: case SCANCODE_TAB:                       return GBKEY_BACK;
        default:                                                    return 0;
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * keypress () - appelee par les pilotes clavier de STECCY
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
keypress (uint16_t scancode)
{
    uint16_t    sc = scancode & ~PS2KEY_RELEASED_FLAG;
    uint8_t     b  = scancode_to_button (sc);

    if (scancode & PS2KEY_RELEASED_FLAG)
    {
        kbd_buttons = (uint8_t) (kbd_buttons & ~b);
        return;
    }

    kbd_buttons = (uint8_t) (kbd_buttons | b);
    gbinput_last_key = scancode_to_menukey (sc);

    switch (sc)
    {
        case SCANCODE_TAB:
        case SCANCODE_ESC:  gbinput_menu_request = 1;                                   break;
        case SCANCODE_F1:   gb_settings.orientation = (uint8_t) ((gb_settings.orientation + 1) & 3); gbstm32_display_request = 1; break;
        case SCANCODE_F2:   gb_settings.rgb_order = ! gb_settings.rgb_order;            gbstm32_display_request = 1; break;
        case SCANCODE_F3:   gb_settings.turbo_mode = ! gb_settings.turbo_mode;          break;
        case SCANCODE_F5:   gbstm32_reset_request = 1;                                  break;
        default:                                                                        break;
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * decode_wii () - convertit wii_data (rempli par wii-gamepad.c de STECCY) en boutons Game Boy
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint8_t
decode_wii (void)
{
    uint8_t     b = 0;
    uint16_t    w = wii_data.buttons;

    if (wii_type == WII_TYPE_GAMEPAD)
    {
        if (wii_data.lx > WII_GAMEPAD_LX_RIGHT_VALUE || (w & WII_BUTTON_DIRECTION_RIGHT_MASK))  b |= GB_BTN_RIGHT;
        if (wii_data.lx < WII_GAMEPAD_LX_LEFT_VALUE  || (w & WII_BUTTON_DIRECTION_LEFT_MASK))   b |= GB_BTN_LEFT;
        if (wii_data.ly > WII_GAMEPAD_LY_UP_VALUE    || (w & WII_BUTTON_DIRECTION_UP_MASK))     b |= GB_BTN_UP;
        if (wii_data.ly < WII_GAMEPAD_LY_DOWN_VALUE  || (w & WII_BUTTON_DIRECTION_DOWN_MASK))   b |= GB_BTN_DOWN;
        if (w & (WII_BUTTON_A_MASK | WII_BUTTON_X_MASK))                                        b |= GB_BTN_A;
        if (w & (WII_BUTTON_B_MASK | WII_BUTTON_Y_MASK))                                        b |= GB_BTN_B;
        if (w & WII_BUTTON_PLUS_MASK)                                                           b |= GB_BTN_START;
        if (w & WII_BUTTON_MINUS_MASK)                                                          b |= GB_BTN_SELECT;
    }
    else if (wii_type == WII_TYPE_NUNCHUK)
    {
        if (wii_data.lx > WII_NUNCHUK_LX_RIGHT_VALUE)   b |= GB_BTN_RIGHT;
        if (wii_data.lx < WII_NUNCHUK_LX_LEFT_VALUE)    b |= GB_BTN_LEFT;
        if (wii_data.ly > WII_NUNCHUK_LY_UP_VALUE)      b |= GB_BTN_UP;
        if (wii_data.ly < WII_NUNCHUK_LY_DOWN_VALUE)    b |= GB_BTN_DOWN;

        if ((w & WII_BUTTON_Z_LEFT_MASK) && (w & WII_BUTTON_C_MASK))
        {
            b |= GB_BTN_START;
        }
        else
        {
            if (w & WII_BUTTON_Z_LEFT_MASK)             b |= GB_BTN_A;
            if (w & WII_BUTTON_C_MASK)                  b |= GB_BTN_B;
        }
    }
    return b;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbinput_poll () - appelee une fois par image : manette Wii (I2C/DMA non bloquant) et transmission au coeur
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gbinput_poll (uint_fast8_t in_menu)
{
    static uint8_t  last_wii;
    static uint8_t  repeat;

    if (wii_type != WII_TYPE_NONE)
    {
        if (wii_state == WII_STATE_BUSY)
        {
            if (wii_finished_transmission ())                               // decode wii_data
            {
                uint8_t b = decode_wii ();

                if (wii_type == WII_TYPE_GAMEPAD && (wii_data.buttons & WII_BUTTON_HOME_MASK))
                {
                    gbinput_menu_request = 1;
                }

                if (in_menu)                                                // navigation dans le menu avec repetition lente
                {
                    if (b != last_wii || ++repeat >= 12)
                    {
                        repeat = 0;

                        if (b & GB_BTN_UP)                                  gbinput_last_key = GBKEY_UP;
                        else if (b & GB_BTN_DOWN)                           gbinput_last_key = GBKEY_DOWN;
                        else if (b & GB_BTN_LEFT)                           gbinput_last_key = GBKEY_PGUP;
                        else if (b & GB_BTN_RIGHT)                          gbinput_last_key = GBKEY_PGDN;
                        else if ((b & (GB_BTN_A | GB_BTN_START)) && ! (last_wii & (GB_BTN_A | GB_BTN_START)))  gbinput_last_key = GBKEY_OK;
                        else if ((b & GB_BTN_B) && ! (last_wii & GB_BTN_B)) gbinput_last_key = GBKEY_BACK;
                    }
                }
                last_wii    = b;
                wii_buttons = in_menu ? 0 : b;
            }
        }
        else
        {
            (void) wii_start_transmission ();                               // resultat lu a l'image suivante
        }
    }

    if (in_menu)
    {
        gb_set_buttons (0);
    }
    else
    {
        gb_set_buttons ((uint8_t) (kbd_buttons | wii_buttons));
    }
}

uint16_t
gbinput_get_key (void)
{
    uint16_t k = gbinput_last_key;
    gbinput_last_key = 0;
    return k;
}
