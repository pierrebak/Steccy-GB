/*-------------------------------------------------------------------------------------------------------------------------------------------
 * gbaudio.h - sortie son STM32 (DAC PA4 + TIM6)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifndef GBAUDIO_H
#define GBAUDIO_H

#include <stdint.h>

extern uint32_t             gbaudio_init (void);           // retourne la frequence d'echantillonnage reelle (Hz)
extern void                 gbaudio_regulate (void);       // une fois par image : regulation de la vitesse du son

#endif
