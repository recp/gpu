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

#ifndef gpu_backend_surface_apple_h
#define gpu_backend_surface_apple_h

#include "../api/surface_internal.h"

GPU_HIDE
void*
createMetalLayer(void *nativeHandle, GPUSurfaceType type, float scale);

GPU_HIDE
void
resizeMetalLayer(void    *metalLayer,
                 uint32_t width,
                 uint32_t height,
                 float    scale);

GPU_HIDE
void
destroyMetalLayer(void *metalLayer);

/* whether the system shows a view's frames, from its word on the main thread:
 * on macOS its window covered, minimized, on another Space or the screen
 * locked; on iOS its scene in the background (the app's, for a view in no
 * scene). nextDrawable waits out its second there, and on iOS nothing could
 * be drawn: frames are refused there at once. Read from any thread; NULL
 * (nothing watched) is never hidden */
GPU_HIDE
void*
appleWatchView(void *view);

GPU_HIDE
bool
appleViewHidden(const void *watch);

GPU_HIDE
void
appleUnwatchView(void *watch);

/* iOS: the process may give the GPU no work, from the app's word that it went
 * to the background until it comes back (the system refuses a command buffer
 * committed there a moment later, and MoltenVK and Dawn lose the device to
 * one). Followed from the first appleWatchGPUAccess; never refused elsewhere */
GPU_HIDE
void
appleWatchGPUAccess(void);

GPU_HIDE
bool
appleGPURefused(void);

/* tests: as if the app had gone to the background, or come back */
GPU_HIDE
void
appleSetGPURefused(bool refused);

#endif /* gpu_backend_surface_apple_h */
