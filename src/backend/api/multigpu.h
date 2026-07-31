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

#ifndef gpu_gpudef_multigpu_h
#define gpu_gpudef_multigpu_h

#include <gpu/gpu.h>

typedef struct GPUApiMultiGPU {
  GPUResult
  (*createInterop)(GPUDevice           *firstDevice,
                   GPUDevice           *secondDevice,
                   GPUDeviceInteropEXT *interop);

  void
  (*destroyInterop)(GPUDeviceInteropEXT *interop);

  GPUResult
  (*getBufferRequirements)(GPUDeviceInteropEXT       *interop,
                           const GPUBufferCreateInfo *firstInfo,
                           const GPUBufferCreateInfo *secondInfo,
                           GPUMemoryRequirements     *outRequirements);

  GPUResult
  (*createBuffer)(GPUDeviceInteropEXT       *interop,
                  const GPUBufferCreateInfo *firstInfo,
                  const GPUBufferCreateInfo *secondInfo,
                  GPUBuffer                **outFirstBuffer,
                  GPUBuffer                **outSecondBuffer);

  GPUResult
  (*getTextureRequirements)(GPUDeviceInteropEXT        *interop,
                            const GPUTextureCreateInfo *firstInfo,
                            const GPUTextureCreateInfo *secondInfo,
                            GPUMemoryRequirements      *outRequirements);

  GPUResult
  (*createTexture)(GPUDeviceInteropEXT        *interop,
                   const GPUTextureCreateInfo *firstInfo,
                   const GPUTextureCreateInfo *secondInfo,
                   GPUTexture                **outFirstTexture,
                   GPUTexture                **outSecondTexture);

  GPUResult
  (*createSemaphore)(GPUDeviceInteropEXT          *interop,
                     const GPUSemaphoreCreateInfo *info,
                     GPUSemaphore                 *firstSemaphore,
                     GPUSemaphore                 *secondSemaphore);
} GPUApiMultiGPU;

#endif /* gpu_gpudef_multigpu_h */
