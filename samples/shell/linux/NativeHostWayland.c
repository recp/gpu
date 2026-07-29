#include "../../common/linux.h"
#include "../../common/sample_orbit.h"

#include <libdecor.h>
#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

extern int
gpu_linux_sample_start(void);

#ifndef GPU_LINUX_SAMPLE_NAME
#  define GPU_LINUX_SAMPLE_NAME "GPU + USL Sample"
#endif

typedef struct GPUWaylandHost {
  struct wl_display     *display;
  struct wl_compositor  *compositor;
  struct wl_registry    *registry;
  struct wl_seat        *seat;
  struct wl_pointer     *pointer;
  struct wl_surface     *surface;
  struct libdecor       *decor;
  struct libdecor_frame *frame;
  GPULinuxWindow         window;
  float                  pointerX;
  float                  pointerY;
  bool                   configured;
  bool                   running;
} GPUWaylandHost;

static void
decor_error(struct libdecor *decor,
            enum libdecor_error error,
            const char       *message) {
  (void)decor;
  fprintf(stderr,
          "GPU: Wayland decoration error %d: %s\n",
          error,
          message ? message : "unknown error");
}

static struct libdecor_interface decorInterface = {
  .error = decor_error
};

static void
pointer_enter(void              *data,
              struct wl_pointer *pointer,
              uint32_t           serial,
              struct wl_surface *surface,
              wl_fixed_t         x,
              wl_fixed_t         y) {
  GPUWaylandHost *host;

  (void)pointer;
  (void)serial;
  (void)surface;
  host           = data;
  host->pointerX = (float)wl_fixed_to_double(x);
  host->pointerY = (float)wl_fixed_to_double(y);
}

static void
pointer_leave(void              *data,
              struct wl_pointer *pointer,
              uint32_t           serial,
              struct wl_surface *surface) {
  (void)data;
  (void)pointer;
  (void)serial;
  (void)surface;
  sample_orbit_pointer_end();
}

static void
pointer_motion(void              *data,
               struct wl_pointer *pointer,
               uint32_t           time,
               wl_fixed_t         x,
               wl_fixed_t         y) {
  GPUWaylandHost *host;

  (void)pointer;
  (void)time;
  host           = data;
  host->pointerX = (float)wl_fixed_to_double(x);
  host->pointerY = (float)wl_fixed_to_double(y);
  sample_orbit_pointer_move(host->pointerX, host->pointerY);
}

static void
pointer_button(void              *data,
               struct wl_pointer *pointer,
               uint32_t           serial,
               uint32_t           time,
               uint32_t           button,
               uint32_t           state) {
  GPUWaylandHost *host;

  (void)pointer;
  (void)serial;
  (void)time;
  host = data;
  if (button != BTN_LEFT) {
    return;
  }
  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    sample_orbit_pointer_begin(host->pointerX, host->pointerY);
  } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
    sample_orbit_pointer_end();
  }
}

static void
pointer_axis(void              *data,
             struct wl_pointer *pointer,
             uint32_t           time,
             uint32_t           axis,
             wl_fixed_t         value) {
  (void)data;
  (void)pointer;
  (void)time;
  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
    sample_orbit_zoom((float)-wl_fixed_to_double(value) * 0.04f);
  }
}

static void
pointer_frame(void *data, struct wl_pointer *pointer) {
  (void)data;
  (void)pointer;
}

static void
pointer_axis_source(void              *data,
                    struct wl_pointer *pointer,
                    uint32_t           source) {
  (void)data;
  (void)pointer;
  (void)source;
}

static void
pointer_axis_stop(void              *data,
                  struct wl_pointer *pointer,
                  uint32_t           time,
                  uint32_t           axis) {
  (void)data;
  (void)pointer;
  (void)time;
  (void)axis;
}

static void
pointer_axis_discrete(void              *data,
                      struct wl_pointer *pointer,
                      uint32_t           axis,
                      int32_t            discrete) {
  (void)data;
  (void)pointer;
  (void)axis;
  (void)discrete;
}

static const struct wl_pointer_listener pointerListener = {
  .enter         = pointer_enter,
  .leave         = pointer_leave,
  .motion        = pointer_motion,
  .button        = pointer_button,
  .axis          = pointer_axis,
  .frame         = pointer_frame,
  .axis_source   = pointer_axis_source,
  .axis_stop     = pointer_axis_stop,
  .axis_discrete = pointer_axis_discrete
};

static void
seat_capabilities(void           *data,
                  struct wl_seat *seat,
                  uint32_t        capabilities) {
  GPUWaylandHost *host;

  host = data;
  if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0u && !host->pointer) {
    host->pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(host->pointer, &pointerListener, host);
  } else if ((capabilities & WL_SEAT_CAPABILITY_POINTER) == 0u &&
             host->pointer) {
    wl_pointer_destroy(host->pointer);
    host->pointer = NULL;
  }
}

static void
seat_name(void *data, struct wl_seat *seat, const char *name) {
  (void)data;
  (void)seat;
  (void)name;
}

static const struct wl_seat_listener seatListener = {
  .capabilities = seat_capabilities,
  .name         = seat_name
};

