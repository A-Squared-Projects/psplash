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

/* The most a theme's text may hold. */
#define PSPLASH_THEME_MAX 4096

/* Apply the theme text in `data` to `theme`. The text is lines of key=value;
 * a blank line or one starting with '#' is skipped. A key is lower-case
 * letters, digits and '-', and one psplash does not know is ignored, so a
 * newer theme still suits an older psplash. Keys:
 *
 *   background, text, bar, bar-background, bar-frame   #rrggbb
 *   text-top                                           N/D, 0 <= N < D
 *
 * A key left out keeps its value in `theme`. Anything else - a byte that is
 * not printable ASCII or a newline, a line with no '=', a malformed key, a
 * known key given twice or with a value of the wrong form - and `theme` is
 * left untouched.
 * Returns 0, or -1 with `*why` saying what was wrong. */
int
psplash_theme_parse (const char   *data,
		     size_t        len,
		     PSplashTheme *theme,
		     const char  **why);

/* A colour's three components, as psplash_fb_draw_rect and friends take them. */
#define PSPLASH_RGB(c) (c)[0], (c)[1], (c)[2]

#endif
