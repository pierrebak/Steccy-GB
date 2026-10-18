#!/usr/bin/env python3
# make-keytest.py - genere keytest.gb : ecran sombre tant que le bouton A est enfonce, clair sinon.
# ROM minimale ecrite a la main (22 octets de code), utile pour verifier le clavier / la manette.
r = bytearray(0x8000)
r[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])                    # NOP ; JP 0x0150
r[0x134:0x13C] = b'KEYTEST\0'
code = [0x3E, 0x10,         # 0150 LD A,0x10      selection des boutons A/B/Select/Start
        0xE0, 0x00,         # 0152 LDH (P1),A
        0xF0, 0x00,         # 0154 LDH A,(P1)
        0xE6, 0x01,         # 0156 AND 0x01       bit 0 = A (0 = enfonce)
        0x28, 0x06,         # 0158 JR Z,0x0160
        0x3E, 0x00,         # 015A LD A,0x00      palette claire
        0xE0, 0x47,         # 015C LDH (BGP),A
        0x18, 0xF0,         # 015E JR 0x0150
        0x3E, 0xFF,         # 0160 LD A,0xFF      palette sombre
        0xE0, 0x47,         # 0162 LDH (BGP),A
        0x18, 0xEA]         # 0164 JR 0x0150
r[0x150:0x150 + len(code)] = bytes(code)
open('keytest.gb', 'wb').write(r)
print('keytest.gb ecrit')
