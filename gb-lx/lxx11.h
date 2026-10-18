/*-------------------------------------------------------------------------------------------------------------------------------------------
 * lxx11.h - routines X11 de STECCY-GB
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifndef LXX11_H
#define LXX11_H

#include <stdint.h>

extern void                 x11_event (void);
extern void                 x11_draw_frame (const uint8_t * frame, const uint32_t * pal, unsigned int zoom, unsigned int left, unsigned int top);
extern int                  x11_init (char *);
extern void                 x11_deinit (void);

#endif
