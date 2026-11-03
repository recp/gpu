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

typedef struct GPUSwapchain     GPUSwapchain;
typedef struct GPUFrame         GPUFrame;
typedef struct GPUFramePacer    GPUFramePacer;
typedef struct GPUFrameFeedback GPUFrameFeedback;

typedef enum GPUFrameEventKind {
  GPU_FRAME_EVENT_GPU_COMPLETE            = 0,
  GPU_FRAME_EVENT_PRESENTED               = 1,
  GPU_FRAME_EVENT_PRESENTATION_UNAVAILABLE = 2,
  GPU_FRAME_EVENT_DISCARDED                = 3
} GPUFrameEventKind;

typedef uint32_t GPUFrameEventFlags;
enum {
  GPU_FRAME_EVENT_TARGET_TIME_VALID = 1u << 0,
  GPU_FRAME_EVENT_GPU_TIME_VALID    = 1u << 1
};

typedef struct GPUFrameEvent {
  uint64_t           frameId;
  double             targetPresentTime;
  double             gpuStartTime;
  double             gpuEndTime;
  double             presentedTime;
  GPUFrameEventKind  kind;
  GPUFrameEventFlags flags;
  GPUResult          result;
} GPUFrameEvent;

typedef void (*GPUFrameFeedbackFn)(const GPUFrameEvent *event, void *user);

typedef struct GPUFrameFeedbackCreateInfo {
  GPUChainedStruct   chain;
  GPUFrameFeedbackFn callback;
  void              *user;
  uint64_t           frameId;
} GPUFrameFeedbackCreateInfo;

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

/* opt in once per acquired frame, before scheduling presentation. enable
 * GPU_FEATURE_FRAME_FEEDBACK. frameId is an immutable caller-supplied cookie.
 * callbacks receive borrowed event values on backend or calling threads,
 * serialized per token. GPU completion and presentation have no guaranteed order.
 * times are host-clock seconds; manual frames have no target time. GPU times
 * cover the presenting command buffer, not separate uploads or other commands.
 * presentation unavailable is not proof of a dropped frame; a drawable that is
 * never shown may have no presentation event. cancel when no longer interested.
 * callbacks may run before submission returns; keep them short, do not reenter
 * the submitting queue or wait for GPU work, other callbacks or parent teardown.
 */
GPU_EXPORT
GPUResult
GPUCreateFrameFeedback(GPUFrame                         *frame,
                        const GPUFrameFeedbackCreateInfo *info,
                        GPUFrameFeedback                **outFeedback);

/* cancels new delivery and drains callbacks already entered before returning.
 * self-destruction from this token's callback is supported; that callback may
 * finish, but no later callback starts. consumes the public token reference.
 * the token does not keep the frame, swapchain or device alive.
 */
GPU_EXPORT
void
GPUDestroyFrameFeedback(GPUFrameFeedback *feedback);

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
