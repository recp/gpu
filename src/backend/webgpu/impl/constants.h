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

#ifndef gpu_webgpu_constants_h
#define gpu_webgpu_constants_h

#include "../../../api/constants_internal.h"

static inline uint32_t
webgpu_pipelineConstants(const GPUChainedStruct *chain,
                         WGPUConstantEntry      *entries,
                         char                  (*ids)[11]) {
  const GPUPipelineConstants *constants;
  const GPUConstant          *constant;
  uint32_t                    i;

  constants = pipelineConstants(chain);

  if (!constants) {
    return 0u;
  }

  memset(entries, 0, (size_t)constants->constantCount * sizeof(entries[0]));

  for (i = 0u; i < constants->constantCount; i++) {
    constant = &constants->pConstants[i];
    snprintf(ids[i], sizeof(ids[i]), "%u", constant->id);
    entries[i].key = webgpuString(ids[i]);

    switch (constant->type) {
      case GPU_CONSTANT_BOOL: entries[i].value = constant->value.boolean ? 1.0 : 0.0; break;
      case GPU_CONSTANT_I32:  entries[i].value = constant->value.i32; break;
      case GPU_CONSTANT_U32:  entries[i].value = constant->value.u32; break;
      case GPU_CONSTANT_F32:  entries[i].value = constant->value.f32; break;
      default: break;
    }
  }

  return constants->constantCount;
}

#endif /* gpu_webgpu_constants_h */
