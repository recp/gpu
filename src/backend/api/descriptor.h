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

#ifndef gpu_gpudef_descriptor_h
#define gpu_gpudef_descriptor_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/common.h>

struct GPUBindGroup;
struct GPUBindGroupEntry;
struct GPUBindGroupLayout;
struct GPUComputePassEncoder;
struct GPUDevice;
struct GPUPipelineLayout;
struct GPURenderPassEncoder;

/* Covers the portable v1 dynamic-buffer limits without heap storage. */
enum {
  GPU_ENCODER_DYNAMIC_OFFSET_SHADOW_CAPACITY = 12u
};

typedef uint32_t GPUDynamicOffsetShadow[
  GPU_ENCODER_DYNAMIC_OFFSET_SHADOW_CAPACITY
];

typedef struct GPUApiDescriptor {
  GPUResult
  (*createBindGroupLayout)(struct GPUDevice          *device,
                           struct GPUBindGroupLayout *layout);

  void
  (*destroyBindGroupLayout)(struct GPUBindGroupLayout *layout);

  GPUResult
  (*createPipelineLayout)(struct GPUDevice         *device,
                          struct GPUPipelineLayout *layout);

  void
  (*destroyPipelineLayout)(struct GPUPipelineLayout *layout);

  GPUResult
  (*createBindGroup)(struct GPUDevice    *device,
                     struct GPUBindGroup *group);

  bool
  (*updateBindGroup)(struct GPUBindGroup            *group,
                     uint32_t                        entryCount,
                     const struct GPUBindGroupEntry *entries);

  void
  (*destroyBindGroup)(struct GPUBindGroup *group);

  bool
  (*bindRenderGroup)(struct GPURenderPassEncoder *pass,
                     struct GPUPipelineLayout       *pipelineLayout,
                     uint32_t                        groupIndex,
                     struct GPUBindGroup            *group,
                     uint32_t                        dynamicOffsetCount,
                     const uint32_t                 *dynamicOffsets);

  bool
  (*bindComputeGroup)(struct GPUComputePassEncoder *pass,
                      struct GPUPipelineLayout     *pipelineLayout,
                      uint32_t                      groupIndex,
                      struct GPUBindGroup          *group,
                      uint32_t                      dynamicOffsetCount,
                      const uint32_t               *dynamicOffsets);
} GPUApiDescriptor;

#ifdef __cplusplus
}
#endif
#endif /* gpu_gpudef_descriptor_h */
