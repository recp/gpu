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

#ifndef gpu_test_frame_pacer_host_h
#define gpu_test_frame_pacer_host_h

#include <gpu/gpu.h>
#include <stdbool.h>
#include <stdint.h>

/* what frame_pacer.c needs of the platform: a window to present to and the
 * calling thread's event loop (a pacer ticks through it) */

/* a window (or an offscreen consumer) and its surface */
bool
pacerHostOpen(GPUInstance *inst, GPUAdapter *adapter, uint32_t width, uint32_t height, GPUSurface **outSurface);

/* the window's size follows the swapchain's; false when the consumer's is fixed */
bool
pacerHostResize(uint32_t width, uint32_t height);

/* runs this thread's event loop for that long */
void
pacerHostPump(double seconds);

/* now, on the clock GPUFrameTiming's times are in */
double
pacerHostNow(void);

/* times the consumer took the presents since the last call, in present order;
 * zero when the host can't tell (the display's own timing is used then) */
uint32_t
pacerHostPresented(double *times, uint32_t capacity);

/* the consumer's own name for what presentation means there */
const char*
pacerHostPresentKind(void);

void
pacerHostClose(void);

uintptr_t
pacerHostThread(void);

#endif /* gpu_test_frame_pacer_host_h */
