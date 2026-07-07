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

typedef struct GPUBuffer GPUBuffer;
typedef struct GPURenderCommandEncoder GPURenderCommandEncoder;
typedef struct GPUSampler GPUSampler;
typedef struct GPUTexture GPUTexture;

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
  GPUBindStage stage;
  GPUBindKind kind;
  uint32_t binding;
} GPUBindGroupLayoutEntry;

typedef struct GPUBindGroupEntry {
  GPUBindStage stage;
  uint32_t binding;
  GPUBindKind kind;
  GPUBuffer *buffer;
  GPUTexture *texture;
  GPUSampler *sampler;
  size_t offset;
} GPUBindGroupEntry;

GPU_EXPORT
int
GPUCreateBindGroupLayout(const GPUBindGroupLayoutEntry *entries,
                         uint32_t count,
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
int
GPUCreateBindGroup(GPUBindGroupLayout *layout,
                   const GPUBindGroupEntry *entries,
                   uint32_t count,
                   GPUBindGroup **outGroup);

GPU_EXPORT
void
GPUDestroyBindGroup(GPUBindGroup *group);

GPU_EXPORT
void
GPUBindRenderGroup(GPURenderCommandEncoder *rce, GPUBindGroup *group);

#ifdef __cplusplus
}
#endif
#endif /* gpu_bindgroup_h */
