/*
 *  psplash - a lightweight framebuffer splashscreen for embedded devices.
 *
 *  A logo read at runtime from a signed FIT bundle in a disk partition.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef _HAVE_PSPLASH_LOGO_H
#define _HAVE_PSPLASH_LOGO_H

#include <stddef.h>

#include "psplash-theme.h"

typedef struct PSplashLogo
{
  int            width, height;
  unsigned char *rgb;		/* width * height * 3, row 0 drawn at the top */
}
PSplashLogo;

/* Fill `logo` from the BMP in `data`. Rows are kept in file order, so the
 * first row stored is drawn at the top whatever the header's height sign
 * says: u-boot on a VOP without y-mirror scans a BMP out the same way, and
 * the artwork is authored for that. Uncompressed 8 bpp (palettised) and
 * 24 bpp, and RLE8, are accepted. Returns 0, or -1 with the reason on
 * stderr. */
int
psplash_logo_decode_bmp (const unsigned char *data,
			 size_t               len,
			 PSplashLogo         *logo);

/* Read the FIT bundle from the partition labelled `partname`, check every
 * hash of its "bmp" image, require a signature over that image made by the
 * RSA public key in the PEM file `key_path`, and decode the BMP.
 *
 * A bundle may also carry a "theme" image, checked the same way, whose text
 * (psplash_theme_parse) is applied to `theme`. A theme that is present but
 * fails any check refuses the whole bundle, logo included: a logo is drawn
 * only with the colours made for it.
 *
 * Returns 0, or -1 with the reason on stderr, leaving `logo` empty and
 * `theme` untouched. */
int
psplash_logo_load (const char   *partname,
		   const char   *key_path,
		   PSplashLogo  *logo,
		   PSplashTheme *theme);

/* Verify and decode a FIT bundle already in memory, as psplash_logo_load
 * does once it has read the partition. */
int
psplash_logo_from_fit (const unsigned char *fit,
		       size_t               len,
		       const char          *key_path,
		       PSplashLogo         *logo,
		       PSplashTheme        *theme);

void
psplash_logo_free (PSplashLogo *logo);

#endif
