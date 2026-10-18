/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbmem.c - plan memoire du Game Boy et controleurs de cartouche (MBC1, MBC2, MBC3, MBC5)
 *
 * Remplace zxram/zxram.c de STECCY (banques RAM/ROM du Spectrum 128K) par le banking des cartouches.
 *
 * La ROM n'est jamais copiee par le coeur : il demande un pointeur par banque de 16 Ko a la plateforme
 * (gbplat_rom_bank). Sous Linux la ROM entiere est en memoire ; sur STM32 un cache de banques lit la
 * carte SD a la demande (voir gb-stm32/gbstm32.c).
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <string.h>
#include "gbint.h"

#if defined STM32F4XX
#define GB_MAX_CART_RAM             0x8000                                  // 32 Ko max sur STM32 (RAM limitee)
#else
#define GB_MAX_CART_RAM             0x20000                                 // 128 Ko (MBC5)
#endif

GB_FAST_RAM uint8_t         gb_vram[0x2000];
GB_FAST_RAM uint8_t         gb_wram[0x2000];
GB_FAST_RAM uint8_t         gb_oam[0xA0];
GB_FAST_RAM uint8_t         gb_hram[0x80];
GB_FAST_RAM uint8_t         gb_io[0x80];
uint8_t                     gb_ie;

const uint8_t *             gb_rom0;
const uint8_t *             gb_romx;

GB_CART_INFO                gb_cart;

static uint8_t              cart_ram[GB_MAX_CART_RAM];
static uint8_t              cart_ram_dirty;

static uint8_t              ram_enabled;
static uint16_t             rom_bank;                                       // MBC1 : 5 bits bas ; MBC5 : 9 bits
static uint8_t              ram_bank;                                       // MBC1 : "bank2" (2 bits) ; MBC3/5 : banque RAM
static uint8_t              mbc1_mode;
static uint8_t              rtc_regs[5];                                    // MBC3 RTC (S M H DL DH) - valeurs figees
static uint8_t              rtc_latched[5];
static uint8_t              rtc_latch_last;

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_cart_init () - analyse de l'en-tete de cartouche
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
int
gb_cart_init (const uint8_t * header, uint32_t rom_size)
{
    static const uint32_t   ram_sizes[6] = { 0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000 };
    uint8_t                 ramcode;
    uint8_t                 i;

    memset (&gb_cart, 0, sizeof (gb_cart));

    for (i = 0; i < 16; i++)
    {
        uint8_t ch = header[0x134 + i];

        if (ch < 0x20 || ch > 0x7E)
        {
            break;
        }
        gb_cart.title[i] = (char) ch;
    }

    gb_cart.type        = header[0x147];
    gb_cart.cgb_flag    = header[0x143];
    gb_cart.rom_banks   = (uint16_t) (2u << (header[0x148] & 0x0F));

    if (rom_size && rom_size / 0x4000 < gb_cart.rom_banks)                 // fichier plus petit que l'en-tete
    {
        gb_cart.rom_banks = (uint16_t) ((rom_size + 0x3FFF) / 0x4000);
    }
    if (gb_cart.rom_banks < 2)
    {
        gb_cart.rom_banks = 2;
    }

    ramcode = header[0x149];
    gb_cart.ram_size = (ramcode < 6) ? ram_sizes[ramcode] : 0;

    switch (gb_cart.type)
    {
        case 0x00:                                              break;                          // ROM ONLY
        case 0x08:                                              break;                          // ROM+RAM
        case 0x09: gb_cart.has_battery = 1;                     break;                          // ROM+RAM+BATTERY
        case 0x01: case 0x02: gb_cart.mbc = 1;                  break;
        case 0x03: gb_cart.mbc = 1; gb_cart.has_battery = 1;    break;
        case 0x05: gb_cart.mbc = 2;                             break;
        case 0x06: gb_cart.mbc = 2; gb_cart.has_battery = 1;    break;
        case 0x0F: case 0x10: gb_cart.mbc = 3; gb_cart.has_battery = 1; gb_cart.has_rtc = 1; break;
        case 0x11: case 0x12: gb_cart.mbc = 3;                  break;
        case 0x13: gb_cart.mbc = 3; gb_cart.has_battery = 1;    break;
        case 0x19: case 0x1A: case 0x1C: case 0x1D: gb_cart.mbc = 5; break;
        case 0x1B: case 0x1E: gb_cart.mbc = 5; gb_cart.has_battery = 1; break;
        default:
            return -1;                                                                          // MBC non supporte (MMM01, HuC, camera...)
    }

    if (gb_cart.mbc == 2)
    {
        gb_cart.ram_size = 512;                                                                 // 512 x 4 bits integres au MBC2
    }

    if (gb_cart.ram_size > GB_MAX_CART_RAM)
    {
        return -2;                                                                              // RAM cartouche trop grande pour cette plateforme
    }

    memset (cart_ram, 0xFF, sizeof (cart_ram));
    return 0;
}

