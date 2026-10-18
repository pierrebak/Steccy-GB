/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lxmain.c - fonction main de STECCY-GB sous Linux (X11 ou framebuffer)
 *
 * Meme structure que steccy-lx/lxmain.c de STECCY : x11_main() / fb_main() avec un thread clavier en mode
 * framebuffer. zx_spectrum() est remplace par gb_lx_loop().
 *
 * usage : xsteccy-gb [-g LxH] [rom.gb]       (fenetre X11)
 *         steccy-gb  [-g LxH] [rom.gb]       (console framebuffer, ex. Raspberry Pi)
 * Sans argument, la ROM est celle de la ligne ROM= du fichier ini.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2020-2021 Frank Meyer - frank(at)fli4l.de
 * Copyright (c) 2026 STECCY-GB contributors
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#if defined FRAMEBUFFER
#include "lxkbd.h"
#include "lxfb.h"
#elif defined X11
#include "lxx11.h"
#endif

#include "gb.h"
#include "lxgb.h"
#include "lxcart.h"
#include "lxdisplay.h"

#if defined FRAMEBUFFER
#include <pthread.h>
static pthread_t    tid[1];

static void
clear_terminal (void)
{
    printf ("%c[2J", 0x1B);
    fflush (stdout);
    usleep (100000);
}
#endif

void
gbplat_poll (void)
{
}

void
gbplat_audio_sample (int16_t sample)
{
    (void) sample;                                                          // pas de sortie son sous Linux pour l'instant
}

void
gbplat_serial_byte (uint8_t value)
{
    (void) value;                                                           // pas de cable link
}

static uint64_t
now_usec (void)
{
    struct timespec ts;

    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000u + (uint64_t) ts.tv_nsec / 1000u;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_lx_loop - boucle principale : une image, affichage, evenements, freinage a 59.73 Hz
 *              (equivalent de z80() + z80_idle_time() de STECCY)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
gb_lx_loop (void)
{
    uint64_t        next = now_usec ();
    unsigned int    frame = 0;
    unsigned int    skip = 0;

    while (! steccy_exit)
    {
        extern uint_fast8_t gb_ppu_render_enabled;

        gb_ppu_render_enabled = (skip == 0);
        gb_run_frame ();

        if (skip == 0)
        {
            lxdisplay_update ();
        }

#if defined X11
        x11_event ();
#endif
        if (lx_reset_request)
        {
            lx_reset_request = 0;
            lx_save_ram (0);
            gb_reset ();
        }

        if (++frame % 300 == 0)                                             // toutes les ~5 s : sauvegarde .sav si modifiee
        {
            lx_save_ram (0);
        }

        next += GB_FRAME_USEC;

        if (! gb_settings.turbo_mode)
        {
            uint64_t t = now_usec ();

            if (next > t)
            {
                usleep ((useconds_t) (next - t));
                skip = 0;
            }
            else if (skip < gb_settings.frameskip)                          // en retard : sauter l'affichage de l'image suivante
            {
                skip++;
            }
            else
            {
                skip = 0;
            }

            if (t > next + 100000)                                          // plus de 100 ms de retard : on se recale
            {
                next = t;
            }
        }
        else
        {
            next = now_usec ();
            skip = (skip < 4) ? skip + 1 : 0;                               // turbo : afficher une image sur 5
        }
    }
    lx_save_ram (0);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * sigcatch - signal catcher
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
sigcatch (int sig)
{
    (void) sig;
    steccy_exit = 1;
}

static char *
parse_args (int argc, char ** argv, char ** geometry)
{
    int     i;
    char *  rom = NULL;

    for (i = 1; i < argc; i++)
    {
        if (! strcmp (argv[i], "-g") && i + 1 < argc)
        {
            *geometry = argv[++i];
        }
        else
        {
            rom = argv[i];
        }
    }
    if (! rom && gb_settings.romfile[0])
    {
        rom = gb_settings.romfile;
    }
    return rom;
}

#if defined X11

static int
x11_main (int argc, char ** argv)
{
    char *  geometry = (char *) "640x576";                                  // zoom x4
    char *  rom;

    lx_load_ini ();
    rom = parse_args (argc, argv, &geometry);

    if (! rom)
    {
        fprintf (stderr, "usage: %s [-g LxH] rom.gb\n", argv[0]);
        return 1;
    }
    if (lx_load_rom (rom) < 0)
    {
        return 1;
    }
    if (x11_init (geometry) < 0)
    {
        return 1;
    }

    signal (SIGTERM, sigcatch);
    signal (SIGINT, sigcatch);
    gb_lx_loop ();

    x11_deinit ();
    return 0;
}

#elif defined FRAMEBUFFER

static int
fb_main (int argc, char ** argv)
{
    char *  geometry = (char *) 0;
    char *  rom;
    int     err;

    lx_load_ini ();
    rom = parse_args (argc, argv, &geometry);

    if (! rom)
    {
        fprintf (stderr, "usage: %s [-g LxH] rom.gb\n", argv[0]);
        return 1;
    }
    if (lx_load_rom (rom) < 0)
    {
        return 1;
    }
    if (lxkbd_init () < 0)
    {
        return 1;
    }
    if (fb_init (geometry) < 0)
    {
        lxkbd_deinit ();
        return 1;
    }

    err = pthread_create (&(tid[0]), NULL, &lxkbd_read, NULL);

    if (err == 0)
    {
        signal (SIGTERM, sigcatch);
        gb_lx_loop ();
    }
    else
    {
        fprintf (stderr, "can't create thread :[%s]", strerror (err));
        return 1;
    }

    lxkbd_deinit ();
    fb_deinit ();
    clear_terminal ();
    return 0;
}

#endif

int
main (int argc, char ** argv)
{
    int rtc;

#if defined X11
    rtc = x11_main (argc, argv);
#elif defined FRAMEBUFFER
    rtc = fb_main (argc, argv);
#endif
    return rtc;
}
