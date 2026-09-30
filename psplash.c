/* 
 *  pslash - a lightweight framebuffer splashscreen for embedded devices. 
 *
 *  Copyright (c) 2006 Matthew Allum <mallum@o-hand.com>
 *
 *  Parts of this file ( fifo handling ) based on 'usplash' copyright 
 *  Matthew Garret.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "psplash.h"
#include "psplash-config.h"
#include "psplash-colors.h"
#include "psplash-poky-img.h"
#include "psplash-bar-img.h"
#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include FONT_HEADER

#ifdef HAVE_FREETYPE
/* MSG text is rendered with FreeType from a scalable font loaded at runtime
 * (PSPLASH_MSG_FONT_PATH), falling back to the bitmap font above if it cannot
 * be loaded. Word-wrap is ours to do - FreeType only rasterizes glyphs. */
#include <ft2build.h>
#include FT_FREETYPE_H
#endif /* HAVE_FREETYPE */

#define SPLIT_LINE_POS(fb)                                  \
	(  (fb)->height                                     \
	 - ((  PSPLASH_IMG_SPLIT_DENOMINATOR                \
	     - PSPLASH_IMG_SPLIT_NUMERATOR)                 \
	    * (fb)->height / PSPLASH_IMG_SPLIT_DENOMINATOR) \
	)

#if PSPLASH_IMG_FULLSCREEN
#define LOGO_POS_Y(fb) (((fb)->height - POKY_IMG_HEIGHT) / 2)
#else
#define LOGO_POS_Y(fb)                                      \
	(((fb)->height * PSPLASH_IMG_SPLIT_NUMERATOR        \
	  / PSPLASH_IMG_SPLIT_DENOMINATOR - POKY_IMG_HEIGHT) / 2)
#endif

void
psplash_exit (int UNUSED(signum))
{
  DBG("mark");

  psplash_console_reset ();
}

#ifdef HAVE_FREETYPE
#define MSG_MAX_LINES 8
#define MSG_MAX_WORDS 32

struct msg_word {
  const char *start;
  size_t      len;   /* bytes */
  int         width; /* pixels */
};

/* Loaded on first use and kept for the life of the process, so each MSG
 * does not re-parse the font.
 *
 * The face is read from a file, not compiled in: psplash is
 * GPL-2.0-or-later, and a font whose terms forbid modification cannot be
 * linked into it. Read from disk it is data beside the program. */
static FT_Library msg_ft_library;
static FT_Face    msg_ft_face;
static int        msg_ft_ready;
static int        msg_ft_tried;

static void
msg_ft_init (void)
{
  if (msg_ft_tried)
    return;
  msg_ft_tried = 1;              /* one attempt; a missing font stays missing */

  /* The bitmap font still renders the message, so say so on stderr when
   * falling back to it - otherwise the only sign of a missing font is the
   * glyphs looking different. */
  if (FT_Init_FreeType (&msg_ft_library) != 0)
    {
      fprintf (stderr, "psplash: FreeType would not initialise, falling back"
	       " to the built-in bitmap font\n");
      return;
    }
  if (FT_New_Face (msg_ft_library, PSPLASH_MSG_FONT_PATH, 0, &msg_ft_face) != 0)
    {
      fprintf (stderr, "psplash: cannot open %s, falling back to the built-in"
	       " bitmap font\n", PSPLASH_MSG_FONT_PATH);
      return;
    }
  if (FT_Set_Pixel_Sizes (msg_ft_face, 0, PSPLASH_MSG_FONT_SIZE) != 0)
    {
      fprintf (stderr, "psplash: %s opened but %dpx was refused, falling back"
	       " to the built-in bitmap font\n",
	       PSPLASH_MSG_FONT_PATH, PSPLASH_MSG_FONT_SIZE);
      return;
    }
  msg_ft_ready = 1;
}

/* Decodes the next UTF-8 codepoint at *s, advances *s past it. Invalid
 * sequences are treated as a single Latin-1 byte - never crashes, worst
 * case shows a wrong glyph. */
