/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbstm32.c - plateforme STM32F407 de STECCY-GB : cache ROM sur carte SD, affichage TFT, boucle principale
 *
 * Remplace la partie STM32F4XX de z80.c (zx_spectrum(), z80_idle_time()) et zxscr.c de STECCY.
 *
 * Memoire (STM32F407VE : 128 Ko SRAM + 64 Ko CCM) :
 *   - VRAM, WRAM, OAM, HRAM, E/S et image precedente (cache d'affichage) en CCM (gbint.h : GB_FAST_RAM)
 *   - RAM cartouche (32 Ko max), banque ROM 0 et GB_ROM_CACHE_SLOTS banques de 16 Ko en SRAM
 *     (la SRAM est accessible au DMA de la carte SD, la CCM ne l'est pas)
 *
 * Vitesse : le CPU emule lit la ROM en permanence. La banque 0 et les banques actives sont donc executees
 * depuis la SRAM (sans etat d'attente) : bank0_buf + GB_ROM_CACHE_SLOTS emplacements remplis depuis la flash.
 *
 * ROM du jeu : comme STECCY, on ne lit plus la carte SD pendant le jeu. Au chargement, la ROM (384 Ko max)
 * est copiee dans les secteurs 5 a 7 de la flash interne (0x08020000-0x0807FFFF), seulement si elle differe
 * de ce qui s'y trouve deja. Les banques sont ensuite lues directement en flash.
 * ROMs plus grandes (512 Ko, 1 Mo) : repli sur un cache de banques lu sur la carte SD a la demande.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "stm32f4xx.h"
#include "stm32f4xx_flash.h"
#include "tft.h"
#include "font.h"
#include "delay.h"
#include "ps2key.h"
#include "usb_hid_host.h"
#include "z80.h"
#include "gb.h"
#include "gbint.h"
#include "gbstm32.h"
#include "gbaudio.h"

#ifndef GB_ROM_CACHE_SLOTS
#define GB_ROM_CACHE_SLOTS      3
#endif

#if defined SSD1963                                             // 800x480 : zoom x3 => 480x432
#define DEFAULT_SCALE_NUM       3
#define DEFAULT_SCALE_DEN       1
#else                                                           // ILI9341 320x240 : zoom x1,5 => 240x216
#define DEFAULT_SCALE_NUM       3
#define DEFAULT_SCALE_DEN       2
#endif
#define MAX_OUT_WIDTH           ((GB_LCD_WIDTH * 3) + 1)

volatile uint_fast8_t           gbstm32_reset_request;

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Mesure du temps CPU (INFO=2) avec le compteur de cycles DWT du Cortex-M4 (adresses directes : le CMSIS
 * de STECCY est trop ancien pour definir DWT)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#define DEMCR_REG               (*(volatile uint32_t *) 0xE000EDFCUL)
#define DWT_CTRL_REG            (*(volatile uint32_t *) 0xE0001000UL)
#define DWT_CYCCNT_REG          (*(volatile uint32_t *) 0xE0001004UL)

static uint32_t                 prof_tft;                       // cycles passes a envoyer les lignes au TFT
static uint32_t                 prof_poll;                      // cycles passes dans l'USB pendant l'emulation
static uint8_t                  in_frame;

static void
prof_init (void)
{
    DEMCR_REG       |= (1UL << 24);                             // TRCENA
    DWT_CYCCNT_REG   = 0;
    DWT_CTRL_REG    |= 1UL;                                     // CYCCNTENA
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * tft_fast_writes () - ILI9341 : ecritures FSMC en ~13 cycles (77 ns) au lieu de ~29 (175 ns) reglees par STECCY.
 * Mode etendu du FSMC : les lectures gardent le reglage lent de STECCY, seules les ecritures sont accelerees.
 * La datasheet ILI9341 demande un cycle d'ecriture d'au moins 66 ns. TFTFAST=no dans l'ini pour desactiver.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
tft_fast_writes (void)
{
#if defined (ILI9341)
    if (gb_settings.tft_fast)
    {
        FSMC_Bank1E->BWTR[0]  = 0x0FF00000UL | (7UL << 8) | 5UL;  // DATAST = 7, ADDSET = 5, ADDHLD = 0, mode A
        FSMC_Bank1->BTCR[0]  |= FSMC_BCR1_EXTMOD;
    }
#endif
}
volatile uint_fast8_t           gbstm32_display_request;        // F1/F2 : orientation / ordre RGB changes

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * sd_read / sd_write - lecture/ecriture par blocs de 512 octets
 *
 * IMPORTANT : MMC_disk_read() et MMC_disk_write() de STECCY (sdcard/stm32_sdcard.c, commentaire "fm: TODO")
 * ignorent le parametre count et ne transferent qu'UN secteur. Quand f_read() lit plusieurs secteurs d'un coup
 * directement dans le tampon de l'appelant, seuls les 512 premiers octets sont corrects. STECCY n'y est pas
 * expose car il lit par petits morceaux. On decoupe donc tout transfert en morceaux de 512 octets au plus,
 * ce qui force FatFs a ne demander qu'un secteur a la fois.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static FRESULT
sd_read (FIL * fp, void * buf, UINT len, UINT * done)
{
    uint8_t *   p   = buf;
    FRESULT     res = FR_OK;

    *done = 0;

    while (len > 0)
    {
        UINT n = (len > 512) ? 512 : len;
        UINT br;

        res = f_read (fp, p, n, &br);

        if (res != FR_OK)
        {
            break;
        }
        *done += br;
        p     += br;
        len   -= br;

        if (br < n)
        {
            break;                                              // fin de fichier
        }
    }
    return res;
}

static FRESULT
sd_write (FIL * fp, const void * buf, UINT len, UINT * done)
{
    const uint8_t * p   = buf;
    FRESULT         res = FR_OK;

    *done = 0;

    while (len > 0)
    {
        UINT n = (len > 512) ? 512 : len;
        UINT bw;

        res = f_write (fp, p, n, &bw);

        if (res != FR_OK || bw < n)
        {
            break;
        }
        *done += bw;
        p     += bw;
        len   -= bw;
    }
    return res;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Cache des banques ROM
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static FIL                      rom_fil;
static uint8_t                  rom_open;
static uint8_t                  rom_ok;                         // 1 = ROM valide chargee
static uint8_t                  bank0_buf[0x4000] __attribute__((aligned(4)));
static uint8_t                  cache_buf[GB_ROM_CACHE_SLOTS][0x4000] __attribute__((aligned(4)));
static int16_t                  cache_bank[GB_ROM_CACHE_SLOTS];
static uint32_t                 cache_age[GB_ROM_CACHE_SLOTS];
static uint32_t                 cache_clock;
static const uint8_t *          last_returned;
uint32_t                        gbstm32_cache_misses;

static void
cache_clear (void)
{
    uint_fast8_t i;

    for (i = 0; i < GB_ROM_CACHE_SLOTS; i++)
    {
        cache_bank[i] = -1;
        cache_age[i]  = 0;
    }
    last_returned = 0;
}

#define FLASH_ROM_BASE          0x08020000UL                    // secteur 5
#define FLASH_ROM_SIZE          (3UL * 0x20000UL)               // secteurs 5, 6, 7 = 384 Ko
static uint8_t                  rom_in_flash;                   // 1 = banques lues en flash interne

const uint8_t *
gbplat_rom_bank (uint16_t bank)
{
    uint_fast8_t    i;
    uint_fast8_t    victim = 0;
    uint32_t        oldest = 0xFFFFFFFF;
    UINT            br;

    if (bank == 0)
    {
        return last_returned = bank0_buf;
    }

    cache_clock++;

    for (i = 0; i < GB_ROM_CACHE_SLOTS; i++)
    {
        if (cache_bank[i] == (int16_t) bank)
        {
            cache_age[i] = cache_clock;
            return last_returned = cache_buf[i];
        }
    }

    for (i = 0; i < GB_ROM_CACHE_SLOTS; i++)                    // choisir la banque la plus ancienne
    {
        if (cache_buf[i] == last_returned || cache_buf[i] == gb_rom0)
        {
            continue;                                           // ne pas ecraser la banque 0x0000-0x3FFF en cours (MBC1 mode 1)
        }
        if (cache_age[i] < oldest)
        {
            oldest = cache_age[i];
            victim = (uint_fast8_t) i;
        }
    }

    gbstm32_cache_misses++;
    cache_bank[victim] = (int16_t) bank;
    cache_age[victim]  = cache_clock;

    if (rom_in_flash)                                           // ROM copiee en flash : copie rapide flash -> SRAM (~0,2 ms)
    {
        memcpy (cache_buf[victim], (const void *) (FLASH_ROM_BASE + (uint32_t) bank * 0x4000UL), 0x4000);
    }
    else if (rom_open && f_lseek (&rom_fil, (FSIZE_t) bank * 0x4000u) == FR_OK)
    {
        if (sd_read (&rom_fil, cache_buf[victim], 0x4000, &br) != FR_OK)
        {
            br = 0;
        }
        if (br < 0x4000)
        {
            memset (cache_buf[victim] + br, 0xFF, 0x4000 - br);
        }
    }
    return last_returned = cache_buf[victim];
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Affichage TFT : agrandissement num/den au plus proche voisin, ligne par ligne, lignes inchangees sautees
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint16_t                 palette565[4];
static uint8_t                  scale_num = DEFAULT_SCALE_NUM;
static uint8_t                  scale_den = DEFAULT_SCALE_DEN;
static uint16_t                 out_width;
static uint16_t                 left_offset;
static uint16_t                 top_offset;
static uint8_t                  xmap[MAX_OUT_WIDTH];
static GB_FAST_RAM uint8_t      shadow[GB_LCD_HEIGHT][GB_LCD_WIDTH];
static uint8_t                  display_cached;

static const uint8_t            palettes_rgb[3][4][3] =
{
    { { 0xE0, 0xF8, 0xD0 }, { 0x88, 0xC0, 0x70 }, { 0x34, 0x68, 0x56 }, { 0x08, 0x18, 0x20 } },     // vert DMG
    { { 0xFF, 0xFF, 0xFF }, { 0xAA, 0xAA, 0xAA }, { 0x55, 0x55, 0x55 }, { 0x00, 0x00, 0x00 } },     // gris
    { { 0xC4, 0xCF, 0xA1 }, { 0x8B, 0x95, 0x6D }, { 0x4D, 0x53, 0x3C }, { 0x1F, 0x1F, 0x1F } },     // "pocket"
};

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * set_display_flags () - ordre RGB et orientation (repris de zxscr_set_display_flags () de STECCY)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
set_display_flags (void)
{
    uint_fast8_t    flags;
    uint_fast8_t    flip_v;
    uint_fast8_t    flip_h;

#if defined (SSD1963)
    flags  = (gb_settings.rgb_order == 0) ? SSD1963_GLOBAL_FLAGS_RGB_ORDER : 0;
    flip_v = SSD1963_GLOBAL_FLAGS_FLIP_VERTICAL;
    flip_h = SSD1963_GLOBAL_FLAGS_FLIP_HORIZONTAL;
#else
    flags  = (gb_settings.rgb_order == 0) ? 0 : ILI9341_GLOBAL_FLAGS_RGB_ORDER;
    flip_v = ILI9341_GLOBAL_FLAGS_FLIP_VERTICAL;
    flip_h = ILI9341_GLOBAL_FLAGS_FLIP_HORIZONTAL;
#endif

    switch (gb_settings.orientation)
    {
        case 1:  flags |= flip_v;           break;
        case 2:  flags |= flip_h;           break;
        case 3:  flags |= flip_v | flip_h;  break;
        default: gb_settings.orientation = 0; break;
    }
#if defined (SSD1963)
    ssd1963_set_flags (flags);
#else
    ili9341_set_flags (flags);
#endif
}

static void
display_init (void)
{
    uint_fast16_t   i;
    uint8_t         p = gb_settings.palette % 3;

    if (gb_settings.scale >= 1 && gb_settings.scale <= 3)       // SCALE=1..3 dans l'ini, sinon valeur par defaut
    {
        scale_num = gb_settings.scale;
        scale_den = 1;
    }
    while (GB_LCD_WIDTH * scale_num / scale_den > TFT_WIDTH || GB_LCD_HEIGHT * scale_num / scale_den > TFT_HEIGHT)
    {
        scale_num--;                                            // ne doit pas depasser l'ecran
    }

    out_width   = (uint16_t) (GB_LCD_WIDTH * scale_num / scale_den);
    left_offset = (uint16_t) ((TFT_WIDTH  - out_width) / 2);
    top_offset  = (uint16_t) ((TFT_HEIGHT - GB_LCD_HEIGHT * scale_num / scale_den) / 2);

    for (i = 0; i < out_width; i++)
    {
        xmap[i] = (uint8_t) (i * scale_den / scale_num);
    }
    for (i = 0; i < 4; i++)
    {
        palette565[i] = (uint16_t) tft_rgb256_to_color565 (palettes_rgb[p][i][0], palettes_rgb[p][i][1], palettes_rgb[p][i][2]);
    }
    set_display_flags ();
    tft_fill_screen (BLACK565);
    display_cached = 0;
}

static void draw_line (uint_fast8_t ly, const uint8_t * shades);

void
gbplat_draw_line (uint_fast8_t ly, const uint8_t * shades)
{
    uint32_t t0 = DWT_CYCCNT_REG;

    draw_line (ly, shades);
    prof_tft += DWT_CYCCNT_REG - t0;
}

static void
draw_line (uint_fast8_t ly, const uint8_t * shades)
{
    uint16_t        y0;
    uint16_t        y1;
    uint_fast16_t   ox;
    uint_fast16_t   r;

    uint_fast16_t   x0 = 0;
    uint_fast16_t   x1 = GB_LCD_WIDTH - 1;
    uint_fast16_t   ox0;
    uint_fast16_t   ox1;
    uint8_t *       sh = shadow[ly];

    if (display_cached)                                         // seule la partie modifiee de la ligne est envoyee
    {
        while (x0 < GB_LCD_WIDTH && sh[x0] == shades[x0])
        {
            x0++;
        }
        if (x0 == GB_LCD_WIDTH)
        {
            return;                                             // ligne identique a l'image precedente
        }
        while (sh[x1] == shades[x1])
        {
            x1--;
        }
    }
    memcpy (sh + x0, shades + x0, x1 - x0 + 1);

    y0 = (uint16_t) (ly * scale_num / scale_den);
    y1 = (uint16_t) ((ly + 1) * scale_num / scale_den);

    if (y1 == y0)
    {
        return;
    }

    ox0 = (x0 * scale_num + scale_den - 1) / scale_den;         // colonnes ecran couvrant les pixels x0..x1 (formule
    ox1 = ((x1 + 1) * scale_num + scale_den - 1) / scale_den - 1;   // verifiee pour les zooms 1, 2, 3 et 1,5)

    if (ox1 >= out_width)
    {
        ox1 = out_width - 1;
    }

    tft_set_area (left_offset + ox0, left_offset + ox1, top_offset + y0, top_offset + y1 - 1);

#if defined ILI9341                                             // mode paysage de tft.c : Y varie le plus vite
    for (ox = ox0; ox <= ox1; ox++)
    {
        uint16_t c = palette565[shades[xmap[ox]] & 3];

        for (r = y0; r < y1; r++)
        {
            tft_write_data (c);
        }
    }
#else                                                           // SSD1963 : X varie le plus vite
    for (r = y0; r < y1; r++)
    {
        for (ox = ox0; ox <= ox1; ox++)
        {
            tft_write_data (palette565[shades[xmap[ox]] & 3]);
        }
    }
#endif
}

void
gbplat_serial_byte (uint8_t value)
{
    (void) value;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbplat_poll () - appele par le coeur toutes les ~200 us emulees : machine d'etat USB de STECCY (comme z80_idle_time())
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gbplat_poll (void)
{
    if (gb_settings.keyboard & KEYBOARD_USB)
    {
        uint32_t t0 = DWT_CYCCNT_REG;

        usb_hid_host_process (0);

        if (in_frame)
        {
            prof_poll += DWT_CYCCNT_REG - t0;
        }
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Fichiers : ini, ROM, sauvegarde
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static char                     sav_name[GB_MAX_FILENAME_LEN + 1];

static void
load_ini_file (void)
{
    FILE *  fp = fopen ("STECCYGB.INI", "r");                 // 8.3 obligatoire : FatFs sans noms longs (FF_USE_LFN = 0)
    char    line[128];

    if (fp)
    {
        while (fgets (line, sizeof (line), fp))
        {
            line[strcspn (line, "\r\n")] = '\0';
            gb_load_ini_line (line);
        }
        fclose (fp);
    }
    z80_settings.keyboard = gb_settings.keyboard;               // pour ps2key.c / usb de STECCY
}

static void
save_ram (void)
{
    FIL     f;
    UINT    bw;

    if (! rom_ok || ! gb_cart.has_battery || ! gb_cart.ram_size || ! gb_cart_ram_dirty ())
    {
        return;
    }
    if (f_open (&f, sav_name, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
    {
        (void) sd_write (&f, gb_cart_ram (), gb_cart.ram_size, &bw);
        f_close (&f);
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * rom_to_flash () - copie la ROM ouverte (rom_fil) dans les secteurs 5-7 de la flash interne.
 * Un secteur n'est efface/reprogramme que si son contenu differe du fichier (economise la flash : ~10000 cycles).
 * Retour : 0 = OK, -1 = erreur de lecture SD, -2 = erreur flash
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
status_line (const char * text)
{
    char buf[40];

    snprintf (buf, sizeof (buf), "%-36s", text);
    draw_string ((unsigned char *) buf, TFT_HEIGHT / 2, 8, WHITE565, BLUE565);
}

static int
read_chunk (uint32_t offset, uint8_t * buf, UINT len)
{
    UINT br;

    if (f_lseek (&rom_fil, (FSIZE_t) offset) != FR_OK || sd_read (&rom_fil, buf, len, &br) != FR_OK)
    {
        return -1;
    }
    if (br < len)
    {
        memset (buf + br, 0xFF, len - br);                      // fin de fichier : complete avec 0xFF (etat efface)
    }
    return 0;
}

static int
rom_to_flash (uint32_t rom_size)
{
    static const uint16_t   sectors[3] = { FLASH_Sector_5, FLASH_Sector_6, FLASH_Sector_7 };
    uint8_t *               buf   = cache_buf[0];               // 16 Ko de tampon (le cache n'est pas utilise ici)
    const UINT              chunk = 0x4000;
    uint32_t                s;
    char                    msg[40];

    for (s = 0; s < 3 && s * 0x20000UL < rom_size; s++)
    {
        uint32_t        start  = s * 0x20000UL;
        uint32_t        end    = (rom_size < start + 0x20000UL) ? rom_size : start + 0x20000UL;
        uint32_t        off;
        uint_fast8_t    same   = 1;

        snprintf (msg, sizeof (msg), "Verification flash %lu/3...", (unsigned long) s + 1);
        status_line (msg);

        for (off = start; off < end && same; off += chunk)      // contenu deja identique ?
        {
            if (read_chunk (off, buf, chunk) != 0)
            {
                return -1;
            }
            if (memcmp ((const void *) (FLASH_ROM_BASE + off), buf, (end - off < chunk) ? end - off : chunk) != 0)
            {
                same = 0;
            }
        }

        if (same)
        {
            continue;
        }

        snprintf (msg, sizeof (msg), "Copie en flash %lu/3...", (unsigned long) s + 1);
        status_line (msg);

        FLASH_Unlock ();
        FLASH_ClearFlag (FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

        if (FLASH_EraseSector (sectors[s], VoltageRange_3) != FLASH_COMPLETE)
        {
            FLASH_Lock ();
            return -2;
        }

        for (off = start; off < end; off += chunk)
        {
            uint32_t i;
            uint32_t n = (end - off < chunk) ? end - off : chunk;

            if (read_chunk (off, buf, chunk) != 0)
            {
                FLASH_Lock ();
                return -1;
            }
            for (i = 0; i < n; i += 4)
            {
                uint32_t w;

                memcpy (&w, buf + i, 4);

                if (FLASH_ProgramWord (FLASH_ROM_BASE + off + i, w) != FLASH_COMPLETE)
                {
                    FLASH_Lock ();
                    return -2;
                }
            }
        }
        FLASH_Lock ();

        for (off = start; off < end; off += chunk)              // relecture de controle
        {
            uint32_t n = (end - off < chunk) ? end - off : chunk;

            if (read_chunk (off, buf, chunk) != 0)
            {
                return -1;
            }
            if (memcmp ((const void *) (FLASH_ROM_BASE + off), buf, n) != 0)
            {
                return -2;
            }
        }
    }
    return 0;
}

static int
load_rom (const char * fname)
{
    UINT    br;
    char *  dot;
    int     rtc;
    FIL     f;

    if (rom_open)
    {
        save_ram ();
        f_close (&rom_fil);
        rom_open = 0;
    }
    cache_clear ();
    rom_ok = 0;
    rom_in_flash = 0;

    if (f_open (&rom_fil, fname, FA_READ) != FR_OK)
    {
        printf ("%s: ouverture impossible\r\n", fname);
        return -1;
    }
    rom_open = 1;

    if (sd_read (&rom_fil, bank0_buf, 0x4000, &br) != FR_OK || br < 0x150)
    {
        return -1;
    }

    rtc = gb_cart_init (bank0_buf, (uint32_t) f_size (&rom_fil));

    if (rtc == -1)
    {
        printf ("%s: cartouche 0x%02X non supportee\r\n", fname, bank0_buf[0x147]);
        return -1;
    }
    if (rtc == -2)
    {
        printf ("%s: RAM cartouche trop grande (%lu octets)\r\n", fname, (unsigned long) gb_cart.ram_size);
        return -1;
    }

    rom_in_flash = 0;

    if (f_size (&rom_fil) <= FLASH_ROM_SIZE)                    // cas normal : ROM copiee en flash, plus de lecture SD en jeu
    {
        tft_fill_screen (BLACK565);
        rtc = rom_to_flash ((uint32_t) f_size (&rom_fil));
        cache_clear ();

        if (rtc != 0)
        {
            printf ("%s: copie en flash impossible (%d)\r\n", fname, rtc);
            status_line (rtc == -1 ? "Erreur de lecture carte SD" : "Erreur d'ecriture flash");
            delay_msec (3000);
            return -1;
        }
        rom_in_flash = 1;
    }

    strncpy (sav_name, fname, sizeof (sav_name) - 5);
    sav_name[sizeof (sav_name) - 5] = '\0';
    dot = strrchr (sav_name, '.');

    if (dot)
    {
        *dot = '\0';
    }
    strcat (sav_name, ".SAV");

    if (gb_cart.has_battery && gb_cart.ram_size && f_open (&f, sav_name, FA_READ) == FR_OK)
    {
        (void) sd_read (&f, gb_cart_ram (), gb_cart.ram_size, &br);
        f_close (&f);
    }

    gb_reset ();
    rom_ok = 1;
    printf ("ROM '%s' MBC%d, %u banques, RAM %lu\r\n", gb_cart.title, gb_cart.mbc, gb_cart.rom_banks, (unsigned long) gb_cart.ram_size);
    return 0;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * poll_devices () - equivalent du travail fait par z80_idle_time() de STECCY pendant les pauses
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
poll_devices (uint_fast8_t in_menu)
{
    if (gb_settings.keyboard & KEYBOARD_USB)
    {
        usb_hid_host_process (0);
    }
    gbinput_poll (in_menu);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * run_menu () - menu (TAB / Echap / HOME), puis retour au jeu
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
run_menu (void)
{
    char    fname[GB_MAX_FILENAME_LEN + 1];
    int     action;

    for (;;)
    {
        action = gbmenu (fname, sizeof (fname), rom_ok);

        if (action == GBMENU_LOAD_ROM)
        {
            if (load_rom (fname) == 0)
            {
                break;
            }
            continue;                                           // echec : retour au menu
        }
        if (action == GBMENU_RESET)
        {
            save_ram ();
            gb_reset ();
        }
        else if (action == GBMENU_SAVE)
        {
            save_ram ();
        }
        if (rom_ok)
        {
            break;
        }
    }
    display_init ();
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_stm32 () - point d'entree appele par main() (remplace zx_spectrum() de STECCY)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_stm32 (void)
{
    uint64_t        next_usec;
    uint32_t        frame = 0;
    uint_fast8_t    skip  = 0;
    uint_fast8_t    late  = 0;
    uint32_t        frames_in_second = 0;
    uint32_t        prof_frame = 0;
    uint32_t        prof_t0    = DWT_CYCCNT_REG;
    uint32_t        info_uptime = uptime;

    load_ini_file ();
    prof_init ();
    tft_fast_writes ();
    set_display_flags ();                                       // ORIENTATION= / RGB= appliques des le menu
    set_font (FONT_08x12);

    if (gb_settings.keyboard & KEYBOARD_PS2)
    {
        ps2key_init ();
    }
    if (gb_settings.keyboard & KEYBOARD_USB)
    {
        usb_hid_host_init ();
    }

    if (gb_settings.sound)
    {
        gb_apu_set_rate (gbaudio_init ());                      // DAC PA4, 16 kHz
    }

    if (! gb_settings.romfile[0] || load_rom (gb_settings.romfile) != 0)
    {
        run_menu ();                                            // pas de ROM= dans l'ini : choisir une ROM
    }
    display_init ();

    next_usec = (uint64_t) uptime * 20u;

    for (;;)
    {
        gb_ppu_render_enabled = (skip == 0);
        {
            uint32_t t0 = DWT_CYCCNT_REG;

            in_frame = 1;
            gb_run_frame ();
            in_frame = 0;
            prof_frame += DWT_CYCCNT_REG - t0;
        }
        display_cached = 1;
        frames_in_second++;

        if (gb_settings.show_info && uptime - info_uptime >= 50000)     // 1 fois par seconde
        {
            char buf[48];

            if (gb_settings.show_info == 2)                             // repartition du temps CPU, en %
            {
                uint32_t total = DWT_CYCCNT_REG - prof_t0;
                uint32_t emu   = prof_frame - prof_tft - prof_poll;

                snprintf (buf, sizeof (buf), "emu%3lu ecr%3lu usb%3lu lib%3lu %% %2lu fps",
                          (unsigned long) ((uint64_t) emu * 100 / total), (unsigned long) ((uint64_t) prof_tft * 100 / total),
                          (unsigned long) ((uint64_t) prof_poll * 100 / total),
                          (unsigned long) ((uint64_t) (total - prof_frame) * 100 / total), (unsigned long) frames_in_second);
                draw_string ((unsigned char *) buf, 0, 0, YELLOW565, BLACK565);
            }
            else
            {
                snprintf (buf, sizeof (buf), "%3lu fps", (unsigned long) frames_in_second);
                draw_string ((unsigned char *) buf, 0, TFT_WIDTH - 9 * 8, YELLOW565, BLACK565);   // marge a droite
            }
            frames_in_second = 0;
            info_uptime      = uptime;
            prof_frame       = prof_tft = prof_poll = 0;
            prof_t0          = DWT_CYCCNT_REG;
        }

        poll_devices (0);

        if (gb_settings.sound)
        {
            gbaudio_regulate ();
        }

        if (gbinput_menu_request)
        {
            gbinput_menu_request = 0;
            save_ram ();
            run_menu ();
            next_usec = (uint64_t) uptime * 20u;
        }
        if (gbstm32_display_request)
        {
            gbstm32_display_request = 0;
            display_init ();
        }
        if (gbstm32_reset_request)
        {
            gbstm32_reset_request = 0;
            save_ram ();
            gb_reset ();
        }
        if (++frame % 600 == 0)                                 // ~10 s : ecrire le .SAV si la RAM a change
        {
            save_ram ();
        }

        /*-----------------------------------------------------------------------------------------------------------------------------------
         * Frein a 59.73 Hz. Frameskip automatique si l'emulation prend du retard (FRAMESKIP= dans l'ini = maximum).
         *-----------------------------------------------------------------------------------------------------------------------------------
         */
        next_usec += GB_FRAME_USEC;

        if (gb_settings.turbo_mode)
        {
            next_usec = (uint64_t) uptime * 20u;
            skip = (skip < 4) ? skip + 1 : 0;                   // en turbo, n'afficher qu'une image sur 5
            continue;
        }

        late = ((uint64_t) uptime * 20u > next_usec);

        while ((uint64_t) uptime * 20u < next_usec)
        {
            gbplat_poll ();                                     // attente active : USB interroge en continu
        }

        if (late && skip < gb_settings.frameskip)
        {
            skip++;
        }
        else
        {
            skip = 0;
        }

        if ((uint64_t) uptime * 20u > next_usec + 100000u)       // plus de 100 ms de retard : on se recale
        {
            next_usec = (uint64_t) uptime * 20u;
        }
    }
}
