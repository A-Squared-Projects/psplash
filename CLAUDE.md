# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

psplash is a userspace framebuffer boot splash screen for embedded Linux (16bpp/32bpp fbdev), depending only on libc (plus optional libsystemd and FreeType). It is maintained as part of the Yocto Project. There is no test suite.

## Build

Autotools project with no `autogen.sh`:

```sh
autoreconf -fi
./configure [options]
make
```

Configure options:
- `--with-systemd` — link libsystemd (`sd_notify` READY) and build `psplash-systemd`
- `--with-font=NAME` — uses `NAME-font.h` / `NAME_font` (default `radeon`)
- `--enable-freetype` — render `MSG` text with FreeType (off by default); `--with-msg-font=PATH` sets the runtime font path
- `--disable-bar-animation` — no idle animation of the progress bar (also disables takeover detection)
- `--disable-startup-msg`, `--disable-progress-bar`, `--enable-img-fullscreen`

These become `-D` defines via `EXTRA_GCC_FLAGS`, which `psplash-config.h` turns into the `PSPLASH_*` macros the code uses (e.g. `--disable-progress-bar` → `PSPLASH_DISABLE_PROGRESS_BAR` → `PSPLASH_SHOW_PROGRESS_BAR` is left undefined). `psplash-config.h` also holds tunables wrapped in `#ifndef` so a build can override them with `-D` in CFLAGS: `PSPLASH_MSG_FONT_SIZE`, `PSPLASH_MSG_MAX_WIDTH_PERCENT`, `PSPLASH_MSG_TOP_NUMERATOR/DENOMINATOR` (where the FreeType text area starts; defaults to the bottom of the logo image), `PSPLASH_BAR_ANIMATION_IDLE_MS` and `PSPLASH_BAR_ANIMATION_PERIOD_MS`.

Every commit must build under every configure option and useful combination (at least default, `--enable-freetype`, `--enable-img-fullscreen`, `--disable-startup-msg`, `--disable-progress-bar`, `--disable-bar-animation`, `--with-systemd`, and FreeType combined with the others) with no new warnings. The obsolete-macro warnings from autoreconf and the unused `chdir`/`write` results are pre-existing. If the FreeType or libsystemd headers are not installed, `apt-get download` the `-dev` package, unpack it with `dpkg-deb -x`, and pass `FREETYPE_CFLAGS`/`FREETYPE_LIBS` or `SYSTEMD_CFLAGS`/`SYSTEMD_LIBS` to configure.

`psplash-poky-img.h` and `psplash-bar-img.h` are generated from `base-images/*.png` by `make-image-header.sh`, which needs `gdk-pixbuf-csource` on the build host. They are `BUILT_SOURCES` and not checked in. To change the logo, replace the PNG. Colors are in `psplash-colors.h`.

Manual run (needs a framebuffer device and usually root): `PSPLASH_FIFO_DIR=/tmp ./psplash -n &` then `PSPLASH_FIFO_DIR=/tmp ./psplash-write "MSG hello"`, `"PROGRESS 50"`, `"QUIT"`.

## Architecture

Three binaries share `psplash.h`:

- **psplash** (`psplash.c`): the server. It creates the FIFO `psplash_fifo` in `$PSPLASH_FIFO_DIR` (default `/run`), switches VT (`psplash-console.c`, skipped with `-n`), opens the framebuffer (`psplash-fb.c`), draws the background, logo and progress-bar frame, then runs `psplash_main()`, a `select()` loop over the FIFO. Commands are terminated by NUL or newline and several can arrive in one read: `MSG <text>`, `PROGRESS <n>` (negative values fill from the right), `QUIT`. On EOF the FIFO is reopened so more writers can connect.
- **psplash-write** (`psplash-write.c`): the client. It writes `argv[1]` plus the trailing NUL to the FIFO and fails silently if the FIFO is missing.
- **psplash-systemd** (`psplash-systemd.c`, only with `--with-systemd`): polls systemd's `Progress` property over the private bus using sd-event, sends `PROGRESS` through the FIFO, and sends `QUIT` when progress reaches 1.0.

Screen layout: the screen is split at `PSPLASH_IMG_SPLIT_NUMERATOR/DENOMINATOR` (5/6) of the height (`SPLIT_LINE_POS`). The logo is centred above the split, or on the whole screen when fullscreen is enabled. The progress bar sits just below the split. Bitmap message text sits just above the split; FreeType text is word-wrapped and centred between the bottom of the logo and the split, and each `MSG` clears that whole area. A bare `MSG` clears the message.

Idle animation (`PSPLASH_ANIMATE_BAR`): when no command arrives for `PSPLASH_BAR_ANIMATION_IDLE_MS`, `psplash_main()` uses the `select()` timeout to draw frames of a glint sweeping through the filled part of the bar (`psplash_fb_draw_scanner`); any command stops it. While animating it also samples pixels around the bar in the visible buffer and quits if something else draws to the display (takeover detection). Bar geometry comes from `psplash_progress_geometry()` for both the static bar and the animation.

Framebuffer layer (`psplash-fb.c`):
- It tries to switch the pixel format, then detects `RGB565`/`BGR565`/`RGB888`/`BGR888` fast paths, with a `GENERIC` fallback.
- It handles rotation (`-a 0|90|180|270`) in `psplash_fb_plot_pixel`. `fb->width/height` are the rotated sizes and `real_width/real_height` are the physical ones.
- Without hardware page-flipping, drawing goes to a malloc'd shadow buffer that `psplash_fb_flip()` copies to the display, so partial frames are never visible. If `PSPLASH_UNBLANK` is set in the environment, the first flip also issues `FBIOBLANK` unblank, which some kernels need.
- Double buffering is used when the driver supports `FBIOPAN_DISPLAY` with `yres_virtual = 2*yres`. Drawing goes to `bdata`, and `psplash_fb_flip()` pans after vsync and swaps buffers. The first flip uses `sync=1` to copy the full scene into both buffers. After that only the text and bar regions are redrawn, so every draw must fully overwrite the region it updates.
- Images are RLE pixel data from gdk-pixbuf-csource (`psplash_fb_draw_image`). Fonts are `PSplashFont` tables (`radeon-font.h`), selected at compile time via the `FONT_HEADER`/`FONT_DEF` macros.

## Conventions

- GNU-ish C style with a space before the call parenthesis (`foo (a, b)`), mixed tabs and spaces in the older files. Match the surrounding file; `psplash-systemd.c` uses kernel style.
- `DBG()` is compiled out unless `DEBUG` is set to 1 in `psplash.h`. Use `UNUSED(x)` for unused parameters (the build uses `-Wall -Wextra`).
- Comments describe what the code does now, never its history ("was X, now Y", "replaces", "upstream's"). Commit messages explain why the change was made.
- Commit subjects are prefixed with the file or area (e.g. `configure.ac: ...`, `psplash.c: ...`).
- Contributions are patches sent to the mailing list, not PRs: `git format-patch -M -s --subject-prefix='psplash][PATCH' origin` and `git send-email --to yocto-patches@lists.yoctoproject.org`. Follow the OpenEmbedded commit message guidelines and include a Signed-off-by line.
