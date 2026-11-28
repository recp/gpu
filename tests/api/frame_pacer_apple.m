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

/* frame_pacer.c's window on Apple platforms (apple_window.m): a small window
 * shown by the system, the main run loop */

#include "frame_pacer_host.h"
#include "apple_window.h"
#import <QuartzCore/QuartzCore.h>

static AppleWindow *pacer__window;

/* ticks are the system's to give: a covered window gets none, or a few, so the
 * window is waited for, bounded; one never shown is no window to pace */
bool
pacerHostOpen(GPUInstance *inst, GPUAdapter *adapter, uint32_t width, uint32_t height, GPUSurface **outSurface) {
  *outSurface = NULL;
  if (!(pacer__window = appleWindowOpen(40.0, 40.0, width, height)))
    return false;

  if (!appleWindowWaitShown(pacer__window, true, 0.5)) {
    pacerHostClose();
    return false;
  }

  *outSurface = appleWindowSurface(pacer__window, inst, adapter);
  return *outSurface != NULL;
}

bool
pacerHostResize(uint32_t width, uint32_t height) {
  appleWindowResize(pacer__window, width, height);
  return true;
}

void
pacerHostPump(double seconds) {
  appleWindowPump(seconds);
}

double
pacerHostNow(void) {
  return CACurrentMediaTime();
}

uint32_t
pacerHostPresented(double *times, uint32_t capacity) {
  (void)times;
  (void)capacity;
  return 0u;
}

const char*
pacerHostPresentKind(void) {
  return "display";
}

void
pacerHostClose(void) {
  appleWindowClose(pacer__window);
  pacer__window = NULL;
}
