/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbint.h - declarations internes partagees entre les modules du coeur STECCY-GB
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifndef GBINT_H
#define GBINT_H

#include <stdint.h>
#include "gb.h"

#if defined STM32F4XX
#define GB_INLINE                   static inline __attribute__((always_inline))
#define GB_FAST_RAM                 __attribute__((section(".ccram")))         // RAM CCM 64 Ko (meme section que zxram.c de STECCY), acces CPU seulement
#else
#define GB_INLINE                   static inline
#define GB_FAST_RAM
#endif

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Memoire (gbmem.c)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
extern uint8_t              gb_vram[0x2000];
extern uint8_t              gb_wram[0x2000];
extern uint8_t              gb_oam[0xA0];
extern uint8_t              gb_hram[0x80];                              // 0xFF80-0xFFFE (+ IE en 0xFFFF)
extern uint8_t              gb_io[0x80];                                // 0xFF00-0xFF7F
extern uint8_t              gb_ie;

extern const uint8_t *      gb_rom0;                                    // 0x0000-0x3FFF
extern const uint8_t *      gb_romx;                                    // 0x4000-0x7FFF

extern uint8_t              gb_mem_read_slow (uint16_t addr);
extern void                 gb_mem_write (uint16_t addr, uint8_t value);
extern void                 gb_mem_reset (void);

GB_INLINE uint8_t
gb_mem_read (uint16_t addr)
{
    if (addr < 0x4000)
    {
        return gb_rom0[addr];
    }
    if (addr < 0x8000)
    {
        return gb_romx[addr - 0x4000];
    }
    if (addr >= 0xC000 && addr < 0xE000)
    {
        return gb_wram[addr - 0xC000];
    }
    if (addr >= 0xFF80 && addr < 0xFFFF)
    {
        return gb_hram[addr - 0xFF80];
    }
    return gb_mem_read_slow (addr);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Registres E/S (adresses relatives a 0xFF00)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#define IO_P1                       0x00
#define IO_SB                       0x01
#define IO_SC                       0x02
#define IO_DIV                      0x04
#define IO_TIMA                     0x05
#define IO_TMA                      0x06
#define IO_TAC                      0x07
#define IO_IF                       0x0F
#define IO_NR52                     0x26
#define IO_LCDC                     0x40
#define IO_STAT                     0x41
#define IO_SCY                      0x42
#define IO_SCX                      0x43
#define IO_LY                       0x44
#define IO_LYC                      0x45
#define IO_DMA                      0x46
#define IO_BGP                      0x47
#define IO_OBP0                     0x48
#define IO_OBP1                     0x49
#define IO_WY                       0x4A
#define IO_WX                       0x4B

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * E/S (gbio.c)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
extern uint8_t              gb_io_read (uint8_t reg);
extern void                 gb_io_write (uint8_t reg, uint8_t value);
extern void                 gb_io_reset (void);
extern void                 gb_timer_tick (uint32_t cycles);
extern void                 gb_serial_tick (uint32_t cycles);
extern uint16_t             gb_div_counter;
extern int32_t              gb_serial_cycles;

GB_INLINE void
gb_timer_advance (uint32_t cycles)                                          // timer arrete : seul DIV avance (sans appel)
{
    if (gb_io[0x07] & 0x04)
    {
        gb_timer_tick (cycles);
    }
    else
    {
        gb_div_counter = (uint16_t) (gb_div_counter + cycles);
    }
}

GB_INLINE void
gb_serial_advance (uint32_t cycles)
{
    if (gb_serial_cycles > 0)
    {
        gb_serial_tick (cycles);
    }
}

GB_INLINE void
gb_request_interrupt (uint8_t mask)
{
    gb_io[IO_IF] |= mask;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * PPU (gbppu.c)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
extern void                 gb_ppu_reset (void);
extern void                 gb_ppu_tick (uint32_t cycles);
extern void                 gb_ppu_lcdc_write (uint8_t value);
extern void                 gb_ppu_stat_update (void);
extern volatile uint_fast8_t gb_ppu_frame_done;
extern uint32_t             gb_ppu_pending;
extern uint32_t             gb_ppu_to_event;
extern void                 gb_ppu_flush (void);

GB_INLINE void
gb_ppu_advance (uint32_t cycles)                                            // appel reel seulement au prochain evenement
{
    gb_ppu_pending += cycles;

    if (gb_ppu_pending >= gb_ppu_to_event)
    {
        gb_ppu_flush ();
    }
}
extern uint_fast8_t         gb_ppu_render_enabled;                      // 0 => frameskip : pas de rendu

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Son (gbapu.c)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
extern void                 gb_apu_reset (void);
extern uint32_t             gb_apu_pending;
extern void                 gb_apu_flush (void);

GB_INLINE void
gb_apu_advance (uint32_t cycles)                                            // calcul du son par paquets de 256 cycles
{
    gb_apu_pending += cycles;

    if (gb_apu_pending >= 256)
    {
        gb_apu_flush ();
    }
}
extern void                 gb_apu_write (uint8_t reg, uint8_t value);
extern uint8_t              gb_apu_read_nr52 (void);

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * CPU (sm83.c)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
extern void                 sm83_reset (void);
extern uint32_t             sm83_step (void);                           // execute une instruction, retourne les T-cycles

#endif
