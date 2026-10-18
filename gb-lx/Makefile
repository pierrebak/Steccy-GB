#-------------------------------------------------------------------------------------------------------------------------------------------
# Makefile - STECCY-GB pour Linux (meme structure que steccy-lx/Makefile de STECCY)
#
#   make xsteccy-gb      fenetre X11            (sudo apt-get install libx11-dev)
#   make steccy-gb       console framebuffer    (Raspberry Pi...)
#   make gb-headless     sans affichage, pour les ROMs de test
#-------------------------------------------------------------------------------------------------------------------------------------------
OPTS        = -O2 -Wall -Wextra -Werror -Wstrict-prototypes
INCDIRS     = -I. -I../src/gb
CORE        = ../src/gb/gb.c ../src/gb/sm83.c ../src/gb/gbmem.c ../src/gb/gbio.c ../src/gb/gbppu.c ../src/gb/gbapu.c
CORE_H      = ../src/gb/gb.h ../src/gb/gbint.h
COMMON      = lxcart.c
COMMON_H    = lxcart.h lxgb.h lxdisplay.h lxmapkey.h scancodes.h

all: xsteccy-gb steccy-gb gb-headless

xsteccy-gb: $(CORE) $(CORE_H) $(COMMON) $(COMMON_H) lxmain.c lxx11.c lxx11.h lxdisplay.c lxmapkey.c
	$(CC) $(OPTS) $(INCDIRS) -DX11 -o $@ $(CORE) $(COMMON) lxmain.c lxx11.c lxdisplay.c lxmapkey.c -lX11

steccy-gb: $(CORE) $(CORE_H) $(COMMON) $(COMMON_H) lxmain.c lxfb.c lxfb.h lxkbd.c lxkbd.h lxdisplay.c lxmapkey.c
	$(CC) $(OPTS) -Wno-unused-result $(INCDIRS) -DFRAMEBUFFER -o $@ $(CORE) $(COMMON) lxmain.c lxfb.c lxkbd.c lxdisplay.c lxmapkey.c -lpthread

gb-headless: $(CORE) $(CORE_H) $(COMMON) lxcart.h lxheadless.c
	$(CC) $(OPTS) $(INCDIRS) -o $@ $(CORE) $(COMMON) lxheadless.c

install: xsteccy-gb steccy-gb
	cp xsteccy-gb steccy-gb /usr/local/bin

clean:
	rm -f xsteccy-gb steccy-gb gb-headless
