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

#ifndef gpu_gpudef_device_h
#define gpu_gpudef_device_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/common.h>
#include <gpu/gpu.h>

struct GPUApi;
struct GPUInstance;

typedef struct GPUQueueCreateInfo {
  GPUQueueFlagBits flags;
  GPUQueueFlagBits optionalFlags;
  uint32_t         count;
} GPUQueueCreateInfo;

typedef enum GPUBackendSubgroupOperationFlagBits {
  GPU_BACKEND_SUBGROUP_OPERATION_BASIC_BIT            = 1u << 0,
  GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_BIT          = 1u << 1,
  GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_RELATIVE_BIT = 1u << 2
} GPUBackendSubgroupOperationFlagBits;

typedef uint32_t GPUBackendSubgroupOperationFlags;

typedef struct GPUApiDevice {
  GPUAdapter*
  (*getAvailableAdapters)(GPUInstance   * __restrict inst,
                          uint32_t maxNumberOfItems);

  GPUAdapter*
  (*selectAdapter)(GPUInstance * __restrict inst,
                   GPUAdapter  * __restrict adapters);

  void
  (*destroyAdapter)(GPUAdapter * __restrict adapter);

  GPUResult
  (*getAdapterProperties)(const GPUAdapter     * __restrict adapter,
                          GPUAdapterProperties * __restrict outProps);

  bool
  (*supportsFeature)(const GPUAdapter * __restrict adapter,
                     GPUFeature feature);

  bool
  (*supportsSubgroupOperations)(
    const GPUAdapter                 * __restrict adapter,
    GPUShaderStageFlags                           stage,
    GPUBackendSubgroupOperationFlags              operations
  );

  void
  (*getLimits)(const GPUAdapter * __restrict adapter,
               GPULimits       * __restrict outLimits);

  void
  (*getFormatCapabilities)(const GPUAdapter      * __restrict adapter,
                           GPUFormat              format,
                           GPUFormatCapabilities * __restrict outCaps);

  GPUResult
  (*getSubgroupMatrixProperties)(
    const GPUAdapter               * __restrict adapter,
    uint32_t                       * __restrict inoutPropertyCount,
    GPUSubgroupMatrixPropertiesEXT * __restrict outProperties
  );

  GPUDevice* (*createDevice)(GPUAdapter             * __restrict adapter,
                             GPUQueueCreateInfo       queCI[],
                             uint32_t                 nQueCI,
                             uint64_t                 enabledFeatureMask);

  GPUResult (*waitIdle)(GPUDevice * __restrict device);
  void (*destroyDevice)(GPUDevice * __restrict device);
} GPUApiDevice;

#ifdef __cplusplus
}
#endif
#endif /* gpu_gpudef_device_h */
