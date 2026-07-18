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

#ifndef gpu_bindgroup_h
#define gpu_bindgroup_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "cmdqueue.h"
#include "sampler.h"
#include "texture.h"

typedef struct GPUBuffer                   GPUBuffer;
typedef struct GPUDevice                   GPUDevice;
typedef struct GPUShaderLibrary            GPUShaderLibrary;
typedef struct GPUSampler                  GPUSampler;
typedef struct GPUBindGroupLayout          GPUBindGroupLayout;
typedef struct GPUBindGroup                GPUBindGroup;
typedef struct GPUPipelineLayout           GPUPipelineLayout;

typedef struct GPUAccelerationStructureEXT GPUAccelerationStructureEXT;
typedef struct GPUSamplerFeedbackMapEXT     GPUSamplerFeedbackMapEXT;

#ifndef GPU_RENDER_ENCODER_TYPES_DEFINED
#define GPU_RENDER_ENCODER_TYPES_DEFINED
typedef struct GPURenderPassEncoder GPURenderPassEncoder;
#endif

typedef struct GPUBindGroupLayoutEntry {
  GPUSamplerDesc      immutableSamplerDesc;
  GPUBindingType      bindingType;
  GPUShaderStageFlags visibility;
  uint32_t            binding;
  uint32_t            arrayCount;
  bool                hasDynamicOffset;
  bool                immutableSampler;
} GPUBindGroupLayoutEntry;

typedef struct GPUBindGroupLayoutCreateInfo {
  GPUChainedStruct               chain;
  const char                    *label;
  const GPUBindGroupLayoutEntry *pEntries;
  uint32_t                       entryCount;
} GPUBindGroupLayoutCreateInfo;

/* makes fixed-capacity bindings sparse and mutable. */
typedef struct GPUBindlessLayoutEXT {
  GPUChainedStruct          chain;
  const GPUBindGroupLayout *sourceLayout;
} GPUBindlessLayoutEXT;

typedef struct GPUBindGroupEntry {
  union {
    GPUTextureView              *textureView;
    GPUSampler                  *sampler;
    GPUAccelerationStructureEXT *accelerationStructure;
    GPUSamplerFeedbackMapEXT    *samplerFeedback;
    struct {
      GPUBuffer *buffer;
      uint64_t   offset;
      uint64_t   size;
    }                            buffer;
  };
  uint32_t                       binding;
  uint32_t                       arrayIndex; /* 0 for non-array. */
  GPUBindingType                 bindingType;
} GPUBindGroupEntry;

typedef struct GPUBindGroupCreateInfo {
  GPUChainedStruct         chain;
  const char              *label;
  GPUBindGroupLayout      *layout;
  const GPUBindGroupEntry *pEntries;
  uint32_t                 entryCount;
} GPUBindGroupCreateInfo;

typedef struct GPUShaderLayout {
  GPUPipelineLayout   *pipelineLayout;
  GPUBindGroupLayout **bindGroupLayouts;
  uint32_t             bindGroupLayoutCount;
} GPUShaderLayout;

typedef struct GPUPipelineLayoutCreateInfo {
  GPUChainedStruct    chain;
  const char         *label;
  GPUBindGroupLayout *const *ppBindGroupLayouts;
  uint32_t            bindGroupLayoutCount;
  uint32_t            pushConstantSizeBytes;
  GPUShaderStageFlags pushConstantStages;
} GPUPipelineLayoutCreateInfo;

GPU_EXPORT
GPUResult
GPUCreateBindGroupLayout(GPUDevice *device,
                         const GPUBindGroupLayoutCreateInfo *info,
                         GPUBindGroupLayout **outLayout);

GPU_EXPORT
const GPUBindGroupLayoutEntry *
GPUGetBindGroupLayoutEntries(const GPUBindGroupLayout *layout,
                             uint32_t                 *outCount);

GPU_EXPORT
void
GPUDestroyBindGroupLayout(GPUBindGroupLayout *layout);

GPU_EXPORT
GPUResult
GPUCreateBindGroup(GPUDevice *device,
                   const GPUBindGroupCreateInfo *info,
                   GPUBindGroup **outGroup);

GPU_EXPORT
GPUResult
GPUUpdateBindGroupEXT(GPUBindGroup            *group,
                      uint32_t                 entryCount,
                      const GPUBindGroupEntry *pEntries);

GPU_EXPORT
void
GPUDestroyBindGroup(GPUBindGroup *group);

GPU_EXPORT
GPUResult
GPUCreatePipelineLayout(GPUDevice *device,
                        const GPUPipelineLayoutCreateInfo *info,
                        GPUPipelineLayout **outLayout);

GPU_EXPORT
GPUResult
GPUCreateBindGroupLayoutsFromReflection(GPUDevice *device,
                                        const GPUShaderLibrary *library,
                                        uint32_t *inoutLayoutCount,
                                        GPUBindGroupLayout **outLayouts);

GPU_EXPORT
GPUResult
GPUCreatePipelineLayoutFromReflection(GPUDevice *device,
                                      const GPUShaderLibrary *library,
                                      uint32_t bindGroupLayoutCount,
                                      GPUBindGroupLayout * const *ppLayouts,
                                      GPUPipelineLayout **outLayout);

GPU_EXPORT
GPUResult
GPUCreateShaderLayout(GPUDevice *device,
                      const GPUShaderLibrary *library,
                      GPUShaderLayout **outLayout);

GPU_EXPORT
void
GPUDestroyShaderLayout(GPUShaderLayout *layout);

GPU_EXPORT
void
GPUDestroyPipelineLayout(GPUPipelineLayout *layout);

GPU_EXPORT
void
GPUBindRenderGroup(GPURenderPassEncoder *pass,
                   uint32_t groupIndex,
                   GPUBindGroup *group,
                   uint32_t dynamicOffsetCount,
                   const uint32_t *pDynamicOffsets);

#ifdef __cplusplus
}
#endif
#endif /* gpu_bindgroup_h */
