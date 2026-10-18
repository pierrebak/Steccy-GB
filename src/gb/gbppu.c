/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbppu.c - PPU du Game Boy (DMG) : modes LCD, interruptions STAT/VBlank, rendu ligne par ligne
 *
 * Remplace zxscr/zxscr.c de STECCY. Le Spectrum a un ecran bitmap 256x192 que STECCY relit 50 fois
 * par seconde ; le Game Boy compose chaque ligne a partir de tuiles (fond, fenetre) et de sprites.
 * Le rendu se fait a la fin du mode 3 de chaque ligne et la ligne (160 nuances 0..3) est envoyee a
 * la plateforme par gbplat_draw_line() : aucun framebuffer complet n'est necessaire (utile sur STM32).
 *
 * Simplifications : duree du mode 3 fixe (172 dots), pas de FIFO pixel => les effets "mid-scanline"
 * ne sont pas reproduits. Les jeux classiques n'en ont pas besoin.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <string.h>
#include "gbint.h"

#define DOTS_PER_LINE           456
#define MODE2_DOTS              80
#define MODE3_END               (80 + 172)
#define LINES_PER_FRAME         154

#define LCDC_ON                 0x80
#define LCDC_WIN_MAP            0x40
#define LCDC_WIN_ON             0x20
#define LCDC_TILE_DATA          0x10
#define LCDC_BG_MAP             0x08
#define LCDC_OBJ_SIZE           0x04
#define LCDC_OBJ_ON             0x02
#define LCDC_BG_ON              0x01

volatile uint_fast8_t       gb_ppu_frame_done;
uint32_t                    gb_ppu_pending;                                 // cycles pas encore traites (gbint.h : gb_ppu_advance)
uint32_t                    gb_ppu_to_event = 1;                            // cycles jusqu'au prochain changement d'etat
uint_fast8_t                gb_ppu_render_enabled = 1;

static uint32_t             dot;                                            // position dans la ligne courante
static uint32_t             off_dots;                                       // ecran eteint : cycles depuis la derniere "image"
static uint8_t              window_line;                                    // compteur de ligne interne de la fenetre
static uint8_t              stat_irq_line;                                  // etat de la ligne d'interruption STAT (detection de front)
static uint8_t              line_rendered;

#define LY                  gb_io[IO_LY]
#define STAT                gb_io[IO_STAT]
#define LCDC                gb_io[IO_LCDC]

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_ppu_stat_update () - mise a jour du flag LY=LYC et interruption STAT sur front montant
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_ppu_stat_update (void)
{
    uint8_t mode = STAT & 0x03;
    uint8_t line;

    if (LY == gb_io[IO_LYC])
    {
        STAT |= 0x04;
    }
    else
    {
        STAT &= (uint8_t) ~0x04;
    }

    if (! (LCDC & LCDC_ON))
    {
        stat_irq_line = 0;
        return;
    }

    line = (uint8_t) (((STAT & 0x40) && (STAT & 0x04)) ||
                      ((STAT & 0x08) && mode == 0) ||
                      ((STAT & 0x10) && mode == 1) ||
                      ((STAT & 0x20) && mode == 2));

    if (line && ! stat_irq_line)
    {
        gb_request_interrupt (GB_INT_STAT);
    }
    stat_irq_line = line;
}

