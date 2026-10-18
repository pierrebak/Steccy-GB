/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbapu.c - son du Game Boy (DMG) : 2 voies carrees, 1 voie d'onde, 1 voie de bruit
 *
 * Remplace le haut-parleur 1 bit du Spectrum (speaker.c de STECCY). Le resultat est un flux mono 16 bits
 * signe, a la frequence choisie par la plateforme (gb_apu_set_rate), livre echantillon par echantillon
 * a gbplat_audio_sample().
 *
 * Fonctionnement emule :
 *   - sequenceur a 512 Hz (toutes les 8192 T-cycles) : longueur (256 Hz), sweep (128 Hz), enveloppe (64 Hz)
 *   - voies 1/2 : rapports cycliques 12,5 / 25 / 50 / 75 %, enveloppe, longueur ; voie 1 : sweep de frequence
 *   - voie 3 : 32 echantillons 4 bits (0xFF30-0xFF3F), volume 0/100/50/25 %
 *   - voie 4 : generateur pseudo-aleatoire 15 ou 7 bits
 *   - NR50 (volume general), NR51 (panoramique, melange en mono), NR52 (marche/arret)
 *   - filtre passe-haut comme le condensateur de sortie de la vraie console (supprime la composante continue)
 * Simplifications : echantillonnage ponctuel (pas de filtrage anti-repliement), pas de bizarreries
 * "zombie mode" ni des acces a la RAM d'onde pendant la lecture.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <string.h>
#include "gbint.h"

#define NR10    0x10
#define NR11    0x11
#define NR12    0x12
#define NR13    0x13
#define NR14    0x14
#define NR21    0x16
#define NR22    0x17
#define NR23    0x18
#define NR24    0x19
#define NR30    0x1A
#define NR31    0x1B
#define NR32    0x1C
#define NR33    0x1D
#define NR34    0x1E
#define NR41    0x20
#define NR42    0x21
#define NR43    0x22
#define NR44    0x23
#define NR50    0x24
#define NR51    0x25
#define NR52    0x26

typedef struct
{
    uint8_t     enabled;                                    // voie active (bit de NR52)
    uint8_t     dac;                                        // DAC de la voie alimente
    uint16_t    length;                                     // compteur de longueur
    uint8_t     length_enable;
    int32_t     timer;                                      // T-cycles avant le pas suivant
    uint16_t    freq;                                       // 11 bits
    uint8_t     pos;                                        // position dans la forme d'onde
    uint8_t     volume;                                     // enveloppe (0..15)
    uint8_t     env_period;
    uint8_t     env_up;
    uint8_t     env_timer;
    uint16_t    lfsr;                                       // voie 4
} CHANNEL;

static CHANNEL              ch[4];
static uint8_t              sweep_enabled;
static uint8_t              sweep_timer;
static uint16_t             sweep_shadow;
static uint32_t             seq_cycles;                     // sequenceur 512 Hz
static uint8_t              seq_step;
static uint32_t             sample_rate = 32768;
static uint32_t             sample_acc;
static int32_t              hp_cap;                         // filtre passe-haut (x 32768, calcul 32 bits)
static int32_t              hp_factor = 32670;              // x 32768 : 0,99700 a 32768 Hz (recalcule par gb_apu_set_rate)

static const uint8_t        duty_table[4] = { 0x01, 0x81, 0x87, 0x7E };     // 12,5 % 25 % 50 % 75 % (bit = pas)
static const uint8_t        noise_div[8]  = { 8, 16, 32, 48, 64, 80, 96, 112 };

