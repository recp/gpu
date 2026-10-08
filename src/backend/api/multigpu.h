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

typedef enum ExternalMemoryType {
  GPU_EXTERNAL_MEMORY_NONE,
  GPU_EXTERNAL_MEMORY_OPAQUE_FD,
  GPU_EXTERNAL_MEMORY_OPAQUE_WIN32,
  GPU_EXTERNAL_MEMORY_D3D12_RESOURCE
} ExternalMemoryType;

typedef enum ExternalSemaphoreType {
  GPU_EXTERNAL_SEMAPHORE_NONE,
  GPU_EXTERNAL_SEMAPHORE_OPAQUE_FD,
  GPU_EXTERNAL_SEMAPHORE_OPAQUE_WIN32,
  GPU_EXTERNAL_SEMAPHORE_D3D12_FENCE,
  GPU_EXTERNAL_SEMAPHORE_TIMELINE_FD,
  GPU_EXTERNAL_SEMAPHORE_TIMELINE_WIN32
} ExternalSemaphoreType;

typedef union ExternalHandle {
  void *win32;
  int   fd;
} ExternalHandle;

typedef struct ExternalMemoryExport {
  ExternalHandle        handle;
  uint64_t              sizeBytes;
  ExternalMemoryType    type;
  bool                  dedicated;
} ExternalMemoryExport;

typedef struct ExternalSemaphoreExport {
  ExternalHandle           handle;
  ExternalSemaphoreType    type;
} ExternalSemaphoreExport;

typedef struct ApiMultiGPU {
  GPUResult (*createInterop)(GPUDevice *firstDevice, GPUDevice *secondDevice, GPUDeviceInteropEXT *interop);

  void (*destroyInterop)(GPUDeviceInteropEXT *interop);

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

  GPUResult
  (*encodeRelease)(GPUDeviceInteropEXT            *interop,
                   GPUCommandBuffer               *cmdb,
                   const GPUSharedBarrierBatchEXT *barriers);

  GPUResult
  (*encodeAcquire)(GPUDeviceInteropEXT            *interop,
                   GPUCommandBuffer               *cmdb,
                   const GPUSharedBarrierBatchEXT *barriers);

  GPUResult
  (*getExternalBufferRequirements)(GPUDevice                 *device,
                                   const GPUBufferCreateInfo *info,
                                   GPUMemoryRequirements     *outRequirements);

  GPUResult
  (*createExternalBuffer)(GPUDevice                 *device,
                          const GPUBufferCreateInfo *info,
                          GPUBuffer                **outBuffer,
                          ExternalMemoryExport      *outExport);

  GPUResult
  (*getExternalTextureRequirements)(GPUDevice                  *device,
                                    const GPUTextureCreateInfo *info,
                                    GPUMemoryRequirements      *outRequirements);

  GPUResult
  (*createExternalTexture)(GPUDevice                  *device,
                           const GPUTextureCreateInfo *info,
                           GPUTexture                **outTexture,
                           ExternalMemoryExport       *outExport);

  GPUResult
  (*createExternalSemaphore)(GPUDevice                    *device,
                             const GPUSemaphoreCreateInfo *info,
                             GPUSemaphore                 *semaphore,
                             ExternalSemaphoreExport      *outExport);

  GPUResult (*encodeExternalRelease)(GPUCommandBuffer *cmdb, const GPUSharedBarrierBatchEXT *barriers);

  GPUResult (*encodeExternalAcquire)(GPUCommandBuffer *cmdb, const GPUSharedBarrierBatchEXT *barriers);
} ApiMultiGPU;

#endif /* gpu_gpudef_multigpu_h */
