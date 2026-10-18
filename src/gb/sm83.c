/*-------------------------------------------------------------------------------------------------------------------------------------------
 * sm83.c - emulation du CPU Sharp SM83 (LR35902) du Game Boy
 *
 * Remplace z80/z80.c de STECCY. Le SM83 ressemble au Z80 mais n'est PAS compatible :
 *   - pas de registres IX/IY, pas de jeu alternatif (EXX, EX AF,AF'), pas de prefixes DD/ED/FD
 *   - pas de ports IN/OUT : les E/S sont mappees en memoire (0xFF00-0xFF7F)
 *   - flags Z N H C uniquement (bits 7..4 de F), pas de S ni P/V
 *   - nouvelles instructions : LDH, LD (HL+)/(HL-), SWAP, STOP, ADD SP,e, LD HL,SP+e, RETI(EI)
 *   - DAA, RLCA/RRCA/RLA/RRA et les rotations CB ont des flags differents
 * On garde la philosophie du coeur Z80 de STECCY : decodage par switch, comptage des cycles
 * (ADD_CLOCKCYCLES), gestion des interruptions au debut de chaque instruction.
 *
 * Cycles : comptes en T-cycles (4 par M-cycle), par instruction (pas de timing au niveau
 * de chaque acces memoire).
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include "gbint.h"

#define FLAG_Z                      0x80
#define FLAG_N                      0x40
#define FLAG_H                      0x20
#define FLAG_C                      0x10

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Registres : ordre B C D E H L - A (index 0..7, 6 = (HL)) comme dans le decodage des opcodes
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint8_t              regs[8];                                    // B C D E H L F A : index = codage des opcodes (6 = F)

#define reg_B               regs[0]
#define reg_C               regs[1]
#define reg_D               regs[2]
#define reg_E               regs[3]
#define reg_H               regs[4]
#define reg_L               regs[5]
#define reg_F               regs[6]                                     // l'index 6 designe (HL) dans les opcodes : F y est range
#define reg_A               regs[7]
static uint16_t             reg_SP;
static uint16_t             reg_PC;
static uint8_t              ime;                                        // interrupt master enable
static uint8_t              ime_pending;                                // EI : IME actif apres l'instruction suivante
static uint8_t              halted;
static uint8_t              halt_bug;
static uint8_t              stopped;

#define BC                  ((uint16_t) ((reg_B << 8) | reg_C))
#define DE                  ((uint16_t) ((reg_D << 8) | reg_E))
#define HL                  ((uint16_t) ((reg_H << 8) | reg_L))
#define SET_BC(v)           do { uint16_t _v = (v); reg_B = (uint8_t) (_v >> 8); reg_C = (uint8_t) _v; } while (0)
#define SET_DE(v)           do { uint16_t _v = (v); reg_D = (uint8_t) (_v >> 8); reg_E = (uint8_t) _v; } while (0)
#define SET_HL(v)           do { uint16_t _v = (v); reg_H = (uint8_t) (_v >> 8); reg_L = (uint8_t) _v; } while (0)

#define RD(a)               gb_mem_read ((uint16_t) (a))
#define WR(a,v)             gb_mem_write ((uint16_t) (a), (uint8_t) (v))

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Cycles de base (en M-cycles) des opcodes non prefixes, branche NON prise.
 * Branche prise : JR +1, JP cc +1, CALL cc +3, RET cc +3.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static const uint8_t        op_mcycles[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    1, 3, 2, 2, 1, 1, 2, 1, 5, 2, 2, 2, 1, 1, 2, 1,     // 0x00
    1, 3, 2, 2, 1, 1, 2, 1, 3, 2, 2, 2, 1, 1, 2, 1,     // 0x10
    2, 3, 2, 2, 1, 1, 2, 1, 2, 2, 2, 2, 1, 1, 2, 1,     // 0x20
    2, 3, 2, 2, 3, 3, 3, 1, 2, 2, 2, 2, 1, 1, 2, 1,     // 0x30
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,     // 0x40
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,     // 0x50
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,     // 0x60
    2, 2, 2, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 2, 1,     // 0x70
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,     // 0x80
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,     // 0x90
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,     // 0xA0
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,     // 0xB0
    2, 3, 3, 4, 3, 4, 2, 4, 2, 4, 3, 0, 3, 6, 2, 4,     // 0xC0
    2, 3, 3, 1, 3, 4, 2, 4, 2, 4, 3, 1, 3, 1, 2, 4,     // 0xD0
    3, 3, 2, 1, 1, 4, 2, 4, 4, 1, 4, 1, 1, 1, 2, 4,     // 0xE0
    3, 3, 2, 1, 1, 4, 2, 4, 3, 2, 4, 1, 1, 1, 2, 4      // 0xF0
};

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Acces aux registres 8 bits par index (6 = (HL))
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
GB_INLINE uint8_t
get_r (uint8_t idx)                                                         // acces direct au tableau (plus de switch)
{
    return (idx == 6) ? RD (HL) : regs[idx];
}

GB_INLINE void
set_r (uint8_t idx, uint8_t v)
{
    if (idx == 6)
    {
        WR (HL, v);
    }
    else
    {
        regs[idx] = v;
    }
}

GB_INLINE uint8_t
fetch8 (void)
{
    return RD (reg_PC++);
}

GB_INLINE uint16_t
fetch16 (void)
{
    uint16_t lo = fetch8 ();
    uint16_t hi = fetch8 ();
    return (uint16_t) (lo | (hi << 8));
}

GB_INLINE void
push16 (uint16_t v)
{
    reg_SP--;
    WR (reg_SP, v >> 8);
    reg_SP--;
    WR (reg_SP, v & 0xFF);
}

GB_INLINE uint16_t
pop16 (void)
{
    uint16_t lo = RD (reg_SP++);
    uint16_t hi = RD (reg_SP++);
    return (uint16_t) (lo | (hi << 8));
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * ALU 8 bits (op = bits 3..5 de l'opcode : ADD ADC SUB SBC AND XOR OR CP)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
GB_INLINE void
alu (uint8_t op, uint8_t v)
{
    unsigned int    res;
    unsigned int    carry;

    switch (op)
    {
        case 0:                                                                     // ADD
        case 1:                                                                     // ADC
            carry = (op == 1 && (reg_F & FLAG_C)) ? 1 : 0;
            res   = reg_A + v + carry;
            reg_F = (uint8_t) ((((res & 0xFF) == 0) ? FLAG_Z : 0) |
                               (((reg_A & 0x0F) + (v & 0x0F) + carry) > 0x0F ? FLAG_H : 0) |
                               (res > 0xFF ? FLAG_C : 0));
            reg_A = (uint8_t) res;
            break;
        case 2:                                                                     // SUB
        case 3:                                                                     // SBC
        case 7:                                                                     // CP
            carry = (op == 3 && (reg_F & FLAG_C)) ? 1 : 0;
            res   = (unsigned int) reg_A - v - carry;
            reg_F = (uint8_t) ((((res & 0xFF) == 0) ? FLAG_Z : 0) | FLAG_N |
                               (((int) (reg_A & 0x0F) - (int) (v & 0x0F) - (int) carry) < 0 ? FLAG_H : 0) |
                               (res > 0xFF ? FLAG_C : 0));
            if (op != 7)
            {
                reg_A = (uint8_t) res;
            }
            break;
        case 4:                                                                     // AND
            reg_A &= v;
            reg_F = (uint8_t) ((reg_A ? 0 : FLAG_Z) | FLAG_H);
            break;
        case 5:                                                                     // XOR
            reg_A ^= v;
            reg_F = reg_A ? 0 : FLAG_Z;
            break;
        default:                                                                    // OR
            reg_A |= v;
            reg_F = reg_A ? 0 : FLAG_Z;
            break;
    }
}

GB_INLINE uint8_t
inc8 (uint8_t v)
{
    uint8_t r = (uint8_t) (v + 1);
    reg_F = (uint8_t) ((reg_F & FLAG_C) | (r ? 0 : FLAG_Z) | ((v & 0x0F) == 0x0F ? FLAG_H : 0));
    return r;
}

GB_INLINE uint8_t
dec8 (uint8_t v)
{
    uint8_t r = (uint8_t) (v - 1);
    reg_F = (uint8_t) ((reg_F & FLAG_C) | FLAG_N | (r ? 0 : FLAG_Z) | ((v & 0x0F) == 0x00 ? FLAG_H : 0));
    return r;
}

GB_INLINE void
add_hl (uint16_t v)
{
    uint32_t r = (uint32_t) HL + v;
    reg_F = (uint8_t) ((reg_F & FLAG_Z) | (((HL & 0x0FFF) + (v & 0x0FFF)) > 0x0FFF ? FLAG_H : 0) | (r > 0xFFFF ? FLAG_C : 0));
    SET_HL (r);
}

GB_INLINE uint16_t
sp_plus_e8 (void)                                                                   // ADD SP,e / LD HL,SP+e
{
    int8_t      e  = (int8_t) fetch8 ();
    uint16_t    ue = (uint16_t) (uint8_t) e;
    reg_F = (uint8_t) ((((reg_SP & 0x0F) + (ue & 0x0F)) > 0x0F ? FLAG_H : 0) | (((reg_SP & 0xFF) + (ue & 0xFF)) > 0xFF ? FLAG_C : 0));
    return (uint16_t) (reg_SP + e);
}

GB_INLINE void
daa (void)
{
    uint8_t a = reg_A;
    uint8_t c = reg_F & FLAG_C;

    if (! (reg_F & FLAG_N))
    {
        if (c || a > 0x99)
        {
            a = (uint8_t) (a + 0x60);
            c = FLAG_C;
        }
        if ((reg_F & FLAG_H) || (a & 0x0F) > 0x09)
        {
            a = (uint8_t) (a + 0x06);
        }
    }
    else
    {
        if (c)
        {
            a = (uint8_t) (a - 0x60);
        }
        if (reg_F & FLAG_H)
        {
            a = (uint8_t) (a - 0x06);
        }
    }
    reg_A = a;
    reg_F = (uint8_t) ((reg_F & FLAG_N) | (a ? 0 : FLAG_Z) | c);
}

GB_INLINE uint_fast8_t
cond (uint8_t cc)                                                                   // NZ Z NC C
{
    switch (cc & 3)
    {
        case 0:  return ! (reg_F & FLAG_Z);
        case 1:  return (reg_F & FLAG_Z) != 0;
        case 2:  return ! (reg_F & FLAG_C);
        default: return (reg_F & FLAG_C) != 0;
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Prefixe CB : rotations, decalages, SWAP, BIT, RES, SET
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint32_t
exec_cb (void)
{
    uint8_t     op  = fetch8 ();
    uint8_t     idx = op & 7;
    uint8_t     bit = (op >> 3) & 7;
    uint8_t     v   = get_r (idx);
    uint8_t     r;
    uint8_t     c;

    switch (op >> 6)
    {
        case 0:                                                                     // rotations / decalages
            switch (bit)
            {
                case 0:  c = v >> 7;  r = (uint8_t) ((v << 1) | c); break;                              // RLC
                case 1:  c = v & 1;   r = (uint8_t) ((v >> 1) | (c << 7)); break;                       // RRC
                case 2:  c = v >> 7;  r = (uint8_t) ((v << 1) | ((reg_F & FLAG_C) ? 1 : 0)); break;     // RL
                case 3:  c = v & 1;   r = (uint8_t) ((v >> 1) | ((reg_F & FLAG_C) ? 0x80 : 0)); break;  // RR
                case 4:  c = v >> 7;  r = (uint8_t) (v << 1); break;                                    // SLA
                case 5:  c = v & 1;   r = (uint8_t) ((v >> 1) | (v & 0x80)); break;                     // SRA
                case 6:  c = 0;       r = (uint8_t) ((v >> 4) | (v << 4)); break;                       // SWAP
                default: c = v & 1;   r = (uint8_t) (v >> 1); break;                                    // SRL
            }
            reg_F = (uint8_t) ((r ? 0 : FLAG_Z) | (c ? FLAG_C : 0));
            set_r (idx, r);
            break;
        case 1:                                                                     // BIT b,r
            reg_F = (uint8_t) ((reg_F & FLAG_C) | FLAG_H | ((v & (1 << bit)) ? 0 : FLAG_Z));
            return (idx == 6) ? 12 : 8;
        case 2:                                                                     // RES b,r
            set_r (idx, (uint8_t) (v & ~(1 << bit)));
            break;
        default:                                                                    // SET b,r
            set_r (idx, (uint8_t) (v | (1 << bit)));
            break;
    }
    return (idx == 6) ? 16 : 8;
}

uint16_t
gb_debug_pc (void)
{
    return reg_PC;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * sm83_reset () - etat des registres apres la boot ROM DMG (la boot ROM Nintendo n'est pas fournie)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
sm83_reset (void)
{
    reg_A = 0x01; reg_F = 0xB0;
    reg_B = 0x00; reg_C = 0x13;
    reg_D = 0x00; reg_E = 0xD8;
    reg_H = 0x01; reg_L = 0x4D;
    reg_SP = 0xFFFE;
    reg_PC = 0x0100;
    ime = 0;
    ime_pending = 0;
    halted = 0;
    halt_bug = 0;
    stopped = 0;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * service_interrupt () - equivalent du bloc "if (iff1 && z80_interrupt)" de la boucle z80() de STECCY
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
GB_INLINE uint32_t
service_interrupt (void)
{
    uint8_t pending = gb_io[IO_IF] & gb_ie & 0x1F;

    if (pending)
    {
        halted  = 0;
        stopped = 0;

        if (ime)
        {
            uint8_t n;

            for (n = 0; n < 5; n++)
            {
                if (pending & (1 << n))
                {
                    break;
                }
            }
            ime = 0;
            gb_io[IO_IF] &= (uint8_t) ~(1 << n);
            push16 (reg_PC);
            reg_PC = (uint16_t) (0x40 + 8 * n);
            return 20;
        }
    }
    return 0;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * sm83_step () - execute une instruction (ou une interruption, ou 4 cycles en HALT)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint32_t
sm83_step (void)
{
    uint8_t     op;
    uint32_t    cycles;
    uint32_t    extra = 0;

    cycles = service_interrupt ();

    if (cycles)
    {
        return cycles;
    }

    if (halted || stopped)
    {
        return 4;
    }

    if (ime_pending)                                                                // EI prend effet apres l'instruction suivante
    {
        ime_pending = 0;
        ime = 1;
    }

    op = RD (reg_PC);

    if (halt_bug)                                                                   // bug HALT : PC n'est pas incremente
    {
        halt_bug = 0;
    }
    else
    {
        reg_PC++;
    }

    /*---------------------------------------------------------------------------------------------------------------------------------------
     * Blocs reguliers : LD r,r' (0x40-0x7F) et ALU A,r (0x80-0xBF)
     *---------------------------------------------------------------------------------------------------------------------------------------
     */
    if (op >= 0x40 && op < 0x80)
    {
        if (op == 0x76)                                                             // HALT
        {
            if (! ime && (gb_io[IO_IF] & gb_ie & 0x1F))
            {
                halt_bug = 1;
            }
            else
            {
                halted = 1;
            }
        }
        else
        {
            set_r ((op >> 3) & 7, get_r (op & 7));
        }
        return op_mcycles[op] * 4;
    }

    if (op >= 0x80 && op < 0xC0)
    {
        alu ((op >> 3) & 7, get_r (op & 7));
        return op_mcycles[op] * 4;
    }

    switch (op)
    {
        case 0x00: break;                                                                   // NOP
        case 0x01: { uint16_t v = fetch16 (); SET_BC (v); } break;                          // LD BC,nn
        case 0x11: { uint16_t v = fetch16 (); SET_DE (v); } break;                          // LD DE,nn
        case 0x21: { uint16_t v = fetch16 (); SET_HL (v); } break;                          // LD HL,nn
        case 0x31: reg_SP = fetch16 (); break;                                              // LD SP,nn

        case 0x02: WR (BC, reg_A); break;                                                   // LD (BC),A
        case 0x12: WR (DE, reg_A); break;                                                   // LD (DE),A
        case 0x22: WR (HL, reg_A); SET_HL (HL + 1); break;                                  // LD (HL+),A
        case 0x32: WR (HL, reg_A); SET_HL (HL - 1); break;                                  // LD (HL-),A
        case 0x0A: reg_A = RD (BC); break;                                                  // LD A,(BC)
        case 0x1A: reg_A = RD (DE); break;                                                  // LD A,(DE)
        case 0x2A: reg_A = RD (HL); SET_HL (HL + 1); break;                                 // LD A,(HL+)
        case 0x3A: reg_A = RD (HL); SET_HL (HL - 1); break;                                 // LD A,(HL-)

        case 0x03: SET_BC (BC + 1); break;                                                  // INC rr
        case 0x13: SET_DE (DE + 1); break;
        case 0x23: SET_HL (HL + 1); break;
        case 0x33: reg_SP++; break;
        case 0x0B: SET_BC (BC - 1); break;                                                  // DEC rr
        case 0x1B: SET_DE (DE - 1); break;
        case 0x2B: SET_HL (HL - 1); break;
        case 0x3B: reg_SP--; break;

        case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C:   // INC r
            set_r ((op >> 3) & 7, inc8 (get_r ((op >> 3) & 7)));
            break;
        case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D:   // DEC r
            set_r ((op >> 3) & 7, dec8 (get_r ((op >> 3) & 7)));
            break;
        case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E:   // LD r,n
            set_r ((op >> 3) & 7, fetch8 ());
            break;

        case 0x07: { uint8_t c = reg_A >> 7; reg_A = (uint8_t) ((reg_A << 1) | c); reg_F = c ? FLAG_C : 0; } break;           // RLCA
        case 0x0F: { uint8_t c = reg_A & 1;  reg_A = (uint8_t) ((reg_A >> 1) | (c << 7)); reg_F = c ? FLAG_C : 0; } break;    // RRCA
        case 0x17: { uint8_t c = reg_A >> 7; reg_A = (uint8_t) ((reg_A << 1) | ((reg_F & FLAG_C) ? 1 : 0)); reg_F = c ? FLAG_C : 0; } break;     // RLA
        case 0x1F: { uint8_t c = reg_A & 1;  reg_A = (uint8_t) ((reg_A >> 1) | ((reg_F & FLAG_C) ? 0x80 : 0)); reg_F = c ? FLAG_C : 0; } break;  // RRA

        case 0x08: { uint16_t a = fetch16 (); WR (a, reg_SP & 0xFF); WR (a + 1, reg_SP >> 8); } break;  // LD (nn),SP
        case 0x09: add_hl (BC); break;                                                      // ADD HL,rr
        case 0x19: add_hl (DE); break;
        case 0x29: add_hl (HL); break;
        case 0x39: add_hl (reg_SP); break;

        case 0x10:                                                                          // STOP
            (void) fetch8 ();
            if (! (gb_io[IO_IF] & gb_ie & 0x1F))
            {
                stopped = 1;
            }
            gb_io_write (IO_DIV, 0);
            break;

        case 0x18: { int8_t e = (int8_t) fetch8 (); reg_PC = (uint16_t) (reg_PC + e); } break;   // JR e
        case 0x20: case 0x28: case 0x30: case 0x38:                                         // JR cc,e
        {
            int8_t e = (int8_t) fetch8 ();
            if (cond ((op >> 3) & 3))
            {
                reg_PC = (uint16_t) (reg_PC + e);
                extra = 4;
            }
            break;
        }

        case 0x27: daa (); break;                                                           // DAA
        case 0x2F: reg_A = (uint8_t) ~reg_A; reg_F |= FLAG_N | FLAG_H; break;               // CPL
        case 0x37: reg_F = (uint8_t) ((reg_F & FLAG_Z) | FLAG_C); break;                    // SCF
        case 0x3F: reg_F = (uint8_t) ((reg_F & FLAG_Z) | ((reg_F & FLAG_C) ? 0 : FLAG_C)); break;  // CCF

        case 0xC0: case 0xC8: case 0xD0: case 0xD8:                                         // RET cc
            if (cond ((op >> 3) & 3))
            {
                reg_PC = pop16 ();
                extra = 12;
            }
            break;
        case 0xC9: reg_PC = pop16 (); break;                                                // RET
        case 0xD9: reg_PC = pop16 (); ime = 1; break;                                       // RETI

        case 0xC1: SET_BC (pop16 ()); break;                                                // POP rr
        case 0xD1: SET_DE (pop16 ()); break;
        case 0xE1: SET_HL (pop16 ()); break;
        case 0xF1: { uint16_t v = pop16 (); reg_A = (uint8_t) (v >> 8); reg_F = (uint8_t) (v & 0xF0); } break;
        case 0xC5: push16 (BC); break;                                                      // PUSH rr
        case 0xD5: push16 (DE); break;
        case 0xE5: push16 (HL); break;
        case 0xF5: push16 ((uint16_t) ((reg_A << 8) | reg_F)); break;

        case 0xC2: case 0xCA: case 0xD2: case 0xDA:                                         // JP cc,nn
        {
            uint16_t a = fetch16 ();
            if (cond ((op >> 3) & 3))
            {
                reg_PC = a;
                extra = 4;
            }
            break;
        }
        case 0xC3: reg_PC = fetch16 (); break;                                              // JP nn
        case 0xE9: reg_PC = HL; break;                                                      // JP (HL)

        case 0xC4: case 0xCC: case 0xD4: case 0xDC:                                         // CALL cc,nn
        {
            uint16_t a = fetch16 ();
            if (cond ((op >> 3) & 3))
            {
                push16 (reg_PC);
                reg_PC = a;
                extra = 12;
            }
            break;
        }
        case 0xCD: { uint16_t a = fetch16 (); push16 (reg_PC); reg_PC = a; } break;         // CALL nn

        case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:   // ALU A,n
            alu ((op >> 3) & 7, fetch8 ());
            break;

        case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:   // RST
            push16 (reg_PC);
            reg_PC = op & 0x38;
            break;

        case 0xCB: return exec_cb ();                                                       // prefixe CB

        case 0xE0: WR (0xFF00 + fetch8 (), reg_A); break;                                   // LDH (n),A
        case 0xF0: reg_A = RD (0xFF00 + fetch8 ()); break;                                  // LDH A,(n)
        case 0xE2: WR (0xFF00 + reg_C, reg_A); break;                                       // LD (C),A
        case 0xF2: reg_A = RD (0xFF00 + reg_C); break;                                      // LD A,(C)
        case 0xEA: WR (fetch16 (), reg_A); break;                                           // LD (nn),A
        case 0xFA: reg_A = RD (fetch16 ()); break;                                          // LD A,(nn)

        case 0xE8: reg_SP = sp_plus_e8 (); break;                                           // ADD SP,e
        case 0xF8: { uint16_t v = sp_plus_e8 (); SET_HL (v); } break;                       // LD HL,SP+e
        case 0xF9: reg_SP = HL; break;                                                      // LD SP,HL

        case 0xF3: ime = 0; ime_pending = 0; break;                                         // DI
        case 0xFB: ime_pending = 1; break;                                                  // EI

        default:                                                                            // opcodes inexistants : le vrai CPU se bloque
            halted = 1;
            gb_ie  = 0;
            break;
    }

    return op_mcycles[op] * 4 + extra;
}
