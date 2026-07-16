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

typedef enum GPUBindKind {
  GPUBindKindBuffer  = 0,
  GPUBindKindTexture = 1,
  GPUBindKindSampler = 2,
  GPUBindKindAccelerationStructure = 3,
  GPUBindKindCount
} GPUBindKind;

typedef struct GPUBindGroupBindingView {
  GPUBuffer      *buffer;
  GPUTextureView *textureView;
  GPUSampler     *sampler;
  GPUAccelerationStructureEXT *accelerationStructure;
  uint64_t        offset;
  uint64_t        size;
  GPUShaderStageFlags visibility;
  GPUBindingType  bindingType;
  uint32_t        binding;
  uint32_t        arrayIndex;
  uint32_t        kindIndex;
  GPUBindKind     kind;
  bool            hasDynamicOffset;
} GPUBindGroupBindingView;

typedef void (*GPUBindGroupBindingFn)(void *ctx,
                                      const GPUBindGroupBindingView *binding);

static GPU_INLINE bool
gpuBindGroupShadowMatches(GPUBindGroup  *boundGroup,
                          uint32_t       boundOffsetCount,
                          const uint32_t *boundOffsets,
                          GPUBindGroup  *group,
                          uint32_t       offsetCount,
                          const uint32_t *offsets) {
  if (boundGroup != group || boundOffsetCount != offsetCount ||
      offsetCount > GPU_ENCODER_DYNAMIC_OFFSET_SHADOW_CAPACITY) {
    return false;
  }

  if (offsetCount == 0u) {
    return true;
  }
  if (!offsets) {
    return false;
  }
  switch (offsetCount) {
    case 1u:
      return boundOffsets[0] == offsets[0];
    case 2u:
      return boundOffsets[0] == offsets[0] &&
             boundOffsets[1] == offsets[1];
    case 3u:
      return boundOffsets[0] == offsets[0] &&
             boundOffsets[1] == offsets[1] &&
             boundOffsets[2] == offsets[2];
    case 4u:
      return boundOffsets[0] == offsets[0] &&
             boundOffsets[1] == offsets[1] &&
             boundOffsets[2] == offsets[2] &&
             boundOffsets[3] == offsets[3];
    default:
      return memcmp(boundOffsets,
                    offsets,
                    offsetCount * sizeof(*offsets)) == 0;
  }
}

static GPU_INLINE void
gpuStoreBindGroupShadow(uint32_t       *boundOffsetCount,
                        uint32_t       *boundOffsets,
                        uint32_t        offsetCount,
                        const uint32_t *offsets) {
  if (offsetCount > GPU_ENCODER_DYNAMIC_OFFSET_SHADOW_CAPACITY ||
      (offsetCount > 0u && !offsets)) {
    *boundOffsetCount = UINT32_MAX;
    return;
  }

  switch (offsetCount) {
    case 0u:
      break;
    case 1u:
      boundOffsets[0] = offsets[0];
      break;
    case 2u:
      boundOffsets[0] = offsets[0];
      boundOffsets[1] = offsets[1];
      break;
    case 3u:
      boundOffsets[0] = offsets[0];
      boundOffsets[1] = offsets[1];
      boundOffsets[2] = offsets[2];
      break;
    case 4u:
      boundOffsets[0] = offsets[0];
      boundOffsets[1] = offsets[1];
      boundOffsets[2] = offsets[2];
      boundOffsets[3] = offsets[3];
      break;
    default:
      memcpy(boundOffsets, offsets, offsetCount * sizeof(*offsets));
      break;
  }
  *boundOffsetCount = offsetCount;
}

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
gpuForEachBindGroupEntry(GPUBindGroup            *group,
                         uint32_t                 entryCount,
                         const GPUBindGroupEntry *entries,
                         GPUBindGroupBindingFn    fn,
                         void                    *ctx);

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
const uint32_t *
gpuGetPipelineLayoutBackendBindings(GPUPipelineLayout *layout,
                                    uint32_t groupIndex,
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
bool
gpuBindGroupLayoutIsBindless(GPUBindGroupLayout *layout);

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
