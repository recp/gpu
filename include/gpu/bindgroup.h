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
#include "buffer.h"
#include "cmd-enc.h"
#include "library.h"
#include "sampler.h"
#include "texture.h"

typedef struct GPUBindGroupLayout {
  void *_priv;
} GPUBindGroupLayout;

typedef struct GPUBindGroup {
  void *_priv;
} GPUBindGroup;

typedef enum GPUBindStage {
  GPUBindStageVertex = 1,
  GPUBindStageFragment = 2
} GPUBindStage;

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