static void
registry_global(void               *data,
                struct wl_registry *registry,
                uint32_t            name,
                const char         *interface,
                uint32_t            version) {
  GPUWaylandHost *host;

  host = data;
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    host->compositor = wl_registry_bind(registry,
                                        name,
                                        &wl_compositor_interface,
                                        version < 4u ? version : 4u);
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    host->seat = wl_registry_bind(registry,
                                  name,
                                  &wl_seat_interface,
                                  version < 5u ? version : 5u);
    wl_seat_add_listener(host->seat, &seatListener, host);
  }
}

static void
registry_remove(void               *data,
                struct wl_registry *registry,
                uint32_t            name) {
  (void)data;
  (void)registry;
  (void)name;
}

static const struct wl_registry_listener registryListener = {
  .global        = registry_global,
  .global_remove = registry_remove
};

static void
frame_configure(struct libdecor_frame         *frame,
                struct libdecor_configuration *configuration,
                void                          *userData) {
  GPUWaylandHost *host;
  struct libdecor_state *state;
  int width, height;

  host   = userData;
  width  = (int)(host->window.width ? host->window.width : 1120u);
  height = (int)(host->window.height ? host->window.height : 720u);
  (void)libdecor_configuration_get_content_size(configuration,
                                                frame,
                                                &width,
                                                &height);
  state = libdecor_state_new(width, height);
  libdecor_frame_commit(frame, state, configuration);
  libdecor_state_free(state);
  host->window.width  = (uint32_t)width;
  host->window.height = (uint32_t)height;
  host->configured = true;
}

static void
frame_close(struct libdecor_frame *frame, void *userData) {
  GPUWaylandHost *host;

  (void)frame;
  host          = userData;
  host->running = false;
}

static void
frame_commit(struct libdecor_frame *frame, void *userData) {
  GPUWaylandHost *host;

  (void)frame;
  host = userData;
  wl_surface_commit(host->surface);
}

static void
frame_dismiss_popup(struct libdecor_frame *frame,
                    const char            *seatName,
                    void                  *userData) {
  (void)frame;
  (void)seatName;
  (void)userData;
}

static struct libdecor_frame_interface frameInterface = {
  .configure     = frame_configure,
  .close         = frame_close,
  .commit        = frame_commit,
  .dismiss_popup = frame_dismiss_popup
};

static void
destroy_host(GPUWaylandHost *host) {
  if (host->pointer) {
    wl_pointer_destroy(host->pointer);
  }
  if (host->seat) {
    wl_seat_destroy(host->seat);
  }
  if (host->frame) {
    libdecor_frame_unref(host->frame);
  }
  if (host->surface) {
    wl_surface_destroy(host->surface);
  }
  if (host->decor) {
    libdecor_unref(host->decor);
  }
  if (host->compositor) {
    wl_compositor_destroy(host->compositor);
  }
  if (host->registry) {
    wl_registry_destroy(host->registry);
  }
  if (host->display) {
    wl_display_disconnect(host->display);
  }
}

int
main(void) {
  GPUWaylandHost host;
  GPULinuxSample *sample;

  memset(&host, 0, sizeof(host));
  host.display = wl_display_connect(NULL);
  if (!host.display) {
    fprintf(stderr, "GPU: failed to connect to the Wayland compositor\n");
    return 1;
  }

  host.registry = wl_display_get_registry(host.display);
  wl_registry_add_listener(host.registry, &registryListener, &host);
  wl_display_roundtrip(host.display);
  if (!host.compositor) {
    destroy_host(&host);
    return 1;
  }

  host.decor   = libdecor_new(host.display, &decorInterface);
  host.surface = wl_compositor_create_surface(host.compositor);
  if (!host.decor || !host.surface) {
    destroy_host(&host);
    return 1;
  }
  host.frame = libdecor_decorate(host.decor,
                                 host.surface,
                                 &frameInterface,
                                 &host);
  if (!host.frame) {
    destroy_host(&host);
    return 1;
  }
  libdecor_frame_set_title(host.frame, GPU_LINUX_SAMPLE_NAME);
  libdecor_frame_set_app_id(host.frame, "gpu.samples");
  libdecor_frame_map(host.frame);
  while (!host.configured && libdecor_dispatch(host.decor, -1) >= 0) {
  }
  if (!host.configured) {
    destroy_host(&host);
    return 1;
  }

  host.window.display = host.display;
  host.window.surface = host.surface;
  host.window.width   = host.window.width ? host.window.width : 1120u;
  host.window.height  = host.window.height ? host.window.height : 720u;
  host.window.scale   = 1.0f;
  host.window.system  = GPU_LINUX_WINDOW_WAYLAND;
  host.running        = true;

  sample = GPUSampleLinuxCreate(&host.window,
                                GPU_LINUX_SAMPLE_NAME,
                                gpu_linux_sample_start);
  if (!sample || GPUSampleLinuxFailed(sample)) {
    fprintf(stderr, "%s\n", GPUSampleLinuxStatus(sample));
    destroy_host(&host);
    return 1;
  }

  while (host.running && GPUSampleLinuxRender(sample)) {
    if (libdecor_dispatch(host.decor, 0) < 0) {
      break;
    }
  }

  GPUSampleLinuxStop(sample);
  destroy_host(&host);
  return 0;
}
