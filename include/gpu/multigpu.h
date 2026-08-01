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

#ifndef gpu_multigpu_h
#define gpu_multigpu_h
#ifdef __cplusplus
extern "C" {
#endif

#include "buffer.h"
#include "barrier.h"
#include "memory.h"
#include "texture.h"

typedef struct GPUDeviceInteropEXT GPUDeviceInteropEXT;

typedef struct GPUSharedBufferBarrierEXT {
  GPUBuffer     *sourceBuffer;
  GPUBuffer     *destinationBuffer;
  uint64_t       offset;
  uint64_t       sizeBytes;
  GPUAccessMask  srcAccess;
  GPUAccessMask  dstAccess;
} GPUSharedBufferBarrierEXT;

typedef struct GPUSharedTextureBarrierEXT {
  GPUTexture    *sourceTexture;
  GPUTexture    *destinationTexture;
  GPUAccessMask  srcAccess;
  GPUAccessMask  dstAccess;
  uint32_t       baseMip;
  uint32_t       mipCount;
  uint32_t       baseLayer;
  uint32_t       layerCount;
} GPUSharedTextureBarrierEXT;

typedef struct GPUSharedBarrierBatchEXT {
  const GPUSharedBufferBarrierEXT  *pBufferBarriers;
  const GPUSharedTextureBarrierEXT *pTextureBarriers;
  GPUPipelineStageMask              srcStages;
  GPUPipelineStageMask              dstStages;
  uint32_t                          bufferBarrierCount;
  uint32_t                          textureBarrierCount;
} GPUSharedBarrierBatchEXT;

/* Devices outlive the zero-copy bridge and every handle created from it. */
GPU_EXPORT
GPUResult
GPUCreateDeviceInteropEXT(GPUDevice            *firstDevice,
                          GPUDevice            *secondDevice,
                          GPUDeviceInteropEXT **outInterop);

GPU_EXPORT
void
GPUDestroyDeviceInteropEXT(GPUDeviceInteropEXT *interop);

GPU_EXPORT
GPUResult
GPUGetSharedBufferMemoryRequirementsEXT(GPUDeviceInteropEXT       *interop,
                                        const GPUBufferCreateInfo *firstInfo,
                                        const GPUBufferCreateInfo *secondInfo,
                                        GPUMemoryRequirements     *outRequirements);

/* Returns ordinary device-owned handles over one native allocation. */
GPU_EXPORT
GPUResult
GPUCreateSharedBufferEXT(GPUDeviceInteropEXT       *interop,
                         const GPUBufferCreateInfo *firstInfo,
                         const GPUBufferCreateInfo *secondInfo,
                         GPUBuffer                **outFirstBuffer,
                         GPUBuffer                **outSecondBuffer);

GPU_EXPORT
GPUResult
GPUGetSharedTextureMemoryRequirementsEXT(GPUDeviceInteropEXT        *interop,
                                         const GPUTextureCreateInfo *firstInfo,
                                         const GPUTextureCreateInfo *secondInfo,
                                         GPUMemoryRequirements      *outRequirements);

GPU_EXPORT
GPUResult
GPUCreateSharedTextureEXT(GPUDeviceInteropEXT        *interop,
                          const GPUTextureCreateInfo *firstInfo,
                          const GPUTextureCreateInfo *secondInfo,
                          GPUTexture                **outFirstTexture,
                          GPUTexture                **outSecondTexture);

GPU_EXPORT
GPUResult
GPUCreateSharedSemaphoreEXT(GPUDeviceInteropEXT          *interop,
                            const GPUSemaphoreCreateInfo *info,
                            GPUSemaphore                **outFirstSemaphore,
                            GPUSemaphore                **outSecondSemaphore);

/* Record release before signaling the source device's shared semaphore. */
GPU_EXPORT
GPUResult
GPUEncodeSharedReleaseEXT(GPUDeviceInteropEXT           *interop,
                          GPUCommandBuffer               *cmdb,
                          const GPUSharedBarrierBatchEXT *barriers);

/* Record acquire after the destination waits on its shared semaphore. */
GPU_EXPORT
GPUResult
GPUEncodeSharedAcquireEXT(GPUDeviceInteropEXT           *interop,
                          GPUCommandBuffer               *cmdb,
                          const GPUSharedBarrierBatchEXT *barriers);

#ifdef __cplusplus
}
#endif
#endif /* gpu_multigpu_h */