static void
set_mode (uint8_t mode)
{
    STAT = (uint8_t) ((STAT & ~0x03) | mode);
    gb_ppu_stat_update ();
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * bg_tile_addr () - adresse (relative a 0x8000) d'une tuile de fond/fenetre selon LCDC.4
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint16_t
bg_tile_addr (uint8_t tile_index)
{
    if (LCDC & LCDC_TILE_DATA)
    {
        return (uint16_t) (tile_index * 16);                                // 0x8000, non signe
    }
    return (uint16_t) (0x1000 + (int8_t) tile_index * 16);                  // 0x9000, signe
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * render_line () - compose la ligne LY
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
render_line (void)
{
    static uint8_t  shades[GB_LCD_WIDTH];
    uint8_t         bg_idx[GB_LCD_WIDTH];                                   // index brut du fond (priorite des sprites)
    uint8_t         ly  = LY;
    uint8_t         bgp = gb_io[IO_BGP];
    uint_fast16_t   x;
    uint8_t         win_drawn = 0;

    /*---------------------------------------------------------------------------------------------------------------------------------------
     * Fond et fenetre
     *---------------------------------------------------------------------------------------------------------------------------------------
     */
    if (LCDC & LCDC_BG_ON)
    {
        uint8_t     scy     = gb_io[IO_SCY];
        uint8_t     scx     = gb_io[IO_SCX];
        uint8_t     y       = (uint8_t) (ly + scy);
        uint16_t    map     = (LCDC & LCDC_BG_MAP) ? 0x1C00 : 0x1800;
        int         win_x   = 256;
        uint8_t     wx      = gb_io[IO_WX];

        if ((LCDC & LCDC_WIN_ON) && gb_io[IO_WY] <= ly && wx <= 166)
        {
            win_x = (int) wx - 7;
        }

        uint16_t    wmap    = (LCDC & LCDC_WIN_MAP) ? 0x1C00 : 0x1800;
        uint_fast16_t bg_end = (win_x < 0) ? 0 : ((win_x > GB_LCD_WIDTH) ? GB_LCD_WIDTH : (uint_fast16_t) win_x);
        uint8_t     pal[4];
        uint8_t     bx      = scx;
        const uint8_t * maprow = &gb_vram[map + (y >> 3) * 32];
        uint8_t     row2    = (uint8_t) ((y & 7) * 2);

        pal[0] = bgp & 3;
        pal[1] = (bgp >> 2) & 3;
        pal[2] = (bgp >> 4) & 3;
        pal[3] = (bgp >> 6) & 3;

        /*-----------------------------------------------------------------------------------------------------------------------------------
         * Fond : une tuile (8 pixels) a la fois au lieu d'un pixel a la fois
         *-----------------------------------------------------------------------------------------------------------------------------------
         */
        x = 0;

        while (x < bg_end)
        {
            uint16_t        taddr = (uint16_t) (bg_tile_addr (maprow[bx >> 3]) + row2);
            uint_fast8_t    lo    = gb_vram[taddr];
            uint_fast8_t    hi    = gb_vram[taddr + 1];
            uint_fast8_t    px    = bx & 7;
            uint_fast8_t    n     = (uint_fast8_t) (8 - px);

            if (n > bg_end - x)
            {
                n = (uint_fast8_t) (bg_end - x);
            }
            bx = (uint8_t) (bx + n);

            while (n--)
            {
                uint_fast8_t bit = (uint_fast8_t) (7 - px++);
                uint_fast8_t idx = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);

                bg_idx[x] = (uint8_t) idx;
                shades[x] = pal[idx];
                x++;
            }
        }

        /*-----------------------------------------------------------------------------------------------------------------------------------
         * Fenetre (de win_x a la fin de la ligne), meme principe
         *-----------------------------------------------------------------------------------------------------------------------------------
         */
        if (x < GB_LCD_WIDTH)
        {
            const uint8_t * wrow = &gb_vram[wmap + (window_line >> 3) * 32];
            uint8_t         wr2  = (uint8_t) ((window_line & 7) * 2);
            uint_fast16_t   wxp  = (uint_fast16_t) ((int) x - win_x);       // > 0 si WX < 7 (fenetre coupee a gauche)

            win_drawn = 1;

            while (x < GB_LCD_WIDTH)
            {
                uint16_t        taddr = (uint16_t) (bg_tile_addr (wrow[(wxp >> 3) & 31]) + wr2);
                uint_fast8_t    lo    = gb_vram[taddr];
                uint_fast8_t    hi    = gb_vram[taddr + 1];
                uint_fast8_t    px    = wxp & 7;
                uint_fast8_t    n     = (uint_fast8_t) (8 - px);

                if (n > GB_LCD_WIDTH - x)
                {
                    n = (uint_fast8_t) (GB_LCD_WIDTH - x);
                }
                wxp += n;

                while (n--)
                {
                    uint_fast8_t bit = (uint_fast8_t) (7 - px++);
                    uint_fast8_t idx = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);

                    bg_idx[x] = (uint8_t) idx;
                    shades[x] = pal[idx];
                    x++;
                }
            }
        }
    }
    else
    {
        memset (bg_idx, 0, sizeof (bg_idx));
        memset (shades, bgp & 3, sizeof (shades));
    }

    if (win_drawn)
    {
        window_line++;
    }

    /*---------------------------------------------------------------------------------------------------------------------------------------
     * Sprites : 10 par ligne max, priorite DMG = plus petite abscisse puis plus petit index OAM
     *---------------------------------------------------------------------------------------------------------------------------------------
     */
    if (LCDC & LCDC_OBJ_ON)
    {
        uint8_t     height = (LCDC & LCDC_OBJ_SIZE) ? 16 : 8;
        uint8_t     list[10];
        uint8_t     n = 0;
        uint8_t     i;
        uint8_t     done[GB_LCD_WIDTH];

        for (i = 0; i < 40 && n < 10; i++)
        {
            int sy = (int) gb_oam[i * 4] - 16;

            if ((int) ly >= sy && (int) ly < sy + height)
            {
                uint8_t j = n++;

                while (j > 0 && gb_oam[list[j - 1] * 4 + 1] > gb_oam[i * 4 + 1])   // tri stable par abscisse
                {
                    list[j] = list[j - 1];
                    j--;
                }
                list[j] = i;
            }
        }

        memset (done, 0, sizeof (done));

        for (i = 0; i < n; i++)                                             // du plus prioritaire au moins prioritaire
        {
            const uint8_t * o     = &gb_oam[list[i] * 4];
            int             sx    = (int) o[1] - 8;
            uint8_t         attr  = o[3];
            uint8_t         row   = (uint8_t) (ly - (o[0] - 16));
            uint8_t         tile  = o[2];
            uint8_t         pal   = (attr & 0x10) ? gb_io[IO_OBP1] : gb_io[IO_OBP0];
            uint16_t        taddr;
            uint8_t         lo, hi;
            int             p;

            if (attr & 0x40)                                                // miroir vertical
            {
                row = (uint8_t) (height - 1 - row);
            }
            if (height == 16)
            {
                tile &= 0xFE;
            }

            taddr = (uint16_t) (tile * 16 + row * 2);
            lo    = gb_vram[taddr];
            hi    = gb_vram[taddr + 1];

            for (p = 0; p < 8; p++)
            {
                int     px = sx + p;
                uint8_t bit;
                uint8_t idx;

                if (px < 0 || px >= GB_LCD_WIDTH || done[px])
                {
                    continue;
                }

                bit = (uint8_t) ((attr & 0x20) ? p : 7 - p);                // miroir horizontal
                idx = (uint8_t) (((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));

                if (idx == 0)                                               // transparent
                {
                    continue;
                }

                done[px] = 1;                                               // ce pixel appartient a ce sprite

                if ((attr & 0x80) && bg_idx[px] != 0)                       // derriere le fond (couleurs 1-3)
                {
                    continue;
                }
                shades[px] = (pal >> (idx * 2)) & 3;
            }
        }
    }

    gbplat_draw_line (ly, shades);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_ppu_lcdc_write () - allumage / extinction de l'ecran
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_ppu_lcdc_write (uint8_t value)
{
    uint8_t was_on = LCDC & LCDC_ON;

    LCDC = value;

    if (was_on && ! (value & LCDC_ON))                                      // extinction : LY = 0, mode 0
    {
        LY  = 0;
        dot = 0;
        window_line = 0;
        STAT &= (uint8_t) ~0x03;
        stat_irq_line = 0;
    }
    else if (! was_on && (value & LCDC_ON))                                 // allumage : debut de la ligne 0
    {
        LY  = 0;
        dot = 0;
        line_rendered = 0;
        window_line = 0;
        set_mode (2);
    }
    gb_ppu_flush ();                                                        // nouvelle echeance
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_ppu_tick () - avance le PPU de 'cycles' T-cycles
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_ppu_tick (uint32_t cycles)
{
    if (! (LCDC & LCDC_ON))
    {
        off_dots += cycles;                                                 // ecran eteint : on livre quand meme des "images"

        if (off_dots >= GB_CYCLES_PER_FRAME)
        {
            off_dots -= GB_CYCLES_PER_FRAME;
            gb_ppu_frame_done = 1;
        }
        return;
    }

    dot += cycles;

    for (;;)
    {
        uint8_t mode = STAT & 0x03;

        if (LY < GB_LCD_HEIGHT)
        {
            if (mode == 2 && dot >= MODE2_DOTS)
            {
                set_mode (3);
                continue;
            }
            if (mode == 3 && dot >= MODE3_END)
            {
                if (! line_rendered)
                {
                    if (gb_ppu_render_enabled)
                    {
                        render_line ();
                    }
                    else if ((LCDC & LCDC_WIN_ON) && gb_io[IO_WY] <= LY && gb_io[IO_WX] <= 166)
                    {
                        window_line++;                                      // garder le compteur fenetre coherent en frameskip
                    }
                    line_rendered = 1;
                }
                set_mode (0);
                continue;
            }
        }

        if (dot < DOTS_PER_LINE)
        {
            break;
        }

        dot -= DOTS_PER_LINE;
        LY++;
        line_rendered = 0;

        if (LY == GB_LCD_HEIGHT)                                            // debut du VBlank
        {
            set_mode (1);
            gb_request_interrupt (GB_INT_VBLANK);
            gb_ppu_frame_done = 1;
        }
        else if (LY >= LINES_PER_FRAME)                                     // nouvelle image
        {
            LY = 0;
            window_line = 0;
            set_mode (2);
        }
        else if (LY < GB_LCD_HEIGHT)
        {
            set_mode (2);
        }
        else
        {
            gb_ppu_stat_update ();                                          // LY change pendant le VBlank (LYC)
        }
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_ppu_reset ()
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_ppu_reset (void)
{
    gb_ppu_pending  = 0;
    off_dots        = 0;
    dot             = 0;
    window_line     = 0;
    stat_irq_line   = 0;
    line_rendered   = 0;
    LY              = 0;
    STAT            = (uint8_t) ((STAT & ~0x03) | 2);
    gb_ppu_stat_update ();
    gb_ppu_flush ();
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_ppu_flush () - traite les cycles en attente puis calcule l'echeance du prochain evenement (changement de mode,
 * fin de ligne). Entre deux evenements, LY et STAT ne changent pas : il est donc exact de ne rien faire, ce qui evite
 * un appel de fonction a chaque instruction (gb_ppu_advance dans gbint.h).
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_ppu_flush (void)
{
    uint32_t c = gb_ppu_pending;
    uint32_t target;

    gb_ppu_pending = 0;

    if (c)
    {
        gb_ppu_tick (c);
    }

    if (! (LCDC & LCDC_ON))
    {
        target = GB_CYCLES_PER_FRAME - off_dots;
    }
    else
    {
        uint8_t mode = STAT & 0x03;

        if (LY < GB_LCD_HEIGHT && mode == 2)
        {
            target = MODE2_DOTS - dot;
        }
        else if (LY < GB_LCD_HEIGHT && mode == 3)
        {
            target = MODE3_END - dot;
        }
        else
        {
            target = DOTS_PER_LINE - dot;
        }
    }
    gb_ppu_to_event = (target == 0 || target > DOTS_PER_LINE * 160) ? 1 : target;
}
