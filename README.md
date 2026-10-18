# STECCY-GB — émulateur Game Boy dérivé de STECCY

STECCY-GB émule une Game Boy classique (DMG, 1989). Il est bâti sur l'architecture et les pilotes de [STECCY](https://github.com/ukw100/STECCY), l'émulateur ZX Spectrum de Frank Meyer pour STM32F407 et Linux.

Le cœur Spectrum (Z80, ULA) a été remplacé par un cœur Game Boy. Tout le reste est conservé, et la plupart des pilotes sont utilisés **sans modification** :
- la carte et l'écran TFT ;
- la carte SD et FatFs ;
- les claviers PS/2 et USB ;
- les manettes Wii ;
- les ports Linux X11 et framebuffer.

## Correspondance avec STECCY

| STECCY (ZX Spectrum)            | STECCY-GB (Game Boy)                | Remarque |
|---------------------------------|-------------------------------------|----------|
| `src/z80/z80.c`                 | `src/gb/sm83.c`                     | Nouveau CPU : le SM83 du Game Boy n'est pas compatible Z80 (pas d'IX/IY, d'ED, d'IN/OUT, de flags S et P/V ; instructions propres) |
| `src/zxram/zxram.c`             | `src/gb/gbmem.c`                    | Banques 128K remplacées par les contrôleurs de cartouche MBC1/2/3/5 |
| `src/zxscr/zxscr.c`             | `src/gb/gbppu.c` + plateformes      | Rendu de tuiles et sprites ligne par ligne |
| `src/zxio/zxio.c`               | `src/gb/gbio.c`                     | Joypad, timer, port série, DMA |
| `zx_spectrum()`, `z80_idle_time()` | `gb_run_frame()` + boucle de plateforme | Une image à 59,73 Hz |
| `steccy-lx/lxfb.c`, `lxkbd.c`, `scancodes.h` | `gb-lx/` (copiés)       | `lxkbd.c` : seule la ligne `#include "z80.h"` change |
| `steccy-lx/lxx11.c`             | `gb-lx/lxx11.c`                     | Fenêtre et table de touches reprises ; affichage via XImage |
| `src/main.c`                    | `gb-stm32/main.c`                   | Quelques lignes modifiées, marquées « STECCY-GB » (manette Wii, appel `gb_stm32()`) |
| `menu/menu.c`, `joystick/joystick.c`, `keypress/keypress.c` | `gb-stm32/gbmenu.c`, `gbinput.c` | Réécrits, car ces fichiers sont liés au clavier ZX |
| tous les autres pilotes STM32   | pris directement dans le dépôt STECCY | Aucune copie, aucune modification |

## Organisation

```
src/gb/        coeur portable (aucune dépendance de plateforme)
gb-lx/         Linux : xsteccy-gb (X11), steccy-gb (framebuffer), gb-headless (tests)
gb-stm32/      STM32F407VET6 Black Board : Makefile, main.c, gbstm32.c, gbinput.c, gbmenu.c,
               shim/ (en-têtes de compatibilité pour les pilotes STECCY), script de liens
bin/           fichiers .hex précompilés (non testés sur matériel, voir plus bas)
tools/         run-tests.sh (ROMs de test), cachetest.c (cache ROM sur PC), make-keytest.py
steccy-gb.ini  exemple de fichier de réglages
```

## Linux

```
sudo apt-get install build-essential libx11-dev
cd gb-lx
make                    # xsteccy-gb, steccy-gb, gb-headless
./xsteccy-gb jeu.gb     # fenêtre 640x576 (zoom x4), option -g LxH
./steccy-gb jeu.gb      # console framebuffer 32 bpp (ex. Raspberry Pi), comme steccy de STECCY
```

## STM32F407VET6

Le matériel est le même que STECCY (Black Board + TFT SSD1963 800x480 ou ILI9341 320x240, carte SD, clavier PS/2 ou USB, manette Wii). Le câblage est donc celui décrit dans le README de STECCY.

