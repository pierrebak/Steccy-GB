/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbaudio.c - sortie son de STECCY-GB sur le DAC du STM32F407 (PA4 = DAC canal 1)
 *
 * Le coeur produit des echantillons a 16 kHz (gbplat_audio_sample) qui vont dans un tampon circulaire.
 * TIM6 declenche une interruption 16000 fois par seconde et copie l'echantillon suivant dans le DAC.
 * TIM6 et TIM2 (horloge "uptime" de main.c) ont la meme source d'horloge : le son et l'emulation restent
 * synchronises sans derive. Une reserve de 64 ms (PREFILL) est constituee avant de jouer : une image en
 * retard (beaucoup de lignes a envoyer au TFT) ne vide plus le tampon, l'emulateur rattrape ensuite.
 *
 * Branchement : PA4 -> condensateur 10 uF -> entree d'un module ampli (LM386...) -> haut-parleur.
 * Au demarrage, un bip de 0,3 s a 1 kHz verifie toute la chaine, independamment du jeu.
 * PA4 est libre sur la Black Board (STECCY ne l'utilise que pour un module ESP8266 optionnel).
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <stdint.h>
#include "stm32f4xx.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_tim.h"
#include "stm32f4xx_dac.h"
#include "misc.h"
#include "gb.h"
#include "gbaudio.h"

#define AUDIO_RATE              16000                       // Hz (16 kHz : moitie moins de calcul qu'a 32 kHz)
#define RING_SIZE               2048                        // 128 ms de son a 16 kHz (puissance de 2)
#define RING_MASK               (RING_SIZE - 1)
#define PREFILL                 1024                        // reserve de 64 ms : absorbe les images en retard

static volatile uint16_t        ring[RING_SIZE];
static volatile uint32_t        ring_wr;
static volatile uint32_t        ring_rd;
static uint8_t                  audio_on;
static volatile uint32_t        beep_samples;                   // bip de test au demarrage (1 kHz)
static volatile uint8_t         buffering = 1;                  // 1 = on attend d'avoir PREFILL echantillons
static uint32_t                 base_period;                    // periode TIM6 nominale (16 kHz exacts)
static int32_t                  fill_avg = PREFILL * 16;        // niveau moyen du tampon (x 16)

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbplat_audio_sample () - appele par le coeur pour chaque echantillon
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gbplat_audio_sample (int16_t sample)
{
    int32_t     v;
    uint32_t    next;

    if (! audio_on)
    {
        return;
    }

    v = 2048 + (sample >> 3);                               // 16 bits signe -> 12 bits, centre a 2048

    if (v < 0)
    {
        v = 0;
    }
    else if (v > 4095)
    {
        v = 4095;
    }

    next = (ring_wr + 1) & RING_MASK;

    if (next != ring_rd)                                    // tampon plein (turbo) : echantillon abandonne
    {
        ring[ring_wr] = (uint16_t) v;
        ring_wr       = next;
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * TIM6_DAC_IRQHandler () - 32000 fois par seconde
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
extern void TIM6_DAC_IRQHandler (void);

void
TIM6_DAC_IRQHandler (void)
{
    if (TIM6->SR & TIM_SR_UIF)
    {
        TIM6->SR = (uint16_t) ~TIM_SR_UIF;

        if (beep_samples)                                   // bip de test : carre 1 kHz pendant 0,3 s
        {
            beep_samples--;
            DAC->DHR12R1 = ((beep_samples / (AUDIO_RATE / 2000)) & 1) ? 2048 + 600 : 2048 - 600;
            ring_rd = ring_wr;                              // le son du jeu attend la fin du bip
        }
        else
        {
            uint32_t fill = (ring_wr - ring_rd) & RING_MASK;

            if (buffering)                                  // constitution de la reserve
            {
                if (fill >= PREFILL)
                {
                    buffering = 0;
                }
            }
            else if (fill == 0)                             // reserve epuisee : on la reconstitue (silence bref)
            {
                buffering = 1;
            }
            else
            {
                DAC->DHR12R1 = ring[ring_rd];
                ring_rd      = (ring_rd + 1) & RING_MASK;
            }
        }
    }
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbaudio_regulate () - appele une fois par image : ajuste tres legerement la vitesse de sortie (3 % max)
 * pour garder la reserve autour de PREFILL. Si l'emulation tourne un peu sous les 59,73 images/s, le son
 * ralentit d'autant au lieu de se couper (comme les emulateurs de PC avec "dynamic rate control").
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
gbaudio_regulate (void)
{
    int32_t fill;
    int32_t err;
    int32_t adj;
    int32_t max_adj;

    if (! audio_on || beep_samples || buffering)
    {
        return;
    }

    fill      = (int32_t) ((ring_wr - ring_rd) & RING_MASK);
    fill_avg += fill - fill_avg / 16;                       // moyenne glissante (~16 images)
    err       = fill_avg / 16 - PREFILL;                    // > 0 : trop plein -> sortie plus rapide
    max_adj   = (int32_t) base_period * 3 / 100;
    adj       = (int32_t) ((int64_t) base_period * err * 3 / 100 / PREFILL);

    if (adj > max_adj)
    {
        adj = max_adj;
    }
    else if (adj < -max_adj)
    {
        adj = -max_adj;
    }
    TIM6->ARR = (uint32_t) ((int32_t) base_period - adj) - 1;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbaudio_init () - DAC canal 1 sur PA4 + TIM6 ; retourne la frequence reelle
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint32_t
gbaudio_init (void)
{
    GPIO_InitTypeDef        gpio;
    DAC_InitTypeDef         dac;
    TIM_TimeBaseInitTypeDef tim;
    NVIC_InitTypeDef        nvic;
    RCC_ClocksTypeDef       clocks;
    uint32_t                timclk;
    uint32_t                period;

    RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd (RCC_APB1Periph_DAC | RCC_APB1Periph_TIM6, ENABLE);

    GPIO_StructInit (&gpio);
    gpio.GPIO_Pin   = GPIO_Pin_4;
    gpio.GPIO_Mode  = GPIO_Mode_AN;
    gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init (GPIOA, &gpio);

    DAC_StructInit (&dac);
    dac.DAC_Trigger         = DAC_Trigger_None;
    dac.DAC_OutputBuffer    = DAC_OutputBuffer_Enable;
    DAC_Init (DAC_Channel_1, &dac);
    DAC_Cmd (DAC_Channel_1, ENABLE);
    DAC_SetChannel1Data (DAC_Align_12b_R, 2048);            // milieu : silence

    RCC_GetClocksFreq (&clocks);                            // horloge des timers APB1 (x2 si le prediviseur APB1 > 1)
    timclk = clocks.PCLK1_Frequency;

    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1)
    {
        timclk *= 2;
    }
    period      = timclk / AUDIO_RATE;
    base_period = period;

    TIM_TimeBaseStructInit (&tim);
    tim.TIM_Prescaler       = 0;
    tim.TIM_Period          = period - 1;
    tim.TIM_CounterMode     = TIM_CounterMode_Up;
    TIM_TimeBaseInit (TIM6, &tim);
    TIM_ITConfig (TIM6, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel                    = TIM6_DAC_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority  = 0x0F;
    nvic.NVIC_IRQChannelSubPriority         = 0x0F;
    nvic.NVIC_IRQChannelCmd                 = ENABLE;
    NVIC_Init (&nvic);

    ring_rd      = ring_wr = 0;
    audio_on     = 1;
    beep_samples = AUDIO_RATE * 3 / 10;                     // bip de 0,3 s : teste DAC + ampli + haut-parleur
    TIM_Cmd (TIM6, ENABLE);

    return timclk / period;
}