static uint32_t
msg_utf8_next (const char **s)
{
  const unsigned char *p = (const unsigned char *)*s;
  uint32_t cp;
  int extra;

  if (p[0] < 0x80) { cp = p[0]; extra = 0; }
  else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; extra = 1; }
  else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; extra = 2; }
  else if ((p[0] & 0xF8) == 0xF0) { cp = p[0] & 0x07; extra = 3; }
  else { *s = (const char *)(p + 1); return p[0]; }

  p++;
  while (extra-- > 0 && (p[0] & 0xC0) == 0x80) {
    cp = (cp << 6) | (p[0] & 0x3F);
    p++;
  }
  *s = (const char *)p;
  return cp;
}

static int
msg_measure (const char *start, size_t len)
{
  const char *s = start, *end = start + len;
  int width = 0;
  FT_UInt prev = 0;
  int has_kerning = FT_HAS_KERNING (msg_ft_face);

  while (s < end) {
    uint32_t cp = msg_utf8_next (&s);
    FT_UInt glyph = FT_Get_Char_Index (msg_ft_face, cp);

    if (has_kerning && prev && glyph) {
      FT_Vector delta;
      FT_Get_Kerning (msg_ft_face, prev, glyph, FT_KERNING_DEFAULT, &delta);
      width += (int)(delta.x >> 6);
    }
    if (FT_Load_Glyph (msg_ft_face, glyph, FT_LOAD_DEFAULT) == 0)
      width += (int)(msg_ft_face->glyph->advance.x >> 6);
    prev = glyph;
  }
  return width;
}

/* Glyph coverage is blended against PSPLASH_BACKGROUND_COLOR, which the
 * text area is always cleared to, so the framebuffer is never read back. */
static void
msg_draw_run (PSplashFB *fb, const char *start, size_t len, int x, int y)
{
  static const uint8 bg[3] = { PSPLASH_BACKGROUND_COLOR };
  static const uint8 fg[3] = { PSPLASH_TEXT_COLOR };
  const char *s = start, *end = start + len;
  FT_UInt prev = 0;
  int has_kerning = FT_HAS_KERNING (msg_ft_face);
  int pen_x = x;

  while (s < end) {
    uint32_t cp = msg_utf8_next (&s);
    FT_UInt glyph = FT_Get_Char_Index (msg_ft_face, cp);
    FT_GlyphSlot slot;
    int row, col;

    if (has_kerning && prev && glyph) {
      FT_Vector delta;
      FT_Get_Kerning (msg_ft_face, prev, glyph, FT_KERNING_DEFAULT, &delta);
      pen_x += (int)(delta.x >> 6);
    }
    if (FT_Load_Glyph (msg_ft_face, glyph, FT_LOAD_RENDER) != 0) {
      prev = glyph;
      continue;
    }
    slot = msg_ft_face->glyph;
    for (row = 0; row < (int)slot->bitmap.rows; row++) {
      for (col = 0; col < (int)slot->bitmap.width; col++) {
	uint8_t coverage = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
	if (coverage == 0)
	  continue;
	psplash_fb_plot_pixel (fb,
	  pen_x + slot->bitmap_left + col,
	  y - slot->bitmap_top + row,
	  bg[0] + ((fg[0] - bg[0]) * coverage) / 255,
	  bg[1] + ((fg[1] - bg[1]) * coverage) / 255,
	  bg[2] + ((fg[2] - bg[2]) * coverage) / 255);
      }
    }
    pen_x += (int)(slot->advance.x >> 6);
    prev = glyph;
  }
}

/* Greedy word-wrap: pack space-separated words into lines no wider than
 * max_width. Breaks only between words; a single word wider than max_width
 * gets a line of its own and overflows it. */
static int
msg_wrap (const char *msg, int max_width,
	  struct msg_word lines[MSG_MAX_LINES][MSG_MAX_WORDS],
	  int line_word_count[MSG_MAX_LINES], int space_width)
{
  const char *s = msg;
  int line = 0, line_width = 0;

  line_word_count[0] = 0;
  while (*s && line < MSG_MAX_LINES) {
    const char *word_start;
    int word_width;

    while (*s == ' ')
      s++;
    if (!*s)
      break;
    word_start = s;
    while (*s && *s != ' ')
      s++;

    word_width = msg_measure (word_start, (size_t)(s - word_start));

    if (line_word_count[line] > 0 &&
	line_width + space_width + word_width > max_width) {
      line++;
      if (line >= MSG_MAX_LINES)
	break;
      line_word_count[line] = 0;
      line_width = 0;
    }
    if (line_word_count[line] >= MSG_MAX_WORDS)
      continue;

    lines[line][line_word_count[line]].start = word_start;
    lines[line][line_word_count[line]].len = (size_t)(s - word_start);
    lines[line][line_word_count[line]].width = word_width;
    line_width += (line_word_count[line] > 0 ? space_width : 0) + word_width;
    line_word_count[line]++;
  }

  return (line_word_count[0] > 0 || line > 0) ? line + 1 : 0;
}

