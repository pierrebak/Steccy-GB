# STECCY-GB — Game Boy emulator derived from STECCY

STECCY-GB emulates a classic Game Boy (DMG, 1989). It is built on the architecture and drivers of [STECCY](https://github.com/ukw100/STECCY), Frank Meyer’s ZX Spectrum emulator for STM32. The Spectrum core (Z80, ULA) has been replaced by a Game Boy core. Everything else is kept, and most drivers are used **without modification**:
- the board and the TFT display;
- the SD card and FatFs;
- PS/2 and USB keyboards;
- Wii controllers;
- the Linux X11 and framebuffer ports.

## Correspondence with STECCY

| STECCY (ZX Spectrum)            | STECCY-GB (Game Boy)                | Note |
|---------------------------------|-------------------------------------|------|
| `src/z80/z80.c`                 | `src/gb/sm83.c`                     | New CPU: the Game Boy SM83 is not Z80-compatible (no IX/IY, ED, IN/OUT, no S and P/V flags; instructions differ) |
| `src/zxram/zxram.c`             | `src/gb/gbmem.c`                    | 128K banked RAM replaced by cartridge controllers MBC1/2/3/5 |
| `src/zxscr/zxscr.c`             | `src/gb/gbppu.c` + platforms        | Tile and sprite rendering line by line |
| `src/zxio/zxio.c`               | `src/gb/gbio.c`                     | Joypad, timer, serial port, DMA |
| `zx_spectrum()`, `z80_idle_time()` | `gb_run_frame()` + platform loop  | A frame at 59.73 Hz |
| `steccy-lx/lxfb.c`, `lxkbd.c`, `scancodes.h` | `gb-lx/` (copied)       | `lxkbd.c`: only the line `#include "z80.h"` changes |
| `steccy-lx/lxx11.c`             | `gb-lx/lxx11.c`                     | Window and key table reused; display via XImage |
| `src/main.c`                    | `gb-stm32/main.c`                   | A few lines modified, marked “STECCY-GB” (Wii controller, call to `gb_stm32()`) |
| `menu/menu.c`, `joystick/joystick.c`, `keypress/keypress.c` | `gb-stm32/gbmenu.c`, `gbinput.c` | Rewritten, because these files are tied to the ZX keyboard |
| all other STM32 drivers         | taken directly from the STECCY repo  | No copy, no modification |

## Structure

```
src/gb/        portable core (no platform dependency)
gb-lx/         Linux: xsteccy-gb (X11), steccy-gb (framebuffer), gb-headless (tests)
gb-stm32/      STM32F407VET6 Black Board: Makefile, main.c, gbstm32.c, gbinput.c, gbmenu.c,
               shim/ (compatibility headers for STECCY drivers), linker script
bin/           precompiled .hex files (tested on hardware)
tools/         run-tests.sh (test ROMs), cachetest.c (ROM cache on PC), make-keytest.py
steccy-gb.ini  example configuration file
```

## Linux

```
sudo apt-get install build-essential libx11-dev
cd gb-lx
make                    # xsteccy-gb, steccy-gb, gb-headless
./xsteccy-gb game.gb    # 640x576 window (4x zoom), option -g LxH
./steccy-gb game.gb     # framebuffer console 32 bpp (e.g. Raspberry Pi), like STECCY's steccy
```

## STM32F407VET6

Hardware is the same as STECCY (Black Board + TFT SSD1963 800x480 or ILI9341 320x240, SD card, PS/2 or USB keyboard, Wii controller). Wiring is therefore as described in STECCY’s README and examples.

```
sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi
git clone https://github.com/ukw100/STECCY.git          # drivers are taken from here
cd gb-stm32
make STECCY=../../STECCY                 # -> steccy-gb-ssd1963.hex
make STECCY=../../STECCY TFT=ILI9341     # -> steccy-gb-ili9341.hex
```

Setup:
1. Flash the `.hex` as for STECCY.
2. Copy `.GB` ROMs to the SD card (FAT32, not exFAT) using 8.3 filename format.
3. Also copy, if needed, the `bin/STECCYGB.INI` file. On STM32 the name must be **`STECCYGB.INI`**: FatFs is compiled without long filenames, and `steccy-gb.ini` (9 characters before the dot) does not work on STM32.

Without a `ROM=` line in the ini, a selection menu is shown at startup. If no ROM is found, the red screen indicates a FatFs error or lists files seen at the root.

Display:
- SSD1963: 3x zoom, i.e. 480x432.
- ILI9341: 1.5x zoom, i.e. 240x216.
- Only modified lines are sent to the TFT, using the same cache principle as STECCY.

## Controls

Keys are read by physical position, so the same key table works for AZERTY, QWERTY and QWERTZ.

| Game Boy  | Keyboard                               | Wii Gamepad          | Wii Nunchuk         |
|-----------|----------------------------------------|----------------------|---------------------|
| D-Pad     | Arrow keys, numeric keypad 8 4 6 2     | D-Pad, left stick    | Stick               |
| A         | X, Space, numeric keypad 0             | A, X                 | Z                   |
| B         | W (AZERTY) / Z (QWERTY) / Y (QWERTZ)  | B, Y                 | C                   |
| START     | Enter                                  | +                    | Z + C together      |
| SELECT    | Backspace, right Shift                  | −                    | —                   |

Common shortcuts:
- F3: turbo (like STECCY);
- F5: reset.

Linux only:
- F2: palette;
- F12 or Esc: quit.

STM32 only:
- TAB or Esc: menu;
- HOME on the Gamepad: menu;
- F1: orientation;
- F2: RGB order (like STECCY).

## `steccy-gb.ini` (Linux) / `STECCYGB.INI` (STM32)

Syntax is that of `steccy.ini`. Available keys:
- `PATH` and `ROM`: location and ROM to load;
- `PALETTE`: `green`, `gray` or `pocket`;
- `SCALE`: zoom;
- `FRAMESKIP`: maximum number of frames skipped when late;
- `TURBO`: unlimited speed;
- `KEYBOARD`, `ORIENTATION` and `RGB`: STM32 only.

A commented example is in `steccy-gb.ini`.

Important: for USB keyboard support on the STM32 build, you must enable the USB keyboard in the STM32 ini file on the SD card. Add the line:

```
KEYBOARD=USB
```

to `STECCYGB.INI` (on the SD card) to activate USB HID host keyboard handling. Under Linux, add `KEYBOARD=USB` to `steccy-gb.ini` or use `KEYBOARD=PS2` / `KEYBOARD=ZX` as needed.

Also note: on some Black Board revisions the USB host pull-up resistor (commonly referred to as R21 on older V2.0 boards) must be desoldered to allow the STM32 to act as USB host. If your board has an R21 resistor fitted and a USB keyboard is not recognized, desoldering R21 may be required. Verify the resistor location on your board revision before modifying hardware.

## Sound

The Game Boy's 4 channels are emulated (`src/gb/gbapu.c`):
- 2 square channels;
- 1 wave channel;
- 1 noise channel.

Sound is mixed to mono, at 16 kHz on STM32 to limit CPU load. On STM32, it is output on the **DAC, pin PA4**.

| LM386 module | STM32 board |
|--------------|-------------|
| VCC          | 5V          |
| GND          | GND (common ground) |
| IN           | PA4 **via a 10 µF capacitor** (+ side to PA4) |
| output       | 8 Ω speaker  |

Start with the module’s potentiometer almost at minimum. If whining occurs, add 100 to 470 µF between the module’s VCC and GND. `SOUND=no` in the ini disables sound. Under Linux, `gb-headless` disables audio.

## What has been verified

These are **facts**: checks performed on PC with `tools/run-tests.sh` and screenshots.

**Public test ROMs:**

| Public test ROM                | Result |
|--------------------------------|--------|
| Blargg `cpu_instrs` (11 tests) | Passed all tests |
| Blargg `instr_timing`          | Passed |
| Blargg `halt_bug`              | Passed |
| `dmg-acid2` (PPU rendering)    | Pixel-perfect identical to the reference image |
| Blargg `mem_timing`            | **Expected failure**: cycles are counted per instruction, not per memory access |
| Blargg `dmg_sound` (12 tests)  | 8/12. Failures: 05 (sweep detail), 09 and 12 (wave RAM access during read), 10 (rare trigger case) |
| Home test ROM (A 440 Hz tone)  | 439.6 Hz measured in the WAV |

**Linux and STM32 ports:**
- **X11, under Xvfb**: display is correct. A key actually sent to the window (X → A) does change the screen, verified with `keytest.gb`.
- **STM32 ROM cache**: tested on PC with only 2 slots, forcing evictions. `cpu_instrs` still passes.
- **STM32 compilation**: both variants compile without warnings with `arm-none-eabi-gcc` 13.2. They take about 73 KB of flash, about 108 KB of SRAM and about 40 KB of CCM.
- **Speed on PC**: 100 to 140 times real time on a recent x86 PC.



## Known limitations

- Classic Game Boy only: no Game Boy Color, and “CGB only” games will not work.
- PPU mode 3 has a fixed duration: mid-line graphical effects are not reproduced.
- MBC3: the real-time clock is frozen and does not advance.
- Not supported: MMM01, HuC1/HuC3, camera, link cable.
- STM32: cartridge RAM is limited to 32 KB, excluding some MBC5 games.
- No boot ROM, because Nintendo’s is protected. The emulator starts directly in the post-boot state.

## ROMs and rights

Unlike the Sinclair ROMs used by STECCY, no Game Boy ROMs are provided. Commercial games are copyright-protected: only use games you own.

## License

MIT. Code taken from STECCY remains © Frank Meyer (ukw100); see `LICENSE`. Original MIT headers are kept in copied files.
