#ifndef gpu_sample_orbit_h
#define gpu_sample_orbit_h

#include <stdbool.h>

typedef struct SampleOrbit {
  double lastTime;
  float  yaw;
  float  pitch;
  float  yawSpeed;
  float  pitchSpeed;
  float  sensitivity;
  float  zoom;
  float  pointerX;
  float  pointerY;
  bool   dragging;
  bool   hasTime;
} SampleOrbit;

void
sample_orbit_init(SampleOrbit *orbit,
                  float        yaw,
                  float        pitch,
                  float        yawSpeed,
                  float        pitchSpeed);

void
sample_orbit_activate(SampleOrbit *orbit);

void
sample_orbit_deactivate(SampleOrbit *orbit);

void
sample_orbit_update(SampleOrbit *orbit, double time);

bool
sample_orbit_active(void);

void
sample_orbit_pointer_begin(float x, float y);

void
sample_orbit_pointer_move(float x, float y);

void
sample_orbit_pointer_end(void);

void
sample_orbit_zoom(float amount);

#endif