static void
psplash_draw_msg_ft (PSplashFB *fb, const char *msg)
{
  struct msg_word lines[MSG_MAX_LINES][MSG_MAX_WORDS];
  int line_word_count[MSG_MAX_LINES];
  int line_count, i, space_width, max_width, line_height, w, h;
  int area_top, text_top;

  FT_Load_Char (msg_ft_face, ' ', FT_LOAD_DEFAULT);
  space_width = (int)(msg_ft_face->glyph->advance.x >> 6);
  line_height = (int)(msg_ft_face->size->metrics.height >> 6);
  max_width = fb->width * PSPLASH_MSG_MAX_WIDTH_PERCENT / 100;

  line_count = msg_wrap (msg, max_width, lines, line_word_count, space_width);

  w = 0;
  for (i = 0; i < line_count; i++) {
    int lw = 0, j;
    for (j = 0; j < line_word_count[i]; j++)
      lw += (j > 0 ? space_width : 0) + lines[i][j].width;
    if (lw > w)
      w = lw;
  }
  h = line_count > 0 ? line_count * line_height : line_height;

  DBG("displaying '%s' %ix%i over %i line(s)\n", msg, w, h, line_count);

  /* The text area runs from below the logo down to the split line, and the
   * text is centred vertically within it. If the text is taller than that
   * gap it sits directly above the split line and overlaps the logo. */
#if defined(PSPLASH_MSG_TOP_NUMERATOR) && defined(PSPLASH_MSG_TOP_DENOMINATOR)
  area_top = fb->height * PSPLASH_MSG_TOP_NUMERATOR
	     / PSPLASH_MSG_TOP_DENOMINATOR;
#else
  area_top = LOGO_POS_Y(fb) + POKY_IMG_HEIGHT;
#endif
  if (area_top > SPLIT_LINE_POS(fb) - h)
    area_top = SPLIT_LINE_POS(fb) - h;
  text_top = area_top + (SPLIT_LINE_POS(fb) - area_top - h) / 2;

  /* Clear the whole area, so a short message leaves nothing behind of a
   * longer one before it. */
  psplash_fb_draw_rect (fb,
			0,
			area_top,
			fb->width,
			SPLIT_LINE_POS(fb) - area_top,
			PSPLASH_BACKGROUND_COLOR);

  for (i = 0; i < line_count; i++) {
    int lw = 0, j, pen_x;
    int baseline_y = text_top + (i + 1) * line_height;

    for (j = 0; j < line_word_count[i]; j++)
      lw += (j > 0 ? space_width : 0) + lines[i][j].width;
    pen_x = (fb->width - lw) / 2;

    for (j = 0; j < line_word_count[i]; j++) {
      msg_draw_run (fb, lines[i][j].start, lines[i][j].len, pen_x, baseline_y);
      pen_x += lines[i][j].width + space_width;
    }
  }
}
#endif /* HAVE_FREETYPE */

/* Single-line rendering in the compiled-in bitmap font: used without
 * FreeType, and when the scalable font cannot be loaded. */
static void
psplash_draw_msg_bitmap (PSplashFB *fb, const char *msg)
{
  int w, h;

  psplash_fb_text_size (&w, &h, &FONT_DEF, msg);

  psplash_fb_draw_rect (fb, 0, SPLIT_LINE_POS(fb) - h, fb->width, h,
			PSPLASH_BACKGROUND_COLOR);
  psplash_fb_draw_text (fb, (fb->width - w) / 2, SPLIT_LINE_POS(fb) - h,
			PSPLASH_TEXT_COLOR, &FONT_DEF, msg);
}

