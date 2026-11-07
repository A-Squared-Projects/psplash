AC_PREREQ(2.53)
AC_INIT(psplash, 0.1, [http://bugzilla.o-hand.com/enter_bug.cgi?product=psplash])
AM_INIT_AUTOMAKE()
AC_CONFIG_SRCDIR(psplash.c)
AM_CONFIG_HEADER(config.h)

AC_ISC_POSIX
AC_PROG_CC
AC_STDC_HEADERS

if test "x$GCC" = "xyes"; then
        GCC_FLAGS="-g -Wall -Wextra"
fi

AC_ARG_ENABLE([freetype],
    AS_HELP_STRING([--enable-freetype], [Render MSG text with FreeType from a scalable font]),
    [enable_freetype=$enableval],
    [enable_freetype=no])
AS_IF([test "x$enable_freetype" = "xyes"], [
    PKG_CHECK_MODULES(FREETYPE, freetype2)
    EXTRA_GCC_FLAGS="$EXTRA_GCC_FLAGS -DHAVE_FREETYPE"
])

# Path to the scalable MSG font, loaded at runtime rather than compiled in -
# see psplash.c's msg_ft_init(). Deliberately a path and not an embedded
# blob: a font whose licence forbids modification cannot live inside a
# GPL-2 binary. Only used with --enable-freetype; if the file is absent at
# runtime psplash falls back to the built-in bitmap font.
AC_ARG_WITH([msg-font],
	    AS_HELP_STRING([--with-msg-font=PATH],
			   [runtime path to the scalable MSG font]),
	    [PSPLASH_MSG_FONT_PATH="$withval"],
	    [PSPLASH_MSG_FONT_PATH="${datadir}/psplash/msg-font.ttf"])
AC_SUBST(PSPLASH_MSG_FONT_PATH)

AC_ARG_ENABLE([wayland],
    AS_HELP_STRING([--enable-wayland], [Draw as a Wayland client on the compositor's background layer instead of on the framebuffer]),
    [enable_wayland=$enableval],
    [enable_wayland=no])
AS_IF([test "x$enable_wayland" = "xyes"], [
    PKG_CHECK_MODULES(WAYLAND, wayland-client)
    PKG_CHECK_VAR(WAYLAND_PROTOCOLS_DIR, wayland-protocols, pkgdatadir)
    AS_IF([test "x$WAYLAND_PROTOCOLS_DIR" = "x"],
	  [AC_MSG_ERROR([wayland-protocols is required for --enable-wayland])])
    AC_PATH_PROG(WAYLAND_SCANNER, wayland-scanner)
    AS_IF([test "x$WAYLAND_SCANNER" = "x"],
	  [AC_MSG_ERROR([wayland-scanner is required for --enable-wayland])])
    EXTRA_GCC_FLAGS="$EXTRA_GCC_FLAGS -DHAVE_WAYLAND"
])
AM_CONDITIONAL([HAVE_WAYLAND], [test "x$enable_wayland" = "xyes"])

# A logo read at boot from a signed FIT bundle in a partition, drawn in
# place of the built-in one when every hash matches and the signature
# verifies against the public key at --with-logo-key. Anything else, and no
# partition at all, draws the built-in logo.
AC_ARG_ENABLE([logo-bundle],
    AS_HELP_STRING([--enable-logo-bundle], [Draw the logo from a signed FIT bundle in a partition when it verifies]),
    [enable_logo_bundle=$enableval],
    [enable_logo_bundle=no])
AC_ARG_WITH([logo-partition],
	    AS_HELP_STRING([--with-logo-partition=NAME],
			   [partition label holding the logo bundle (default logo)]),
	    [PSPLASH_LOGO_PARTITION="$withval"],
	    [PSPLASH_LOGO_PARTITION="logo"])
AC_ARG_WITH([logo-key],
	    AS_HELP_STRING([--with-logo-key=PATH],
			   [runtime path to the PEM public key that signs logo bundles]),
	    [PSPLASH_LOGO_KEY="$withval"],
	    [PSPLASH_LOGO_KEY="${datadir}/psplash/logo-key.pem"])
AS_IF([test "x$enable_logo_bundle" = "xyes"], [
    PKG_CHECK_MODULES(LIBCRYPTO, libcrypto)
    # libfdt ships a pkg-config file only from some builds of dtc.
    PKG_CHECK_MODULES(LIBFDT, libfdt, [], [
	AC_CHECK_HEADER([libfdt.h], [],
			[AC_MSG_ERROR([libfdt is required for --enable-logo-bundle])])
	AC_CHECK_LIB([fdt], [fdt_check_full], [LIBFDT_LIBS=-lfdt],
		     [AC_MSG_ERROR([libfdt is required for --enable-logo-bundle])])
    ])
    EXTRA_GCC_FLAGS="$EXTRA_GCC_FLAGS -DHAVE_LOGO_BUNDLE"
])
AM_CONDITIONAL([HAVE_LOGO_BUNDLE], [test "x$enable_logo_bundle" = "xyes"])
AC_SUBST(PSPLASH_LOGO_PARTITION)
AC_SUBST(PSPLASH_LOGO_KEY)

AC_ARG_WITH([systemd], AS_HELP_STRING([--with-systemd], [Build with systemd
	     support]))

AS_IF([test "x$with_systemd" = "xyes"], [
   PKG_CHECK_MODULES(SYSTEMD, libsystemd >= 221)
])

AM_CONDITIONAL([HAVE_SYSTEMD], [test "x$with_systemd" = "xyes"])

AC_SUBST(GCC_FLAGS)

AC_ARG_WITH([font],
    AS_HELP_STRING([--with-font], [Set font to use (default is 'radeon')]),
    [FONT_NAME=$withval],
    [FONT_NAME=radeon])
AC_SUBST([FONT_NAME])

AC_ARG_ENABLE([startup-msg],
    AS_HELP_STRING([--disable-startup-msg], [Disable text banner output on startup]),
    [enable_startup_msg=$enableval],
    [enable_startup_msg=yes])
AS_IF([test "x$enable_startup_msg" = "xno"], [
    EXTRA_GCC_FLAGS="$EXTRA_GCC_FLAGS -DPSPLASH_DISABLE_STARTUP_MSG"
])

AC_ARG_ENABLE([progress-bar],
    AS_HELP_STRING([--disable-progress-bar], [Disable progress bar]),
    [enable_progress_bar=$enableval],
    [enable_progress_bar=yes])
AS_IF([test "x$enable_progress_bar" = "xno"], [
    EXTRA_GCC_FLAGS="$EXTRA_GCC_FLAGS -DPSPLASH_DISABLE_PROGRESS_BAR"
])

AC_ARG_ENABLE([bar-animation],
    AS_HELP_STRING([--disable-bar-animation], [Disable progress bar animation while no progress is reported]),
    [enable_bar_animation=$enableval],
    [enable_bar_animation=yes])
AS_IF([test "x$enable_bar_animation" = "xno"], [
    EXTRA_GCC_FLAGS="$EXTRA_GCC_FLAGS -DPSPLASH_DISABLE_BAR_ANIMATION"
])

AC_ARG_ENABLE([img-fullscreen],
    AS_HELP_STRING([--enable-img-fullscreen], [Enable the logo image in fullscreen mode)]),
    [enable_img_fullscreen=$enableval],
    [enable_img_fullscreen=no])
AS_IF([test "x$enable_img_fullscreen" = "xyes"], [
    EXTRA_GCC_FLAGS="$EXTRA_GCC_FLAGS -DPSPLASH_IMG_FULLSCREEN=1"
])

AC_SUBST(EXTRA_GCC_FLAGS)

AC_OUTPUT([
Makefile
])
