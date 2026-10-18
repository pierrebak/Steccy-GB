#-------------------------------------------------------------------------------------------------------------------------------------------
# Makefile - STECCY-GB pour STM32F407VET6 "Black Board" (remplace le projet EmBitz steccy.ebp)
#
# -nostartfiles : on utilise le _start de startup_stm32f4xx.S de STECCY, pas le crt0 de newlib.
#
# Les pilotes (TFT, carte SD, FatFs, PS/2, USB, Wii, SPL, CMSIS, startup) sont pris DIRECTEMENT dans une copie
# du depot STECCY, sans modification :
#     git clone https://github.com/ukw100/STECCY.git ../../STECCY      (ou make STECCY=/chemin/vers/STECCY)
#
#   make                 => steccy-gb-ssd1963.hex   (TFT 800x480 SSD1963)
#   make TFT=ILI9341     => steccy-gb-ili9341.hex   (TFT 3.2" 320x240 ILI9341)
#   make TFT=ILI9341 LTO=1 => steccy-gb-ili9341-lto.hex (optimisation a l'edition de liens : plus rapide)
#-------------------------------------------------------------------------------------------------------------------------------------------
STECCY      ?= ../../STECCY
TFT         ?= SSD1963
CROSS       ?= arm-none-eabi-
CC          = $(CROSS)gcc
OBJCOPY     = $(CROSS)objcopy
SIZE        = $(CROSS)size

TARGET      = steccy-gb-$(shell echo $(TFT) | tr A-Z a-z)$(if $(LTO),-lto)
BUILD       = build-$(TFT)$(if $(LTO),-lto)
LTOFLAGS    = $(if $(LTO),-flto)

S           = $(STECCY)/src
DRIVERS     = $(S)/board-led/board-led.c $(S)/console/console.c $(S)/delay/delay.c \
              $(S)/fatfs/diskio.c $(S)/fatfs/ff.c $(S)/fatfs/ffsystem.c $(S)/fatfs/ffunicode.c \
              $(S)/font/font.c $(S)/fs-stdio/fs-stdio.c $(S)/i2c1-dma/i2c1-dma.c $(S)/ps2key/ps2key.c \
              $(S)/sdcard/stm32_sdcard.c $(S)/speaker/speaker.c $(S)/tft/tft.c $(S)/wii-gamepad/wii-gamepad.c \
              $(S)/usb_hid_host/usb_hid_host.c $(wildcard $(S)/usb_hid_host_lolevel/*.c) \
              $(S)/system_stm32f4xx.c $(wildcard $(STECCY)/SPL/src/*.c)
ifeq ($(TFT),SSD1963)
DRIVERS    += $(S)/ssd1963/ssd1963.c
else
DRIVERS    += $(S)/ili9341/ili9341.c
endif
STARTUP     = $(S)/startup_stm32f4xx.S

CORE        = ../src/gb/gb.c ../src/gb/sm83.c ../src/gb/gbmem.c ../src/gb/gbio.c ../src/gb/gbppu.c ../src/gb/gbapu.c
PORT        = main.c gbstm32.c gbinput.c gbmenu.c gbaudio.c syscalls.c

# shim/ en premier : remplace z80.h, keypress.h, zxkbd.h de STECCY pour les pilotes ps2key.c et usb_hid_host.c
INCDIRS     = -Ishim -I. -I../src/gb -I$(STECCY)/inc -I$(STECCY)/cmsis -I$(STECCY)/SPL/inc \
              -I$(S) -I$(S)/board-led -I$(S)/console -I$(S)/delay -I$(S)/fatfs -I$(S)/font -I$(S)/fs-stdio \
              -I$(S)/i2c1-dma -I$(S)/io -I$(S)/ps2key -I$(S)/sdcard -I$(S)/speaker -I$(S)/tft -I$(S)/uart \
              -I$(S)/wii-gamepad -I$(S)/usb_hid_host -I$(S)/usb_hid_host_lolevel -I$(S)/ssd1963 -I$(S)/ili9341

MCU         = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
DEFS        = -DARM_MATH_CM4 -D__FPU_USED -DSTM32F407VE -DSTM32F407 -DSTM32F4XX -DUSE_STDPERIPH_DRIVER \
              -DHSE_VALUE=8000000 -D$(TFT)
CFLAGS      = $(MCU) $(DEFS) $(INCDIRS) -O2 -fno-strict-aliasing -ffunction-sections -fdata-sections -g
WARN_GB     = -Wall -Wextra -Werror -Wstrict-prototypes
LDFLAGS     = $(MCU) -nostartfiles -T stm32f407ve_flash.ld -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map --specs=nano.specs \
              -Wl,--no-warn-rwx-segments

GB_OBJ      = $(addprefix $(BUILD)/gb/,$(notdir $(CORE:.c=.o))) $(addprefix $(BUILD)/port/,$(PORT:.c=.o))
DRV_OBJ     = $(addprefix $(BUILD)/drv/,$(notdir $(DRIVERS:.c=.o))) $(BUILD)/drv/startup_stm32f4xx.o

vpath %.c $(sort $(dir $(DRIVERS)))

all: $(TARGET).hex

$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@
	$(SIZE) $<

$(BUILD)/$(TARGET).elf: $(GB_OBJ) $(DRV_OBJ) stm32f407ve_flash.ld
	$(CC) $(LDFLAGS) $(if $(LTO),-O2 -flto) -o $@ $(GB_OBJ) $(DRV_OBJ) -lm

$(BUILD)/gb/%.o: ../src/gb/%.c ../src/gb/gb.h ../src/gb/gbint.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARN_GB) $(LTOFLAGS) -c -o $@ $<

$(BUILD)/port/%.o: %.c gbstm32.h gbaudio.h ../src/gb/gb.h ../src/gb/gbint.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -Wextra $(LTOFLAGS) -c -o $@ $<

$(BUILD)/drv/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c -o $@ $<

$(BUILD)/drv/startup_stm32f4xx.o: $(STARTUP)
	@mkdir -p $(dir $@)
	$(CC) $(MCU) -c -o $@ $<

clean:
	rm -rf build-SSD1963* build-ILI9341* steccy-gb-*.hex
