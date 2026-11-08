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

#include <stddef.h>
#include <string.h>

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

static int
hex_digit (char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/* "#rrggbb", exactly. */
static int
parse_colour (const char *v, size_t len, uint8 rgb[3])
{
  int i;

  if (len != 7 || v[0] != '#')
    return -1;
  for (i = 0; i < 3; i++)
    {
      int hi = hex_digit (v[1 + 2 * i]), lo = hex_digit (v[2 + 2 * i]);

      if (hi < 0 || lo < 0)
	return -1;
      rgb[i] = (uint8) (hi * 16 + lo);
    }
  return 0;
}

/* One to four decimal digits; returns the number of characters used. */
static size_t
parse_number (const char *v, size_t len, int *n)
{
  size_t i = 0;

  *n = 0;
  while (i < len && i < 4 && v[i] >= '0' && v[i] <= '9')
    *n = *n * 10 + (v[i++] - '0');
  return i;
}

/* "N/D" with 0 <= N < D. */
static int
parse_fraction (const char *v, size_t len, int *num, int *den)
{
  size_t a = parse_number (v, len, num), b;

  if (!a || a >= len || v[a] != '/')
    return -1;
  b = parse_number (v + a + 1, len - a - 1, den);
  if (!b || a + 1 + b != len || *den < 1 || *num >= *den)
    return -1;
  return 0;
}

int
psplash_theme_parse (const char   *data,
		     size_t        len,
		     PSplashTheme *theme,
		     const char  **why)
{
  static const struct
  {
    const char *key;
    size_t      offset;
  }
  colours[] = {
    { "background",     offsetof (PSplashTheme, background) },
    { "text",           offsetof (PSplashTheme, text) },
    { "bar",            offsetof (PSplashTheme, bar) },
    { "bar-background", offsetof (PSplashTheme, bar_background) },
    { "bar-frame",      offsetof (PSplashTheme, bar_frame) },
  };
  enum { N_COLOURS = sizeof (colours) / sizeof (colours[0]) };
  PSplashTheme t = *theme;
  int          seen[N_COLOURS + 1] = { 0 };
  size_t       i, start;

  if (len > PSPLASH_THEME_MAX)
    {
      *why = "too long";
      return -1;
    }
  for (i = 0; i < len; i++)
    if (data[i] != '\n' && (data[i] < 0x20 || data[i] > 0x7e))
      {
	*why = "not printable text";
	return -1;
      }

  for (start = 0; start < len; )
    {
      const char *line = data + start, *eol, *eq;
      size_t      line_len, key_len, value_len, k;

      eol = memchr (line, '\n', len - start);
      line_len = eol ? (size_t) (eol - line) : len - start;
      start += line_len + 1;

      if (!line_len || line[0] == '#')
	continue;
      eq = memchr (line, '=', line_len);
      if (!eq)
	{
	  *why = "a line is not key=value";
	  return -1;
	}
      key_len = eq - line;
      value_len = line_len - key_len - 1;
      if (!key_len)
	{
	  *why = "a line has no key";
	  return -1;
	}
      for (k = 0; k < key_len; k++)
	if (!((line[k] >= 'a' && line[k] <= 'z')
	      || (line[k] >= '0' && line[k] <= '9') || line[k] == '-'))
	  {
	    *why = "a key is not lower-case letters, digits and '-'";
	    return -1;
	  }

      for (k = 0; k < N_COLOURS; k++)
	if (strlen (colours[k].key) == key_len
	    && !memcmp (colours[k].key, line, key_len))
	  break;
      if (k < N_COLOURS)
	{
	  if (seen[k]++)
	    {
	      *why = "a colour is set twice";
	      return -1;
	    }
	  if (parse_colour (eq + 1, value_len,
			    (uint8 *) &t + colours[k].offset))
	    {
	      *why = "a colour is not #rrggbb";
	      return -1;
	    }
	}
      else if (key_len == 8 && !memcmp (line, "text-top", 8))
	{
	  if (seen[N_COLOURS]++)
	    {
	      *why = "text-top is set twice";
	      return -1;
	    }
	  if (parse_fraction (eq + 1, value_len,
			      &t.text_top_num, &t.text_top_den))
	    {
	      *why = "text-top is not N/D below 1";
	      return -1;
	    }
	}
    }

  *theme = t;
  return 0;
}
