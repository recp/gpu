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

#include "../common.h"
#include "instance_internal.h"

GPU_EXPORT
GPUResult
GPUCreateInstance(const GPUInstanceCreateInfo * __restrict info,
                  GPUInstance                ** __restrict outInstance) {
  GPUInstanceCreateInfo defaultInfo;
  GPUApi               *api;

  if (!outInstance) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outInstance = NULL;

  if (!info) {
    memset(&defaultInfo, 0, sizeof(defaultInfo));
    defaultInfo.chain.sType = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    defaultInfo.chain.structSize = sizeof(defaultInfo);
    defaultInfo.preferredBackend = GPU_BACKEND_DEFAULT;
    info = &defaultInfo;
  }

  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  api = gpuApiForBackend(info->preferredBackend);
  if (!api || !api->instance.createInstance) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outInstance = api->instance.createInstance(api, info);
  if (!*outInstance) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  gpuSetActiveGPUApi(api);

  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyInstance(GPUInstance *instance) {
  GPUApi *api;

  if (!instance) {
    return;
  }

  api = gpuActiveGPUApi();
  if (api && api->instance.destroyInstance) {
    api->instance.destroyInstance(api, instance);
    return;
  }

  free(instance->_priv);
  free(instance);
}
