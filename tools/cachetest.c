/* cachetest.c - test sur PC du cache de banques ROM du port STM32 (code extrait automatiquement de gbstm32.c)
 * compilation : gcc -O2 -DGB_ROM_CACHE_SLOTS=2 -I../src/gb -o cachetest cachetest.c ../src/gb/gb.c ../src/gb/sm83.c ../src/gb/gbmem.c ../src/gb/gbio.c ../src/gb/gbppu.c
 * usage : ./cachetest rom.gb [images]   -- 2 emplacements pour forcer les evictions */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gb.h"
#include "gbint.h"
typedef unsigned int UINT;
static FILE *                   rom_fil;
static uint8_t                  rom_open;
static uint8_t                  rom_ok;                         // 1 = ROM valide chargee
static uint8_t                  bank0_buf[0x4000];
static uint8_t                  cache_buf[GB_ROM_CACHE_SLOTS][0x4000];
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

    if (rom_open && fseek (rom_fil, (long) bank * 0x4000L, SEEK_SET) == 0)
    {
        br = (UINT) fread (cache_buf[victim], 1, 0x4000, rom_fil);
        if (br < 0x4000)
        {
            memset (cache_buf[victim] + br, 0xFF, 0x4000 - br);
        }
    }
    return last_returned = cache_buf[victim];
}


static uint8_t frame[144][160];
void gbplat_draw_line (uint_fast8_t ly, const uint8_t * s) { memcpy (frame[ly], s, 160); }
void gbplat_poll (void) { }
void gbplat_audio_sample (int16_t s) { (void) s; }
void gbplat_serial_byte (uint8_t v) { putchar (v); fflush (stdout); }
int main (int argc, char ** argv)
{
    long n = argc > 2 ? atol (argv[2]) : 3000, i;
    (void) rom_ok;
    UINT br;
    rom_fil = fopen (argv[1], "rb");
    if (! rom_fil) { perror (argv[1]); return 1; }
    rom_open = 1;
    cache_clear ();
    br = (UINT) fread (bank0_buf, 1, 0x4000, rom_fil);
    fseek (rom_fil, 0, SEEK_END);
    if (br < 0x150 || gb_cart_init (bank0_buf, (uint32_t) ftell (rom_fil)) != 0) { fprintf (stderr, "ROM invalide\n"); return 1; }
    gb_reset ();
    for (i = 0; i < n; i++) gb_run_frame ();
    fprintf (stderr, "\nemplacements de cache: %d, lectures de banques (defauts de cache): %u\n", GB_ROM_CACHE_SLOTS, gbstm32_cache_misses);
    return 0;
}
