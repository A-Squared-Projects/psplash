/*
 *  pslash - a lightweight framebuffer splashscreen for embedded devices.
 *
 *  Wayland display backend.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  The splash as a client of a compositor: one wlr-layer-shell surface on the
 *  background layer, sized by the compositor, drawn by the same code as the
 *  framebuffer path. Frames are composed in a shadow buffer exactly as on a
 *  single-buffered framebuffer, and a flip copies the finished frame into
 *  whichever of two shared-memory buffers the compositor is not holding.
 *
 *  The compositor ends the splash by closing the surface - typically when an
 *  application maps its first window - and psplash then exits as it does on
 *  QUIT. A compositor that goes away instead is a failure, and psplash exits
 *  non-zero so that a supervisor starts it again for the next compositor.
 *  Being on the background layer, anything the compositor stacks above it
 *  covers it, with no stacking decision needed from either side.
 */

#include <poll.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include "psplash.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define PSPLASH_WL_NAMESPACE "psplash"
#define PSPLASH_WL_NBUF      2

struct psplash_wl_buffer
{
  struct wl_buffer *wl;
  char             *data;
  int               busy;
};

static struct
{
  struct wl_display            *display;
  struct wl_registry           *registry;
  struct wl_compositor         *compositor;
  struct wl_shm                *shm;
  struct zwlr_layer_shell_v1   *layer_shell;
  struct wl_surface            *surface;
  struct zwlr_layer_surface_v1 *layer_surface;
  uint32_t                      width, height;
  int                           configured;
  int                           closed;
  char                         *pool_data;
  size_t                        pool_size;
  struct psplash_wl_buffer      buffers[PSPLASH_WL_NBUF];
} wl;

static void
registry_global (void               *UNUSED(data),
		 struct wl_registry *registry,
		 uint32_t            name,
		 const char         *interface,
		 uint32_t            version)
{
  if (!strcmp (interface, wl_compositor_interface.name))
    wl.compositor = wl_registry_bind (registry, name,
				      &wl_compositor_interface, 4);
  else if (!strcmp (interface, wl_shm_interface.name))
    wl.shm = wl_registry_bind (registry, name, &wl_shm_interface, 1);
  else if (!strcmp (interface, zwlr_layer_shell_v1_interface.name))
    wl.layer_shell = wl_registry_bind (registry, name,
				       &zwlr_layer_shell_v1_interface,
				       version < 4 ? version : 4);
}

static void
registry_global_remove (void               *UNUSED(data),
			struct wl_registry *UNUSED(registry),
			uint32_t            UNUSED(name))
{
}

static const struct wl_registry_listener registry_listener = {
  registry_global,
  registry_global_remove,
};

static void
layer_surface_configure (void                         *UNUSED(data),
			 struct zwlr_layer_surface_v1 *surface,
			 uint32_t                      serial,
			 uint32_t                      width,
			 uint32_t                      height)
{
  zwlr_layer_surface_v1_ack_configure (surface, serial);

  /* The size is decided once, at the first configure: the splash is laid out
   * for one screen and is not redrawn for another. */
  if (!wl.configured)
    {
      wl.width  = width;
      wl.height = height;
      wl.configured = 1;
    }
}

static void
layer_surface_closed (void                         *UNUSED(data),
		      struct zwlr_layer_surface_v1 *UNUSED(surface))
{
  wl.closed = 1;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
  layer_surface_configure,
  layer_surface_closed,
};

