/*
 *  psplash - a lightweight framebuffer splashscreen for embedded devices.
 *
 *  A logo read at runtime from a signed FIT bundle in a disk partition.
 *
 *  The bundle is a FIT whose image /images/bmp carries the BMP as embedded
 *  data, with at least one sha256 hash node and a signature node made the
 *  way u-boot's mkimage signs an image: RSA PKCS#1 v1.5 over sha256 of the
 *  image data. The same bundle is what u-boot verifies against its
 *  key-logo, so the boot loader and the splash show the same logo or both
 *  fall back to their own. An optional /images/theme, hashed and signed
 *  the same way, carries the colours drawn with the logo; u-boot draws only
 *  the BMP and ignores it.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "psplash-logo.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libfdt.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

/* A bundle never needs more: the partition is far smaller. The cap bounds
 * what is read from a partition whose header lies about its size. */
#define LOGO_FIT_MAX_SIZE   (1024 * 1024)
#define LOGO_MAX_DIMENSION  4096

static int
logo_fail (const char *what)
{
  fprintf (stderr, "psplash: logo bundle ignored: %s\n", what);
  return -1;
}

static uint32_t
le32 (const unsigned char *p)
{
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

static uint16_t
le16 (const unsigned char *p)
{
  return p[0] | (p[1] << 8);
}

void
psplash_logo_free (PSplashLogo *logo)
{
  free (logo->rgb);
  logo->rgb = NULL;
  logo->width = logo->height = 0;
}

static void
put_index (PSplashLogo *logo, const unsigned char *palette, int x, int y,
	   unsigned idx)
{
  unsigned char *d = logo->rgb + ((size_t) y * logo->width + x) * 3;

  /* Palette entries are stored blue, green, red, reserved. */
  d[0] = palette[idx * 4 + 2];
  d[1] = palette[idx * 4 + 1];
  d[2] = palette[idx * 4 + 0];
}

static int
decode_rle8 (const unsigned char *p, const unsigned char *end,
	     const unsigned char *palette, unsigned colours, PSplashLogo *logo)
{
  int x = 0, y = 0;

  while (p + 2 <= end)
    {
      unsigned count = p[0], value = p[1];

      p += 2;
      if (count)
	{
	  if (value >= colours)
	    return logo_fail ("RLE8 colour index outside the palette");
	  while (count--)
	    {
	      if (x >= logo->width || y >= logo->height)
		return logo_fail ("RLE8 run past the image");
	      put_index (logo, palette, x++, y, value);
	    }
	  continue;
	}

      switch (value)
	{
	case 0:			/* end of line */
	  x = 0;
	  y++;
	  break;
	case 1:			/* end of bitmap */
	  return 0;
	case 2:			/* delta */
	  if (p + 2 > end)
	    return logo_fail ("RLE8 delta truncated");
	  x += p[0];
	  y += p[1];
	  p += 2;
	  break;
	default:		/* absolute run of `value` indices, word aligned */
	  if (p + value + (value & 1) > end)
	    return logo_fail ("RLE8 absolute run truncated");
	  for (count = 0; count < value; count++)
	    {
	      if (p[count] >= colours)
		return logo_fail ("RLE8 colour index outside the palette");
	      if (x >= logo->width || y >= logo->height)
		return logo_fail ("RLE8 run past the image");
	      put_index (logo, palette, x++, y, p[count]);
	    }
	  p += value + (value & 1);
	  break;
	}
    }

  /* A bitmap may end at the data's end without an end-of-bitmap marker. */
  return 0;
}

int
psplash_logo_decode_bmp (const unsigned char *data,
			 size_t               len,
			 PSplashLogo         *logo)
{
  uint32_t offset, dib, compression, colours = 0;
  int32_t  width, height;
  unsigned bpp, x, y;
  size_t   stride;
  const unsigned char *palette = NULL;

  memset (logo, 0, sizeof (*logo));

  if (len < 54 || data[0] != 'B' || data[1] != 'M')
    return logo_fail ("not a BMP");

  offset      = le32 (data + 10);
  dib         = le32 (data + 14);
  width       = (int32_t) le32 (data + 18);
  height      = (int32_t) le32 (data + 22);
  bpp         = le16 (data + 28);
  compression = le32 (data + 30);

  if (dib < 40 || 14 + (size_t) dib > len || le16 (data + 26) != 1)
    return logo_fail ("unsupported BMP header");
  if (height < 0)
    height = -height;
  if (width <= 0 || height <= 0
      || width > LOGO_MAX_DIMENSION || height > LOGO_MAX_DIMENSION)
    return logo_fail ("BMP dimensions out of range");
  if (!((bpp == 24 && compression == 0)
	|| (bpp == 8 && (compression == 0 || compression == 1))))
    return logo_fail ("BMP format is not 24 bpp, 8 bpp or RLE8");
  if (offset >= len)
    return logo_fail ("BMP pixel data outside the file");

  if (bpp == 8)
    {
      colours = le32 (data + 46);
      if (colours == 0 || colours > 256)
	colours = 256;
      if (14 + (size_t) dib + 4 * (size_t) colours > offset)
	return logo_fail ("BMP palette overlaps the pixel data");
      palette = data + 14 + dib;
    }

  logo->width  = width;
  logo->height = height;
  /* Pixels an RLE8 bitmap skips are black, as u-boot leaves them. */
  logo->rgb    = calloc ((size_t) width * height, 3);
  if (!logo->rgb)
    return logo_fail ("out of memory");

  if (compression == 1)
    {
      if (decode_rle8 (data + offset, data + len, palette, colours, logo))
	{
	  psplash_logo_free (logo);
	  return -1;
	}
      return 0;
    }

  stride = (((size_t) width * bpp + 31) / 32) * 4;
  if (offset + stride * height > len)
    {
      psplash_logo_free (logo);
      return logo_fail ("BMP pixel data truncated");
    }

  for (y = 0; y < (unsigned) height; y++)
    {
      const unsigned char *row = data + offset + stride * y;
      unsigned char       *d   = logo->rgb + (size_t) y * width * 3;

      for (x = 0; x < (unsigned) width; x++, d += 3)
	if (bpp == 24)
	  {
	    d[0] = row[x * 3 + 2];
	    d[1] = row[x * 3 + 1];
	    d[2] = row[x * 3 + 0];
	  }
	else if (row[x] >= colours)
	  {
	    psplash_logo_free (logo);
	    return logo_fail ("BMP colour index outside the palette");
	  }
	else
	  put_index (logo, palette, x, y, row[x]);
    }

  return 0;
}

static int
logo_fail_in (const char *image, const char *what)
{
  fprintf (stderr, "psplash: logo bundle ignored: %s: %s\n", image, what);
  return -1;
}

static int
check_hash (const void *fit, int node, const void *data, int len,
	    const char *image)
{
  const char    *algo = fdt_getprop (fit, node, "algo", NULL);
  const void    *value;
  int            value_len;
  unsigned char  md[EVP_MAX_MD_SIZE];
  unsigned int   md_len;

  value = fdt_getprop (fit, node, "value", &value_len);
  if (!algo || strcmp (algo, "sha256") || !value)
    return logo_fail_in (image, "a hash is not sha256");
  if (!EVP_Digest (data, len, md, &md_len, EVP_sha256 (), NULL)
      || (int) md_len != value_len || memcmp (md, value, md_len))
    return logo_fail_in (image, "hash mismatch");
  return 0;
}

static int
check_signature (const void *fit, int node, const void *data, int len,
		 EVP_PKEY *key)
{
  const char *algo    = fdt_getprop (fit, node, "algo", NULL);
  const char *padding = fdt_getprop (fit, node, "padding", NULL);
  const void *value;
  int         value_len, ok = 0;
  EVP_MD_CTX *ctx;

  value = fdt_getprop (fit, node, "value", &value_len);
  if (!algo || strncmp (algo, "sha256,rsa", 10) || !value
      || (padding && strcmp (padding, "pkcs-1.5")))
    return -1;

  ctx = EVP_MD_CTX_new ();
  if (ctx
      && EVP_DigestVerifyInit (ctx, NULL, EVP_sha256 (), NULL, key) == 1
      && EVP_DigestVerify (ctx, value, value_len, data, len) == 1)
    ok = 1;
  EVP_MD_CTX_free (ctx);
  return ok ? 0 : -1;
}

static EVP_PKEY *
read_key (const char *key_path)
{
  FILE     *f = fopen (key_path, "r");
  EVP_PKEY *key;

  if (!f)
    return NULL;
  key = PEM_read_PUBKEY (f, NULL, NULL, NULL);
  fclose (f);
  return key;
}

/* Check the image `name` under /images: its embedded data, every hash of
 * it, and a signature by `key`. Returns 0 with `*data` and `*len` set, 1 if
 * there is no such image, or -1 with the reason on stderr. */
static int
check_image (const void *fit, const char *name, EVP_PKEY *key,
	     const void **data, int *len)
{
  char path[32];
  int  image, sub, hashes = 0, signed_ok = 0;

  snprintf (path, sizeof (path), "/images/%s", name);
  image = fdt_path_offset (fit, path);
  if (image == -FDT_ERR_NOTFOUND)
    return 1;
  if (image < 0)
    return logo_fail_in (name, "unreadable image node");
  *data = fdt_getprop (fit, image, "data", len);
  if (!*data || *len <= 0)
    return logo_fail_in (name, "no embedded data");

  fdt_for_each_subnode (sub, fit, image)
    {
      const char *sub_name = fdt_get_name (fit, sub, NULL);

      if (!sub_name)
	continue;
      if (!strncmp (sub_name, "hash", 4))
	{
	  if (check_hash (fit, sub, *data, *len, name))
	    return -1;
	  hashes++;
	}
      else if (!strncmp (sub_name, "signature", 9)
	       && !check_signature (fit, sub, *data, *len, key))
	signed_ok = 1;
    }

  if (!hashes)
    return logo_fail_in (name, "no hash");
  if (!signed_ok)
    return logo_fail_in (name, "not signed by the logo key");
  return 0;
}

int
psplash_logo_from_fit (const unsigned char *fit,
		       size_t               len,
		       const char          *key_path,
		       PSplashLogo         *logo,
		       PSplashTheme        *theme)
{
  const void   *bmp, *text;
  int           bmp_len, text_len, ret;
  PSplashTheme  t = *theme;
  const char   *why;
  EVP_PKEY     *key;

  memset (logo, 0, sizeof (*logo));

  if (len < sizeof (struct fdt_header) || fdt_check_full (fit, len))
    return logo_fail ("not a FIT");

  key = read_key (key_path);
  if (!key)
    return logo_fail ("cannot read the logo public key");

  ret = check_image (fit, "bmp", key, &bmp, &bmp_len);
  if (ret == 1)
    ret = logo_fail ("no /images/bmp");
  if (!ret)
    {
      ret = check_image (fit, "theme", key, &text, &text_len);
      if (ret == 1)
	ret = 0;			/* no theme: the built-in colours */
      else if (!ret && psplash_theme_parse (text, text_len, &t, &why))
	ret = logo_fail_in ("theme", why);
    }
  EVP_PKEY_free (key);
  if (ret)
    return -1;

  if (psplash_logo_decode_bmp (bmp, bmp_len, logo))
    return -1;
  *theme = t;
  return 0;
}

/* The block device of the partition labelled `partname`, from sysfs: the
 * splash starts before udev, so there are no by-partlabel links yet. */
static int
open_partition (const char *partname)
{
  DIR           *dir = opendir ("/sys/class/block");
  struct dirent *e;
  int            fd = -1;

  if (!dir)
    return -1;
  while (fd < 0 && (e = readdir (dir)))
    {
      char  path[300], line[256], dev[256] = "";
      int   match = 0;
      FILE *f;

      if (e->d_name[0] == '.')
	continue;
      snprintf (path, sizeof (path), "/sys/class/block/%s/uevent", e->d_name);
      if (!(f = fopen (path, "r")))
	continue;
      while (fgets (line, sizeof (line), f))
	{
	  line[strcspn (line, "\n")] = 0;
	  if (!strncmp (line, "PARTNAME=", 9) && !strcmp (line + 9, partname))
	    match = 1;
	  else if (!strncmp (line, "DEVNAME=", 8))
	    snprintf (dev, sizeof (dev), "%s", line + 8);
	}
      fclose (f);
      if (match && dev[0])
	{
	  snprintf (path, sizeof (path), "/dev/%s", dev);
	  fd = open (path, O_RDONLY | O_CLOEXEC);
	}
    }
  closedir (dir);
  return fd;
}

static int
read_full (int fd, unsigned char *buf, size_t len)
{
  size_t done = 0;

  while (done < len)
    {
      ssize_t n = read (fd, buf + done, len - done);

      if (n < 0 && errno == EINTR)
	continue;
      if (n <= 0)
	return -1;
      done += n;
    }
  return 0;
}

int
psplash_logo_load (const char   *partname,
		   const char   *key_path,
		   PSplashLogo  *logo,
		   PSplashTheme *theme)
{
  struct fdt_header hdr;
  unsigned char    *fit;
  uint32_t          size;
  int               fd, ret;

  memset (logo, 0, sizeof (*logo));

  fd = open_partition (partname);
  if (fd < 0)
    {
      /* No partition is the ordinary case on an unbranded unit. */
      fprintf (stderr, "psplash: no '%s' partition, using the built-in logo\n",
	       partname);
      return -1;
    }

  if (read_full (fd, (unsigned char *) &hdr, sizeof (hdr))
      || fdt_check_header (&hdr))
    {
      close (fd);
      return logo_fail ("partition holds no FIT");
    }
  size = fdt_totalsize (&hdr);
  if (size > LOGO_FIT_MAX_SIZE)
    {
      close (fd);
      return logo_fail ("FIT larger than any bundle");
    }

  fit = malloc (size);
  if (!fit)
    {
      close (fd);
      return logo_fail ("out of memory");
    }
  memcpy (fit, &hdr, sizeof (hdr));
  ret = read_full (fd, fit + sizeof (hdr), size - sizeof (hdr));
  close (fd);
  if (ret)
    {
      free (fit);
      return logo_fail ("partition shorter than its FIT");
    }

  ret = psplash_logo_from_fit (fit, size, key_path, logo, theme);
  free (fit);
  if (!ret)
    fprintf (stderr, "psplash: verified logo from '%s'\n", partname);
  return ret;
}
