#include "../../common/linux.h"
#include "../../common/sample_orbit.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include <stdbool.h>
#include <stdio.h>

extern int
gpu_linux_sample_start(void);

#ifndef GPU_LINUX_SAMPLE_NAME
#  define GPU_LINUX_SAMPLE_NAME "GPU + USL Sample"
#endif

int
main(void) {
  GPULinuxWindow windowInfo;
  GPULinuxSample *sample;
  Display        *display;
  Window          window;
  Atom            deleteWindow;
  XEvent          event;
  bool            running;

  display = XOpenDisplay(NULL);
  if (!display) {
    fprintf(stderr, "GPU: failed to open the X display\n");
    return 1;
  }

  window = XCreateSimpleWindow(display,
                               DefaultRootWindow(display),
                               0,
                               0,
                               1120u,
                               720u,
                               0u,
                               BlackPixel(display, DefaultScreen(display)),
                               BlackPixel(display, DefaultScreen(display)));
  if (!window) {
    XCloseDisplay(display);
    return 1;
  }

  XStoreName(display, window, GPU_LINUX_SAMPLE_NAME);
  XSelectInput(display,
               window,
               StructureNotifyMask |
                 KeyPressMask |
                 ButtonPressMask |
                 ButtonReleaseMask |
                 PointerMotionMask);
  deleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, window, &deleteWindow, 1);
  XMapWindow(display, window);
  XFlush(display);

  windowInfo.display = display;
  windowInfo.surface = NULL;
  windowInfo.window  = (uintptr_t)window;
  windowInfo.width   = 1120u;
  windowInfo.height  = 720u;
  windowInfo.scale   = 1.0f;
  windowInfo.system  = GPU_LINUX_WINDOW_XLIB;
  sample = GPUSampleLinuxCreate(&windowInfo,
                                GPU_LINUX_SAMPLE_NAME,
                                gpu_linux_sample_start);
  if (!sample || GPUSampleLinuxFailed(sample)) {
    fprintf(stderr, "%s\n", GPUSampleLinuxStatus(sample));
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 1;
  }

  running = true;
  while (running && GPUSampleLinuxRender(sample)) {
    while (XPending(display) > 0) {
      XNextEvent(display, &event);
      switch (event.type) {
        case ConfigureNotify:
          if (event.xconfigure.width > 0 && event.xconfigure.height > 0) {
            windowInfo.width  = (uint32_t)event.xconfigure.width;
            windowInfo.height = (uint32_t)event.xconfigure.height;
          }
          break;
        case ButtonPress:
          if (event.xbutton.button == Button1) {
            sample_orbit_pointer_begin((float)event.xbutton.x,
                                       (float)event.xbutton.y);
          } else if (event.xbutton.button == Button4) {
            sample_orbit_zoom(1.0f);
          } else if (event.xbutton.button == Button5) {
            sample_orbit_zoom(-1.0f);
          }
          break;
        case ButtonRelease:
          if (event.xbutton.button == Button1) {
            sample_orbit_pointer_end();
          }
          break;
        case MotionNotify:
          sample_orbit_pointer_move((float)event.xmotion.x,
                                    (float)event.xmotion.y);
          break;
        case KeyPress:
          if (XLookupKeysym(&event.xkey, 0) == XK_Escape) {
            running = false;
          }
          break;
        case ClientMessage:
          running = (Atom)event.xclient.data.l[0] != deleteWindow;
          break;
        default:
          break;
      }
    }
  }

  GPUSampleLinuxStop(sample);
  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return 0;
}
