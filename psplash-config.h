/*
 *  pslash - a lightweight framebuffer splashscreen for embedded devices.
 *
 *  Copyright (c) 2014 MenloSystems GmbH
 *  Author: Olaf Mandel <o.mandel@menlosystems.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef _HAVE_PSPLASH_CONFIG_H
#define _HAVE_PSPLASH_CONFIG_H

/* Text to output on program start; if undefined, output nothing */
#ifndef PSPLASH_DISABLE_STARTUP_MSG
#define PSPLASH_STARTUP_MSG ""
#endif

/* Bool indicating if the image is fullscreen, as opposed to split screen */
#ifndef PSPLASH_IMG_FULLSCREEN
#define PSPLASH_IMG_FULLSCREEN 0
#endif

/* Bool indicated if the progress bar should be disabled */
#ifndef PSPLASH_DISABLE_PROGRESS_BAR
#define PSPLASH_SHOW_PROGRESS_BAR 1
#endif

/* Position of the image split from top edge, numerator of fraction */
#define PSPLASH_IMG_SPLIT_NUMERATOR 5

/* Position of the image split from top edge, denominator of fraction */
#define PSPLASH_IMG_SPLIT_DENOMINATOR 6

/* Top edge of the FreeType MSG text area, as a fraction of the screen height
 * from the top edge. If undefined, the bottom edge of the logo image is used;
 * define both to place it relative to the artwork instead, e.g. when a
 * fullscreen image has its own wordmark partway down. */
/* #define PSPLASH_MSG_TOP_NUMERATOR 281 */
/* #define PSPLASH_MSG_TOP_DENOMINATOR 480 */

/* Pixel size of the FreeType MSG font */
#ifndef PSPLASH_MSG_FONT_SIZE
#define PSPLASH_MSG_FONT_SIZE 34
#endif

/* Widest line of FreeType MSG text, in percent of the screen width */
#ifndef PSPLASH_MSG_MAX_WIDTH_PERCENT
#define PSPLASH_MSG_MAX_WIDTH_PERCENT 90
#endif

#endif