```
sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi
git clone https://github.com/ukw100/STECCY.git          # les pilotes sont pris ici
cd gb-stm32
make STECCY=../../STECCY                 # -> steccy-gb-ssd1963.hex
make STECCY=../../STECCY TFT=ILI9341     # -> steccy-gb-ili9341.hex
```

**Mise en place :**
1. Flasher le `.hex` comme pour STECCY.
2. Copier sur la carte SD (FAT32, pas exFAT) les ROMs `.GB`, avec des noms au format 8.3.
3. Copier aussi, si besoin, le fichier `bin/STECCYGB.INI`. Sur STM32 le nom doit être **`STECCYGB.INI`** : FatFs est compilé sans noms longs, et `steccy-gb.ini` (9 caractères avant le point) ne peut pas être ouvert.

Sans ligne `ROM=` dans l'ini, un menu de choix s'affiche au démarrage. Si aucune ROM n'est trouvée, l'écran rouge indique l'erreur FatFs ou liste les fichiers vus à la racine. Les parties sont sauvegardées dans un fichier `.SAV` à côté de la ROM, pour les cartouches à pile. L'écriture a lieu toutes les ~10 s si la RAM a changé, et à l'ouverture du menu.

**Affichage :**
- SSD1963 : zoom x3, soit 480x432.
- ILI9341 : zoom x1,5, soit 240x216.
- Seules les lignes modifiées sont renvoyées au TFT, selon le même principe de cache que STECCY.

## Commandes

Les touches sont lues par **position physique**, la même table fonctionne donc en AZERTY, QWERTY et QWERTZ.

| Game Boy  | Clavier                                | Wii Gamepad          | Wii Nunchuk         |
|-----------|----------------------------------------|----------------------|---------------------|
| Croix     | Flèches, pavé 8 4 6 2                  | Croix, stick gauche  | Stick               |
| A         | X, Espace, pavé 0                      | A, X                 | Z                   |
| B         | W (AZERTY) / Z (QWERTY) / Y (QWERTZ)   | B, Y                 | C                   |
| START     | Entrée                                 | +                    | Z + C ensemble      |
| SELECT    | Retour arrière, Shift droit            | −                    | —                   |

**Raccourcis communs :**
- F3 : turbo (comme STECCY) ;
- F5 : reset.

**Linux uniquement :**
- F2 : palette ;
- F12 ou Échap : quitter.

**STM32 uniquement :**
- TAB ou Échap : menu ;
- HOME sur le Gamepad : menu ;
- F1 : orientation ;
- F2 : ordre RGB (comme STECCY).

## Fichier `steccy-gb.ini` (Linux) / `STECCYGB.INI` (STM32)

La syntaxe est celle de `steccy.ini`. Les clés disponibles sont :
- `PATH` et `ROM` : emplacement et ROM à charger ;
- `PALETTE` : `green`, `gray` ou `pocket` ;
- `SCALE` : zoom ;
- `FRAMESKIP` : nombre maximal d'images sautées en cas de retard ;
- `TURBO` : vitesse non limitée ;
- `KEYBOARD`, `ORIENTATION` et `RGB` : STM32 uniquement.

L'exemple commenté est dans `steccy-gb.ini`.

## Son

Les 4 voies du Game Boy sont émulées (`src/gb/gbapu.c`) :
- 2 voies carrées ;
- 1 voie d'onde ;
- 1 voie de bruit.

Le son est mixé en mono, à 16 kHz sur STM32 pour limiter la charge du processeur. Sur STM32, il sort sur le **DAC, broche PA4**.

| Module LM386 | Carte STM32 |
|---|---|
| VCC | 5V |
| GND | GND (masse commune) |
| IN | PA4 **via un condensateur de 10 µF** (+ côté PA4) |
| sortie | haut-parleur 8 Ω |

Commencer avec le potentiomètre du module presque au minimum. En cas de souffle, ajouter 100 à 470 µF entre VCC et GND du module. `SOUND=no` dans l'ini coupe le son. Sous Linux, `gb-headless rom.gb 600 - son.wav` enregistre le son dans un fichier WAV ; les versions X11 et framebuffer restent muettes pour l'instant.

