/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lxcart.c - chargement de la ROM, sauvegarde .sav et fichier ini sous Linux (commun X11 / framebuffer / headless)
 *
 * Reprend la logique de load_ini_file() de STECCY (z80.c) : $HOME/.steccy-gb.ini puis ./steccy-gb.ini
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gb.h"
#include "lxcart.h"

static uint8_t *            rom;
static uint32_t             rom_size;
static char                 sav_name[2 * GB_MAX_FILENAME_LEN + 8];
uint8_t                     lx_frame[GB_LCD_HEIGHT][GB_LCD_WIDTH];         // derniere image (nuances 0..3)

const uint8_t *
gbplat_rom_bank (uint16_t bank)
{
    uint32_t off = (uint32_t) bank * 0x4000u;

    if (off + 0x4000u > rom_size)
    {
        off = 0;
    }
    return rom + off;
}

void
gbplat_draw_line (uint_fast8_t ly, const uint8_t * shades)
{
    memcpy (lx_frame[ly], shades, GB_LCD_WIDTH);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lx_load_ini () - lit le fichier ini (facultatif)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
lx_load_ini (void)
{
    char        fname[512];
    const char *home = getenv ("HOME");
    FILE *      fp   = NULL;
    char        line[512];

    if (home)
    {
        snprintf (fname, sizeof (fname), "%s/.steccy-gb.ini", home);
        fp = fopen (fname, "r");
    }
    if (! fp)
    {
        fp = fopen ("steccy-gb.ini", "r");
    }
    if (! fp)
    {
        return;
    }
    while (fgets (line, sizeof (line), fp))
    {
        line[strcspn (line, "\r\n")] = '\0';
        gb_load_ini_line (line);
    }
    fclose (fp);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lx_load_rom () - charge la ROM entiere en memoire et la RAM sauvegardee (.sav)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
int
lx_load_rom (const char * name)
{
    char    fname[2 * GB_MAX_FILENAME_LEN + 2];
    FILE *  fp;
    long    len;
    int     rtc;
    char *  dot;

    fp = fopen (name, "rb");

    if (! fp && gb_settings.path[0] && name[0] != '/')                      // essayer dans PATH= du fichier ini
    {
        snprintf (fname, sizeof (fname), "%s/%s", gb_settings.path, name);
        fp = fopen (fname, "rb");
        name = fname;
    }
    if (! fp)
    {
        perror (name);
        return -1;
    }

    fseek (fp, 0, SEEK_END);
    len = ftell (fp);
    fseek (fp, 0, SEEK_SET);

    if (len < 0x8000 || len > 8 * 1024 * 1024)
    {
        fprintf (stderr, "%s: taille de ROM invalide (%ld octets)\n", name, len);
        fclose (fp);
        return -1;
    }

    free (rom);
    rom_size = (uint32_t) len;
    rom      = calloc (1, ((rom_size + 0x3FFF) / 0x4000) * 0x4000);

    if (! rom || fread (rom, 1, rom_size, fp) != rom_size)
    {
        fprintf (stderr, "%s: erreur de lecture\n", name);
        fclose (fp);
        return -1;
    }
    fclose (fp);

    rtc = gb_cart_init (rom, rom_size);

    if (rtc == -1)
    {
        fprintf (stderr, "%s: type de cartouche 0x%02X non supporte\n", name, rom[0x147]);
        return -1;
    }
    if (gb_cart.cgb_flag == 0xC0)
    {
        fprintf (stderr, "attention: jeu Game Boy Color uniquement, l'emulation DMG risque d'echouer\n");
    }

    snprintf (sav_name, sizeof (sav_name), "%s", name);
    dot = strrchr (sav_name, '.');

    if (dot && ! strchr (dot, '/'))
    {
        *dot = '\0';
    }
    strcat (sav_name, ".sav");

    if (gb_cart.has_battery && gb_cart.ram_size)
    {
        fp = fopen (sav_name, "rb");

        if (fp)
        {
            if (fread (gb_cart_ram (), 1, gb_cart.ram_size, fp) != gb_cart.ram_size)
            {
                fprintf (stderr, "%s: fichier de sauvegarde incomplet\n", sav_name);
            }
            fclose (fp);
        }
    }

    gb_reset ();
    printf ("ROM '%s' : type 0x%02X (MBC%d), %u banques, RAM %lu octets%s\n",
            gb_cart.title, gb_cart.type, gb_cart.mbc, gb_cart.rom_banks, (unsigned long) gb_cart.ram_size,
            gb_cart.has_battery ? ", pile" : "");
    return 0;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lx_save_ram () - ecrit le fichier .sav si la cartouche a une pile et que la RAM a change
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
lx_save_ram (int force)
{
    FILE * fp;

    if (! gb_cart.has_battery || ! gb_cart.ram_size)
    {
        return;
    }
    if (! gb_cart_ram_dirty () && ! force)
    {
        return;
    }

    fp = fopen (sav_name, "wb");

    if (fp)
    {
        fwrite (gb_cart_ram (), 1, gb_cart.ram_size, fp);
        fclose (fp);
    }
}
