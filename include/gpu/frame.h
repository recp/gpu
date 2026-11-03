/*
 * Copyright (C) 2020 Recep Aslantas
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

#ifndef gpu_frame_h
#define gpu_frame_h
#ifdef __cplusplus
extern "C" {
#endif

#include "texture.h"
#include "cmdqueue.h"

typedef struct GPUSwapchain  GPUSwapchain;
typedef struct GPUFrame      GPUFrame;
typedef struct GPUFramePacer GPUFramePacer;

typedef struct GPUFrameTiming {
  /* monotonic display-clock seconds; refreshInterval is zero on the first tick. */
  double targetPresentTime;
  double deadline;
  double refreshInterval;
} GPUFrameTiming;

typedef void (*GPUFramePacerFn)(GPUFrame *frame, const GPUFrameTiming *timing, void *user);

typedef struct GPUFramePacerCreateInfo {
  GPUChainedStruct chain;
  const char      *label;
  GPUSwapchain    *swapchain;
  GPUFramePacerFn  callback;
  void           *user;
  uint32_t        latencyFrames;
  float           minRate;
  float           maxRate;
  float           preferredRate;
} GPUFramePacerCreateInfo;

/* created paused on the calling thread's run loop; all pacer calls stay there.
 * enable GPU_FEATURE_FRAME_PACER when creating the device. one pacer per swapchain;
 * GPUBeginFrame is allowed while paused, on that thread, outside a callback and
 * without an active frame. acquisition may block; finish the manual frame before
 * resuming; an unpause request while a manual frame is active is ignored.
 * pacer destruction preserves a manual frame. swapchain destruction destroys
 * the pacer and releases any remaining frame.
 * Metal detaches its display link for manual acquisition and recreates it on
 * resume with the original settings. recreation failure leaves the pacer paused
 * and reports through the device error callback.
 * callback frames are borrowed for that callback only; NULL means skip encoding.
 * finish/submit normally; an unconsumed frame is released on callback return.
 * destroying the pacer in its callback is supported. the device must outlive it.
 * latencyFrames: zero/one selects one frame, two permits two; zero rates use defaults.
 */
GPU_EXPORT
GPUResult
GPUCreateFramePacer(GPUDevice                     *device,
                    const GPUFramePacerCreateInfo *info,
                    GPUFramePacer                **outPacer);

GPU_EXPORT
void
GPUSetFramePacerPaused(GPUFramePacer *pacer, bool paused);

GPU_EXPORT
void
GPUDestroyFramePacer(GPUFramePacer *pacer);

/* frame calls share the device's transient allocator/statistics: finish the
 * current frame before beginning another, including across swapchains.
 * serialize on the render thread; see swapchain.h for Apple transaction rules.
 */
GPU_EXPORT
GPUFrame*
GPUBeginFrame(GPUSwapchain *swapchain);

/* returns the acquired render target for this frame. */
GPU_EXPORT
GPUTexture*
GPUFrameGetTarget(GPUFrame *frame);

GPU_EXPORT
GPUTextureView*
GPUFrameGetTargetView(GPUFrame *frame);

GPU_EXPORT
void
GPUEndFrame(GPUFrame *frame);

/* schedules frame presentation on the command buffer. */
GPU_EXPORT
void
GPUSchedulePresent(GPUCommandBuffer *cmdb, GPUFrame *frame);

/* convenience alias for GPUSchedulePresent(). */
GPU_EXPORT
void
GPUPresent(GPUCommandBuffer *cmdb, GPUFrame *frame);

/* schedules, submits, and consumes a frame with one command buffer. */
GPU_EXPORT
GPUResult
GPUFinishFrame(GPUQueue         *__restrict cmdq,
               GPUCommandBuffer *__restrict cmdb,
               GPUFrame         *__restrict frame);

#ifdef __cplusplus
}
#endif
#endif /* gpu_frame_h */
