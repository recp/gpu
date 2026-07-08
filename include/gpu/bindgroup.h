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

typedef enum GPUBindStage {
  GPUBindStageVertex = 1,
  GPUBindStageFragment = 2,
  GPUBindStageCompute = 3
} GPUBindStage;

#define GPU_BIND_GROUP_LAYOUT_USL_INFO_VERSION 3u
#define GPU_BIND_GROUP_LAYOUT_USL_ENTRY_NAME_MAX 128u

typedef struct GPUBindGroupLayoutUSLInfo {
  uint32_t abiVersion;
  uint32_t runtimeInfoVersion;
  uint32_t bytecodeVersion;
  GPUBindStage stage;
  uint32_t resourceBindingCount;
  uint32_t capabilityRequirementCount;
  uint32_t capabilityRequirementTotalCount;
  uint32_t capabilityRequirementFlags;
  uint32_t reserved0;
  uint64_t bytecodeSize;
  uint64_t bytecodeDataSize;
  uint64_t bytecodeContentHash;
  uint64_t capabilityRequirementHash;
  uint32_t entryTargetInfoVersion;
  uint32_t targetBackend;
  uint32_t targetSupported;
  uint32_t targetSupportStatus;
  uint32_t targetAtomCount;
  uint32_t targetAtomTotalCount;
  uint32_t targetInfoFlags;
  uint32_t reserved1;
  uint64_t targetAtomHash;
  char entryPointName[GPU_BIND_GROUP_LAYOUT_USL_ENTRY_NAME_MAX];
} GPUBindGroupLayoutUSLInfo;

typedef struct GPUBindGroupLayout {
  void *_priv;
  GPUBindGroupLayoutUSLInfo _uslInfo;
} GPUBindGroupLayout;

typedef struct GPUBindGroup {
  void *_priv;
} GPUBindGroup;

typedef enum GPUBindKind {
  GPUBindKindBuffer = 0,
  GPUBindKindTexture = 1,
  GPUBindKindSampler = 2
} GPUBindKind;

typedef struct GPUBindGroupLayoutEntry {
  uint32_t binding;
  GPUBindingType bindingType;
  GPUShaderStageFlags visibility;
  uint32_t arrayCount;
  bool hasDynamicOffset;
  bool immutableSampler;
  GPUSamplerDesc immutableSamplerDesc;

  GPUBindStage stage;
  GPUBindKind kind;
} GPUBindGroupLayoutEntry;

typedef struct GPUBindGroupLayoutCreateInfo {
  GPUChainedStruct chain;
  const char *label;
  uint32_t entryCount;
  const GPUBindGroupLayoutEntry *pEntries;
} GPUBindGroupLayoutCreateInfo;

typedef struct GPUBindGroupEntry {
  uint32_t binding;
  GPUBindingType bindingType;

  struct {
    GPUBuffer *buffer;
    uint64_t offset;
    uint64_t size;
  } buffer;

  GPUTextureView *textureView;
  GPUSampler *sampler;

  GPUBindStage stage;
  GPUBindKind kind;
} GPUBindGroupEntry;

typedef struct GPUBindGroupCreateInfo {
  GPUChainedStruct chain;
  const char *label;
  GPUBindGroupLayout *layout;
  uint32_t entryCount;
  const GPUBindGroupEntry *pEntries;
} GPUBindGroupCreateInfo;

typedef struct GPUPipelineLayout {
  void *_priv;
} GPUPipelineLayout;

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
int
GPUCreateBindGroupLayoutFromUSLBytecode(const void *bytecodeData,
                                        uint64_t bytecodeSize,
                                        const char *entryPointName,
                                        GPUBindGroupLayout **outLayout);

GPU_EXPORT
const GPUBindGroupLayoutEntry *
GPUGetBindGroupLayoutEntries(GPUBindGroupLayout *layout, uint32_t *outCount);

GPU_EXPORT
int
GPUGetBindGroupLayoutUSLInfo(GPUBindGroupLayout *layout,
                             GPUBindGroupLayoutUSLInfo *outInfo);

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
                   uint32_t setIndex,
                   GPUBindGroup *group,
                   uint32_t dynamicOffsetCount,
                   const uint32_t *pDynamicOffsets);

#ifdef __cplusplus
}
#endif
#endif /* gpu_bindgroup_h */
