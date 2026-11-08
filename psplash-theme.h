/*
 *  psplash - a lightweight framebuffer splashscreen for embedded devices.
 *
 *  The colours psplash draws in, and where its message text sits.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef _HAVE_PSPLASH_THEME_H
#define _HAVE_PSPLASH_THEME_H

#include "psplash.h"

typedef struct PSplashTheme
{
  uint8 background[3];
  uint8 text[3];
  uint8 bar[3];
  uint8 bar_background[3];
  uint8 bar_frame[3];
  /* Top of the message area as a fraction of the screen height; with a zero
   * denominator the area starts below the logo. */
  int   text_top_num, text_top_den;
}
PSplashTheme;

/* What psplash draws with: psplash-colors.h and PSPLASH_MSG_TOP_* until
 * something replaces it, before the first frame. */
extern PSplashTheme psplash_theme;

/* A colour's three components, as psplash_fb_draw_rect and friends take them. */
#define PSPLASH_RGB(c) (c)[0], (c)[1], (c)[2]

#endif
