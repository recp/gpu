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

#ifndef gpu_frame_internal_h
#define gpu_frame_internal_h

#include "../common.h"

enum {
  GPU_FRAME_TARGET_READY   = 0, /* target and targetView set */
  GPU_FRAME_TARGET_PENDING = 1, /* a paced frame's image acquired at its first use (frameAcquire) */
  GPU_FRAME_TARGET_FAILED  = 2
};

struct GPUFrame {
  GPUDevice        *device;
  GPUSwapchain     *swapchain;
  void             *_priv;
  GPUTexture       *target;
  GPUTextureView   *targetView;
  void             *drawable;
  GPUFrameFeedback *_feedback;
  double            targetPresentTime;
  double            deadline;          /* a paced frame's, host seconds; zero for a manual frame */
  uint64_t          cpuEncodeStartTicks;
  uint64_t          cpuEncodeFrequency;
  uint32_t          transientFrameIndex;
  bool              transientFrameActive;
  bool              presentationScheduled;
  uint8_t           targetState;       /* GPU_FRAME_TARGET_*, set by the backend */
};

struct GPUFramePacer {
  GPUDevice     *device;
  GPUSwapchain  *swapchain;
  GPUFramePacerFn callback;
  void          *user;
  void          *_priv;
};

GPU_HIDE
GPUFrame*
frameBeginDrawable(GPUSwapchain *swapchain, void *drawable);

GPU_HIDE
bool
schedulePresent(GPUCommandBuffer *cmdb, GPUFrame *frame);

/* the target of a frame begun without one; false when it can't be had */
GPU_HIDE
bool
frameAcquire(GPUFrame *frame);

#endif /* gpu_frame_internal_h */
