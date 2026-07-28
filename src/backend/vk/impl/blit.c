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

#include "../common.h"
#include "../../../api/pass/blit_internal.h"

#include "shaders/blit_float_array_spirv.inc"
#include "shaders/blit_float_spirv.inc"
#include "shaders/blit_sint_spirv.inc"
#include "shaders/blit_uint_spirv.inc"

static const GPUBlitShaderSet vk_blitTextureShaders = {
  .filteringFloat = {
    .data   = gpu_blitFloatSPIRV,
    .size   = gpu_blitFloatSPIRV_len,
    .binary = true
  },
  .filteringFloatArray = {
    .data   = gpu_blitFloatArraySPIRV,
    .size   = gpu_blitFloatArraySPIRV_len,
    .binary = true
  },
  .unfilterableFloat = {
    .data   = gpu_blitFloatSPIRV,
    .size   = gpu_blitFloatSPIRV_len,
    .binary = true
  },
  .unsignedInteger = {
    .data   = gpu_blitUintSPIRV,
    .size   = gpu_blitUintSPIRV_len,
    .binary = true
  },
  .signedInteger = {
    .data   = gpu_blitSintSPIRV,
    .size   = gpu_blitSintSPIRV_len,
    .binary = true
  }
};

GPU_HIDE
void
vk_blitTextureRenderFallback(GPUCommandBuffer         *cmdb,
                             const GPUTextureBlitInfo *info) {
  gpuBlitTextureRenderFallback(cmdb, info, &vk_blitTextureShaders);
}