## Ce qui a été vérifié

Ce sont des **faits** : vérifications réalisées sur PC avec `tools/run-tests.sh` et des captures d'écran.

**ROMs de test publiques :**

| ROM de test publique             | Résultat |
|----------------------------------|----------|
| Blargg `cpu_instrs` (11 tests)   | Passed all tests |
| Blargg `instr_timing`            | Passed |
| Blargg `halt_bug`                | Passed |
| `dmg-acid2` (rendu PPU)          | Identique pixel par pixel à l'image de référence |
| Blargg `mem_timing`              | **Échec attendu** : les cycles sont comptés par instruction, pas par accès mémoire |
| Blargg `dmg_sound` (12 tests)    | 8/12. Échecs : 05 (détails du sweep), 09 et 12 (accès à la RAM d'onde pendant la lecture), 10 (cas rare au déclenchement) |
| ROM de test maison (La 440 Hz)   | 439,6 Hz mesurés dans le WAV |

**Ports Linux et STM32 :**
- **X11, sous Xvfb** : l'affichage est correct. Une touche réellement envoyée à la fenêtre (X → A) modifie bien l'écran, vérifié avec `keytest.gb`.
- **Cache ROM du port STM32** : testé sur PC avec seulement 2 emplacements, ce qui force les évictions. `cpu_instrs` passe toujours.
- **Compilation STM32** : les deux variantes compilent sans avertissement avec `arm-none-eabi-gcc` 13.2. Elles occupent environ 73 Ko de flash, environ 108 Ko de SRAM et environ 40 Ko de CCM.
- **Vitesse sur PC** : 100 à 140 fois le temps réel sur un PC x86 récent.

## Ce qui n'a PAS été vérifié

Ce sont des **hypothèses**. Aucun test n'a été fait sur une vraie carte STM32 ni avec de vrais jeux du commerce.

- **Vitesse sur STM32F407 à 168 MHz** : le plein régime est plausible pour le CPU émulé, mais n'est pas mesuré. L'envoi d'image au TFT par FSMC (zoom x3 = environ 207 000 pixels par image) peut coûter cher. Si c'est trop lent, essayer d'abord `SCALE=2`, puis `FRAMESKIP=2`.
- **ROM en flash interne** : comme STECCY, la carte SD n'est plus lue pendant le jeu. Au chargement, une ROM de 384 Ko maximum est copiée dans les secteurs 5 à 7 de la flash du STM32 (quelques secondes, seulement si elle a changé). Les ROMs plus grandes utilisent encore la lecture SD à la demande, ce qui peut provoquer des saccades. Copie en flash non testée sur la carte.
- **Ordre des pixels sur ILI9341 en zoom x1,5** : déduit du code `tft_set_area()` de STECCY (en paysage, Y varie le plus vite), non vérifié sur l'écran.
- **Version framebuffer Linux et clavier USB sur STM32** : compilés seulement.

## Limites connues

- Game Boy classique uniquement : pas de Game Boy Color, et les jeux « CGB only » ne fonctionneront pas.
- Le mode 3 du PPU a une durée fixe : les effets graphiques en milieu de ligne ne sont pas reproduits.
- MBC3 : l'horloge temps réel est figée, elle n'avance pas.
- Non supportés : MMM01, HuC1/HuC3, caméra, câble link.
- STM32 : la RAM de cartouche est limitée à 32 Ko, ce qui exclut certains jeux MBC5.
- Pas de boot ROM, car celle de Nintendo est protégée. L'émulateur démarre directement dans l'état qui suit le boot.

## ROMs et droits

Contrairement aux ROM Sinclair utilisées par STECCY, aucune ROM Game Boy n'est fournie. Les jeux du commerce sont protégés par le droit d'auteur : n'utilisez que des jeux dont vous possédez la cartouche, ou des jeux homebrew libres.

## Licence

MIT. Le code repris de STECCY reste © Frank Meyer (ukw100) ; voir `LICENSE`. Les en-têtes MIT d'origine sont conservés dans les fichiers copiés.