static void
buffer_release (void *data, struct wl_buffer *UNUSED(buffer))
{
  ((struct psplash_wl_buffer *) data)->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = {
  buffer_release,
};

static int
create_buffers (int stride)
{
  size_t frame = (size_t) stride * wl.height;
  struct wl_shm_pool *pool;
  int fd, i;

  wl.pool_size = frame * PSPLASH_WL_NBUF;

  fd = memfd_create ("psplash", MFD_CLOEXEC);
  if (fd < 0)
    {
      perror ("psplash: memfd_create");
      return -1;
    }
  if (ftruncate (fd, wl.pool_size) < 0)
    {
      perror ("psplash: ftruncate");
      close (fd);
      return -1;
    }
  wl.pool_data = mmap (NULL, wl.pool_size, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, 0);
  if (wl.pool_data == MAP_FAILED)
    {
      perror ("psplash: mmap");
      wl.pool_data = NULL;
      close (fd);
      return -1;
    }

  pool = wl_shm_create_pool (wl.shm, fd, wl.pool_size);
  for (i = 0; i < PSPLASH_WL_NBUF; i++)
    {
      struct psplash_wl_buffer *b = &wl.buffers[i];

      b->data = wl.pool_data + frame * i;
      b->busy = 0;
      b->wl = wl_shm_pool_create_buffer (pool, frame * i, wl.width, wl.height,
					 stride, WL_SHM_FORMAT_XRGB8888);
      wl_buffer_add_listener (b->wl, &buffer_listener, b);
    }
  wl_shm_pool_destroy (pool);
  close (fd);
  return 0;
}

PSplashFB*
psplash_fb_new (int UNUSED(angle), int UNUSED(fbdev_id))
{
  struct wl_region *no_input;
  PSplashFB *fb;

  wl.display = wl_display_connect (NULL);
  if (!wl.display)
    {
      fprintf (stderr, "psplash: cannot connect to the Wayland compositor\n");
      return NULL;
    }

  wl.registry = wl_display_get_registry (wl.display);
  wl_registry_add_listener (wl.registry, &registry_listener, NULL);
  wl_display_roundtrip (wl.display);

  if (!wl.compositor || !wl.shm || !wl.layer_shell)
    {
      fprintf (stderr, "psplash: compositor lacks wl_compositor, wl_shm "
	       "or wlr-layer-shell\n");
      goto fail;
    }

  wl.surface = wl_compositor_create_surface (wl.compositor);

  /* Nothing on the splash is touchable. */
  no_input = wl_compositor_create_region (wl.compositor);
  wl_surface_set_input_region (wl.surface, no_input);
  wl_region_destroy (no_input);

  wl.layer_surface =
    zwlr_layer_shell_v1_get_layer_surface (wl.layer_shell, wl.surface, NULL,
					   ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
					   PSPLASH_WL_NAMESPACE);
  zwlr_layer_surface_v1_add_listener (wl.layer_surface,
				      &layer_surface_listener, NULL);
  /* Anchored to every edge with no size of its own: the whole output. */
  zwlr_layer_surface_v1_set_anchor (wl.layer_surface,
				    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				    ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
				    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_size (wl.layer_surface, 0, 0);
  zwlr_layer_surface_v1_set_exclusive_zone (wl.layer_surface, -1);
  wl_surface_commit (wl.surface);

  while (!wl.configured && !wl.closed)
    if (wl_display_dispatch (wl.display) < 0)
      goto fail;

  if (wl.closed || wl.width == 0 || wl.height == 0)
    {
      fprintf (stderr, "psplash: compositor gave the splash no space\n");
      goto fail;
    }

  if ((fb = calloc (1, sizeof (PSplashFB))) == NULL)
    {
      perror ("psplash: calloc");
      goto fail;
    }

  /* XRGB8888 is what the framebuffer path calls a 32bpp RGB888 layout, so
   * every drawing fast path applies unchanged. Rotation is the compositor's
   * business, not the splash's. */
  fb->fd           = -1;
  fb->angle        = 0;
  fb->width        = fb->real_width  = wl.width;
  fb->height       = fb->real_height = wl.height;
  fb->bpp          = 32;
  fb->stride       = wl.width * 4;
  fb->rgbmode      = RGB888;
  fb->red_offset   = 16;
  fb->red_length   = 8;
  fb->green_offset = 8;
  fb->green_length = 8;
  fb->blue_offset  = 0;
  fb->blue_length  = 8;

  /* Compose off-screen; nothing but this process ever writes the shadow, so
   * a takeover watcher reading it can never fire - the compositor closing the
   * surface is what ends the splash here. */
  fb->bdata = malloc (fb->stride * fb->height);
  if (!fb->bdata)
    {
      perror ("psplash: shadow buffer");
      free (fb);
      goto fail;
    }
  fb->data = fb->fdata = fb->bdata;

  if (create_buffers (fb->stride) < 0)
    {
      free (fb->bdata);
      free (fb);
      goto fail;
    }

  return fb;

 fail:
  wl_display_disconnect (wl.display);
  memset (&wl, 0, sizeof (wl));
  return NULL;
}

void
psplash_fb_flip (PSplashFB *fb, int UNUSED(sync))
{
  struct psplash_wl_buffer *b = NULL;
  int i;

  while (!b && !wl.closed)
    {
      for (i = 0; i < PSPLASH_WL_NBUF; i++)
	if (!wl.buffers[i].busy)
	  {
	    b = &wl.buffers[i];
	    break;
	  }
      if (!b && wl_display_dispatch (wl.display) < 0)
	return;
    }
  if (!b)
    return;

  memcpy (b->data, fb->bdata, fb->stride * fb->height);
  wl_surface_attach (wl.surface, b->wl, 0, 0);
  wl_surface_damage_buffer (wl.surface, 0, 0, fb->width, fb->height);
  wl_surface_commit (wl.surface);
  b->busy = 1;
  wl_display_flush (wl.display);
}

int
psplash_fb_event_fd (PSplashFB *UNUSED(fb))
{
  return wl.display ? wl_display_get_fd (wl.display) : -1;
}

int
psplash_fb_dispatch (PSplashFB *UNUSED(fb), int readable)
{
  if (readable)
    {
      if (wl_display_dispatch (wl.display) < 0)
	return -1;
    }
  else
    {
      if (wl_display_dispatch_pending (wl.display) < 0)
	return -1;
      wl_display_flush (wl.display);
    }
  return wl.closed ? 1 : 0;
}

void
psplash_fb_destroy (PSplashFB *fb)
{
  int i;

  for (i = 0; i < PSPLASH_WL_NBUF; i++)
    if (wl.buffers[i].wl)
      wl_buffer_destroy (wl.buffers[i].wl);
  if (wl.layer_surface)
    zwlr_layer_surface_v1_destroy (wl.layer_surface);
  if (wl.surface)
    wl_surface_destroy (wl.surface);
  if (wl.display)
    wl_display_disconnect (wl.display);
  if (wl.pool_data)
    munmap (wl.pool_data, wl.pool_size);
  memset (&wl, 0, sizeof (wl));

  free (fb->bdata);
  free (fb);
}
