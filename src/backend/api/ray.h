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

#ifndef gpu_api_ray_h
#define gpu_api_ray_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/ray.h>

typedef struct GPUApiRayQuery {
  GPUResult
  (*getSizes)(GPUDevice                                    *device,
              const GPUAccelerationStructureBuildInfoEXT  *info,
              GPUAccelerationStructureSizesEXT            *outSizes);

  GPUResult
  (*create)(GPUDevice                                    *device,
            const GPUAccelerationStructureCreateInfoEXT *info,
            GPUAccelerationStructureEXT                 *structure);

  void
  (*destroy)(GPUAccelerationStructureEXT *structure);

  GPUAccelerationStructurePassEncoderEXT *
  (*beginPass)(GPUCommandBuffer *cmdb, const char *label);

  GPUResult
  (*build)(GPUAccelerationStructurePassEncoderEXT     *pass,
           GPUAccelerationStructureEXT                *dst,
           const GPUAccelerationStructureBuildInfoEXT *info,
           GPUBuffer                                   *scratchBuffer,
           uint64_t                                     scratchOffset);

  void
  (*endPass)(GPUAccelerationStructurePassEncoderEXT *pass);
} GPUApiRayQuery;

#ifdef __cplusplus
}
#endif
#endif /* gpu_api_ray_h */
