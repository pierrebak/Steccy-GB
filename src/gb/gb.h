/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb.h - STECCY-GB : coeur d'emulation Game Boy (DMG) portable
 *
 * Architecture reprise de STECCY (ukw100/STECCY) :
 *   STECCY            STECCY-GB
 *   z80/z80.c    ->   gb/sm83.c     (CPU Sharp SM83 / LR35902)
 *   zxram/zxram.c->   gb/gbmem.c    (memoire + controleurs MBC de cartouche)
 *   zxscr/zxscr.c->   gb/gbppu.c    (PPU : generation de l'image ligne par ligne)
 *   zxio/zxio.c  ->   gb/gbio.c     (registres E/S : joypad, timer, serie, interruptions)
 *   speaker.c    ->   gb/gbapu.c    (son : 4 voies au lieu du haut-parleur 1 bit)
 *   zx_spectrum()->   gb_run()      (boucle principale, cadencement)
 *
 * Le coeur ne depend d'aucune plateforme : il appelle les fonctions gbplat_*() que chaque
 * port (Linux X11/framebuffer, STM32F407) doit fournir.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifndef GB_H
#define GB_H

#include <stdint.h>

#define GB_LCD_WIDTH                160
#define GB_LCD_HEIGHT               144
#define GB_CLOCK_HZ                 4194304UL                       // horloge CPU (T-cycles/s)
#define GB_CYCLES_PER_FRAME         70224UL                         // 154 lignes * 456 dots => 59.73 Hz
#define GB_FRAME_USEC               16743UL                         // 70224 / 4194304 s

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Boutons (1 = enfonce)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#define GB_BTN_RIGHT                0x01
#define GB_BTN_LEFT                 0x02
#define GB_BTN_UP                   0x04
#define GB_BTN_DOWN                 0x08
#define GB_BTN_A                    0x10
#define GB_BTN_B                    0x20
#define GB_BTN_SELECT               0x40
#define GB_BTN_START                0x80

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Interruptions (registre IF / IE)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#define GB_INT_VBLANK               0x01
#define GB_INT_STAT                 0x02
#define GB_INT_TIMER                0x04
#define GB_INT_SERIAL               0x08
#define GB_INT_JOYPAD               0x10

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Reglages (equivalent de Z80_SETTINGS dans STECCY, lus dans steccy-gb.ini)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#if defined STM32F4XX
#define GB_MAX_FILENAME_LEN         63
#else
#define GB_MAX_FILENAME_LEN         255
#endif

typedef struct
{
    char                path[GB_MAX_FILENAME_LEN + 1];                  // repertoire des ROMs (Linux)
    char                romfile[GB_MAX_FILENAME_LEN + 1];               // ROM a charger au demarrage
    uint8_t             palette;                                        // 0 = vert DMG, 1 = gris, 2 = "pocket"
    uint8_t             scale;                                          // facteur de zoom (0 = auto)
    uint8_t             turbo_mode;                                     // 1 = pas de frein de vitesse
    uint8_t             frameskip;                                      // 0 = aucun, n = n images sautees sur n+1
    uint8_t             show_info;                                      // STM32 : 1 = images/s en haut a droite, 2 = + repartition du temps CPU
    uint8_t             tft_fast;                                       // STM32 ILI9341 : 1 = ecritures FSMC rapides
    uint8_t             sound;                                          // STM32 : 1 = son sur le DAC (PA4)
    uint_fast8_t        keyboard;                                       // STM32 : PS2 / USB (masque)
    uint8_t             rgb_order;                                      // STM32 : ordre RGB du TFT
    uint8_t             orientation;                                    // STM32 : orientation du TFT
} GB_SETTINGS;

extern GB_SETTINGS          gb_settings;

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Informations cartouche
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
typedef struct
{
    char                title[17];
    uint8_t             type;                                           // octet 0x147
    uint8_t             mbc;                                            // 0 = aucun, 1, 2, 3, 5
    uint8_t             has_battery;
    uint8_t             has_rtc;
    uint8_t             cgb_flag;                                       // octet 0x143
    uint16_t            rom_banks;                                      // nombre de banques de 16 Ko
    uint32_t            ram_size;                                       // taille RAM cartouche en octets
} GB_CART_INFO;

extern GB_CART_INFO         gb_cart;

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * API du coeur
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
extern int                  gb_cart_init (const uint8_t * header, uint32_t rom_size);   // analyse l'en-tete (0x100-0x14F au minimum)
extern void                 gb_reset (void);                                            // etat "apres boot ROM"
extern uint32_t             gb_run_frame (void);                                        // emule une image (~70224 cycles)
extern void                 gb_set_buttons (uint8_t buttons);                           // etat complet des boutons
extern void                 gb_press_button (uint8_t mask);
extern void                 gb_release_button (uint8_t mask);
extern uint8_t *            gb_cart_ram (void);                                         // pour sauvegarde .sav
extern uint_fast8_t         gb_cart_ram_dirty (void);                                   // 1 si modifiee depuis le dernier appel
extern void                 gb_load_ini_line (const char * line);
extern uint16_t             gb_debug_pc (void);                                         // PC du CPU (diagnostic)
extern void                 gb_apu_set_rate (uint32_t rate);                            // frequence audio de la plateforme (Hz)                       // analyse une ligne du fichier ini

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Fonctions a fournir par la plateforme
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
extern const uint8_t *      gbplat_rom_bank (uint16_t bank);                // pointeur sur une banque ROM de 16 Ko
extern void                 gbplat_draw_line (uint_fast8_t ly, const uint8_t * shades); // 160 nuances 0..3 (0 = clair)
extern void                 gbplat_serial_byte (uint8_t value);             // octet emis sur le port serie (tests)
extern void                 gbplat_poll (void);                             // appele toutes les ~200 us emulees (clavier USB...)
extern void                 gbplat_audio_sample (int16_t sample);           // un echantillon son mono (frequence : gb_apu_set_rate)

#endif
