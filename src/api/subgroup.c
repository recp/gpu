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
#include "adapter_internal.h"

GPU_EXPORT
GPUResult
GPUGetSubgroupMatrixPropertiesEXT(
  const GPUAdapter               *adapter,
  uint32_t                       *inoutPropertyCount,
  GPUSubgroupMatrixPropertiesEXT *outProperties) {
  GPUApi *api;

  if (!adapter || !inoutPropertyCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  api = gpuAdapterApi(adapter);
  if (!api || !api->device.getSubgroupMatrixProperties) {
    *inoutPropertyCount = 0u;
    return GPU_ERROR_UNSUPPORTED;
  }

  return api->device.getSubgroupMatrixProperties(adapter,
                                                 inoutPropertyCount,
                                                 outProperties);
}
