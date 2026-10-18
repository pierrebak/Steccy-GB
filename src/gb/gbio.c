/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbio.c - registres d'E/S du Game Boy : joypad, timer, port serie, interruptions, DMA OAM
 *
 * Remplace zxio/zxio.c de STECCY. Sur le Spectrum les touches etaient lues par IN (port 0xFE) sur une
 * matrice 8x5 ; ici le joypad est le registre P1 (0xFF00), deux groupes de 4 boutons.
 * Le son (registres 0xFF10-0xFF3F) est traite par gbapu.c.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <string.h>
#include "gbint.h"

static volatile uint8_t     buttons;                                        // GB_BTN_xxx, 1 = enfonce (peut etre modifie depuis une IRQ)
uint16_t                    gb_div_counter;                                 // compteur interne 16 bits, DIV = bits 15..8
int32_t                     gb_serial_cycles;                               // > 0 : transfert serie en cours

static const uint8_t        tac_bit[4] = { 9, 3, 5, 7 };                    // 4096 Hz, 262144 Hz, 65536 Hz, 16384 Hz

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Joypad
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_set_buttons (uint8_t b)
{
    uint8_t newly = (uint8_t) (b & ~buttons);
    buttons = b;

    if (newly)
    {
        gb_request_interrupt (GB_INT_JOYPAD);                                // sort aussi le CPU de STOP/HALT
    }
}

void
gb_press_button (uint8_t mask)
{
    gb_set_buttons ((uint8_t) (buttons | mask));
}

void
gb_release_button (uint8_t mask)
{
    gb_set_buttons ((uint8_t) (buttons & ~mask));
}

