/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lxheadless.c - STECCY-GB sans affichage : execute N images, affiche la sortie du port serie, peut ecrire
 *                la derniere image au format PGM. Sert aux ROMs de test (blargg, dmg-acid2...).
 *
 * usage : gb-headless rom.gb [nb_images] [image.pgm|-] [son.wav]   (son mono 16 bits a 32768 Hz)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gb.h"
#include "lxcart.h"

void
gbplat_poll (void)
{
}

static int16_t *            wav_buf;
static size_t               wav_len;
static size_t               wav_cap;

void
gbplat_audio_sample (int16_t sample)
{
    if (wav_len == wav_cap)
    {
        wav_cap = wav_cap ? wav_cap * 2 : 65536;
        wav_buf = realloc (wav_buf, wav_cap * sizeof (int16_t));
    }
    wav_buf[wav_len++] = sample;
}

static void
put32 (FILE * f, uint32_t v)
{
    fputc ((int) (v & 0xFF), f); fputc ((int) ((v >> 8) & 0xFF), f); fputc ((int) ((v >> 16) & 0xFF), f); fputc ((int) (v >> 24), f);
}

static void
put16 (FILE * f, uint16_t v)
{
    fputc (v & 0xFF, f); fputc (v >> 8, f);
}

static void
write_wav (const char * name, uint32_t rate)
{
    FILE *  f = fopen (name, "wb");
    size_t  i;

    if (! f)
    {
        perror (name);
        return;
    }
    fputs ("RIFF", f); put32 (f, (uint32_t) (36 + wav_len * 2)); fputs ("WAVEfmt ", f);
    put32 (f, 16); put16 (f, 1); put16 (f, 1); put32 (f, rate); put32 (f, rate * 2); put16 (f, 2); put16 (f, 16);
    fputs ("data", f); put32 (f, (uint32_t) (wav_len * 2));

    for (i = 0; i < wav_len; i++)
    {
        put16 (f, (uint16_t) wav_buf[i]);
    }
    fclose (f);
    fprintf (stderr, "%s : %lu echantillons\n", name, (unsigned long) wav_len);
}

void
gbplat_serial_byte (uint8_t value)
{
    putchar (value);
    fflush (stdout);
}

int
main (int argc, char ** argv)
{
    long            frames = 3600;
    long            i;
    struct timespec t0, t1;
    double          sec;

    if (argc < 2)
    {
        fprintf (stderr, "usage: %s rom.gb [nb_images] [image.pgm|-] [son.wav]\n", argv[0]);
        return 1;
    }
    if (argc > 2)
    {
        frames = atol (argv[2]);
    }
    if (lx_load_rom (argv[1]) < 0)
    {
        return 1;
    }
    gb_apu_set_rate (32768);

    clock_gettime (CLOCK_MONOTONIC, &t0);

    for (i = 0; i < frames; i++)
    {
        gb_run_frame ();
    }

    clock_gettime (CLOCK_MONOTONIC, &t1);
    sec = (double) (t1.tv_sec - t0.tv_sec) + (double) (t1.tv_nsec - t0.tv_nsec) / 1e9;
    fprintf (stderr, "\n%ld images en %.2f s (%.1f x temps reel)\n", frames, sec, (frames / 59.7275) / sec);

    if (argc > 3 && strcmp (argv[3], "-") != 0)
    {
        FILE * fp = fopen (argv[3], "wb");
        int    y, x;

        if (fp)
        {
            fprintf (fp, "P5\n%d %d\n255\n", GB_LCD_WIDTH, GB_LCD_HEIGHT);

            for (y = 0; y < GB_LCD_HEIGHT; y++)
            {
                for (x = 0; x < GB_LCD_WIDTH; x++)
                {
                    fputc (255 - lx_frame[y][x] * 85, fp);                  // nuance 0 = blanc
                }
            }
            fclose (fp);
        }
    }
    if (argc > 4)
    {
        write_wav (argv[4], 32768);
    }
    lx_save_ram (0);
    return 0;
}
