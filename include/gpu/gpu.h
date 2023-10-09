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

#ifndef gpu_h
#define gpu_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "geometry.h"

#include "pixelformat.h"
#include "device.h"
#include "pipeline.h"
#include "library.h"
#include "vertex.h"
#include "depthstencil.h"
#include "pass.h"
#include "cmdqueue.h"
#include "buffer.h"
#include "resource.h"
#include "cmd-enc.h"
#include "stage-io.h"
#include "texture.h"
#include "shading/library.h"
#include "shading/pipeline.h"
#include "swapchain.h"
#include "frame.h"
#include "feature.h"
#include "descriptor.h"
#include "sampler.h"
#include "instance.h"
#include "surface.h"

typedef enum GPUBackend {
  GPU_BACKEND_NULL      = 0,
  GPU_BACKEND_METAL     = 1,
  GPU_BACKEND_VULKAN    = 2,
  GPU_BACKEND_DIRECTX12 = 3,
  GPU_BACKEND_OPENGL    = 4
} GPUBackend;

GPU_EXPORT
void
GPUSwitchGPUApi(GPUBackend backend);

GPU_EXPORT
void
gpuSwitchGPUApiAuto(void);

GPU_EXPORT
GPUBackend
gpuActiveGPUBackend(void);

#ifdef __cplusplus
}
#endif
#endif /* gpu_h */
