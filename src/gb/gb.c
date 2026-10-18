/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb.c - boucle principale du coeur STECCY-GB, reglages et fichier ini
 *
 * Dans STECCY, z80() tourne en boucle et z80_idle_time() freine l'emulation tous les N cycles
 * (10 ms sous Linux, 200 us sur STM32) puis declenche l'interruption ULA a 50 Hz.
 * Ici gb_run_frame() emule exactement une image (VBlank a 59.73 Hz) ; la plateforme gere le
 * freinage, la lecture des touches et l'affichage entre deux images.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "gbint.h"

GB_SETTINGS                 gb_settings =
{
    "",                                                                     // path
    "",                                                                     // romfile
    0,                                                                      // palette
    0,                                                                      // scale (auto)
    0,                                                                      // turbo
    0,                                                                      // frameskip
    1,                                                                      // show_info
    1,                                                                      // tft_fast
    1,                                                                      // sound
    0x01,                                                                   // keyboard : PS2
    0,                                                                      // rgb_order
    0                                                                       // orientation
};

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_reset ()
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_reset (void)
{
    gb_io_reset ();
    gb_apu_reset ();
    gb_mem_reset ();
    gb_ppu_reset ();
    sm83_reset ();
    gb_ppu_frame_done = 0;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_run_frame () - execute le CPU jusqu'au prochain VBlank (ou au plus 2 images si l'ecran est eteint)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint32_t
gb_run_frame (void)
{
    uint32_t total = 0;

    gb_ppu_frame_done = 0;

    while (! gb_ppu_frame_done && total < 2 * GB_CYCLES_PER_FRAME)
    {
        static uint32_t poll_cycles;
        uint32_t        cycles = sm83_step ();

        gb_timer_advance (cycles);                                          // fonctions "inline" (gbint.h) : pas d'appel
        gb_serial_advance (cycles);                                         // de fonction a chaque instruction, sauf si
        gb_ppu_advance (cycles);                                            // un evenement est atteint

        if (gb_settings.sound)
        {
            gb_apu_advance (cycles);                                        // SOUND=no : son entierement coupe (plus rapide)
        }
        total       += cycles;
        poll_cycles += cycles;

        if (poll_cycles >= 4200)                                            // ~1 ms emulee : suffisant pour un clavier USB
        {
            poll_cycles = 0;
            gbplat_poll ();
        }
    }
    return total;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_load_ini_line () - meme syntaxe que steccy.ini : CLE=valeur, commentaires avec # ou ;
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
copy_value (char * dst, const char * src)
{
    size_t len = strlen (src);

    while (len > 0 && isspace ((unsigned char) src[len - 1]))
    {
        len--;
    }
    if (len > GB_MAX_FILENAME_LEN)
    {
        len = GB_MAX_FILENAME_LEN;
    }
    memcpy (dst, src, len);
    dst[len] = '\0';
}

static int
key_is (const char * line, const char * key, const char ** value)
{
    size_t n = strlen (key);

    if (strncmp (line, key, n) == 0 && line[n] == '=')
    {
        *value = line + n + 1;
        return 1;
    }
    return 0;
}

void
gb_load_ini_line (const char * line)
{
    const char *    v;
    char            buf[GB_MAX_FILENAME_LEN + 1];
    char *          p;

    while (isspace ((unsigned char) *line))
    {
        line++;
    }
    if (*line == '#' || *line == ';' || *line == '\0')
    {
        return;
    }

    copy_value (buf, line);
    p = strpbrk (buf, "#;");                                                // commentaire en fin de ligne

    if (p)
    {
        *p = '\0';
        copy_value (buf, buf);
    }

    if (key_is (buf, "PATH", &v))               { copy_value (gb_settings.path, v); }
    else if (key_is (buf, "ROM", &v))           { copy_value (gb_settings.romfile, v); }
    else if (key_is (buf, "PALETTE", &v))
    {
        if (! strcmp (v, "gray") || ! strcmp (v, "grey") || ! strcmp (v, "1"))  { gb_settings.palette = 1; }
        else if (! strcmp (v, "pocket") || ! strcmp (v, "2"))                   { gb_settings.palette = 2; }
        else                                                                    { gb_settings.palette = 0; }
    }
    else if (key_is (buf, "SCALE", &v))         { gb_settings.scale = (uint8_t) atoi (v); }
    else if (key_is (buf, "FRAMESKIP", &v))     { gb_settings.frameskip = (uint8_t) atoi (v); }
    else if (key_is (buf, "INFO", &v))          { gb_settings.show_info = (! strcmp (v, "2")) ? 2 : (! strcmp (v, "yes") || ! strcmp (v, "1")); }
    else if (key_is (buf, "TFTFAST", &v))       { gb_settings.tft_fast = (! strcmp (v, "yes") || ! strcmp (v, "1")); }
    else if (key_is (buf, "SOUND", &v))         { gb_settings.sound = (! strcmp (v, "yes") || ! strcmp (v, "1")); }
    else if (key_is (buf, "TURBO", &v))         { gb_settings.turbo_mode = (! strcmp (v, "yes") || ! strcmp (v, "1")); }
    else if (key_is (buf, "ORIENTATION", &v))   { gb_settings.orientation = (uint8_t) atoi (v); }
    else if (key_is (buf, "RGB", &v))           { gb_settings.rgb_order = (uint8_t) atoi (v); }
    else if (key_is (buf, "KEYBOARD", &v))
    {
        static uint8_t first = 1;

        if (first)                                                          // la 1re ligne KEYBOARD remplace la valeur par defaut
        {
            gb_settings.keyboard = 0;
            first = 0;
        }
        if (! strcmp (v, "PS2"))      { gb_settings.keyboard |= 0x01; }
        else if (! strcmp (v, "USB")) { gb_settings.keyboard |= 0x02; }
    }
}