#define REG(r)              gb_io[r]

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_apu_set_rate () - frequence d'echantillonnage de la plateforme (ex. 32000 Hz sur STM32)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_apu_set_rate (uint32_t rate)
{
    uint32_t i;
    double   f = 1.0;

    if (rate < 8000)
    {
        rate = 8000;
    }
    sample_rate = rate;

    for (i = 0; i < GB_CLOCK_HZ / rate; i++)                // facteur du condensateur par echantillon : 0,999958^(cycles/ech.)
    {
        f *= 0.999958;
    }
    hp_factor = (int32_t) (f * 32768.0);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Periodes des minuteries
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static int32_t
square_period (const CHANNEL * c)
{
    return (int32_t) (2048 - c->freq) * 4;
}

static int32_t
wave_period (const CHANNEL * c)
{
    return (int32_t) (2048 - c->freq) * 2;
}

static int32_t
noise_period (void)
{
    uint8_t nr43 = REG (NR43);
    return (int32_t) noise_div[nr43 & 7] << (nr43 >> 4);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Sweep (voie 1)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint16_t
sweep_calc (void)
{
    uint8_t  nr10  = REG (NR10);
    uint16_t delta = (uint16_t) (sweep_shadow >> (nr10 & 7));
    uint16_t f     = (nr10 & 0x08) ? (uint16_t) (sweep_shadow - delta) : (uint16_t) (sweep_shadow + delta);

    if (f > 2047)
    {
        ch[0].enabled = 0;                                  // debordement : la voie se coupe
    }
    return f;
}

static void
sweep_clock (void)
{
    uint8_t nr10   = REG (NR10);
    uint8_t period = (nr10 >> 4) & 7;

    if (sweep_timer > 0)
    {
        sweep_timer--;
    }
    if (sweep_timer == 0)
    {
        sweep_timer = period ? period : 8;

        if (sweep_enabled && period)
        {
            uint16_t f = sweep_calc ();

            if (f <= 2047 && (nr10 & 7))
            {
                sweep_shadow = f;
                ch[0].freq   = f;
                (void) sweep_calc ();                       // 2e calcul : seulement le test de debordement
            }
        }
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * Enveloppe et longueur
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
envelope_clock (CHANNEL * c)
{
    if (c->env_period == 0)
    {
        return;
    }
    if (c->env_timer > 0)
    {
        c->env_timer--;
    }
    if (c->env_timer == 0)
    {
        c->env_timer = c->env_period;

        if (c->env_up && c->volume < 15)
        {
            c->volume++;
        }
        else if (! c->env_up && c->volume > 0)
        {
            c->volume--;
        }
    }
}

static void
length_clock (CHANNEL * c)
{
    if (c->length_enable && c->length > 0)
    {
        c->length--;

        if (c->length == 0)
        {
            c->enabled = 0;
        }
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * trigger () - ecriture du bit 7 de NRx4
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
trigger (uint8_t n)
{
    CHANNEL *   c       = &ch[n];
    uint8_t     env_reg = (n == 0) ? REG (NR12) : (n == 1) ? REG (NR22) : REG (NR42);

    c->enabled = c->dac;

    if (c->length == 0)
    {
        c->length = (n == 2) ? 256 : 64;
    }

    switch (n)
    {
        case 0:
        case 1:
            c->timer = square_period (c);
            break;
        case 2:
            c->timer = wave_period (c) + 6;
            c->pos   = 0;
            break;
        default:
            c->timer = noise_period ();
            c->lfsr  = 0x7FFF;
            break;
    }

    if (n != 2)
    {
        c->volume     = env_reg >> 4;
        c->env_up     = (env_reg & 0x08) ? 1 : 0;
        c->env_period = env_reg & 7;
        c->env_timer  = c->env_period ? c->env_period : 8;
    }

    if (n == 0)
    {
        uint8_t nr10 = REG (NR10);

        sweep_shadow  = c->freq;
        sweep_timer   = ((nr10 >> 4) & 7) ? ((nr10 >> 4) & 7) : 8;
        sweep_enabled = ((nr10 & 0x70) || (nr10 & 7)) ? 1 : 0;

        if (nr10 & 7)
        {
            (void) sweep_calc ();
        }
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_apu_write () - ecriture d'un registre son (0xFF10-0xFF3F), appelee par gbio.c
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */

void
gb_apu_write (uint8_t reg, uint8_t value)
{
    gb_apu_flush ();
    if (reg == NR52)
    {
        if (! (value & 0x80))                               // extinction : tous les registres son a zero
        {
            uint8_t r;

            uint16_t len[4];
            uint8_t  n;

            for (r = NR10; r < NR52; r++)
            {
                gb_io[r] = 0;
            }
            for (n = 0; n < 4; n++)                         // DMG : les compteurs de longueur survivent a l'extinction
            {
                len[n] = ch[n].length;
            }
            memset (ch, 0, sizeof (ch));

            for (n = 0; n < 4; n++)
            {
                ch[n].length = len[n];
            }
            sweep_enabled = 0;
        }
        else if (! (gb_io[NR52] & 0x80))                    // allumage : le sequenceur repart a 0
        {
            seq_step = 0;
        }
        gb_io[NR52] = value & 0x80;
        return;
    }

    if (reg >= 0x30 && reg <= 0x3F)                         // RAM d'onde, toujours accessible
    {
        gb_io[reg] = value;
        return;
    }

    if (! (gb_io[NR52] & 0x80))                             // son eteint : registres en lecture seule (DMG : longueurs exceptees)
    {
        if (reg == NR11)        { ch[0].length = (uint16_t) (64 - (value & 0x3F)); }
        else if (reg == NR21)   { ch[1].length = (uint16_t) (64 - (value & 0x3F)); }
        else if (reg == NR31)   { ch[2].length = (uint16_t) (256 - value); }
        else if (reg == NR41)   { ch[3].length = (uint16_t) (64 - (value & 0x3F)); }
        return;
    }

    gb_io[reg] = value;

    switch (reg)
    {
        case NR11: ch[0].length = (uint16_t) (64 - (value & 0x3F)); break;
        case NR21: ch[1].length = (uint16_t) (64 - (value & 0x3F)); break;
        case NR31: ch[2].length = (uint16_t) (256 - value);         break;
        case NR41: ch[3].length = (uint16_t) (64 - (value & 0x3F)); break;

        case NR12: case NR22: case NR42:                    // DAC alimente si les 5 bits hauts ne sont pas nuls
        {
            uint8_t n = (reg == NR12) ? 0 : (reg == NR22) ? 1 : 3;

            ch[n].dac = (value & 0xF8) ? 1 : 0;

            if (! ch[n].dac)
            {
                ch[n].enabled = 0;
            }
            break;
        }
        case NR30:
            ch[2].dac = (value & 0x80) ? 1 : 0;

            if (! ch[2].dac)
            {
                ch[2].enabled = 0;
            }
            break;

        case NR13: ch[0].freq = (uint16_t) ((ch[0].freq & 0x700) | value); break;
        case NR23: ch[1].freq = (uint16_t) ((ch[1].freq & 0x700) | value); break;
        case NR33: ch[2].freq = (uint16_t) ((ch[2].freq & 0x700) | value); break;

        case NR14: case NR24: case NR34: case NR44:
        {
            uint8_t n = (reg == NR14) ? 0 : (reg == NR24) ? 1 : (reg == NR34) ? 2 : 3;

            uint8_t was_enabled = ch[n].length_enable;
            uint8_t no_len_next = (seq_step & 1);           // le prochain pas du sequenceur ne compte pas la longueur

            if (n < 3)
            {
                ch[n].freq = (uint16_t) ((ch[n].freq & 0xFF) | ((value & 7) << 8));
            }
            ch[n].length_enable = (value & 0x40) ? 1 : 0;

            if (no_len_next && ! was_enabled && ch[n].length_enable && ch[n].length > 0)    // decompte supplementaire
            {
                ch[n].length--;

                if (ch[n].length == 0 && ! (value & 0x80))
                {
                    ch[n].enabled = 0;
                }
            }

            if (value & 0x80)
            {
                uint8_t was_zero = (ch[n].length == 0);

                trigger (n);

                if (was_zero && no_len_next && ch[n].length_enable)
                {
                    ch[n].length--;                         // 63 (255) au lieu de 64 (256)
                }
            }
            break;
        }
        default:
            break;
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_apu_read_nr52 () - bits d'etat des voies
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint8_t
gb_apu_read_nr52 (void)
{
    gb_apu_flush ();
    return (uint8_t) ((gb_io[NR52] & 0x80) | 0x70 |
                      (ch[0].enabled ? 1 : 0) | (ch[1].enabled ? 2 : 0) | (ch[2].enabled ? 4 : 0) | (ch[3].enabled ? 8 : 0));
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * channel_output () - valeur numerique 0..15 de la voie n
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint8_t
channel_output (uint8_t n)
{
    CHANNEL * c = &ch[n];

    if (! c->enabled || ! c->dac)
    {
        return 0;
    }

    switch (n)
    {
        case 0:
        case 1:
        {
            uint8_t duty = (n == 0 ? REG (NR11) : REG (NR21)) >> 6;
            return ((duty_table[duty] >> c->pos) & 1) ? c->volume : 0;
        }
        case 2:
        {
            uint8_t shift = (REG (NR32) >> 5) & 3;
            uint8_t b     = REG (0x30 + (c->pos >> 1));
            uint8_t s     = (c->pos & 1) ? (b & 0x0F) : (b >> 4);

            return shift ? (uint8_t) (s >> (shift - 1)) : 0;
        }
        default:
            return (c->lfsr & 1) ? 0 : c->volume;
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * mix () - un echantillon mono 16 bits signe
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static int16_t
mix (void)
{
    uint8_t     nr51  = REG (NR51);
    uint8_t     nr50  = REG (NR50);
    int32_t     left  = 0;
    int32_t     right = 0;
    int32_t     in;
    int32_t     out;
    uint8_t     n;

    for (n = 0; n < 4; n++)
    {
        int32_t v = channel_output (n);

        if (nr51 & (0x10 << n))
        {
            left += v;
        }
        if (nr51 & (0x01 << n))
        {
            right += v;
        }
    }

    left  *= ((nr50 >> 4) & 7) + 1;
    right *= (nr50 & 7) + 1;
    in     = (left + right) * 32;                           // max 2 x 4 x 15 x 8 x 32 = 30720

    out    = in - (hp_cap >> 15);                           // passe-haut : out = in - cap ; cap = in - out * facteur
    hp_cap = (in << 15) - out * hp_factor;                  // |in| <= 30720 : tient dans 32 bits

    if (out > 32767)
    {
        out = 32767;
    }
    if (out < -32768)
    {
        out = -32768;
    }
    return (int16_t) out;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_apu_tick () - avance le son de 'cycles' T-cycles (appele apres chaque instruction)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint32_t                    gb_apu_pending;                 // cycles pas encore traites (calcul par paquets, voir gbint.h)

static void
apu_run (uint32_t cycles)
{
    uint8_t n;

    if (! (gb_io[NR52] & 0x80))
    {
        sample_acc += cycles * sample_rate;                 // son eteint : on livre quand meme du silence

        while (sample_acc >= GB_CLOCK_HZ)
        {
            sample_acc -= GB_CLOCK_HZ;
            gbplat_audio_sample (mix ());
        }
        return;
    }

    /*-------------------------------------------------------------------------------------------------------------------------------------
     * Sequenceur 512 Hz
     *-------------------------------------------------------------------------------------------------------------------------------------
     */
    seq_cycles += cycles;

    while (seq_cycles >= 8192)
    {
        seq_cycles -= 8192;

        if ((seq_step & 1) == 0)                            // pas 0, 2, 4, 6 : longueur
        {
            for (n = 0; n < 4; n++)
            {
                length_clock (&ch[n]);
            }
        }
        if (seq_step == 2 || seq_step == 6)
        {
            sweep_clock ();
        }
        if (seq_step == 7)
        {
            envelope_clock (&ch[0]);
            envelope_clock (&ch[1]);
            envelope_clock (&ch[3]);
        }
        seq_step = (uint8_t) ((seq_step + 1) & 7);
    }

    /*-------------------------------------------------------------------------------------------------------------------------------------
     * Minuteries des voies
     *-------------------------------------------------------------------------------------------------------------------------------------
     */
    for (n = 0; n < 3; n++)                                 // voies 1, 2 (carrees) et 3 (onde) : calcul direct du nombre de pas
    {
        CHANNEL * c = &ch[n];

        c->timer -= (int32_t) cycles;

        if (c->timer <= 0)
        {
            int32_t  period = (n < 2) ? square_period (c) : wave_period (c);
            uint32_t steps  = 1u + (uint32_t) (-c->timer) / (uint32_t) period;

            c->timer += (int32_t) (steps * (uint32_t) period);
            c->pos    = (uint8_t) ((c->pos + steps) & ((n < 2) ? 7 : 31));
        }
    }

    ch[3].timer -= (int32_t) cycles;

    while (ch[3].timer <= 0)
    {
        uint16_t bit;

        ch[3].timer += noise_period ();
        bit          = (uint16_t) ((ch[3].lfsr ^ (ch[3].lfsr >> 1)) & 1);
        ch[3].lfsr   = (uint16_t) ((ch[3].lfsr >> 1) | (bit << 14));

        if (REG (NR43) & 0x08)                              // mode 7 bits
        {
            ch[3].lfsr = (uint16_t) ((ch[3].lfsr & ~0x40) | (bit << 6));
        }
    }

    /*-------------------------------------------------------------------------------------------------------------------------------------
     * Echantillons a la frequence de la plateforme (compteur fractionnaire exact)
     *-------------------------------------------------------------------------------------------------------------------------------------
     */
    sample_acc += cycles * sample_rate;

    while (sample_acc >= GB_CLOCK_HZ)
    {
        sample_acc -= GB_CLOCK_HZ;
        gbplat_audio_sample (mix ());
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_apu_flush () - traite les cycles en attente. gb_apu_advance () (gbint.h) l'appelle par paquets de 256 cycles au moins
 * (environ un echantillon a 16 kHz), ce qui divise le cout par ~40 sans difference audible. Avant toute lecture ou
 * ecriture de registre son, les cycles en attente sont traites (gb_apu_flush) pour garder l'ordre exact.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_apu_flush (void)
{
    if (gb_apu_pending)
    {
        apu_run (gb_apu_pending);
        gb_apu_pending = 0;
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gb_apu_reset ()
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gb_apu_reset (void)
{
    memset (ch, 0, sizeof (ch));
    sweep_enabled = 0;
    sweep_timer   = 0;
    sweep_shadow  = 0;
    seq_cycles    = 0;
    seq_step      = 0;
    sample_acc    = 0;
    hp_cap        = 0;
    gb_apu_pending = 0;
    ch[0].dac     = (gb_io[NR12] & 0xF8) ? 1 : 0;           // etat laisse par la boot ROM (gb_io_reset)
    ch[0].enabled = ch[0].dac;
}
