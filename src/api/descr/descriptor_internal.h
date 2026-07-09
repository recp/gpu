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

#ifndef gpu_descriptor_internal_h
#define gpu_descriptor_internal_h

#include "../../common.h"

struct GPUBindGroupLayout {
  void *_priv;
};

struct GPUBindGroup {
  void *_priv;
};

struct GPUPipelineLayout {
  void *_priv;
};

typedef struct GPUBindGroupBindingView {
  GPUBindStage stage;
  GPUBindKind  kind;
  uint32_t     binding;
  GPUBuffer   *buffer;
  GPUTextureView *textureView;
  GPUSampler  *sampler;
  uint64_t     offset;
  uint64_t     size;
  GPUShaderStageFlags visibility;
} GPUBindGroupBindingView;

typedef void (*GPUBindGroupBindingFn)(void *ctx,
                                      const GPUBindGroupBindingView *binding);

GPU_HIDE
int
gpuForEachBindGroupBindingWithDynamicOffsets(GPUPipelineLayout *pipelineLayout,
                                             uint32_t groupIndex,
                                             GPUBindGroup *group,
                                             uint32_t dynamicOffsetCount,
                                             const uint32_t *pDynamicOffsets,
                                             GPUBindGroupBindingFn fn,
                                             void *ctx);

GPU_HIDE
void
gpuGetPipelineLayoutPushConstants(GPUPipelineLayout *layout,
                                  uint32_t *outSizeBytes,
                                  GPUShaderStageFlags *outStages);

GPU_HIDE
int
gpuPipelineLayoutAcceptsBindGroup(GPUPipelineLayout *pipelineLayout,
                                  uint32_t groupIndex,
                                  GPUBindGroup *group);

GPU_HIDE
GPUBindGroupLayout *
gpuBindGroupGetLayout(GPUBindGroup *group);

GPU_HIDE
uint32_t
gpuBindGroupDynamicOffsetCount(GPUBindGroup *group);

GPU_HIDE
int
gpuPipelineLayoutMatchesShaderEntries(GPUPipelineLayout *pipelineLayout,
                                      const GPUShaderLibrary *library,
                                      const char * const *entryPoints,
                                      uint32_t entryPointCount,
                                      GPUShaderStageFlags fallbackStages,
                                      uint32_t *outRequiredGroupMask);

GPU_HIDE
int
gpuPipelineLayoutMaskIsBound(GPUPipelineLayout *pipelineLayout,
                             GPUBindGroupLayout * const *boundLayouts,
                             uint32_t boundLayoutCount,
                             uint32_t requiredGroupMask);

GPU_HIDE
int
gpuSetBindGroupLayoutBackendBindings(GPUBindGroupLayout *layout,
                                     const uint32_t *backendBindings,
                                     uint32_t count);

#endif /* gpu_descriptor_internal_h */
