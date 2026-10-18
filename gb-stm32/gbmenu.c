/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbmenu.c - menu STM32 de STECCY-GB : reprendre, reset, sauvegarder, choix d'une ROM (.GB / .GBC) sur la carte SD
 *
 * Version simplifiee du menu STECCY (menu/menu.c) : meme police 8x12 (font.c), memes touches
 * (fleches, Entree/Espace pour valider, Echap/TAB pour revenir) et meme commande par manette Wii.
 * Les noms de fichiers sont au format 8.3 (FatFs compile sans LFN, comme dans STECCY).
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "ff.h"
#include "tft.h"
#include "font.h"
#include "usb_hid_host.h"
#include "z80.h"
#include "gb.h"
#include "gbstm32.h"

#define MAX_FILES               64
#define NAME_LEN                13                              // 8.3 + '\0'
#define N_FIXED                 3                               // Reprendre, Reset, Sauvegarder
#define LINE_HEIGHT             14
#define MENU_X                  16
#define MENU_Y                  32

static char                     files[MAX_FILES][NAME_LEN];
static int                      n_files;

static int
filescmp (const void * p1, const void * p2)
{
    return strcasecmp ((const char *) p1, (const char *) p2);
}

static FRESULT                  scan_result;                    // resultat de f_opendir (diagnostic)
static int                      n_entries;                      // nombre total d'entrees a la racine
static char                     seen[8][NAME_LEN];              // premiers noms vus (diagnostic)

static void
scan_files (void)
{
    DIR     dir;
    FILINFO fno;

    n_files   = 0;
    n_entries = 0;

    scan_result = f_opendir (&dir, "/");

    if (scan_result != FR_OK)
    {
        return;
    }
    while (f_readdir (&dir, &fno) == FR_OK && fno.fname[0])
    {
        size_t len = strlen (fno.fname);

        if (n_entries < 8)
        {
            strncpy (seen[n_entries], fno.fname, NAME_LEN - 1);
            seen[n_entries][NAME_LEN - 1] = '\0';
        }
        n_entries++;

        if (n_files < MAX_FILES && len > 3 && len < NAME_LEN && ! (fno.fattrib & AM_DIR) &&
            (! strcasecmp (fno.fname + len - 3, ".GB") || (len > 4 && ! strcasecmp (fno.fname + len - 4, ".GBC"))))
        {
            strcpy (files[n_files++], fno.fname);
        }
    }
    f_closedir (&dir);
    qsort (files, (size_t) n_files, NAME_LEN, filescmp);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * show_no_rom () - ecran de diagnostic quand aucune ROM n'est trouvee (le programme s'arrete ensuite)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
show_no_rom (void)
{
    char    buf[48];
    int     i;
    int     y = MENU_Y;

    tft_fill_screen (BLACK565);
    draw_string ((unsigned char *) "Aucun fichier .GB sur la carte SD", y, MENU_X, WHITE565, RED565);
    y += 2 * LINE_HEIGHT;

    if (scan_result != FR_OK)
    {
        snprintf (buf, sizeof (buf), "Carte illisible : erreur FatFs %d", (int) scan_result);
        draw_string ((unsigned char *) buf, y, MENU_X, WHITE565, BLACK565);
        y += LINE_HEIGHT;
        draw_string ((unsigned char *) "(1 ou 3 = carte absente ou mal lue,", y, MENU_X, WHITE565, BLACK565);
        y += LINE_HEIGHT;
        draw_string ((unsigned char *) " 13 = pas FAT32 (exFAT ?) => formater)", y, MENU_X, WHITE565, BLACK565);
        return;
    }

    snprintf (buf, sizeof (buf), "Racine lue : %d entree(s)", n_entries);
    draw_string ((unsigned char *) buf, y, MENU_X, WHITE565, BLACK565);
    y += LINE_HEIGHT;

    for (i = 0; i < n_entries && i < 8; i++)
    {
        snprintf (buf, sizeof (buf), "  %.12s", seen[i]);
        draw_string ((unsigned char *) buf, y, MENU_X, YELLOW565, BLACK565);
        y += LINE_HEIGHT;
    }
}

