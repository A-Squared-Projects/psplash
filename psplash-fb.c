/*
 *  pslash - a lightweight framebuffer splashscreen for embedded devices.
 *
 *  Copyright (c) 2006 Matthew Allum <mallum@o-hand.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include <endian.h>
#include "psplash.h"
#include "psplash-colors.h"

static void
psplash_wait_for_vsync(PSplashFB *fb)
{
  int err = ioctl(fb->fd, FBIO_WAITFORVSYNC, 0);
  if (err != 0)
    fprintf(stderr, "Error, FB vsync ioctl [%d]\n", err);
}

void
psplash_fb_flip(PSplashFB *fb, int sync)
{
  char *tmp;

  /* Unblank after the first flip so the display pipeline activates with a
   * fully-rendered frame already in the buffer. Only needed on kernels whose
   * fbdev pipeline is dead until something issues FBIOBLANK. On
   * a modern DRM stack the pipeline is already live (showing the kernel logo),
   * and unblanking forces a redundant DPMS-on modeset that briefly blanks the
   * panel - a black flash as psplash takes over. Gated on PSPLASH_UNBLANK,
   * which the init scripts should set only on the kernels that need it. */
  static int unblanked;
  if (!unblanked && getenv("PSPLASH_UNBLANK")) {
    ioctl(fb->fd, FBIOBLANK, FB_BLANK_UNBLANK);
    unblanked = 1;
  }

  if (fb->double_buffering) {

    /* Carry out the flip after a vsync */
    psplash_wait_for_vsync(fb);

    /* Switch the current activate area in fb */
    if (fb->fb_var.yoffset == 0 ) {
      fb->fb_var.yoffset = fb->real_height;
    } else {
      fb->fb_var.yoffset = 0;
    }
    if (ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->fb_var) == -1 ) {
      fprintf(stderr, "psplash_fb_flip: FBIOPAN_DISPLAY failed\n");
    }

    /* Switch the front and back data pointers */
    tmp = fb->fdata;
    fb->fdata = fb->bdata;
    fb->bdata = tmp;

    /* Sync new front to new back when requested */
    if (sync) {
      memcpy(fb->bdata, fb->fdata, fb->stride * fb->real_height);
    }
  } else if (fb->bdata != fb->data) {
    /* No hardware page-flip: copy the composed frame from the shadow buffer
     * to the display in one go. */
    memcpy(fb->data, fb->bdata, fb->stride * fb->real_height);
  }
}

void
psplash_fb_destroy (PSplashFB *fb)
{
  if (fb->fd >= 0)
    close (fb->fd);

  /* bdata is a malloc'd shadow buffer only on the single-buffered path; when
   * double buffering it points into the mmap'd framebuffer. */
  if (!fb->double_buffering && fb->bdata && fb->bdata != fb->data)
    free(fb->bdata);

  free(fb);
}

static int
attempt_to_change_pixel_format (PSplashFB *fb,
                                struct fb_var_screeninfo *fb_var)
{
  /* By default the framebuffer driver may have set an oversized
   * yres_virtual to support VT scrolling via the panning interface.
   *
   * We don't try and maintain this since it's more likely that we
   * will fail to increase the bpp if the driver's pre allocated
   * framebuffer isn't large enough.
   */
  fb_var->yres_virtual = fb_var->yres;

  /* First try setting an 8,8,8,0 pixel format so we don't have to do
   * any conversions while drawing. */

  fb_var->bits_per_pixel = 32;

  fb_var->red.offset = 0;
  fb_var->red.length = 8;

  fb_var->green.offset = 8;
  fb_var->green.length = 8;

  fb_var->blue.offset = 16;
  fb_var->blue.length = 8;

  fb_var->transp.offset = 0;
  fb_var->transp.length = 0;

  if (ioctl (fb->fd, FBIOPUT_VSCREENINFO, fb_var) == 0)
    {
      fprintf(stdout, "Switched to a 32 bpp 8,8,8 frame buffer\n");
      return 1;
    }
  else
    {
      fprintf(stderr,
              "Error, failed to switch to a 32 bpp 8,8,8 frame buffer\n");
    }

