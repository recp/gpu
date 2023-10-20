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

#ifndef backend_common_h
#define backend_common_h

#include "../common.h"
#include "../../include/gpu/api/gpudef.h"

#define GPUBackendTypeSuffix Vk
#define CONCAT_IMPL(a, b) a ## b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

/*#define GPUCalloc(X) calloc(1, sizeof(X) + sizeof(CONCAT(X, GPUBackendTypeSuffix))) */

#define GPUCalloc(X) ({                                                       \
  X*     x;                                                                   \
  size_t s;                                                                   \
  s        = sizeof(CONCAT(X, GPUBackendTypeSuffix));                         \
  x        = calloc(1, sizeof(X) + s);                                        \
  x->_priv = (void*)((char*)x + sizeof(X));                                   \
  x;                                                                          \
})

#define GPU__API()                                                            \
  ({                                                                          \
    GPUApi *api;                                                              \
                                                                              \
    if (!(api = gpuActiveGPUApi()))                                           \
      return NULL;                                                            \
                                                                              \
    api;                                                                      \
  })                                                                          \

#define GPU__DEFINE_DEFAULT_QUEUES_IF_NEEDED(nQueCI, queCI)                   \
  if (queCI == NULL) {                                                        \
    nQueCI = 3;                                                               \
    queCI  = (GPUCommandQueueCreateInfo[]){                                   \
      [0] = {                                                                 \
        .count = 1,                                                           \
        .flags = GPU_QUEUE_GRAPHICS_BIT | GPU_QUEUE_COMPUTE_BIT,              \
      },                                                                      \
      [1] = {                                                                 \
        .count = 1,                                                           \
        .flags = GPU_QUEUE_GRAPHICS_BIT                                       \
      },                                                                      \
      [2] = {                                                                 \
        .count = 1,                                                           \
        .flags = GPU_QUEUE_COMPUTE_BIT                                        \
      }                                                                       \
    };                                                                        \
  }                                                                           \

#endif /* backend_common_h */
