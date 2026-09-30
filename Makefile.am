bin_PROGRAMS=psplash psplash-write

AM_CFLAGS = $(GCC_FLAGS) $(EXTRA_GCC_FLAGS) -D_GNU_SOURCE -DFONT_HEADER=\"$(FONT_NAME)-font.h\" -DFONT_DEF=$(FONT_NAME)_font \
	     -DPSPLASH_MSG_FONT_PATH=\"$(PSPLASH_MSG_FONT_PATH)\" $(FREETYPE_CFLAGS)

psplash_SOURCES = psplash.c psplash.h psplash-fb.c psplash-fb.h \
                  psplash-console.c psplash-console.h           \
		  psplash-colors.h psplash-config.h		\
		  psplash-poky-img.h psplash-bar-img.h $(FONT_NAME)-font.h
BUILT_SOURCES = psplash-poky-img.h psplash-bar-img.h

psplash_LDADD = $(FREETYPE_LIBS)

psplash_write_SOURCES = psplash-write.c psplash.h

if HAVE_WAYLAND
# The layer-shell client code refers to xdg_popup, so xdg-shell comes too.
XDG_SHELL_XML = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
LAYER_SHELL_XML = $(top_srcdir)/protocol/wlr-layer-shell-unstable-v1.xml
WAYLAND_GENERATED = wlr-layer-shell-unstable-v1-client-protocol.h \
		    wlr-layer-shell-unstable-v1-protocol.c \
		    xdg-shell-client-protocol.h xdg-shell-protocol.c
psplash_SOURCES += psplash-wayland.c
nodist_psplash_SOURCES = $(WAYLAND_GENERATED)
BUILT_SOURCES += $(WAYLAND_GENERATED)
CLEANFILES = $(WAYLAND_GENERATED)
AM_CFLAGS += $(WAYLAND_CFLAGS)
psplash_LDADD += $(WAYLAND_LIBS)

wlr-layer-shell-unstable-v1-client-protocol.h: $(LAYER_SHELL_XML)
	$(WAYLAND_SCANNER) client-header $< $@
wlr-layer-shell-unstable-v1-protocol.c: $(LAYER_SHELL_XML)
	$(WAYLAND_SCANNER) private-code $< $@
xdg-shell-client-protocol.h: $(XDG_SHELL_XML)
	$(WAYLAND_SCANNER) client-header $< $@
xdg-shell-protocol.c: $(XDG_SHELL_XML)
	$(WAYLAND_SCANNER) private-code $< $@
endif

if HAVE_SYSTEMD
psplash_CPPFLAGS = $(SYSTEMD_CFLAGS) -DHAVE_SYSTEMD
psplash_LDADD += $(SYSTEMD_LIBS)
bin_PROGRAMS += psplash-systemd
psplash_systemd_CPPFLAGS = $(SYSTEMD_CFLAGS)
psplash_systemd_LDADD = $(SYSTEMD_LIBS)
psplash_systemd_SOURCES = psplash-systemd.c psplash.h
endif

EXTRA_DIST = make-image-header.sh protocol/wlr-layer-shell-unstable-v1.xml
 
MAINTAINERCLEANFILES = aclocal.m4 compile config.guess config.sub configure depcomp install-sh ltmain.sh Makefile.in missing

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-snap`date +"%Y%m%d"`

psplash-bar-img.h: base-images/psplash-bar.png
	$(top_srcdir)/make-image-header.sh $< BAR
psplash-poky-img.h: base-images/psplash-poky.png
	$(top_srcdir)/make-image-header.sh $< POKY