static void
draw_entry (int line, const char * text, uint_fast8_t active)
{
    char buf[40];

    snprintf (buf, sizeof (buf), " %-30s", text);
    draw_string ((unsigned char *) buf, MENU_Y + line * LINE_HEIGHT, MENU_X,
                 active ? BLACK565 : WHITE565, active ? GREEN565 : BLACK565);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbmenu () - retourne GBMENU_xxx ; pour GBMENU_LOAD_ROM, le nom du fichier est copie dans selected
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
int
gbmenu (char * selected, int size, uint_fast8_t rom_loaded)
{
    static const char * fixed[N_FIXED] = { "Reprendre", "Reset", "Sauvegarder (.SAV)" };
    int     first   = rom_loaded ? 0 : N_FIXED;                 // sans ROM : seulement la liste des fichiers
    int     n_items;
    int     active;
    int     top     = 0;
    int     visible = (TFT_HEIGHT - MENU_Y - LINE_HEIGHT) / LINE_HEIGHT;
    int     redraw  = 1;

    scan_files ();
    n_items = N_FIXED + n_files;
    active  = first;

    if (n_items == first)                                       // aucune ROM sur la carte
    {
        show_no_rom ();

        for (;;)
        {
            // rien a faire : corriger la carte et redemarrer
        }
    }

    for (;;)
    {
        uint16_t key;

        if (redraw)
        {
            int i;

            tft_fill_screen (BLACK565);
            draw_string ((unsigned char *) "STECCY-GB", 8, MENU_X, YELLOW565, BLACK565);

            if (active < top)
            {
                top = active;
            }
            if (active >= top + visible)
            {
                top = active - visible + 1;
            }
            for (i = 0; i < visible && top + i < n_items; i++)
            {
                int idx = top + i;

                if (idx < first)
                {
                    draw_entry (i, "", 0);
                    continue;
                }
                draw_entry (i, idx < N_FIXED ? fixed[idx] : files[idx - N_FIXED], idx == active);
            }
            redraw = 0;
        }

        if (gb_settings.keyboard & KEYBOARD_USB)
        {
            usb_hid_host_process (0);
        }
        gbinput_poll (1);
        key = gbinput_get_key ();

        {
            uint32_t t = uptime + 1000;                         // ~20 ms entre deux lectures de la manette

            while (uptime < t)
            {
                if (gb_settings.keyboard & KEYBOARD_USB)
                {
                    usb_hid_host_process (0);                   // comme STECCY : interroger l'USB en continu
                }
            }
        }

        switch (key)
        {
            case GBKEY_UP:      if (active > first)       { active--; redraw = 1; }                                       break;
            case GBKEY_DOWN:    if (active < n_items - 1) { active++; redraw = 1; }                                       break;
            case GBKEY_PGUP:    active = (active - visible > first) ? active - visible : first;           redraw = 1;     break;
            case GBKEY_PGDN:    active = (active + visible < n_items) ? active + visible : n_items - 1;   redraw = 1;     break;
            case GBKEY_BACK:
                if (rom_loaded)
                {
                    gbinput_menu_request = 0;
                    return GBMENU_RESUME;
                }
                break;
            case GBKEY_OK:
                gbinput_menu_request = 0;

                switch (active)
                {
                    case 0:  return GBMENU_RESUME;
                    case 1:  return GBMENU_RESET;
                    case 2:  return GBMENU_SAVE;
                    default:
                        strncpy (selected, files[active - N_FIXED], (size_t) size - 1);
                        selected[size - 1] = '\0';
                        return GBMENU_LOAD_ROM;
                }
            default:
                break;
        }
    }
}