  /* Otherwise try a 16bpp 5,6,5 format */

  fb_var->bits_per_pixel = 16;

  fb_var->red.offset = 11;
  fb_var->red.length = 5;

  fb_var->green.offset = 5;
  fb_var->green.length = 6;

  fb_var->blue.offset = 0;
  fb_var->blue.length = 5;

  fb_var->transp.offset = 0;
  fb_var->transp.length = 0;

  if (ioctl (fb->fd, FBIOPUT_VSCREENINFO, fb_var) == 0)
    {
      fprintf(stdout, "Switched to a 16 bpp 5,6,5 frame buffer\n");
      return 1;
    }
  else
    {
      fprintf(stderr,
              "Error, failed to switch to a 16 bpp 5,6,5 frame buffer\n");
    }

  return 0;
}

PSplashFB*
psplash_fb_new (int angle, int fbdev_id)
{
  struct fb_var_screeninfo fb_var;
  struct fb_fix_screeninfo fb_fix;
  int                      off;
  char                     fbdev[9] = "/dev/fb0";

  PSplashFB *fb = NULL;

  if (fbdev_id > 0 && fbdev_id < 10)
    {
        // Conversion from integer to ascii.
        fbdev[7] = fbdev_id + 48;
    }

  if ((fb = malloc (sizeof(PSplashFB))) == NULL)
    {
      perror ("Error no memory");
      goto fail;
    }

  memset (fb, 0, sizeof(PSplashFB));

  fb->fd = -1;

  if ((fb->fd = open (fbdev, O_RDWR)) < 0)
    {
      fprintf(stderr,
              "Error opening %s\n",
              fbdev);
      goto fail;
    }

  if (ioctl (fb->fd, FBIOGET_VSCREENINFO, &fb_var) == -1)
    {
      perror ("Error getting variable framebuffer info");
      goto fail;
    }

  if (fb_var.bits_per_pixel < 16)
    {
      fprintf(stderr,
              "Error, no support currently for %i bpp frame buffers\n"
              "Trying to change pixel format...\n",
              fb_var.bits_per_pixel);
      if (!attempt_to_change_pixel_format (fb, &fb_var))
        goto fail;
    }

  if (ioctl (fb->fd, FBIOGET_VSCREENINFO, &fb_var) == -1)
    {
      perror ("Error getting variable framebuffer info (2)");
      goto fail;
    }

  /* NB: It looks like the fbdev concept of fixed vs variable screen info is
   * broken. The line_length is part of the fixed info but it can be changed
   * if you set a new pixel format. */
  if (ioctl (fb->fd, FBIOGET_FSCREENINFO, &fb_fix) == -1)
    {
      perror ("Error getting fixed framebuffer info");
      goto fail;
    }

  /* Setup double virtual resolution for double buffering */
  if (ioctl(fb->fd, FBIOPAN_DISPLAY, &fb_var) == -1) {
    fprintf(stderr, "warning: FBIOPAN_DISPLAY not supported, "
                    "double buffering disabled\n");
  } else {
    if (fb_var.yres_virtual == fb_var.yres * 2) {
      DBG("Virtual resolution already double");
      fb->double_buffering = 1;
    } else {
      fb_var.yres_virtual = fb_var.yres * 2;
      if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb_var) == -1) {
        fprintf(stderr, "warning: FBIOPUT_VSCREENINFO failed, "
                        "double buffering disabled\n");
      } else {
        if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb_fix) == -1) {
          perror(" Error getting the fixed framebuffer info");
          goto fail;
        } else {
          DBG("Virtual resolution set to double");
          fb->double_buffering = 1;
        }
      }
    }
  }

  fb->real_width  = fb->width  = fb_var.xres;
  fb->real_height = fb->height = fb_var.yres;
  fb->bpp    = fb_var.bits_per_pixel;
  fb->stride = fb_fix.line_length;
  fb->type   = fb_fix.type;
  fb->visual = fb_fix.visual;

  fb->red_offset = fb_var.red.offset;
  fb->red_length = fb_var.red.length;
  fb->green_offset = fb_var.green.offset;
  fb->green_length = fb_var.green.length;
  fb->blue_offset = fb_var.blue.offset;
  fb->blue_length = fb_var.blue.length;

  if (fb->red_offset == 11 && fb->red_length == 5 &&
      fb->green_offset == 5 && fb->green_length == 6 &&
      fb->blue_offset == 0 && fb->blue_length == 5) {
         fb->rgbmode = RGB565;
  } else if (fb->red_offset == 0 && fb->red_length == 5 &&
      fb->green_offset == 5 && fb->green_length == 6 &&
      fb->blue_offset == 11 && fb->blue_length == 5) {
         fb->rgbmode = BGR565;
  } else if (fb->red_offset == 16 && fb->red_length == 8 &&
      fb->green_offset == 8 && fb->green_length == 8 &&
      fb->blue_offset == 0 && fb->blue_length == 8) {
         fb->rgbmode = RGB888;
  } else if (fb->red_offset == 0 && fb->red_length == 8 &&
      fb->green_offset == 8 && fb->green_length == 8 &&
      fb->blue_offset == 16 && fb->blue_length == 8) {
         fb->rgbmode = BGR888;
  } else {
         fb->rgbmode = GENERIC;
  }

  DBG("width: %i, height: %i, bpp: %i, stride: %i",
      fb->width, fb->height, fb->bpp, fb->stride);

  fb->base = (char *) mmap ((caddr_t) NULL,
			    fb_fix.smem_len,
			    PROT_READ|PROT_WRITE,
			    MAP_SHARED,
			    fb->fd, 0);

  if (fb->base == (char *)-1)
    {
      perror("Error cannot mmap framebuffer ");
      goto fail;
    }

  off = (unsigned long) fb_fix.smem_start % (unsigned long) getpagesize();

  fb->data = fb->base + off;

  if (fb->double_buffering) {
    /* fb_var is needed when flipping the buffers */
    memcpy(&fb->fb_var, &fb_var, sizeof(struct fb_var_screeninfo));
    if (fb->fb_var.yoffset == 0) {
      printf("to back\n");
      fb->fdata = fb->data;
      fb->bdata = fb->data + fb->stride * fb->height;
    } else {
      printf("to front\n");
      fb->fdata = fb->data + fb->stride * fb->height;
      fb->bdata = fb->data;
    }
  } else {
    /* No hardware page-flipping: compose frames in a shadow buffer, which
     * psplash_fb_flip() copies to the display, so a partly drawn frame is
     * never visible. */
    fb->fdata = fb->data;
    fb->bdata = malloc(fb->stride * fb->real_height);
    if (fb->bdata == NULL)
      fb->bdata = fb->data;  /* OOM: fall back to direct rendering */
  }

