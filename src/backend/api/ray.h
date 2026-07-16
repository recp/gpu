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

typedef struct GPUApiRayTracing {
  GPUResult
  (*createPipeline)(GPUDevice                                 *device,
                    const GPURayTracingPipelineCreateInfoEXT  *info,
                    GPURayTracingPipelineEXT                  *pipeline);

  void
  (*destroyPipeline)(GPURayTracingPipelineEXT *pipeline);

  GPUResult
  (*createShaderTable)(GPUDevice                         *device,
                       const GPUShaderTableCreateInfoEXT *info,
                       GPUShaderTableEXT                 *table);

  void
  (*destroyShaderTable)(GPUShaderTableEXT *table);

  GPURayTracingPassEncoderEXT *
  (*beginPass)(GPUCommandBuffer *cmdb, const char *label);

  void
  (*bindPipeline)(GPURayTracingPassEncoderEXT *pass,
                  GPURayTracingPipelineEXT    *pipeline);

  bool
  (*bindGroup)(GPURayTracingPassEncoderEXT *pass,
               GPUPipelineLayout           *pipelineLayout,
               uint32_t                     groupIndex,
               GPUBindGroup                *group,
               uint32_t                     dynamicOffsetCount,
               const uint32_t              *dynamicOffsets);

  void
  (*dispatch)(GPURayTracingPassEncoderEXT *pass,
              GPUShaderTableEXT           *table,
              uint32_t                     width,
              uint32_t                     height,
              uint32_t                     depth);

  void
  (*endPass)(GPURayTracingPassEncoderEXT *pass);
} GPUApiRayTracing;

#ifdef __cplusplus
}
#endif
#endif /* gpu_api_ray_h */
