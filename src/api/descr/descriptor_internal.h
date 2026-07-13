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
  GPUDevice *_device;
  void      *_native;
  void      *_priv;
};

struct GPUBindGroup {
  GPUDevice *_device;
  void      *_native;
  void      *_priv;
  uint32_t   _refCount;
};

struct GPUPipelineLayout {
  GPUDevice *_device;
  void      *_native;
  void      *_priv;
};

typedef struct GPUBindGroupBindingView {
  GPUBuffer      *buffer;
  GPUTextureView *textureView;
  GPUSampler     *sampler;
  uint64_t        offset;
  uint64_t        size;
  GPUShaderStageFlags visibility;
  GPUBindingType  bindingType;
  uint32_t        binding;
  uint32_t        arrayIndex;
  GPUBindStage    stage;
  GPUBindKind     kind;
  bool            hasDynamicOffset;
} GPUBindGroupBindingView;

typedef void (*GPUBindGroupBindingFn)(void *ctx,
                                      const GPUBindGroupBindingView *binding);

GPU_HIDE
GPUResult
gpuInitBindGroupCacheDevice(GPUDevice *device);

GPU_HIDE
void
gpuDestroyBindGroupCacheDevice(GPUDevice *device);

GPU_HIDE
int
gpuForEachBindGroupBinding(GPUBindGroup *group,
                           GPUBindGroupBindingFn fn,
                           void *ctx);

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
GPUBindGroupLayout * const *
gpuGetPipelineLayoutGroups(GPUPipelineLayout *layout, uint32_t *outCount);

GPU_HIDE
uint32_t
gpuPipelineLayoutBackendSlotMask(GPUPipelineLayout  *layout,
                                 GPUBindKind         kind,
                                 GPUShaderStageFlags stages);

GPU_HIDE
const uint32_t *
gpuGetBindGroupLayoutBackendBindings(GPUBindGroupLayout *layout,
                                     uint32_t *outCount);

GPU_HIDE
int
gpuPipelineLayoutAcceptsBindGroup(GPUPipelineLayout *pipelineLayout,
                                  uint32_t groupIndex,
                                  GPUBindGroup *group);

GPU_HIDE
GPUBindGroupLayout *
gpuBindGroupGetLayout(GPUBindGroup *group);

GPU_HIDE
GPUDevice *
gpuBindGroupGetDevice(GPUBindGroup *group);

GPU_HIDE
uint32_t
gpuBindGroupDynamicOffsetCount(GPUBindGroup *group);

GPU_HIDE
int
gpuValidateBindGroupDynamicOffsets(GPUPipelineLayout *pipelineLayout,
                                   uint32_t groupIndex,
                                   GPUBindGroup *group,
                                   uint32_t dynamicOffsetCount,
                                   const uint32_t *dynamicOffsets);

GPU_HIDE
int
gpuPipelineLayoutMatchesShaderEntries(GPUPipelineLayout *pipelineLayout,
                                      const GPUShaderLibrary *library,
                                      const char * const *entryPoints,
                                      uint32_t entryPointCount,
                                      GPUShaderStageFlags fallbackStages,
                                      uint32_t *outRequiredGroupMask);

#if GPU_BUILD_WITH_VALIDATION
GPU_HIDE
int
gpuPipelineLayoutMaskIsBound(GPUPipelineLayout *pipelineLayout,
                             GPUBindGroupLayout * const *boundLayouts,
                             uint32_t boundLayoutCount,
                             uint32_t requiredGroupMask);
#endif

#endif /* gpu_descriptor_internal_h */
