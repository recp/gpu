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

/* frame_pacer.c on Android from adb shell: an AImageReader's window takes the
 * presents (no app, no SurfaceFlinger layer) and this thread's ALooper carries
 * the Choreographer's refreshes, as a pacer thread of an app's would. A frame
 * counts as presented when its buffer is ready in the reader: queued and its
 * GPU work done (acquireNextImage waits for the buffer's fence), which is when
 * SurfaceFlinger could latch it at its next refresh. */

#include "frame_pacer_host.h"
#include <android/looper.h>
#include <android/native_window.h>
#include <media/NdkImageReader.h>
#include <pthread.h>
#include <time.h>

enum { PRESENTED_CAPACITY = 1024u };

static AImageReader    *pacer__reader;
static ANativeWindow   *pacer__window;
static pthread_mutex_t  pacer__lock = PTHREAD_MUTEX_INITIALIZER;
static double           pacer__presented[PRESENTED_CAPACITY];
static uint32_t         pacer__presentedCount;

double
pacerHostNow(void) {
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec + now.tv_nsec * 1e-9;
}

/* the reader's own thread: each buffer taken when its fence signals */
static void
pacer__available(void *context, AImageReader *reader) {
  AImage *image;
  double  now;

  (void)context;
  while (AImageReader_acquireNextImage(reader, &image) == AMEDIA_OK) {
    now = pacerHostNow();
    pthread_mutex_lock(&pacer__lock);
    if (pacer__presentedCount < PRESENTED_CAPACITY)
      pacer__presented[pacer__presentedCount++] = now;
    pthread_mutex_unlock(&pacer__lock);
    AImage_delete(image);
  }
}

bool
pacerHostOpen(GPUInstance *inst, GPUAdapter *adapter, uint32_t width, uint32_t height, GPUSurface **outSurface) {
  AImageReader_ImageListener listener = {NULL, pacer__available};

  *outSurface = NULL;
  if (!ALooper_forThread())
    ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);

  if (AImageReader_newWithUsage((int32_t)width, (int32_t)height, AIMAGE_FORMAT_RGBA_8888,
                                AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
                                6, &pacer__reader) != AMEDIA_OK
      || AImageReader_setImageListener(pacer__reader, &listener) != AMEDIA_OK
      || AImageReader_getWindow(pacer__reader, &pacer__window) != AMEDIA_OK) {
    if (pacer__reader)
      AImageReader_delete(pacer__reader);
    pacer__reader = NULL;
    return false;
  }

  *outSurface = GPUCreateSurfaceFromNative(inst, adapter, pacer__window, GPU_SURFACE_ANDROID_NATIVE_WINDOW, 1.0f);
  return *outSurface != NULL;
}

bool
pacerHostResize(uint32_t width, uint32_t height) {
  (void)width;
  (void)height;
  return false; /* the reader's size is fixed */
}

void
pacerHostPump(double seconds) {
  double end, left;

  end = pacerHostNow() + seconds;
  while ((left = end - pacerHostNow()) > 0.0)
    ALooper_pollOnce((int)(left * 1e3) + 1, NULL, NULL, NULL);
}

uint32_t
pacerHostPresented(double *times, uint32_t capacity) {
  uint32_t i, n;

  pthread_mutex_lock(&pacer__lock);
  n = pacer__presentedCount < capacity ? pacer__presentedCount : capacity;
  for (i = 0u; i < n; i++)
    times[i] = pacer__presented[i];
  pacer__presentedCount = 0u;
  pthread_mutex_unlock(&pacer__lock);
  return n;
}

const char*
pacerHostPresentKind(void) {
  return "ready in the queue (image reader, GPU done)";
}

void
pacerHostClose(void) {
  if (pacer__reader)
    AImageReader_delete(pacer__reader);
  pacer__reader = NULL;
  pacer__window = NULL;
}
