#include "sample_orbit.h"

#include <string.h>

#if defined(__EMSCRIPTEN__)
#  include <emscripten/html5.h>
#endif

static SampleOrbit *activeOrbit;

static float
wrap_angle(float angle) {
  const float pi    = 3.14159265358979323846f;
  const float tau   = pi * 2.0f;

  while (angle > pi) {
    angle -= tau;
  }
  while (angle < -pi) {
    angle += tau;
  }
  return angle;
}

#if defined(__EMSCRIPTEN__)
static bool
mouse_down(int eventType,
           const EmscriptenMouseEvent *event,
           void                       *userData) {
  (void)eventType;
  (void)userData;
  if (!event || event->button != 0u) {
    return false;
  }
  sample_orbit_pointer_begin((float)event->clientX,
                             (float)event->clientY);
  return sample_orbit_active();
}

static bool
mouse_move(int eventType,
           const EmscriptenMouseEvent *event,
           void                       *userData) {
  (void)eventType;
  (void)userData;
  if (!event || !activeOrbit || !activeOrbit->dragging) {
    return false;
  }
  sample_orbit_pointer_move((float)event->clientX,
                            (float)event->clientY);
  return true;
}

static bool
mouse_up(int eventType,
         const EmscriptenMouseEvent *event,
         void                       *userData) {
  (void)eventType;
  (void)event;
  (void)userData;
  if (!activeOrbit || !activeOrbit->dragging) {
    return false;
  }
  sample_orbit_pointer_end();
  return true;
}

static bool
touch_event(int eventType,
            const EmscriptenTouchEvent *event,
            void                       *userData) {
  (void)userData;
  if (!event || !activeOrbit) {
    return false;
  }

  switch (eventType) {
    case EMSCRIPTEN_EVENT_TOUCHSTART:
      if (event->numTouches > 0) {
        sample_orbit_pointer_begin((float)event->touches[0].clientX,
                                   (float)event->touches[0].clientY);
      }
      break;
    case EMSCRIPTEN_EVENT_TOUCHMOVE:
      if (event->numTouches > 0 && activeOrbit->dragging) {
        sample_orbit_pointer_move((float)event->touches[0].clientX,
                                  (float)event->touches[0].clientY);
      }
      break;
    case EMSCRIPTEN_EVENT_TOUCHEND:
    case EMSCRIPTEN_EVENT_TOUCHCANCEL:
      sample_orbit_pointer_end();
      break;
    default:
      return false;
  }
  return true;
}

static void
install_web_callbacks(void) {
  static bool installed;

  if (installed) {
    return;
  }

  emscripten_set_mousedown_callback("#canvas", NULL, false, mouse_down);
  emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,
                                    NULL,
                                    false,
                                    mouse_move);
  emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,
                                  NULL,
                                  false,
                                  mouse_up);
  emscripten_set_touchstart_callback("#canvas", NULL, false, touch_event);
  emscripten_set_touchmove_callback("#canvas", NULL, false, touch_event);
  emscripten_set_touchend_callback("#canvas", NULL, false, touch_event);
  emscripten_set_touchcancel_callback("#canvas", NULL, false, touch_event);
  installed = true;
}
#endif

void
sample_orbit_init(SampleOrbit *orbit,
                  float        yaw,
                  float        pitch,
                  float        yawSpeed,
                  float        pitchSpeed) {
  if (!orbit) {
    return;
  }

  memset(orbit, 0, sizeof(*orbit));
  orbit->yaw         = yaw;
  orbit->pitch       = pitch;
  orbit->yawSpeed    = yawSpeed;
  orbit->pitchSpeed  = pitchSpeed;
  orbit->sensitivity = 0.006f;
}

void
sample_orbit_activate(SampleOrbit *orbit) {
  activeOrbit = orbit;
#if defined(__EMSCRIPTEN__)
  install_web_callbacks();
#endif
}

void
sample_orbit_deactivate(SampleOrbit *orbit) {
  if (activeOrbit == orbit) {
    activeOrbit = NULL;
  }
}

void
sample_orbit_update(SampleOrbit *orbit, double time) {
  double delta;

  if (!orbit) {
    return;
  }
  if (!orbit->hasTime) {
    orbit->lastTime = time;
    orbit->hasTime  = true;
    return;
  }

  delta           = time - orbit->lastTime;
  orbit->lastTime = time;
  if (delta < 0.0) {
    delta = 0.0;
  } else if (delta > 0.1) {
    delta = 0.1;
  }
  if (orbit->dragging) {
    return;
  }

  orbit->yaw   = wrap_angle(orbit->yaw +
                            orbit->yawSpeed * (float)delta);
  orbit->pitch = wrap_angle(orbit->pitch +
                            orbit->pitchSpeed * (float)delta);
}

bool
sample_orbit_active(void) {
  return activeOrbit != NULL;
}

void
sample_orbit_pointer_begin(float x, float y) {
  if (!activeOrbit) {
    return;
  }

  activeOrbit->pointerX = x;
  activeOrbit->pointerY = y;
  activeOrbit->dragging = true;
}

void
sample_orbit_pointer_move(float x, float y) {
  float deltaX, deltaY;

  if (!activeOrbit || !activeOrbit->dragging) {
    return;
  }

  deltaX = x - activeOrbit->pointerX;
  deltaY = y - activeOrbit->pointerY;
  activeOrbit->pointerX = x;
  activeOrbit->pointerY = y;
  activeOrbit->yaw      = wrap_angle(activeOrbit->yaw +
                                     deltaX * activeOrbit->sensitivity);
  activeOrbit->pitch    = wrap_angle(activeOrbit->pitch +
                                     deltaY * activeOrbit->sensitivity);
}

void
sample_orbit_pointer_end(void) {
  if (activeOrbit) {
    activeOrbit->dragging = false;
  }
}