#if 0
  /* FIXME: No support for 8pp as yet  */
  if (visual == FB_VISUAL_PSEUDOCOLOR
      || visual == FB_VISUAL_STATIC_PSEUDOCOLOR)
  {
    static struct fb_cmap cmap;

    cmap.start = 0;
    cmap.len = 16;
    cmap.red = saved_red;
    cmap.green = saved_green;
    cmap.blue = saved_blue;
    cmap.transp = NULL;

    ioctl (fb, FBIOGETCMAP, &cmap);
  }

  if (!status)
    atexit (bogl_done);
  status = 2;
#endif

  fb->angle = angle;

  switch (fb->angle)
    {
    case 270:
    case 90:
      fb->width  = fb->real_height;
      fb->height = fb->real_width;
      break;
    case 180:
    case 0:
    default:
      break;
    }

  return fb;

 fail:

  if (fb)
    psplash_fb_destroy (fb);

  return NULL;
}

#define OFFSET(fb,x,y) (((y) * (fb)->stride) + ((x) * ((fb)->bpp >> 3)))

void
psplash_fb_plot_pixel (PSplashFB    *fb,
		       int          x,
		       int          y,
		       uint8        red,
		       uint8        green,
		       uint8        blue)
{
  /* Always write to back data (bdata) which points to the right data with or
   * without double buffering support */
  int off;

  if (x < 0 || x > fb->width-1 || y < 0 || y > fb->height-1)
    return;

  switch (fb->angle)
    {
    case 270:
      off = OFFSET (fb, fb->height - y - 1, x);
      break;
    case 180:
      off = OFFSET (fb, fb->width - x - 1, fb->height - y - 1);
      break;
    case 90:
      off = OFFSET (fb, y, fb->width - x - 1);
      break;
    case 0:
    default:
      off = OFFSET (fb, x, y);
      break;
    }

  if (fb->rgbmode == RGB565 || fb->rgbmode == RGB888) {
    switch (fb->bpp)
      {
      case 24:
#if __BYTE_ORDER == __BIG_ENDIAN
        *(fb->bdata + off + 0) = red;
        *(fb->bdata + off + 1) = green;
        *(fb->bdata + off + 2) = blue;
#else
        *(fb->bdata + off + 0) = blue;
        *(fb->bdata + off + 1) = green;
        *(fb->bdata + off + 2) = red;
#endif
        break;
      case 32:
        *(volatile uint32_t *) (fb->bdata + off)
          = (red << 16) | (green << 8) | (blue);
        break;

      case 16:
        *(volatile uint16_t *) (fb->bdata + off)
	  = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
        break;
      default:
        /* depth not supported yet */
        break;
      }
  } else if (fb->rgbmode == BGR565 || fb->rgbmode == BGR888) {
    switch (fb->bpp)
      {
      case 24:
#if __BYTE_ORDER == __BIG_ENDIAN
        *(fb->bdata + off + 0) = blue;
        *(fb->bdata + off + 1) = green;
        *(fb->bdata + off + 2) = red;
#else
        *(fb->bdata + off + 0) = red;
        *(fb->bdata + off + 1) = green;
        *(fb->bdata + off + 2) = blue;
#endif
        break;
      case 32:
        *(volatile uint32_t *) (fb->bdata + off)
          = (blue << 16) | (green << 8) | (red);
        break;
      case 16:
        *(volatile uint16_t *) (fb->bdata + off)
	  = ((blue >> 3) << 11) | ((green >> 2) << 5) | (red >> 3);
        break;
      default:
        /* depth not supported yet */
        break;
      }
  } else {
    switch (fb->bpp)
      {
      case 32:
        *(volatile uint32_t *) (fb->bdata + off)
	  = ((red >> (8 - fb->red_length)) << fb->red_offset) 
	      | ((green >> (8 - fb->green_length)) << fb->green_offset)
	      | ((blue >> (8 - fb->blue_length)) << fb->blue_offset);
        break;
      case 16:
        *(volatile uint16_t *) (fb->bdata + off)
	  = ((red >> (8 - fb->red_length)) << fb->red_offset) 
	      | ((green >> (8 - fb->green_length)) << fb->green_offset)
	      | ((blue >> (8 - fb->blue_length)) << fb->blue_offset);
        break;
      default:
        /* depth not supported yet */
        break;
      }
  }
}

