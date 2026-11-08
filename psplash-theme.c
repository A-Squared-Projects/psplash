/*
 *  psplash - a lightweight framebuffer splashscreen for embedded devices.
 *
 *  The colours psplash draws in, and where its message text sits.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "psplash-theme.h"
#include "psplash-colors.h"
#include "psplash-config.h"

PSplashTheme psplash_theme = {
  .background     = { PSPLASH_BACKGROUND_COLOR },
  .text           = { PSPLASH_TEXT_COLOR },
  .bar            = { PSPLASH_BAR_COLOR },
  .bar_background = { PSPLASH_BAR_BACKGROUND_COLOR },
  .bar_frame      = { PSPLASH_BAR_FRAME_COLOR },
#if defined(PSPLASH_MSG_TOP_NUMERATOR) && defined(PSPLASH_MSG_TOP_DENOMINATOR)
  .text_top_num   = PSPLASH_MSG_TOP_NUMERATOR,
  .text_top_den   = PSPLASH_MSG_TOP_DENOMINATOR,
#endif
};
