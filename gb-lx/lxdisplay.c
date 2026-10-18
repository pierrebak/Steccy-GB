/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lxdisplay.c - affichage de l'ecran Game Boy sous Linux (framebuffer ou X11)
 *
 * Meme principe que lxdisplay.c de STECCY : agrandissement entier, image centree, et en framebuffer seuls
 * les pixels modifies depuis l'image precedente sont redessines (cache "shadow").
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <string.h>
#include "gb.h"
#include "lxcart.h"
#include "lxdisplay.h"

#if defined FRAMEBUFFER
#include "lxfb.h"
#elif defined X11
#include "lxx11.h"
#endif

#define FB_RGB(r,g,b)       (((uint32_t) (r) << 16) | ((uint32_t) (g) << 8) | (uint32_t) (b))

unsigned int                zx_display_width;
unsigned int                zx_display_height;
uint_fast8_t                lx_display_cached;

const uint32_t              lx_palettes[3][4] =
{
    { FB_RGB (0xE0, 0xF8, 0xD0), FB_RGB (0x88, 0xC0, 0x70), FB_RGB (0x34, 0x68, 0x56), FB_RGB (0x08, 0x18, 0x20) },  // vert DMG
    { FB_RGB (0xFF, 0xFF, 0xFF), FB_RGB (0xAA, 0xAA, 0xAA), FB_RGB (0x55, 0x55, 0x55), FB_RGB (0x00, 0x00, 0x00) },  // gris
    { FB_RGB (0xC4, 0xCF, 0xA1), FB_RGB (0x8B, 0x95, 0x6D), FB_RGB (0x4D, 0x53, 0x3C), FB_RGB (0x1F, 0x1F, 0x1F) },  // "pocket"
};

static unsigned int         zoom;
static unsigned int         left_offset;
static unsigned int         top_offset;

void
lxdisplay_init (unsigned int width, unsigned int height)
{
    unsigned int zx = width / GB_LCD_WIDTH;
    unsigned int zy = height / GB_LCD_HEIGHT;

    zx_display_width    = width;
    zx_display_height   = height;
    zoom                = (zx < zy) ? zx : zy;

    if (gb_settings.scale && gb_settings.scale < zoom)
    {
        zoom = gb_settings.scale;
    }
    if (zoom < 1)
    {
        zoom = 1;
    }
    left_offset         = (width  - zoom * GB_LCD_WIDTH)  / 2;
    top_offset          = (height - zoom * GB_LCD_HEIGHT) / 2;
    lx_display_cached   = 0;
}

void
lxdisplay_update (void)
{
    static uint8_t  last_palette = 0xFF;
    const uint32_t *pal;

    if (last_palette != gb_settings.palette)
    {
        last_palette      = gb_settings.palette;
        lx_display_cached = 0;
    }
    pal = lx_palettes[gb_settings.palette % 3];

#if defined X11
    x11_draw_frame (&lx_frame[0][0], pal, zoom, left_offset, top_offset);
    lx_display_cached = 1;
#else
    {
        static uint8_t  shadow[GB_LCD_HEIGHT][GB_LCD_WIDTH];
        unsigned int    x, y;

        for (y = 0; y < GB_LCD_HEIGHT; y++)
        {
            for (x = 0; x < GB_LCD_WIDTH; x++)
            {
                uint8_t s = lx_frame[y][x];

                if (lx_display_cached && shadow[y][x] == s)
                {
                    continue;                                               // pixel inchange
                }
                shadow[y][x] = s;
                fill_rectangle ((uint16_t) (left_offset + x * zoom), (uint16_t) (top_offset + y * zoom),
                                (uint16_t) (left_offset + x * zoom + zoom - 1), (uint16_t) (top_offset + y * zoom + zoom - 1), pal[s & 3]);
            }
        }
        lx_display_cached = 1;
    }
#endif
}