void
psplash_fb_draw_rect (PSplashFB    *fb,
		      int          x,
		      int          y,
		      int          width,
		      int          height,
		      uint8        red,
		      uint8        green,
		      uint8        blue)
{
  int dx, dy;

  for (dy=0; dy < height; dy++)
    for (dx=0; dx < width; dx++)
	psplash_fb_plot_pixel (fb, x+dx, y+dy, red, green, blue);
}

/* Progress bar with a "cylon" scanner glint bouncing through the filled
 * region.
 *
 * The glint is confined to the filled part, so the bar still shows how far the
 * boot actually got - the animation says "still working", not "progress
 * unknown" - and nothing jumps when progress resumes. Its width is a fraction
 * of the fill, so it rescales as the bar grows with no separate logic.
 *
 * `pos_permille` is where the glint's left edge sits, in tenths of a percent of
 * the filled width, and may be negative or past the end: the glint sweeps out
 * of view at both ends and is clipped, so it never appears to bounce off
 * invisible walls short of the edges.
 *
 * Brightness profile: full brightness at the centre, easing to the bar colour
 * by half the radius, then fading out to nothing by the edge, with a short
 * low-alpha glow past the edge. Colours derive from PSPLASH_BAR_COLOR so the
 * animation follows the theme.
 */
