/*-------------------------------------------------------------------------------------------------------------------------------------------
 * z80.h - EN-TETE DE COMPATIBILITE STECCY-GB
 *
 * Les pilotes STECCY ps2key.c et usb_hid_host.c incluent "z80.h" et lisent z80_settings.keyboard.
 * Ce fichier remplace le vrai z80.h de STECCY : il ne fournit que ce dont ces pilotes ont besoin,
 * ce qui permet de les compiler sans aucune modification.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifndef Z80_H
#define Z80_H

#include <stdint.h>

#define KEYBOARD_NONE           0x00
#define KEYBOARD_PS2            0x01
#define KEYBOARD_USB            0x02
#define KEYBOARD_ZX             0x04                    // non supporte par STECCY-GB

typedef struct
{
    uint_fast8_t                keyboard;
} Z80_SETTINGS;

extern Z80_SETTINGS             z80_settings;

#endif
