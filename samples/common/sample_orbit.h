/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