void
psplash_draw_msg (PSplashFB *fb, const char *msg)
{
#ifdef HAVE_FREETYPE
  msg_ft_init ();
  if (msg_ft_ready)
    {
      psplash_draw_msg_ft (fb, msg);
      return;
    }
#endif

  psplash_draw_msg_bitmap (fb, msg);
}

#ifdef PSPLASH_SHOW_PROGRESS_BAR
/* The fillable part of the bar: the bar image less a 4px border. */
static void
psplash_progress_geometry (PSplashFB *fb, int *x, int *y,
			   int *width, int *height)
{
  *x      = ((fb->width - BAR_IMG_WIDTH) / 2) + 4;
  *y      = SPLIT_LINE_POS(fb) + 4;
  *width  = BAR_IMG_WIDTH - 8;
  *height = BAR_IMG_HEIGHT - 8;
}
#endif /* PSPLASH_SHOW_PROGRESS_BAR */

#ifdef PSPLASH_ANIMATE_BAR
/* Idle animation. After PSPLASH_BAR_ANIMATION_IDLE_MS with no command, a
 * glint sweeps through the filled part of the bar, so a long step that sends
 * no PROGRESS does not look like a hung boot. Only a change of percentage
 * stops it and redraws the bar; the glint carries on, where it was, through
 * anything that leaves the bar as it is.
 *
 * While animating, psplash also watches for something else drawing to the
 * display, and quits when it does. When the animation starts it snapshots
 * pixels the animation never touches and quits as soon as any of them
 * changes. It samples the VISIBLE buffer (fb->fdata), which is stable on the
 * single-buffered shadow path; on a hardware page-flipped fb the first frame
 * mismatches and psplash quits immediately, leaving a static bar rather than
 * fighting for the display. */
#define SHIMMER_FRAME_MS       40  /* ~25fps */
#define SHIMMER_GLINT_PM      380  /* glint width, per mille of the fill */
#define SHIMMER_START_PM     (-320) /* left edge of travel, per mille */
#define SHIMMER_END_PM       1000  /* right edge of travel, per mille */
#define SHIMMER_GLOW_PX         6  /* glow past the glint's edge */
#define SHIMMER_SAMPLES         8  /* pixels watched for takeover */

static int  shimmer_value  = -1;   /* last PROGRESS seen; -1 = none yet */
static int  shimmer_phase;
static int  shimmer_active;
static int  shimmer_off[SHIMMER_SAMPLES];
static char shimmer_snap[SHIMMER_SAMPLES];

/* Snapshot the watched pixels as they are now. Taken when the animation
 * starts, and again after psplash itself redraws something near the bar, so
 * its own drawing is never mistaken for a takeover. */
static void
shimmer_snapshot (PSplashFB *fb)
{
  int x, y, width, height, i;

  psplash_progress_geometry (fb, &x, &y, &width, &height);

  /* Sample two rows clear of the bar, spread across its width. */
  for (i = 0; i < SHIMMER_SAMPLES; i++)
    {
      int sx = x + (i * width) / SHIMMER_SAMPLES;
      int sy = (i & 1) ? y - 3 : y + height + 2;

      shimmer_off[i] = psplash_fb_pixel_offset (fb, sx, sy);
      shimmer_snap[i] = (shimmer_off[i] >= 0) ? fb->fdata[shimmer_off[i]] : 0;
    }
}

static void
shimmer_begin (PSplashFB *fb)
{
  shimmer_snapshot (fb);
  shimmer_phase  = 0;
  shimmer_active = 1;
}

/* True once anything other than us has drawn to the display. */
static int
shimmer_taken_over (PSplashFB *fb)
{
  int i;

  for (i = 0; i < SHIMMER_SAMPLES; i++)
    if (shimmer_off[i] >= 0 && fb->fdata[shimmer_off[i]] != shimmer_snap[i])
      return 1;

  return 0;
}

/* Ease-in-out over [0,255]: slow at the turns and quick through the middle,
 * so it reads as a scanner rather than a metronome. */
static int
shimmer_ease (int t)
{
  if (t < 128)
    return (2 * t * t) / 255;

  t = 255 - t;
  return 255 - (2 * t * t) / 255;
}

