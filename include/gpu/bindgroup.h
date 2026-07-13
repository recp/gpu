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

typedef struct GPUBuffer GPUBuffer;
typedef struct GPUDevice GPUDevice;
typedef struct GPULibrary GPUShaderLibrary;
typedef struct GPUSampler GPUSampler;

#ifndef GPU_RENDER_ENCODER_TYPES_DEFINED
#define GPU_RENDER_ENCODER_TYPES_DEFINED
typedef struct GPURenderCommandEncoder GPURenderCommandEncoder;
typedef GPURenderCommandEncoder GPURenderPassEncoder;
#endif

typedef struct GPUBindGroupLayout GPUBindGroupLayout;

typedef struct GPUBindGroup GPUBindGroup;

typedef struct GPUBindGroupLayoutEntry {
  uint32_t binding;
  GPUBindingType bindingType;
  GPUShaderStageFlags visibility;
  uint32_t arrayCount;
  bool hasDynamicOffset;
  bool immutableSampler;
  GPUSamplerDesc immutableSamplerDesc;
} GPUBindGroupLayoutEntry;

typedef struct GPUBindGroupLayoutCreateInfo {
  GPUChainedStruct chain;
  const char *label;
  uint32_t entryCount;
  const GPUBindGroupLayoutEntry *pEntries;
} GPUBindGroupLayoutCreateInfo;

typedef struct GPUBindGroupEntry {
  uint32_t binding;
  uint32_t arrayIndex; /* 0 for non-array. */
  GPUBindingType bindingType;

  struct {
    GPUBuffer *buffer;
    uint64_t offset;
    uint64_t size;
  } buffer;

  GPUTextureView *textureView;
  GPUSampler *sampler;
} GPUBindGroupEntry;

typedef struct GPUBindGroupCreateInfo {
  GPUChainedStruct chain;
  const char *label;
  GPUBindGroupLayout *layout;
  uint32_t entryCount;
  const GPUBindGroupEntry *pEntries;
} GPUBindGroupCreateInfo;

typedef struct GPUPipelineLayout GPUPipelineLayout;

typedef struct GPUShaderLayout {
  GPUPipelineLayout *pipelineLayout;
  uint32_t bindGroupLayoutCount;
  GPUBindGroupLayout **bindGroupLayouts;
} GPUShaderLayout;

typedef struct GPUPipelineLayoutCreateInfo {
  GPUChainedStruct chain;
  const char *label;
  uint32_t bindGroupLayoutCount;
  GPUBindGroupLayout * const *ppBindGroupLayouts;
  uint32_t pushConstantSizeBytes;
  GPUShaderStageFlags pushConstantStages;
} GPUPipelineLayoutCreateInfo;

GPU_EXPORT
GPUResult
GPUCreateBindGroupLayout(GPUDevice *device,
                         const GPUBindGroupLayoutCreateInfo *info,
                         GPUBindGroupLayout **outLayout);

GPU_EXPORT
const GPUBindGroupLayoutEntry *
GPUGetBindGroupLayoutEntries(GPUBindGroupLayout *layout, uint32_t *outCount);

GPU_EXPORT
void
GPUDestroyBindGroupLayout(GPUBindGroupLayout *layout);

GPU_EXPORT
GPUResult
GPUCreateBindGroup(GPUDevice *device,
                   const GPUBindGroupCreateInfo *info,
                   GPUBindGroup **outGroup);

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
