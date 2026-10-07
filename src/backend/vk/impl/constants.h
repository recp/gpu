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

#ifndef gpu_vk_constants_h
#define gpu_vk_constants_h

#include "../../../api/constants_internal.h"

static inline void
vk_pipelineConstants(const GPUChainedStruct   *chain,
                     VkSpecializationInfo     *info,
                     VkSpecializationMapEntry *entries,
                     uint32_t                 *data) {
  const GPUPipelineConstants *constants;
  const GPUConstant          *constant;
  uint32_t                    i;

  constants = gpuPipelineConstants(chain);

  if (!constants) {
    return;
  }

  for (i = 0u; i < constants->constantCount; i++) {
    constant              = &constants->pConstants[i];
    entries[i].constantID = constant->id;
    entries[i].offset     = i * sizeof(data[0]);
    entries[i].size       = sizeof(data[0]);

    if (constant->type == GPU_CONSTANT_BOOL) {
      data[i] = constant->value.boolean ? 1u : 0u;
    } else {
      memcpy(&data[i], &constant->value, sizeof(data[i]));
    }
  }

  info->mapEntryCount = constants->constantCount;
  info->pMapEntries   = entries;
  info->dataSize      = (size_t)constants->constantCount * sizeof(data[0]);
  info->pData         = data;
}

#endif /* gpu_vk_constants_h */