static void
shimmer_frame (PSplashFB *fb)
{
  int x, y, width, height, barwidth;
  int elapsed, t, eased, pos;

  psplash_progress_geometry (fb, &x, &y, &width, &height);
  barwidth = (CLAMP(shimmer_value, 0, 100) * width) / 100;

  /* Two eased half-sweeps per period: out, then back. */
  elapsed = (shimmer_phase * SHIMMER_FRAME_MS)
	    % PSPLASH_BAR_ANIMATION_PERIOD_MS;
  t = (elapsed % (PSPLASH_BAR_ANIMATION_PERIOD_MS / 2)) * 255
      / (PSPLASH_BAR_ANIMATION_PERIOD_MS / 2);
  eased = shimmer_ease (t);
  if (elapsed >= PSPLASH_BAR_ANIMATION_PERIOD_MS / 2)
    eased = 255 - eased;

  pos = SHIMMER_START_PM
	+ ((SHIMMER_END_PM - SHIMMER_START_PM) * eased) / 255;

  psplash_fb_draw_scanner (fb, x, y, width, height, barwidth,
			   pos, SHIMMER_GLINT_PM, SHIMMER_GLOW_PX);
  psplash_fb_flip (fb, 0);
  shimmer_phase++;
}
#endif /* PSPLASH_ANIMATE_BAR */

#ifdef PSPLASH_SHOW_PROGRESS_BAR
/* Set when the bar's frame has just been drawn. Updates after the first frame
 * redraw only what changes, so on a double-buffered framebuffer the frame must
 * reach both buffers: the next flip synchronises them. */
static int bar_frame_resync;

void
psplash_draw_progress (PSplashFB *fb, int value)
{
  static int frame_drawn;
  int x, y, width, height, barwidth;

  if (!frame_drawn)
    {
      psplash_fb_draw_image (fb,
			     (fb->width  - BAR_IMG_WIDTH)/2,
			     SPLIT_LINE_POS(fb),
			     BAR_IMG_WIDTH,
			     BAR_IMG_HEIGHT,
			     BAR_IMG_BYTES_PER_PIXEL,
			     BAR_IMG_ROWSTRIDE,
			     BAR_IMG_RLE_PIXEL_DATA);
      frame_drawn = 1;
      bar_frame_resync = 1;
    }

#ifdef PSPLASH_ANIMATE_BAR
  shimmer_value = value;
#endif

  psplash_progress_geometry (fb, &x, &y, &width, &height);

  if (value > 0)
    {
      barwidth = (CLAMP(value,0,100) * width) / 100;
      psplash_fb_draw_rect (fb, x + barwidth, y, 
    			width - barwidth, height,
			PSPLASH_BAR_BACKGROUND_COLOR);
      psplash_fb_draw_rect (fb, x, y, barwidth,
			    height, PSPLASH_BAR_COLOR);
    }
  else
    {
      barwidth = (CLAMP(-value,0,100) * width) / 100;
      psplash_fb_draw_rect (fb, x, y, 
    			width - barwidth, height,
			PSPLASH_BAR_BACKGROUND_COLOR);
      psplash_fb_draw_rect (fb, x + width - barwidth,
			    y, barwidth, height,
			    PSPLASH_BAR_COLOR);
    }

  DBG("value: %i, width: %i, barwidth :%i\n", value, 
		width, barwidth);
}
#endif /* PSPLASH_SHOW_PROGRESS_BAR */

static int 
parse_command (PSplashFB *fb, char *string)
{
  char *command;

  DBG("got cmd %s", string);

  if (strcmp(string,"QUIT") == 0)
    return 1;

  command = strtok(string," ");

  if (!strcmp(command,"MSG"))
    {
      /* A bare "MSG" (no text) clears the message area. */
      char *arg = strtok(NULL, "\0");

      /* The text is not the bar: the animation carries on through it. */
      psplash_draw_msg (fb, arg ? arg : "");
      psplash_fb_flip(fb, 0);
#ifdef PSPLASH_ANIMATE_BAR
      if (shimmer_active)
	shimmer_snapshot (fb);
#endif
      return 0;
    } 
 #ifdef PSPLASH_SHOW_PROGRESS_BAR
  else  if (!strcmp(command,"PROGRESS"))
    {
      char *arg = strtok(NULL, "\0");

      if (!arg)
	return 0;
#ifdef PSPLASH_ANIMATE_BAR
      /* Only a new percentage changes the bar, so only that stops the
       * animation to redraw it; the same value again changes nothing. */
      if (atoi(arg) == shimmer_value)
	return 0;
      shimmer_active = 0;
#endif
      psplash_draw_progress (fb, atoi(arg));
    } 
#endif
  else if (!strcmp(command,"QUIT")) 
    {
      return 1;
    }
  else
    {
      /* Not a command this splash knows - another splash implementation's,
       * sent by a writer that cannot tell which is running. Nothing is drawn,
       * so neither the animation nor the screen is disturbed. */
      return 0;
    }

#ifdef PSPLASH_SHOW_PROGRESS_BAR
  psplash_fb_flip(fb, bar_frame_resync);
  bar_frame_resync = 0;
#else
  psplash_fb_flip(fb, 0);
#endif
  return 0;
}