void
psplash_fb_draw_scanner (PSplashFB *fb,
			 int        x,
			 int        y,
			 int        width,
			 int        height,
			 int        barwidth,
			 int        pos_permille,
			 int        glint_permille,
			 int        glow_px)
{
  static const uint8 base[3] = { PSPLASH_BAR_COLOR };
  uint8 peak[3];
  int   c, dx, dy, glint_w, left, half;

  for (c = 0; c < 3; c++)
    {
      int v = base[c] + ((255 - base[c]) * 3) / 4;
      peak[c] = v > 255 ? 255 : (uint8) v;
    }

  if (barwidth > width) barwidth = width;
  if (barwidth < 0)     barwidth = 0;

  if (barwidth < width)
    psplash_fb_draw_rect (fb, x + barwidth, y, width - barwidth, height,
			  PSPLASH_BAR_BACKGROUND_COLOR);
  if (barwidth > 0)
    psplash_fb_draw_rect (fb, x, y, barwidth, height, PSPLASH_BAR_COLOR);

  if (barwidth <= 0)
    return;

  glint_w = (barwidth * glint_permille) / 1000;
  if (glint_w < 2)
    return;

  left = (barwidth * pos_permille) / 1000;
  half = glint_w / 2;

  for (dx = 0; dx < barwidth; dx++)
    {
      int  d = dx - (left + half);         /* signed distance from the centre */
      int  ad = d < 0 ? -d : d;
      int  alpha, i;
      uint8 col[3];

      if (ad <= half / 2)
	{
	  /* Core: peak easing to the bar colour by half the radius. */
	  int t = half ? (ad * 255) / (half / 2 ? half / 2 : 1) : 0;

	  if (t > 255) t = 255;
	  for (i = 0; i < 3; i++)
	    col[i] = (uint8) ((peak[i] * (255 - t) + base[i] * t) / 255);
	  alpha = 255;
	}
      else if (ad <= half)
	{
	  /* Falloff: bar colour fading out to nothing by the edge. */
	  int span = half - half / 2;

	  for (i = 0; i < 3; i++)
	    col[i] = base[i];
	  alpha = span ? 255 - ((ad - half / 2) * 255) / span : 0;
	}
      else if (ad <= half + glow_px)
	{
	  /* Glow tail past the edge. */
	  for (i = 0; i < 3; i++)
	    col[i] = peak[i];
	  alpha = glow_px ? (90 * (half + glow_px - ad)) / glow_px : 0;
	}
      else
	continue;

      if (alpha <= 0)
	continue;

      for (dy = 0; dy < height; dy++)
	{
	  /* Ellipse rather than a bar: dim the top and bottom rows a little. */
	  int edge = (dy == 0 || dy == height - 1) ? (alpha * 3) / 5 : alpha;
	  uint8 out[3];

	  for (i = 0; i < 3; i++)
	    out[i] = (uint8) ((col[i] * edge + base[i] * (255 - edge)) / 255);

	  psplash_fb_plot_pixel (fb, x + dx, y + dy, out[0], out[1], out[2]);
	}
    }
}

/* Physical byte offset of a logical (x, y), applying the same rotation as
 * psplash_fb_plot_pixel. -1 if out of bounds. Lets the caller sample the
 * VISIBLE buffer (fb->fdata) to notice someone else drawing to the display. */
int
psplash_fb_pixel_offset (PSplashFB *fb, int x, int y)
{
  switch (fb->angle)
    {
    case 270:
      { int t = x; x = fb->width - y - 1; y = t; break; }
    case 180:
      x = fb->width  - x - 1;
      y = fb->height - y - 1;
      break;
    case 90:
      { int t = x; x = y; y = fb->height - t - 1; break; }
    case 0:
    default:
      break;
    }

  if (x < 0 || x >= fb->real_width || y < 0 || y >= fb->real_height)
    return -1;

  return (y * fb->stride) + (x * (fb->bpp >> 3));
}

