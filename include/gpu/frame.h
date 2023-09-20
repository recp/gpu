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
#include "buffer.h"

typedef struct GPUFrame {
  void       *_priv;
  GPUTexture *target;
  void       *drawable;
  // Other members...
} GPUFrame;

GPU_EXPORT
GPUFrame*
GPUBeginFrame(GPUSwapChain* swapchain);

GPU_EXPORT
void
GPUEndFrame(GPUFrame* frame);

GPU_EXPORT
void
GPUFinishFrame(GPUCommandBuffer * __restrict cmdb, GPUFrame * __restrict frame);

#ifdef __cplusplus
}
#endif
#endif /* gpu_frame_h */