/* Set when the display went away underneath the splash, as opposed to the
 * splash being told to end: a supervisor should restart the one and not the
 * other, so the two exit differently. */
static int display_lost;

static int
psplash_display_done (PSplashFB *fb, int readable)
{
  int r = psplash_fb_dispatch (fb, readable);

  if (r < 0)
    display_lost = 1;
  return r != 0;
}

void 
psplash_main (PSplashFB *fb, int pipe_fd, int timeout) 
{
  int            err;
  ssize_t        length = 0;
  ssize_t        ret = 0;
  fd_set         descriptors;
  struct timeval tv;
  char          *end;
  char          *cmd;
  char           command[2048];

  end = command;

  while (1) 
    {
      int display_fd = psplash_fb_event_fd (fb);
      int max_fd = pipe_fd;

      /* Anything the display sent while we were drawing is handled before
       * waiting, including being told to end. */
      if (psplash_display_done (fb, 0))
	return;

      FD_ZERO(&descriptors);
      FD_SET(pipe_fd, &descriptors);
      if (display_fd >= 0)
	{
	  FD_SET(display_fd, &descriptors);
	  if (display_fd > max_fd)
	    max_fd = display_fd;
	}

#ifdef PSPLASH_ANIMATE_BAR
      /* Wait only as long as the next thing we owe the display: a frame if
       * already animating, otherwise the quiet period that starts one. The
       * animation needs a PROGRESS value to sweep through. */
      if (shimmer_active)
	{
	  tv.tv_sec  = 0;
	  tv.tv_usec = SHIMMER_FRAME_MS * 1000;
	}
      else if (shimmer_value > 0)
	{
	  tv.tv_sec  = PSPLASH_BAR_ANIMATION_IDLE_MS / 1000;
	  tv.tv_usec = (PSPLASH_BAR_ANIMATION_IDLE_MS % 1000) * 1000;
	}
      else
	{
	  tv.tv_sec  = timeout;
	  tv.tv_usec = 0;
	}

      err = select(max_fd+1, &descriptors, NULL, NULL,
		   (shimmer_active || shimmer_value > 0 || timeout != 0)
		     ? &tv : NULL);

      if (err == 0)
	{
	  /* No command in time: draw the next frame, starting if need be. */
	  if (!shimmer_active)
	    shimmer_begin (fb);
	  else if (shimmer_taken_over (fb))
	    return;

	  shimmer_frame (fb);
	  continue;
	}

      if (err < 0)
	return;
#else
      tv.tv_sec = timeout;
      tv.tv_usec = 0;

      if (timeout != 0)
	err = select(max_fd+1, &descriptors, NULL, NULL, &tv);
      else
	err = select(max_fd+1, &descriptors, NULL, NULL, NULL);

      if (err <= 0)
	return;
#endif

      if (display_fd >= 0 && FD_ISSET(display_fd, &descriptors))
	{
	  if (psplash_display_done (fb, 1))
	    return;
	  if (!FD_ISSET(pipe_fd, &descriptors))
	    continue;
	}

      
      ret = read (pipe_fd, end, sizeof(command) - (end - command));

      if (ret <= 0)
	{
	  /* Reopen to see if there's anything more for us */
	  close(pipe_fd);
	  pipe_fd = open(PSPLASH_FIFO,O_RDONLY|O_NONBLOCK);
	  goto out;
	}
      length += ret;

      cmd = command;
      do {
	int cmdlen;
        char *cmdend = memchr(cmd, '\n', length);

        /* Replace newlines with string termination */
        if (cmdend)
            *cmdend = '\0';

        cmdlen = strnlen(cmd, length);

        /* Skip string terminations */
	if (!cmdlen && length)
          {
            length--;
            cmd++;
	    continue;
          }

	if (parse_command(fb, cmd))
	  return;

	length -= cmdlen;
	cmd += cmdlen;
      } while (length);

    out:
      /* The descriptor set and the timeout are recomputed at the top of the
       * loop, which also covers the pipe having been reopened above. */
      end = &command[length];
    }

  return;
}