void
psplash_fb_draw_image (PSplashFB    *fb,
		       int          x,
		       int          y,
		       int          img_width,
		       int          img_height,
		       int          img_bytes_per_pixel,
		       int          img_rowstride,
		       uint8       *rle_data)
{
  uint8       *p = rle_data;
  int          dx = 0, dy = 0,  total_len;
  unsigned int len;

  total_len = img_rowstride * img_height;

  /* FIXME: Optimise, check for over runs ... */
  while ((p - rle_data) < total_len)
    {
      len = *(p++);

      if (len & 128)
	{
	  len -= 128;

	  if (len == 0) break;

	  do
	    {
	      if ((img_bytes_per_pixel < 4 || *(p+3)) && dx < img_width)
	        psplash_fb_plot_pixel (fb, x+dx, y+dy, *(p), *(p+1), *(p+2));
	      if (++dx * img_bytes_per_pixel >= img_rowstride) { dx=0; dy++; }
	    }
	  while (--len);

	  p += img_bytes_per_pixel;
	}
      else
	{
	  if (len == 0) break;

	  do
	    {
	      if ((img_bytes_per_pixel < 4 || *(p+3)) && dx < img_width)
	        psplash_fb_plot_pixel (fb, x+dx, y+dy, *(p), *(p+1), *(p+2));
	      if (++dx * img_bytes_per_pixel >= img_rowstride) { dx=0; dy++; }
	      p += img_bytes_per_pixel;
	    }
	  while (--len && (p - rle_data) < total_len);
	}
    }
}

/* Font rendering code based on BOGL by Ben Pfaff */

static int
psplash_font_glyph (const PSplashFont *font, wchar_t wc, u_int32_t **bitmap)
{
  int mask = font->index_mask;
  int i;

  for (;;)
    {
      for (i = font->offset[wc & mask]; font->index[i]; i += 2)
	{
	  if ((wchar_t)(font->index[i] & ~mask) == (wc & ~mask))
	    {
	      if (bitmap != NULL)
		*bitmap = &font->content[font->index[i+1]];
	      return font->index[i] & mask;
	    }
	}
    }
  return 0;
}

void
psplash_fb_text_size (int                *width,
		      int                *height,
		      const PSplashFont  *font,
		      const char         *text)
{
  char   *c = (char*)text;
  wchar_t wc;
  int     k, n, w, h, mw;

  n = strlen (text);
  mw = h = w = 0;

  mbtowc (0, 0, 0);
  for (; (k = mbtowc (&wc, c, n)) > 0; c += k, n -= k)
    {
      if (*c == '\n')
	{
	  if (w > mw)
	    mw = w;
	  w = 0;
	  h += font->height;
	  continue;
	}

      w += psplash_font_glyph (font, wc, NULL);
    }

  *width  = (w > mw) ? w : mw;
  *height = (h == 0) ? font->height : h;
}

void
psplash_fb_draw_text (PSplashFB         *fb,
		      int                x,
		      int                y,
		      uint8              red,
		      uint8              green,
		      uint8              blue,
		      const PSplashFont *font,
		      const char        *text)
{
  int     h, w, k, n, cx, cy, dx, dy;
  char   *c = (char*)text;
  wchar_t wc;

  n = strlen (text);
  h = font->height;
  dx = dy = 0;

  mbtowc (0, 0, 0);
  for (; (k = mbtowc (&wc, c, n)) > 0; c += k, n -= k)
    {
      u_int32_t *glyph = NULL;

      if (*c == '\n')
	{
	  dy += h;
	  dx  = 0;
	  continue;
	}

      w = psplash_font_glyph (font, wc, &glyph);

      if (glyph == NULL)
	continue;

      for (cy = 0; cy < h; cy++)
	{
	  u_int32_t g = *glyph++;

	  for (cx = 0; cx < w; cx++)
	    {
	      if (g & 0x80000000)
		psplash_fb_plot_pixel (fb, x+dx+cx, y+dy+cy,
				       red, green, blue);
	      g <<= 1;
	    }
	}

      dx += w;
    }
}