uint8_t *
gb_cart_ram (void)
{
    return cart_ram;
}

uint_fast8_t
gb_cart_ram_dirty (void)
{
    uint_fast8_t rtc = cart_ram_dirty;
    cart_ram_dirty = 0;
    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * update_banks () - recalcule les pointeurs ROM apres une ecriture dans les registres MBC
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
update_banks (void)
{
    uint16_t    bank0 = 0;
    uint16_t    bankx = rom_bank;
    uint16_t    mask  = (uint16_t) (gb_cart.rom_banks - 1);                 // les tailles de ROM sont des puissances de 2

    switch (gb_cart.mbc)
    {
        case 1:
            bankx = (uint16_t) ((rom_bank & 0x1F) | ((ram_bank & 3) << 5));
            if ((rom_bank & 0x1F) == 0)
            {
                bankx |= 1;
            }
            if (mbc1_mode)
            {
                bank0 = (uint16_t) ((ram_bank & 3) << 5);
            }
            break;
        case 2:
            bankx = rom_bank & 0x0F;
            if (bankx == 0)
            {
                bankx = 1;
            }
            break;
        case 3:
            bankx = rom_bank & 0x7F;
            if (bankx == 0)
            {
                bankx = 1;
            }
            break;
        case 5:
            bankx = rom_bank & 0x1FF;                                       // la banque 0 est autorisee en 0x4000 sur MBC5
            break;
        default:
            bankx = 1;
            break;
    }

    gb_rom0 = gbplat_rom_bank ((uint16_t) (bank0 & mask));
    gb_romx = gbplat_rom_bank ((uint16_t) (bankx & mask));
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * cart_ram_offset () - adresse dans cart_ram pour 0xA000-0xBFFF, ou -1 si non mappee
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static int32_t
cart_ram_offset (uint16_t addr)
{
    uint32_t off;

    if (! ram_enabled || gb_cart.ram_size == 0)
    {
        return -1;
    }

    if (gb_cart.mbc == 2)
    {
        return (int32_t) (addr & 0x01FF);
    }

    off = addr - 0xA000u;

    if (gb_cart.mbc == 1)
    {
        if (mbc1_mode)
        {
            off += (uint32_t) (ram_bank & 3) * 0x2000u;
        }
    }
    else if (gb_cart.mbc == 3 || gb_cart.mbc == 5)
    {
        off += (uint32_t) (ram_bank & 0x0F) * 0x2000u;
    }

    return (int32_t) (off % gb_cart.ram_size);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_mem_reset ()
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_mem_reset (void)
{
    memset (gb_vram, 0, sizeof (gb_vram));
    memset (gb_wram, 0, sizeof (gb_wram));
    memset (gb_oam,  0, sizeof (gb_oam));
    memset (gb_hram, 0, sizeof (gb_hram));
    gb_ie           = 0;
    ram_enabled     = (gb_cart.mbc == 0 && gb_cart.ram_size) ? 1 : 0;     // ROM+RAM : RAM toujours accessible
    rom_bank        = 1;
    ram_bank        = 0;
    mbc1_mode       = 0;
    rtc_latch_last  = 0xFF;
    memset (rtc_regs, 0, sizeof (rtc_regs));
    memset (rtc_latched, 0, sizeof (rtc_latched));
    update_banks ();
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_mem_read_slow () - tout ce qui n'est pas traite par le chemin rapide gb_mem_read() de gbint.h
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint8_t
gb_mem_read_slow (uint16_t addr)
{
    if (addr < 0x8000)
    {
        return (addr < 0x4000) ? gb_rom0[addr] : gb_romx[addr - 0x4000];
    }
    if (addr < 0xA000)
    {
        return gb_vram[addr - 0x8000];
    }
    if (addr < 0xC000)
    {
        int32_t off;

        if (gb_cart.mbc == 3 && ram_enabled && ram_bank >= 0x08 && ram_bank <= 0x0C)
        {
            return rtc_latched[ram_bank - 0x08];
        }

        off = cart_ram_offset (addr);

        if (off < 0)
        {
            return 0xFF;
        }
        if (gb_cart.mbc == 2)
        {
            return (uint8_t) (cart_ram[off] | 0xF0);
        }
        return cart_ram[off];
    }
    if (addr < 0xFE00)
    {
        return gb_wram[addr & 0x1FFF];                                      // 0xC000-0xDFFF + echo 0xE000-0xFDFF
    }
    if (addr < 0xFEA0)
    {
        return gb_oam[addr - 0xFE00];
    }
    if (addr < 0xFF00)
    {
        return 0xFF;                                                        // zone inutilisable
    }
    if (addr < 0xFF80)
    {
        return gb_io_read ((uint8_t) (addr - 0xFF00));
    }
    if (addr < 0xFFFF)
    {
        return gb_hram[addr - 0xFF80];
    }
    return gb_ie;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * mbc_write () - ecriture dans 0x0000-0x7FFF : registres du controleur de cartouche
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mbc_write (uint16_t addr, uint8_t value)
{
    switch (gb_cart.mbc)
    {
        case 1:
            if (addr < 0x2000)      { ram_enabled = ((value & 0x0F) == 0x0A); }
            else if (addr < 0x4000) { rom_bank = value & 0x1F; }
            else if (addr < 0x6000) { ram_bank = value & 0x03; }
            else                    { mbc1_mode = value & 0x01; }
            break;

        case 2:
            if (addr < 0x4000)
            {
                if (addr & 0x0100)  { rom_bank = value & 0x0F; }            // bit 8 de l'adresse : choix ROM / RAM enable
                else                { ram_enabled = ((value & 0x0F) == 0x0A); }
            }
            break;

        case 3:
            if (addr < 0x2000)      { ram_enabled = ((value & 0x0F) == 0x0A); }
            else if (addr < 0x4000) { rom_bank = value & 0x7F; }
            else if (addr < 0x6000) { ram_bank = value; }                   // 0-3 RAM, 8-C registres RTC
            else
            {
                if (rtc_latch_last == 0x00 && value == 0x01)                // sequence 0 puis 1 : latch de l'horloge
                {
                    memcpy (rtc_latched, rtc_regs, sizeof (rtc_regs));
                }
                rtc_latch_last = value;
            }
            break;

        case 5:
            if (addr < 0x2000)      { ram_enabled = ((value & 0x0F) == 0x0A); }
            else if (addr < 0x3000) { rom_bank = (uint16_t) ((rom_bank & 0x100) | value); }
            else if (addr < 0x4000) { rom_bank = (uint16_t) ((rom_bank & 0x0FF) | ((value & 1) << 8)); }
            else if (addr < 0x6000) { ram_bank = value & 0x0F; }
            break;

        default:                                                            // ROM ONLY : ecritures ignorees
            return;
    }
    update_banks ();
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_mem_write ()
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_mem_write (uint16_t addr, uint8_t value)
{
    if (addr < 0x8000)
    {
        mbc_write (addr, value);
    }
    else if (addr < 0xA000)
    {
        gb_vram[addr - 0x8000] = value;
    }
    else if (addr < 0xC000)
    {
        int32_t off;

        if (gb_cart.mbc == 3 && ram_enabled && ram_bank >= 0x08 && ram_bank <= 0x0C)
        {
            rtc_regs[ram_bank - 0x08] = value;
            return;
        }

        off = cart_ram_offset (addr);

        if (off >= 0)
        {
            if (gb_cart.mbc == 2)
            {
                value &= 0x0F;
            }
            if (cart_ram[off] != value)
            {
                cart_ram[off] = value;
                cart_ram_dirty = 1;
            }
        }
    }
    else if (addr < 0xFE00)
    {
        gb_wram[addr & 0x1FFF] = value;
    }
    else if (addr < 0xFEA0)
    {
        gb_oam[addr - 0xFE00] = value;
    }
    else if (addr < 0xFF00)
    {
        // zone inutilisable
    }
    else if (addr < 0xFF80)
    {
        gb_io_write ((uint8_t) (addr - 0xFF00), value);
    }
    else if (addr < 0xFFFF)
    {
        gb_hram[addr - 0xFF80] = value;
    }
    else
    {
        gb_ie = value;
    }
}