int 
main (int argc, char** argv) 
{
  char      *rundir;
  int        pipe_fd, i = 0, angle = 0, fbdev_id = 0, ret = 0;
  PSplashFB *fb;
  bool       disable_console_switch = FALSE;

  signal(SIGHUP, psplash_exit);
  signal(SIGINT, psplash_exit);
  signal(SIGQUIT, psplash_exit);

  while (++i < argc) {
    if (!strcmp(argv[i],"-n") || !strcmp(argv[i],"--no-console-switch"))
      {
        disable_console_switch = TRUE;
        continue;
      }

    if (!strcmp(argv[i],"-a") || !strcmp(argv[i],"--angle"))
      {
        if (++i >= argc) goto fail;
        angle = atoi(argv[i]);
        continue;
      }

    if (!strcmp(argv[i],"-f") || !strcmp(argv[i],"--fbdev"))
      {
        if (++i >= argc) goto fail;
        fbdev_id = atoi(argv[i]);
        continue;
      }

    fail:
      fprintf(stderr, 
              "Usage: %s [-n|--no-console-switch][-a|--angle <0|90|180|270>][-f|--fbdev <0..9>]\n", 
              argv[0]);
      exit(-1);
  }

  rundir = getenv("PSPLASH_FIFO_DIR");

  if (!rundir)
    rundir = "/run";

  chdir(rundir);

  if (mkfifo(PSPLASH_FIFO, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP))
    {
      if (errno!=EEXIST) 
	    {
	      perror("mkfifo");
	      exit(-1);
	    }
    }

  pipe_fd = open (PSPLASH_FIFO,O_RDONLY|O_NONBLOCK);
  
  if (pipe_fd==-1) 
    {
      perror("pipe open");
      exit(-2);
    }

  if (!disable_console_switch)
    psplash_console_switch ();

  if ((fb = psplash_fb_new(angle,fbdev_id)) == NULL)
    {
	  ret = -1;
	  goto fb_fail;
    }

#ifdef HAVE_SYSTEMD
  sd_notify(0, "READY=1");
#endif

  /* Clear the background with #ecece1 */
  psplash_fb_draw_rect (fb, 0, 0, fb->width, fb->height,
                        PSPLASH_BACKGROUND_COLOR);

  /* Draw the Poky logo  */
  psplash_fb_draw_image (fb, 
			 (fb->width  - POKY_IMG_WIDTH)/2, 
			 LOGO_POS_Y(fb),
			 POKY_IMG_WIDTH,
			 POKY_IMG_HEIGHT,
			 POKY_IMG_BYTES_PER_PIXEL,
			 POKY_IMG_ROWSTRIDE,
			 POKY_IMG_RLE_PIXEL_DATA);

/* The progress bar is not drawn until the first PROGRESS: a splash started
 * after the boot has already made progress would otherwise show an empty bar
 * that then jumps, and one started when nothing reports progress at all would
 * show a bar that never moves. See psplash_draw_progress(). */

#ifdef PSPLASH_STARTUP_MSG
  psplash_draw_msg (fb, PSPLASH_STARTUP_MSG);
#endif

  /* Scene set so let's flip the buffers. */
  /* The first time we also synchronize the buffers so we can build on an
   * existing scene. After the first scene is set in both buffers, only the
   * text and progress bar change which overwrite the specific areas with every
   * update.
   */
  psplash_fb_flip(fb, 1);

  psplash_main (fb, pipe_fd, 0);
  if (display_lost)
    ret = 1;

  psplash_fb_destroy (fb);

 fb_fail:
  unlink(PSPLASH_FIFO);

  if (!disable_console_switch)
    psplash_console_reset ();

  return ret;
}