static uint8_t
joypad_read (void)
{
    uint8_t p1  = gb_io[IO_P1];
    uint8_t low = 0x0F;
    uint8_t b   = buttons;

    if (! (p1 & 0x10))                                                      // directions selectionnees
    {
        low &= (uint8_t) ~(b & 0x0F);
    }
    if (! (p1 & 0x20))                                                      // boutons selectionnes
    {
        low &= (uint8_t) ~((b >> 4) & 0x0F);
    }
    return (uint8_t) (0xC0 | (p1 & 0x30) | low);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Timer : TIMA s'incremente sur chaque front descendant du bit selectionne de gb_div_counter
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
tima_increment (void)
{
    gb_io[IO_TIMA]++;

    if (gb_io[IO_TIMA] == 0)
    {
        gb_io[IO_TIMA] = gb_io[IO_TMA];
        gb_request_interrupt (GB_INT_TIMER);
    }
}

void
gb_timer_tick (uint32_t cycles)
{
    uint32_t    old = gb_div_counter;
    uint32_t    now = old + cycles;
    uint8_t     tac = gb_io[IO_TAC];

    gb_div_counter = (uint16_t) now;

    if (tac & 0x04)
    {
        uint8_t     shift = (uint8_t) (tac_bit[tac & 3] + 1);
        uint32_t    n     = (now >> shift) - (old >> shift);                // nombre de fronts descendants

        while (n--)
        {
            tima_increment ();
        }
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Port serie : seule l'horloge interne est emulee (pas de cable link). Utile pour les ROMs de test.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_serial_tick (uint32_t cycles)
{
    if (gb_serial_cycles > 0)
    {
        gb_serial_cycles -= (int32_t) cycles;

        if (gb_serial_cycles <= 0)
        {
            gbplat_serial_byte (gb_io[IO_SB]);
            gb_io[IO_SB]  = 0xFF;                                           // rien de connecte en face
            gb_io[IO_SC] &= 0x7F;
            gb_request_interrupt (GB_INT_SERIAL);
        }
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_io_read ()
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint8_t
gb_io_read (uint8_t reg)
{
    switch (reg)
    {
        case IO_P1:     return joypad_read ();
        case IO_SC:     return (uint8_t) (gb_io[IO_SC] | 0x7E);
        case IO_DIV:    return (uint8_t) (gb_div_counter >> 8);
        case IO_TAC:    return (uint8_t) (gb_io[IO_TAC] | 0xF8);
        case IO_IF:     return (uint8_t) (gb_io[IO_IF] | 0xE0);
        case IO_STAT:   return (uint8_t) (gb_io[IO_STAT] | 0x80);
        case IO_NR52:   return gb_apu_read_nr52 ();
        case 0x03: case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E:
                        return 0xFF;
        default:
            if (reg >= 0x10 && reg < 0x30)                                  // registres son : bits non lisibles a 1
            {
                static const uint8_t apu_mask[0x20] =
                {
                    0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF,
                    0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
                };
                return (uint8_t) (gb_io[reg] | apu_mask[reg - 0x10]);
            }
            if (reg >= 0x4C)                                                // registres CGB / inexistants sur DMG
            {
                return 0xFF;
            }
            return gb_io[reg];
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_io_write ()
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_io_write (uint8_t reg, uint8_t value)
{
    switch (reg)
    {
        case IO_P1:
            gb_io[IO_P1] = value & 0x30;
            break;

        case IO_SC:
            gb_io[IO_SC] = value;
            if ((value & 0x81) == 0x81)                                     // debut de transfert, horloge interne
            {
                gb_serial_cycles = 4096;                                       // 8 bits a 8192 Hz
            }
            break;

        case IO_DIV:
        {
            uint8_t tac = gb_io[IO_TAC];

            if ((tac & 0x04) && (gb_div_counter & (1u << tac_bit[tac & 3])))   // remise a zero => front descendant
            {
                tima_increment ();
            }
            gb_div_counter = 0;
            break;
        }

        case IO_TAC:
        {
            uint8_t old_tac = gb_io[IO_TAC];
            uint8_t old_in  = (old_tac & 0x04) && (gb_div_counter & (1u << tac_bit[old_tac & 3]));
            uint8_t new_in  = (value & 0x04) && (gb_div_counter & (1u << tac_bit[value & 3]));

            gb_io[IO_TAC] = value & 0x07;

            if (old_in && ! new_in)                                         // glitch du multiplexeur
            {
                tima_increment ();
            }
            break;
        }

        case IO_IF:
            gb_io[IO_IF] = value & 0x1F;
            break;

        case IO_LCDC:
            gb_ppu_flush ();
            gb_ppu_lcdc_write (value);
            break;

        case IO_STAT:
            gb_ppu_flush ();
            gb_io[IO_STAT] = (uint8_t) ((gb_io[IO_STAT] & 0x07) | (value & 0x78));
            gb_ppu_stat_update ();
            break;

        case IO_LY:                                                         // lecture seule
            break;

        case IO_LYC:
            gb_ppu_flush ();
            gb_io[IO_LYC] = value;
            gb_ppu_stat_update ();
            break;

        case IO_DMA:                                                        // DMA OAM : copie immediate de 160 octets
        {
            uint16_t src = (uint16_t) (value << 8);
            uint8_t  i;

            gb_io[IO_DMA] = value;

            for (i = 0; i < 0xA0; i++)
            {
                gb_oam[i] = gb_mem_read ((uint16_t) (src + i));
            }
            break;
        }

        default:
            if (reg >= 0x10 && reg < 0x40)                                  // son : NR10-NR52 et RAM d'onde
            {
                gb_apu_write (reg, value);
                break;
            }
            gb_io[reg] = value;
            break;
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_io_reset () - valeurs des registres apres la boot ROM DMG
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_io_reset (void)
{
    memset (gb_io, 0, sizeof (gb_io));
    gb_io[IO_P1]    = 0x30;
    gb_io[IO_SC]    = 0x00;
    gb_io[IO_TIMA]  = 0x00;
    gb_io[IO_TMA]   = 0x00;
    gb_io[IO_TAC]   = 0x00;
    gb_io[IO_IF]    = 0x01;
    gb_io[0x10]     = 0x80;                                                 // registres son (NR10...)
    gb_io[0x11]     = 0xBF;
    gb_io[0x12]     = 0xF3;
    gb_io[0x14]     = 0xBF;
    gb_io[0x16]     = 0x3F;
    gb_io[0x19]     = 0xBF;
    gb_io[0x1A]     = 0x7F;
    gb_io[0x1B]     = 0xFF;
    gb_io[0x1C]     = 0x9F;
    gb_io[0x1E]     = 0xBF;
    gb_io[0x20]     = 0xFF;
    gb_io[0x23]     = 0xBF;
    gb_io[0x24]     = 0x77;
    gb_io[0x25]     = 0xF3;
    gb_io[IO_NR52]  = 0x80;
    gb_io[IO_LCDC]  = 0x91;
    gb_io[IO_STAT]  = 0x85;
    gb_io[IO_BGP]   = 0xFC;
    gb_io[IO_OBP0]  = 0xFF;
    gb_io[IO_OBP1]  = 0xFF;
    gb_div_counter     = 0xABCC;
    gb_serial_cycles   = 0;
    buttons         = 0;
}
