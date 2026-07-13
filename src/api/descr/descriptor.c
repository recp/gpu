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

#include "../../common.h"
#include "../../backend/mt/binding_limits.h"
#include "../buffer_internal.h"
#include "../device_internal.h"
#include "../library_internal.h"
#include "../render/rce_internal.h"
#include "../texture_internal.h"
#include "descriptor_internal.h"

#if !defined(_WIN32) && !defined(WIN32)
#  include <pthread.h>
#endif

#define GPU_PUSH_CONSTANT_MAX_SIZE_BYTES 4096u

enum {
  GPU_BIND_GROUP_CACHE_CAPACITY = 256u,
  GPU_BIND_GROUP_CACHE_MASK     = GPU_BIND_GROUP_CACHE_CAPACITY - 1u
};

/* Cache entries intern live groups without extending resource lifetimes. */

typedef struct GPUBindGroupLayoutPriv {
  GPUBindGroupLayoutEntry *entries;
  uint32_t                *backendBindings;
  uint32_t                 count;
  bool                     hasBackendBindings;
} GPUBindGroupLayoutPriv;

typedef struct GPUBindGroupBindingPriv {
  GPUBuffer      *buffer;
  GPUTextureView *textureView;
  GPUSampler     *sampler;
  uint64_t        offset;
  uint64_t        size;
  uint32_t        binding;
  GPUBindStage    stage;
  GPUBindKind     kind;
} GPUBindGroupBindingPriv;

typedef struct GPUBindGroupPriv {
  GPUBindGroupLayout       *layout;
  GPUBindGroupBindingPriv *bindings;
  uint64_t                 hash;
  uint32_t                 count;
} GPUBindGroupPriv;

typedef struct GPUBindGroupCacheEntry {
  GPUBindGroup *group;
} GPUBindGroupCacheEntry;

typedef struct GPUBindGroupCache {
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION lock;
#else
  pthread_mutex_t lock;
#endif
  GPUBindGroupCacheEntry entries[GPU_BIND_GROUP_CACHE_CAPACITY];
} GPUBindGroupCache;

_Static_assert((GPU_BIND_GROUP_CACHE_CAPACITY &
                (GPU_BIND_GROUP_CACHE_CAPACITY - 1u)) == 0u,
               "bind group cache capacity must be a power of two");

typedef struct GPUPipelineLayoutPriv {
  GPUBindGroupLayout **bindGroupLayouts;
  uint32_t           **backendBindings;
  uint32_t             bindGroupLayoutCount;
  uint32_t             pushConstantSizeBytes;
  GPUShaderStageFlags  pushConstantStages;
} GPUPipelineLayoutPriv;

typedef struct GPUBindRenderContext {
  GPURenderPassEncoder *pass;
} GPUBindRenderContext;

static GPUBindGroupLayoutPriv *
gpu_layoutPriv(GPUBindGroupLayout *layout) {
  return layout ? layout->_priv : NULL;
}

static GPUBindGroupPriv *
gpu_groupPriv(GPUBindGroup *group) {
  return group ? group->_priv : NULL;
}

static GPUBindGroupCache *
gpu_bindGroupCache(GPUDevice *device) {
  return device ? device->_bindGroupCache : NULL;
}

static void
gpu_bindGroupCacheLock(GPUBindGroupCache *cache) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&cache->lock);
#else
  pthread_mutex_lock(&cache->lock);
#endif
}

static void
gpu_bindGroupCacheUnlock(GPUBindGroupCache *cache) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&cache->lock);
#else
  pthread_mutex_unlock(&cache->lock);
#endif
}

GPU_HIDE
GPUResult
gpuInitBindGroupCacheDevice(GPUDevice *device) {
  GPUBindGroupCache *cache;

  if (!device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  cache = calloc(1, sizeof(*cache));
  if (!cache) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&cache->lock);
#else
  if (pthread_mutex_init(&cache->lock, NULL) != 0) {
    free(cache);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif
  device->_bindGroupCache = cache;
  return GPU_OK;
}

GPU_HIDE
void
gpuDestroyBindGroupCacheDevice(GPUDevice *device) {
  GPUBindGroupCache *cache;

  cache = gpu_bindGroupCache(device);
  if (!cache) {
    return;
  }
#if defined(_WIN32) || defined(WIN32)
  DeleteCriticalSection(&cache->lock);
#else
  pthread_mutex_destroy(&cache->lock);
#endif
  free(cache);
  device->_bindGroupCache = NULL;
}

static uint64_t
gpu_bindGroupHashBytes(uint64_t hash, const void *data, size_t size) {
  const uint8_t *bytes;

  bytes = data;
  for (size_t i = 0u; i < size; i++) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

#define GPU_BIND_GROUP_HASH(HASH, VALUE) \
  gpu_bindGroupHashBytes((HASH), &(VALUE), sizeof(VALUE))

static uint64_t
gpu_bindGroupHash(const GPUBindGroupPriv *priv) {
  uintptr_t layout;
  uint64_t  hash;

  layout = (uintptr_t)priv->layout;
  hash   = 14695981039346656037ull;
  hash   = GPU_BIND_GROUP_HASH(hash, layout);
  hash   = GPU_BIND_GROUP_HASH(hash, priv->count);
  for (uint32_t i = 0u; i < priv->count; i++) {
    const GPUBindGroupBindingPriv *binding;
    uintptr_t                      buffer;
    uintptr_t                      textureView;
    uintptr_t                      sampler;

    binding     = &priv->bindings[i];
    buffer      = (uintptr_t)binding->buffer;
    textureView = (uintptr_t)binding->textureView;
    sampler     = (uintptr_t)binding->sampler;
    hash        = GPU_BIND_GROUP_HASH(hash, buffer);
    hash        = GPU_BIND_GROUP_HASH(hash, textureView);
    hash        = GPU_BIND_GROUP_HASH(hash, sampler);
    hash        = GPU_BIND_GROUP_HASH(hash, binding->offset);
    hash        = GPU_BIND_GROUP_HASH(hash, binding->size);
    hash        = GPU_BIND_GROUP_HASH(hash, binding->binding);
    hash        = GPU_BIND_GROUP_HASH(hash, binding->stage);
    hash        = GPU_BIND_GROUP_HASH(hash, binding->kind);
  }
  return hash;
}

static bool
gpu_bindGroupsEqual(const GPUBindGroup *a, const GPUBindGroup *b) {
  const GPUBindGroupPriv *aPriv;
  const GPUBindGroupPriv *bPriv;

  aPriv = a ? a->_priv : NULL;
  bPriv = b ? b->_priv : NULL;
  if (!aPriv || !bPriv || aPriv->hash != bPriv->hash ||
      aPriv->layout != bPriv->layout || aPriv->count != bPriv->count) {
    return false;
  }

  for (uint32_t i = 0u; i < aPriv->count; i++) {
    const GPUBindGroupBindingPriv *aBinding;
    const GPUBindGroupBindingPriv *bBinding;

    aBinding = &aPriv->bindings[i];
    bBinding = &bPriv->bindings[i];
    if (aBinding->buffer != bBinding->buffer ||
        aBinding->textureView != bBinding->textureView ||
        aBinding->sampler != bBinding->sampler ||
        aBinding->offset != bBinding->offset ||
        aBinding->size != bBinding->size ||
        aBinding->binding != bBinding->binding ||
        aBinding->stage != bBinding->stage ||
        aBinding->kind != bBinding->kind) {
      return false;
    }
  }
  return true;
}

static GPUBindGroup *
gpu_bindGroupCacheFind(GPUDevice *device, GPUBindGroup *candidate) {
  GPUBindGroupCache      *cache;
  GPUBindGroupCacheEntry *entry;
  GPUBindGroupPriv       *priv;
  GPUBindGroup           *result;

  cache  = gpu_bindGroupCache(device);
  priv   = gpu_groupPriv(candidate);
  result = NULL;
  if (!cache || !priv) {
    return NULL;
  }

  entry = &cache->entries[priv->hash & GPU_BIND_GROUP_CACHE_MASK];
  gpu_bindGroupCacheLock(cache);
  if (entry->group && gpu_bindGroupsEqual(entry->group, candidate)) {
    result = entry->group;
    result->_refCount++;
    device->cacheStats.bindGroupHits++;
  }
  gpu_bindGroupCacheUnlock(cache);
  return result;
}

static GPUBindGroup *
gpu_bindGroupCacheStore(GPUDevice *device, GPUBindGroup *candidate) {
  GPUBindGroupCache      *cache;
  GPUBindGroupCacheEntry *entry;
  GPUBindGroupPriv       *priv;
  GPUBindGroup           *result;

  cache  = gpu_bindGroupCache(device);
  priv   = gpu_groupPriv(candidate);
  result = candidate;
  if (!cache || !priv) {
    return result;
  }

  entry = &cache->entries[priv->hash & GPU_BIND_GROUP_CACHE_MASK];
  gpu_bindGroupCacheLock(cache);
  if (entry->group && gpu_bindGroupsEqual(entry->group, candidate)) {
    result = entry->group;
    result->_refCount++;
    device->cacheStats.bindGroupHits++;
  } else {
    if (entry->group) {
      device->cacheStats.bindGroupCollisions++;
    }
    device->cacheStats.bindGroupMisses++;
    entry->group = candidate;
  }
  gpu_bindGroupCacheUnlock(cache);
  return result;
}

static bool
gpu_releaseBindGroup(GPUBindGroup *group) {
  GPUBindGroupCache      *cache;
  GPUBindGroupCacheEntry *entry;
  GPUBindGroupPriv       *priv;
  bool                    destroy;

  if (!group) {
    return false;
  }
  cache = gpu_bindGroupCache(group->_device);
  priv  = gpu_groupPriv(group);
  if (!cache || !priv) {
    if (group->_refCount == 0u) {
      return false;
    }
    return --group->_refCount == 0u;
  }

  entry = &cache->entries[priv->hash & GPU_BIND_GROUP_CACHE_MASK];
  gpu_bindGroupCacheLock(cache);
  destroy = false;
  if (group->_refCount > 0u) {
    group->_refCount--;
    destroy = group->_refCount == 0u;
  }
  if (destroy && entry->group == group) {
    entry->group = NULL;
  }
  gpu_bindGroupCacheUnlock(cache);
  return destroy;
}

static GPUPipelineLayoutPriv *
gpu_pipelineLayoutPriv(GPUPipelineLayout *layout) {
  return layout ? layout->_priv : NULL;
}

GPU_HIDE
void
gpuGetPipelineLayoutPushConstants(GPUPipelineLayout *layout,
                                  uint32_t *outSizeBytes,
                                  GPUShaderStageFlags *outStages) {
  GPUPipelineLayoutPriv *priv;

  priv = gpu_pipelineLayoutPriv(layout);
  if (outSizeBytes) {
    *outSizeBytes = priv ? priv->pushConstantSizeBytes : 0u;
  }
  if (outStages) {
    *outStages = priv ? priv->pushConstantStages : 0u;
  }
}

GPU_HIDE
GPUBindGroupLayout * const *
gpuGetPipelineLayoutGroups(GPUPipelineLayout *layout, uint32_t *outCount) {
  GPUPipelineLayoutPriv *priv;

  priv = gpu_pipelineLayoutPriv(layout);
  if (outCount) {
    *outCount = priv ? priv->bindGroupLayoutCount : 0u;
  }

  return priv ? priv->bindGroupLayouts : NULL;
}

GPU_HIDE
const uint32_t *
gpuGetBindGroupLayoutBackendBindings(GPUBindGroupLayout *layout,
                                     uint32_t *outCount) {
  GPUBindGroupLayoutPriv *priv;

  priv = gpu_layoutPriv(layout);
  if (outCount) {
    *outCount = priv ? priv->count : 0u;
  }

  return priv ? priv->backendBindings : NULL;
}

GPU_HIDE
GPUBindGroupLayout *
gpuBindGroupGetLayout(GPUBindGroup *group) {
  GPUBindGroupPriv *priv;

  priv = gpu_groupPriv(group);
  return priv ? priv->layout : NULL;
}

GPU_HIDE
GPUDevice *
gpuBindGroupGetDevice(GPUBindGroup *group) {
  return group ? group->_device : NULL;
}

GPU_HIDE
uint32_t
gpuBindGroupDynamicOffsetCount(GPUBindGroup *group) {
  GPUBindGroupPriv *priv;
  GPUBindGroupLayoutPriv *layout;
  uint32_t count;

  priv = gpu_groupPriv(group);
  layout = priv ? gpu_layoutPriv(priv->layout) : NULL;
  if (!layout) {
    return 0u;
  }

  count = 0u;
  for (uint32_t i = 0; i < layout->count; i++) {
    if (layout->entries[i].hasDynamicOffset) {
      count++;
    }
  }

  return count;
}

GPU_HIDE
int
gpuPipelineLayoutAcceptsBindGroup(GPUPipelineLayout *pipelineLayout,
                                  uint32_t groupIndex,
                                  GPUBindGroup *group) {
  GPUPipelineLayoutPriv *pipelinePriv;
  GPUBindGroupLayout *groupLayout;

  pipelinePriv = gpu_pipelineLayoutPriv(pipelineLayout);
  groupLayout = gpuBindGroupGetLayout(group);
  if (!pipelinePriv || !groupLayout ||
      groupIndex >= pipelinePriv->bindGroupLayoutCount) {
    return 0;
  }

  return pipelinePriv->bindGroupLayouts[groupIndex] == groupLayout;
}

#if GPU_BUILD_WITH_VALIDATION
GPU_HIDE
int
gpuPipelineLayoutMaskIsBound(GPUPipelineLayout *pipelineLayout,
                             GPUBindGroupLayout * const *boundLayouts,
                             uint32_t boundLayoutCount,
                             uint32_t requiredGroupMask) {
  GPUPipelineLayoutPriv *priv;

  priv = gpu_pipelineLayoutPriv(pipelineLayout);
  if (!priv) {
    return requiredGroupMask == 0u;
  }

  for (uint32_t i = 0u; i < priv->bindGroupLayoutCount; i++) {
    uint32_t groupBit;

    groupBit = 1u << i;
    if ((requiredGroupMask & groupBit) == 0u) {
      continue;
    }
    if (!boundLayouts ||
        i >= boundLayoutCount ||
        boundLayouts[i] != priv->bindGroupLayouts[i]) {
      return 0;
    }
  }

  return (requiredGroupMask >> priv->bindGroupLayoutCount) == 0u;
}
#endif

static int
gpu_bindStageIsValid(GPUBindStage stage);

static int
gpu_bindKindIsValid(GPUBindKind kind);

static int
gpu_u64Add(uint64_t a, uint64_t b, uint64_t *out) {
  if (!out || b > UINT64_MAX - a) {
    return 0;
  }

  *out = a + b;
  return 1;
}

static int
gpu_samplerDescIsValid(const GPUSamplerDesc *desc) {
  if (!desc) {
    return 0;
  }

  return (desc->minFilter == GPU_FILTER_NEAREST ||
          desc->minFilter == GPU_FILTER_LINEAR) &&
         (desc->magFilter == GPU_FILTER_NEAREST ||
          desc->magFilter == GPU_FILTER_LINEAR) &&
         (desc->mipFilter == GPU_MIP_FILTER_NEAREST ||
          desc->mipFilter == GPU_MIP_FILTER_LINEAR) &&
         (desc->addressU == GPU_ADDRESS_MODE_REPEAT ||
          desc->addressU == GPU_ADDRESS_MODE_MIRRORED_REPEAT ||
          desc->addressU == GPU_ADDRESS_MODE_CLAMP_TO_EDGE) &&
         (desc->addressV == GPU_ADDRESS_MODE_REPEAT ||
          desc->addressV == GPU_ADDRESS_MODE_MIRRORED_REPEAT ||
          desc->addressV == GPU_ADDRESS_MODE_CLAMP_TO_EDGE) &&
         (desc->addressW == GPU_ADDRESS_MODE_REPEAT ||
          desc->addressW == GPU_ADDRESS_MODE_MIRRORED_REPEAT ||
          desc->addressW == GPU_ADDRESS_MODE_CLAMP_TO_EDGE);
}

static int
gpu_stageFromVisibility(GPUShaderStageFlags visibility, GPUBindStage *outStage) {
  if (!outStage) {
    return 0;
  }

  if (visibility == 0 ||
      (visibility & ~(GPU_SHADER_STAGE_VERTEX_BIT |
                      GPU_SHADER_STAGE_FRAGMENT_BIT |
                      GPU_SHADER_STAGE_COMPUTE_BIT)) != 0) {
    return 0;
  }

  if ((visibility & GPU_SHADER_STAGE_VERTEX_BIT) != 0) {
    *outStage = GPUBindStageVertex;
    return 1;
  }
  if ((visibility & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0) {
    *outStage = GPUBindStageFragment;
    return 1;
  }

  *outStage = GPUBindStageCompute;
  return 1;
}

static int
gpu_kindFromBindingType(GPUBindingType type, GPUBindKind *outKind) {
  if (!outKind) {
    return 0;
  }

  switch (type) {
    case GPU_BINDING_UNIFORM_BUFFER:
    case GPU_BINDING_STORAGE_BUFFER:
      *outKind = GPUBindKindBuffer;
      return 1;
    case GPU_BINDING_SAMPLED_TEXTURE:
    case GPU_BINDING_STORAGE_TEXTURE:
      *outKind = GPUBindKindTexture;
      return 1;
    case GPU_BINDING_SAMPLER:
      *outKind = GPUBindKindSampler;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_bindingTypeFromKind(GPUBindKind kind, GPUBindingType *outType) {
  if (!outType) {
    return 0;
  }

  switch (kind) {
    case GPUBindKindBuffer:
      *outType = GPU_BINDING_UNIFORM_BUFFER;
      return 1;
    case GPUBindKindTexture:
      *outType = GPU_BINDING_SAMPLED_TEXTURE;
      return 1;
    case GPUBindKindSampler:
      *outType = GPU_BINDING_SAMPLER;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_visibilityFromStage(GPUBindStage stage, GPUShaderStageFlags *outVisibility) {
  if (!outVisibility) {
    return 0;
  }

  switch (stage) {
    case GPUBindStageVertex:
      *outVisibility = GPU_SHADER_STAGE_VERTEX_BIT;
      return 1;
    case GPUBindStageFragment:
      *outVisibility = GPU_SHADER_STAGE_FRAGMENT_BIT;
      return 1;
    case GPUBindStageCompute:
      *outVisibility = GPU_SHADER_STAGE_COMPUTE_BIT;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_normalizeLayoutEntry(const GPUBindGroupLayoutEntry *src,
                         GPUBindGroupLayoutEntry *dst) {
  GPUBindStage stage;
  GPUBindKind kind;
  GPUBindingType type;
  GPUShaderStageFlags visibility;

  if (!src || !dst) {
    return 0;
  }

  *dst = *src;
  visibility = src->visibility;
  type = src->bindingType;

  if (visibility == 0) {
    if (!gpu_visibilityFromStage(src->stage, &visibility)) {
      return 0;
    }
  }
  if (!gpu_stageFromVisibility(visibility, &stage)) {
    return 0;
  }

  if (!gpu_kindFromBindingType(type, &kind)) {
    if (!gpu_bindKindIsValid(src->kind) ||
        !gpu_bindingTypeFromKind(src->kind, &type)) {
      return 0;
    }
    kind = src->kind;
  }

  if (src->arrayCount == 0) {
    dst->arrayCount = 1;
  }

  dst->visibility = visibility;
  dst->bindingType = type;
  dst->stage = stage;
  dst->kind = kind;
  return 1;
}

static int
gpu_bindStageIsValid(GPUBindStage stage) {
  return stage == GPUBindStageVertex ||
         stage == GPUBindStageFragment ||
         stage == GPUBindStageCompute;
}

static int
gpu_bindKindIsValid(GPUBindKind kind) {
  return kind == GPUBindKindBuffer ||
         kind == GPUBindKindTexture ||
         kind == GPUBindKindSampler;
}

static int
gpu_layoutEntryDuplicateExists(const GPUBindGroupLayoutEntry *entries,
                               uint32_t count,
                               uint32_t binding) {
  uint32_t i;

  if (!entries) {
    return 0;
  }

  for (i = 0; i < count; i++) {
    GPUBindGroupLayoutEntry entry;

    if (gpu_normalizeLayoutEntry(&entries[i], &entry) &&
        entry.binding == binding) {
      return 1;
    }
  }

  return 0;
}

static int
gpu_validateLayoutEntries(const GPUBindGroupLayoutEntry *entries,
                          uint32_t count) {
  uint32_t i;

  if (!entries && count > 0) {
    return 0;
  }

  for (i = 0; i < count; i++) {
    GPUBindGroupLayoutEntry entry;

    if (!gpu_normalizeLayoutEntry(&entries[i], &entry) ||
        !gpu_bindStageIsValid(entry.stage) ||
        !gpu_bindKindIsValid(entry.kind) ||
        (entry.hasDynamicOffset && entry.kind != GPUBindKindBuffer) ||
        (entry.immutableSampler &&
         (entry.kind != GPUBindKindSampler ||
          !gpu_samplerDescIsValid(&entry.immutableSamplerDesc))) ||
        gpu_layoutEntryDuplicateExists(entries,
                                       i,
                                       entry.binding)) {
      return 0;
    }
  }

  return 1;
}

GPU_EXPORT
int
GPUUSLStaticSamplerDescIsValid(const GPUUSLStaticSamplerDesc *desc) {
  if (!desc) {
    return 0;
  }

  if (desc->minFilter > GPUUSLSamplerFilterLinear ||
      desc->magFilter > GPUUSLSamplerFilterLinear ||
      desc->mipFilter > GPUUSLSamplerFilterLinear) {
    return 0;
  }

  if (desc->addressMode > GPUUSLSamplerAddressClampToBorder ||
      desc->coordSpace > GPUUSLSamplerCoordPixel ||
      desc->compareFunc > GPUUSLSamplerCompareAlways ||
      desc->hasCompare > 1u ||
      desc->maxAnisotropy > 255u) {
    return 0;
  }

  return 1;
}

static int
gpu_bindGroupEntryHasResource(const GPUBindGroupLayoutEntry *layoutEntry,
                              const GPUBindGroupEntry *entry) {
  if (!layoutEntry) {
    return 0;
  }

  if (!entry) {
    return layoutEntry->immutableSampler &&
           layoutEntry->kind == GPUBindKindSampler;
  }

  switch (layoutEntry->kind) {
    case GPUBindKindBuffer:
      return entry->buffer.buffer != NULL;
    case GPUBindKindTexture:
      return entry->textureView != NULL;
    case GPUBindKindSampler:
      return entry->sampler != NULL;
    default:
      return 0;
  }
}

static int
gpu_bindGroupEntryResourceKind(const GPUBindGroupEntry *entry,
                               GPUBindKind *outKind) {
  GPUBindKind kind;
  uint32_t resourceCount;

  if (!entry || !outKind) {
    return 0;
  }

  kind = GPUBindKindBuffer;
  resourceCount = 0;
  if (entry->buffer.buffer) {
    kind = GPUBindKindBuffer;
    resourceCount++;
  }
  if (entry->textureView) {
    kind = GPUBindKindTexture;
    resourceCount++;
  }
  if (entry->sampler) {
    kind = GPUBindKindSampler;
    resourceCount++;
  }

  if (resourceCount != 1) {
    return 0;
  }

  *outKind = kind;
  return 1;
}

static int
gpu_bindingBufferUsage(GPUBindingType bindingType, GPUBufferUsageFlags *outUsage) {
  if (!outUsage) {
    return 0;
  }

  switch (bindingType) {
    case GPU_BINDING_UNIFORM_BUFFER:
      *outUsage = GPU_BUFFER_USAGE_UNIFORM;
      return 1;
    case GPU_BINDING_STORAGE_BUFFER:
      *outUsage = GPU_BUFFER_USAGE_STORAGE;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_bindGroupBufferRangeValid(const GPUBindGroupLayoutEntry *layoutEntry,
                              const GPUBindGroupEntry       *entry) {
  GPUBufferUsageFlags usage;

  if (!layoutEntry || !entry || layoutEntry->kind != GPUBindKindBuffer) {
    return 1;
  }
  if (!gpu_bindingBufferUsage(layoutEntry->bindingType, &usage)) {
    return 0;
  }

  return gpuBufferHasUsage(entry->buffer.buffer, usage) &&
         gpuBufferRangeValid(entry->buffer.buffer,
                             entry->buffer.offset,
                             entry->buffer.size);
}

static int
gpu_bindingTextureUsage(GPUBindingType bindingType, GPUTextureUsageFlags *outUsage) {
  if (!outUsage) {
    return 0;
  }

  switch (bindingType) {
    case GPU_BINDING_SAMPLED_TEXTURE:
      *outUsage = GPU_TEXTURE_USAGE_SAMPLED;
      return 1;
    case GPU_BINDING_STORAGE_TEXTURE:
      *outUsage = GPU_TEXTURE_USAGE_STORAGE;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_bindGroupTextureViewValid(const GPUBindGroupLayoutEntry *layoutEntry,
                              const GPUBindGroupEntry       *entry) {
  GPUTextureUsageFlags usage;
  GPUTexture *texture;

  if (!layoutEntry || !entry || layoutEntry->kind != GPUBindKindTexture) {
    return 1;
  }
  if (!gpu_bindingTextureUsage(layoutEntry->bindingType, &usage)) {
    return 0;
  }

  texture = entry->textureView ? entry->textureView->_texture : NULL;
  return texture && (texture->usage & usage) == usage;
}

static int
gpu_bindGroupEntryMatchesLayout(const GPUBindGroupLayoutEntry *layoutEntry,
                                const GPUBindGroupEntry *entry) {
  GPUBindKind resourceKind;

  if (!layoutEntry || !entry ||
      entry->binding != layoutEntry->binding ||
      !gpu_bindGroupEntryResourceKind(entry, &resourceKind) ||
      resourceKind != layoutEntry->kind) {
    return 0;
  }

  if (layoutEntry->immutableSampler &&
      layoutEntry->kind == GPUBindKindSampler) {
    return 0;
  }
  if (entry->stage != 0 &&
      entry->stage != layoutEntry->stage) {
    return 0;
  }
  if (entry->kind != 0 &&
      entry->kind != layoutEntry->kind) {
    return 0;
  }
  if (layoutEntry->kind == GPUBindKindBuffer &&
      entry->bindingType != layoutEntry->bindingType) {
    return 0;
  }
  if (layoutEntry->kind == GPUBindKindTexture &&
      entry->bindingType != 0 &&
      entry->bindingType != layoutEntry->bindingType) {
    return 0;
  }
  if (!gpu_bindGroupBufferRangeValid(layoutEntry, entry)) {
    return 0;
  }
  if (!gpu_bindGroupTextureViewValid(layoutEntry, entry)) {
    return 0;
  }

  return gpu_bindGroupEntryHasResource(layoutEntry, entry);
}

static const GPUBindGroupEntry *
gpu_findBindGroupEntry(const GPUBindGroupEntry *entries,
                       uint32_t count,
                       const GPUBindGroupLayoutEntry *layoutEntry) {
  uint32_t i;

  if (!entries || !layoutEntry) {
    return NULL;
  }

  for (i = 0; i < count; i++) {
    if (gpu_bindGroupEntryMatchesLayout(layoutEntry, &entries[i])) {
      return &entries[i];
    }
  }

  return NULL;
}

static int
gpu_bindGroupLayoutEntryNeedsBinding(const GPUBindGroupLayoutEntry *entry) {
  if (!entry) {
    return 0;
  }

  return !(entry->immutableSampler && entry->kind == GPUBindKindSampler);
}

static GPUResult
gpu_validateBindGroupEntries(const GPUBindGroupLayoutPriv *layoutPriv,
                             const GPUBindGroupEntry *entries,
                             uint32_t count) {
  uint8_t *matched;

  if (!layoutPriv || (!entries && count > 0)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  matched = NULL;
  if (layoutPriv->count > 0) {
    matched = calloc(layoutPriv->count, sizeof(*matched));
    if (!matched) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  for (uint32_t i = 0; i < count; i++) {
    uint32_t matchCount;
    uint32_t matchIndex;

    matchCount = 0;
    matchIndex = UINT32_MAX;
    for (uint32_t j = 0; j < layoutPriv->count; j++) {
      if (!gpu_bindGroupEntryMatchesLayout(&layoutPriv->entries[j], &entries[i])) {
        continue;
      }
      matchCount++;
      matchIndex = j;
    }

    if (matchCount != 1 || matched[matchIndex]) {
      free(matched);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    matched[matchIndex] = 1u;
  }

  for (uint32_t i = 0; i < layoutPriv->count; i++) {
    if (gpu_bindGroupLayoutEntryNeedsBinding(&layoutPriv->entries[i]) &&
        !matched[i]) {
      free(matched);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  free(matched);
  return GPU_OK;
}

static GPUResult
gpu_createBindGroupLayout(GPUDevice *device,
                          const GPUBindGroupLayoutCreateInfo *info,
                          const uint32_t *backendBindings,
                          GPUBindGroupLayout **outLayout) {
  GPUBindGroupLayout *layout;
  GPUBindGroupLayoutPriv *priv;
  const GPUBindGroupLayoutEntry *entries;
  GPUApi *api;
  GPUResult result;
  uint32_t count;

  if (!outLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLayout = NULL;
  if (!device || !info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  entries = info->pEntries;
  count = info->entryCount;
  if (!gpu_validateLayoutEntries(entries, count)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layout = calloc(1, sizeof(*layout));
  priv = calloc(1, sizeof(*priv));
  if (!layout || !priv) {
    free(layout);
    free(priv);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if ((size_t)count > SIZE_MAX / sizeof(*priv->entries)) {
    free(priv);
    free(layout);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (count > 0) {
    priv->entries = calloc(count, sizeof(*priv->entries));
    priv->backendBindings = calloc(count, sizeof(*priv->backendBindings));
    if (!priv->entries || !priv->backendBindings) {
      free(priv->entries);
      free(priv->backendBindings);
      free(priv);
      free(layout);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    for (uint32_t i = 0; i < count; i++) {
      if (!gpu_normalizeLayoutEntry(&entries[i], &priv->entries[i])) {
        free(priv->backendBindings);
        free(priv->entries);
        free(priv);
        free(layout);
        return GPU_ERROR_INVALID_ARGUMENT;
      }
      priv->backendBindings[i] = backendBindings
                                   ? backendBindings[i]
                                   : priv->entries[i].binding;
      if (priv->backendBindings[i] == UINT32_MAX) {
        free(priv->backendBindings);
        free(priv->entries);
        free(priv);
        free(layout);
        return GPU_ERROR_INVALID_ARGUMENT;
      }
    }
  }

  priv->count               = count;
  priv->hasBackendBindings = backendBindings != NULL;
  layout->_device           = device;
  layout->_priv             = priv;

  api = gpuDeviceApi(device);
  if (api && api->descriptor.createBindGroupLayout) {
    result = api->descriptor.createBindGroupLayout(device, layout);
    if (result != GPU_OK) {
      if (layout->_native && api->descriptor.destroyBindGroupLayout) {
        api->descriptor.destroyBindGroupLayout(layout);
      }
      free(priv->backendBindings);
      free(priv->entries);
      free(priv);
      free(layout);
      return result;
    }
  }

  *outLayout = layout;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateBindGroupLayout(GPUDevice *device,
                         const GPUBindGroupLayoutCreateInfo *info,
                         GPUBindGroupLayout **outLayout) {
  return gpu_createBindGroupLayout(device, info, NULL, outLayout);
}

GPU_EXPORT
const GPUBindGroupLayoutEntry *
GPUGetBindGroupLayoutEntries(GPUBindGroupLayout *layout, uint32_t *outCount) {
  GPUBindGroupLayoutPriv *priv;

  priv = gpu_layoutPriv(layout);
  if (outCount) {
    *outCount = priv ? priv->count : 0;
  }

  return priv ? priv->entries : NULL;
}

GPU_EXPORT
void
GPUDestroyBindGroupLayout(GPUBindGroupLayout *layout) {
  GPUBindGroupLayoutPriv *priv;
  GPUApi *api;

  if (!layout) {
    return;
  }

  api = gpuDeviceApi(layout->_device);
  if (api && api->descriptor.destroyBindGroupLayout) {
    api->descriptor.destroyBindGroupLayout(layout);
  }

  priv = gpu_layoutPriv(layout);
  if (priv) {
    free(priv->backendBindings);
    free(priv->entries);
    free(priv);
  }

  free(layout);
}

static int
gpu_layoutEntryUsesStage(GPUShaderStageFlags visibility,
                         GPUShaderStageFlags stage) {
  return (visibility & stage) != 0u;
}

static void
gpu_clearPipelineBindings(GPUPipelineLayoutPriv *priv) {
  if (!priv || !priv->backendBindings) {
    return;
  }

  for (uint32_t i = 0; i < priv->bindGroupLayoutCount; i++) {
    free(priv->backendBindings[i]);
  }
  free(priv->backendBindings);
  priv->backendBindings = NULL;
}

static int
gpu_pipelineSlotIsUsed(const GPUPipelineLayoutPriv *priv,
                       GPUBindKind kind,
                       uint32_t slot,
                       GPUShaderStageFlags visibility) {
  if (!priv || !priv->backendBindings) {
    return 0;
  }

  for (uint32_t groupIndex = 0;
       groupIndex < priv->bindGroupLayoutCount;
       groupIndex++) {
    GPUBindGroupLayoutPriv *layout =
      gpu_layoutPriv(priv->bindGroupLayouts[groupIndex]);

    if (!layout || !priv->backendBindings[groupIndex]) {
      continue;
    }
    for (uint32_t entryIndex = 0; entryIndex < layout->count; entryIndex++) {
      if (layout->entries[entryIndex].kind == kind &&
          (layout->entries[entryIndex].visibility & visibility) != 0u &&
          priv->backendBindings[groupIndex][entryIndex] == slot) {
        return 1;
      }
    }
  }

  return 0;
}

static uint32_t
gpu_metalBindingLimit(GPUBindKind kind) {
  static const uint32_t limits[] = {
    [GPUBindKindBuffer]  = MT_BIND_GROUP_BUFFER_COUNT,
    [GPUBindKindTexture] = MT_ARGUMENT_TEXTURE_COUNT,
    [GPUBindKindSampler] = MT_ARGUMENT_SAMPLER_COUNT
  };

  if (kind < GPUBindKindBuffer || kind > GPUBindKindSampler) {
    return 0u;
  }
  return limits[kind];
}

static int
gpu_pipelineBindingsAreUnique(const GPUPipelineLayoutPriv *priv) {
  static const GPUShaderStageFlags stages[] = {
    GPU_SHADER_STAGE_VERTEX_BIT,
    GPU_SHADER_STAGE_FRAGMENT_BIT,
    GPU_SHADER_STAGE_COMPUTE_BIT
  };

  if (!priv || priv->bindGroupLayoutCount == 0u) {
    return 1;
  }

  for (uint32_t groupA = 0; groupA < priv->bindGroupLayoutCount; groupA++) {
    GPUBindGroupLayoutPriv *layoutA =
      gpu_layoutPriv(priv->bindGroupLayouts[groupA]);

    if (!layoutA || (layoutA->count > 0u && !priv->backendBindings[groupA])) {
      return 0;
    }

    for (uint32_t entryA = 0; entryA < layoutA->count; entryA++) {
      const GPUBindGroupLayoutEntry *a = &layoutA->entries[entryA];
      uint32_t backendA = priv->backendBindings[groupA][entryA];

      for (uint32_t groupB = groupA; groupB < priv->bindGroupLayoutCount; groupB++) {
        GPUBindGroupLayoutPriv *layoutB =
          gpu_layoutPriv(priv->bindGroupLayouts[groupB]);
        uint32_t entryStartB = groupA == groupB ? entryA + 1u : 0u;

        if (!layoutB || (layoutB->count > 0u && !priv->backendBindings[groupB])) {
          return 0;
        }

        for (uint32_t entryB = entryStartB; entryB < layoutB->count; entryB++) {
          const GPUBindGroupLayoutEntry *b = &layoutB->entries[entryB];
          uint32_t backendB = priv->backendBindings[groupB][entryB];

          if (a->kind != b->kind || backendA != backendB) {
            continue;
          }

          for (uint32_t stageIndex = 0; stageIndex < GPU_ARRAY_LEN(stages); stageIndex++) {
            if (gpu_layoutEntryUsesStage(a->visibility, stages[stageIndex]) &&
                gpu_layoutEntryUsesStage(b->visibility, stages[stageIndex])) {
              return 0;
            }
          }
        }
      }
    }
  }

  return 1;
}

static GPUResult
gpu_compilePipelineBindings(GPUPipelineLayoutPriv *priv,
                            GPUBackend backend) {
  static const GPUBindKind kinds[] = {
    GPUBindKindBuffer,
    GPUBindKindTexture,
    GPUBindKindSampler
  };

  if (!priv) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (priv->bindGroupLayoutCount == 0u) {
    return GPU_OK;
  }

  priv->backendBindings = calloc(priv->bindGroupLayoutCount,
                                 sizeof(*priv->backendBindings));
  if (!priv->backendBindings) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  for (uint32_t groupIndex = 0;
       groupIndex < priv->bindGroupLayoutCount;
       groupIndex++) {
    GPUBindGroupLayoutPriv *layout =
      gpu_layoutPriv(priv->bindGroupLayouts[groupIndex]);

    if (!layout) {
      gpu_clearPipelineBindings(priv);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    if (layout->count == 0u) {
      continue;
    }

    priv->backendBindings[groupIndex] =
      malloc((size_t)layout->count * sizeof(*priv->backendBindings[groupIndex]));
    if (!priv->backendBindings[groupIndex]) {
      gpu_clearPipelineBindings(priv);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    for (uint32_t entryIndex = 0; entryIndex < layout->count; entryIndex++) {
      priv->backendBindings[groupIndex][entryIndex] = UINT32_MAX;
    }
    if (layout->hasBackendBindings) {
      for (uint32_t entryIndex = 0; entryIndex < layout->count; entryIndex++) {
        const GPUBindGroupLayoutEntry *entry;
        uint32_t backendBinding;

        entry          = &layout->entries[entryIndex];
        backendBinding = layout->backendBindings[entryIndex];
        if (backendBinding == UINT32_MAX) {
          gpu_clearPipelineBindings(priv);
          return GPU_ERROR_INVALID_ARGUMENT;
        }
        if (backend == GPU_BACKEND_METAL &&
            backendBinding >= gpu_metalBindingLimit(entry->kind)) {
          gpu_clearPipelineBindings(priv);
          return GPU_ERROR_UNSUPPORTED;
        }
        priv->backendBindings[groupIndex][entryIndex] = backendBinding;
      }
    }
  }

  if (backend != GPU_BACKEND_METAL) {
    for (uint32_t groupIndex = 0;
         groupIndex < priv->bindGroupLayoutCount;
         groupIndex++) {
      GPUBindGroupLayoutPriv *layout =
        gpu_layoutPriv(priv->bindGroupLayouts[groupIndex]);

      for (uint32_t entryIndex = 0; layout && entryIndex < layout->count; entryIndex++) {
        if (priv->backendBindings[groupIndex][entryIndex] == UINT32_MAX) {
          priv->backendBindings[groupIndex][entryIndex] =
            layout->entries[entryIndex].binding;
        }
      }
    }
    return GPU_OK;
  }

  for (uint32_t kindIndex = 0; kindIndex < GPU_ARRAY_LEN(kinds); kindIndex++) {
    for (;;) {
      uint32_t selectedGroup = UINT32_MAX;
      uint32_t selectedEntry = UINT32_MAX;
      uint32_t selectedBinding = UINT32_MAX;
      uint32_t backendBinding = 0u;
      GPUShaderStageFlags selectedVisibility = 0u;

      for (uint32_t groupIndex = 0;
           groupIndex < priv->bindGroupLayoutCount;
           groupIndex++) {
        GPUBindGroupLayoutPriv *layout =
          gpu_layoutPriv(priv->bindGroupLayouts[groupIndex]);

        for (uint32_t entryIndex = 0; layout && entryIndex < layout->count; entryIndex++) {
          const GPUBindGroupLayoutEntry *entry = &layout->entries[entryIndex];

          if (entry->kind != kinds[kindIndex] ||
              priv->backendBindings[groupIndex][entryIndex] != UINT32_MAX) {
            continue;
          }
          if (selectedGroup == UINT32_MAX ||
              groupIndex < selectedGroup ||
              (groupIndex == selectedGroup && entry->binding < selectedBinding)) {
            selectedGroup = groupIndex;
            selectedEntry = entryIndex;
            selectedBinding = entry->binding;
            selectedVisibility = entry->visibility;
          }
        }
      }

      if (selectedGroup == UINT32_MAX) {
        break;
      }

      while (gpu_pipelineSlotIsUsed(priv,
                                    kinds[kindIndex],
                                    backendBinding,
                                    selectedVisibility)) {
        if (backendBinding == UINT32_MAX - 1u) {
          gpu_clearPipelineBindings(priv);
          return GPU_ERROR_BACKEND_FAILURE;
        }
        backendBinding++;
      }
      if (backendBinding == UINT32_MAX) {
        gpu_clearPipelineBindings(priv);
        return GPU_ERROR_BACKEND_FAILURE;
      }
      if (backendBinding >= gpu_metalBindingLimit(kinds[kindIndex])) {
        gpu_clearPipelineBindings(priv);
        return GPU_ERROR_UNSUPPORTED;
      }
      priv->backendBindings[selectedGroup][selectedEntry] = backendBinding;
    }
  }

  if (!gpu_pipelineBindingsAreUnique(priv)) {
    gpu_clearPipelineBindings(priv);
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreatePipelineLayout(GPUDevice *device,
                        const GPUPipelineLayoutCreateInfo *info,
                        GPUPipelineLayout **outLayout) {
  GPUPipelineLayout *layout;
  GPUPipelineLayoutPriv *priv;
  GPUApi *api;
  GPUResult result;

  if (!outLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLayout = NULL;
  if (!device || !info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->bindGroupLayoutCount > 0 && !info->ppBindGroupLayouts) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->bindGroupLayoutCount > GPU_ENCODER_MAX_BIND_GROUPS) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->pushConstantSizeBytes > 0 && info->pushConstantStages == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->pushConstantSizeBytes > GPU_PUSH_CONSTANT_MAX_SIZE_BYTES) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layout = calloc(1, sizeof(*layout));
  priv = calloc(1, sizeof(*priv));
  if (!layout || !priv) {
    free(layout);
    free(priv);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if ((size_t)info->bindGroupLayoutCount > SIZE_MAX / sizeof(*priv->bindGroupLayouts)) {
    free(priv);
    free(layout);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (info->bindGroupLayoutCount > 0) {
    priv->bindGroupLayouts = calloc(info->bindGroupLayoutCount,
                                    sizeof(*priv->bindGroupLayouts));
    if (!priv->bindGroupLayouts) {
      free(priv);
      free(layout);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    for (uint32_t i = 0; i < info->bindGroupLayoutCount; i++) {
      if (!info->ppBindGroupLayouts[i] ||
          info->ppBindGroupLayouts[i]->_device != device) {
        free(priv->bindGroupLayouts);
        free(priv);
        free(layout);
        return GPU_ERROR_INVALID_ARGUMENT;
      }
      priv->bindGroupLayouts[i] = info->ppBindGroupLayouts[i];
    }
  }

  priv->bindGroupLayoutCount = info->bindGroupLayoutCount;
  api = gpuDeviceApi(device);
  result = gpu_compilePipelineBindings(priv,
                                       api ? api->backend : GPU_BACKEND_NULL);
  if (result != GPU_OK) {
    free(priv->bindGroupLayouts);
    free(priv);
    free(layout);
    return result;
  }
  priv->pushConstantSizeBytes = info->pushConstantSizeBytes;
  priv->pushConstantStages = info->pushConstantStages;
  layout->_device = device;
  layout->_priv = priv;

  if (api && api->descriptor.createPipelineLayout) {
    result = api->descriptor.createPipelineLayout(device, layout);
    if (result != GPU_OK) {
      GPUDestroyPipelineLayout(layout);
      return result;
    }
  }

  *outLayout = layout;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyPipelineLayout(GPUPipelineLayout *layout) {
  GPUPipelineLayoutPriv *priv;
  GPUApi *api;

  if (!layout) {
    return;
  }

  api = gpuDeviceApi(layout->_device);
  if (api && api->descriptor.destroyPipelineLayout) {
    api->descriptor.destroyPipelineLayout(layout);
  }

  priv = gpu_pipelineLayoutPriv(layout);
  if (priv) {
    gpu_clearPipelineBindings(priv);
    free(priv->bindGroupLayouts);
    free(priv);
  }

  free(layout);
}

static uint32_t
gpu_reflectionLayoutCount(const GPUShaderReflection *reflection) {
  uint32_t maxGroup;

  if (!reflection || reflection->resourceCount == 0u || !reflection->pResources) {
    return 0u;
  }

  maxGroup = 0u;
  for (uint32_t i = 0; i < reflection->resourceCount; i++) {
    if (reflection->pResources[i].groupIndex > maxGroup) {
      maxGroup = reflection->pResources[i].groupIndex;
    }
  }

  return maxGroup + 1u;
}

static uint32_t
gpu_reflectionResourceCountForGroup(const GPUShaderReflection *reflection,
                                    uint32_t groupIndex) {
  uint32_t count;

  if (!reflection || !reflection->pResources) {
    return 0u;
  }

  count = 0u;
  for (uint32_t i = 0; i < reflection->resourceCount; i++) {
    if (reflection->pResources[i].groupIndex == groupIndex) {
      count++;
    }
  }

  return count;
}

static uint32_t
gpu_reflectionLayoutCountForStages(const GPUShaderReflection *reflection,
                                   GPUShaderStageFlags stages) {
  uint32_t maxGroup;
  int hasResource;

  if (!reflection || reflection->resourceCount == 0u || !reflection->pResources) {
    return 0u;
  }

  maxGroup = 0u;
  hasResource = 0;
  for (uint32_t i = 0; i < reflection->resourceCount; i++) {
    if ((reflection->pResources[i].visibility & stages) == 0u) {
      continue;
    }
    hasResource = 1;
    if (reflection->pResources[i].groupIndex > maxGroup) {
      maxGroup = reflection->pResources[i].groupIndex;
    }
  }

  return hasResource ? maxGroup + 1u : 0u;
}

static GPUResult
gpu_createLayoutForReflectionGroup(GPUDevice *device,
                                   const GPUShaderLibrary *library,
                                   const GPUShaderReflection *reflection,
                                   uint32_t groupIndex,
                                   GPUBindGroupLayout **outLayout) {
  GPUBindGroupLayoutCreateInfo info;
  GPUBindGroupLayoutEntry *entries;
  uint32_t *backendBindings;
  uint32_t entryCount;
  uint32_t cursor;
  GPUResult rc;

  if (!outLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLayout = NULL;
  entryCount = gpu_reflectionResourceCountForGroup(reflection, groupIndex);
  entries = NULL;
  backendBindings = NULL;
  if (entryCount > 0u) {
    entries = calloc(entryCount, sizeof(*entries));
    backendBindings = calloc(entryCount, sizeof(*backendBindings));
    if (!entries || !backendBindings) {
      free(entries);
      free(backendBindings);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  cursor = 0u;
  for (uint32_t i = 0; reflection && i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *resource;

    resource = &reflection->pResources[i];
    if (resource->groupIndex != groupIndex) {
      continue;
    }

    entries[cursor].binding = resource->binding;
    entries[cursor].bindingType = resource->bindingType;
    entries[cursor].visibility = resource->visibility;
    entries[cursor].arrayCount = resource->arrayCount ? resource->arrayCount : 1u;
    entries[cursor].hasDynamicOffset = resource->hasDynamicOffset;
    backendBindings[cursor] = resource->binding;
    gpuGetShaderResourceBackendBinding(library, resource, &backendBindings[cursor]);
    cursor++;
  }

  memset(&info, 0, sizeof(info));
  info.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.entryCount = entryCount;
  info.pEntries = entries;
  rc = gpu_createBindGroupLayout(device,
                                 &info,
                                 backendBindings,
                                 outLayout);
  free(entries);
  free(backendBindings);
  return rc;
}

static int
gpu_layoutMatchesReflectionResource(const GPUBindGroupLayoutEntry *entry,
                                    const GPUShaderResourceReflection *resource) {
  uint32_t resourceArrayCount;

  if (!entry || !resource) {
    return 0;
  }

  resourceArrayCount = resource->arrayCount ? resource->arrayCount : 1u;
  return entry->binding == resource->binding &&
         entry->bindingType == resource->bindingType &&
         entry->visibility == resource->visibility &&
         entry->arrayCount == resourceArrayCount &&
         entry->hasDynamicOffset == resource->hasDynamicOffset;
}

static int
gpu_layoutContainsStageReflectionResource(const GPUBindGroupLayoutPriv *priv,
                                          const GPUShaderResourceReflection *resource,
                                          GPUShaderStageFlags stages) {
  uint32_t resourceArrayCount;
  GPUShaderStageFlags requiredVisibility;

  if (!priv || !resource) {
    return 0;
  }

  requiredVisibility = resource->visibility & stages;
  if (requiredVisibility == 0u) {
    return 1;
  }

  resourceArrayCount = resource->arrayCount ? resource->arrayCount : 1u;
  for (uint32_t i = 0u; i < priv->count; i++) {
    const GPUBindGroupLayoutEntry *entry;

    entry = &priv->entries[i];
    if (entry->binding == resource->binding &&
        entry->bindingType == resource->bindingType &&
        (entry->visibility & requiredVisibility) == requiredVisibility &&
        entry->arrayCount == resourceArrayCount &&
        entry->hasDynamicOffset == resource->hasDynamicOffset) {
      return 1;
    }
  }

  return 0;
}

static int
gpu_layoutMatchesReflectionGroup(GPUBindGroupLayout *layout,
                                 const GPUShaderReflection *reflection,
                                 uint32_t groupIndex) {
  GPUBindGroupLayoutPriv *priv;
  uint32_t expectedCount;
  uint8_t *matched;

  priv = gpu_layoutPriv(layout);
  expectedCount = gpu_reflectionResourceCountForGroup(reflection, groupIndex);
  if (!priv || priv->count != expectedCount) {
    return 0;
  }

  if (expectedCount == 0u) {
    return 1;
  }

  matched = calloc(expectedCount, sizeof(*matched));
  if (!matched) {
    return 0;
  }

  for (uint32_t i = 0u; i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *resource;
    int found;

    resource = &reflection->pResources[i];
    if (resource->groupIndex != groupIndex) {
      continue;
    }

    found = 0;
    for (uint32_t j = 0u; j < priv->count; j++) {
      if (!matched[j] &&
          gpu_layoutMatchesReflectionResource(&priv->entries[j], resource)) {
        matched[j] = 1u;
        found = 1;
        break;
      }
    }

    if (!found) {
      free(matched);
      return 0;
    }
  }

  free(matched);
  return 1;
}

static int
gpu_pipelineLayoutMatchesShaderReflection(GPUPipelineLayout *pipelineLayout,
                                          const GPUShaderReflection *reflection,
                                          GPUShaderStageFlags stages,
                                          uint32_t *outRequiredGroupMask) {
  GPUPipelineLayoutPriv *pipelinePriv;
  uint32_t requiredGroupMask;
  uint32_t requiredCount;

  if (outRequiredGroupMask) {
    *outRequiredGroupMask = 0u;
  }
  pipelinePriv = gpu_pipelineLayoutPriv(pipelineLayout);
  if (!pipelinePriv) {
    return 0;
  }

  requiredGroupMask = 0u;
  requiredCount = gpu_reflectionLayoutCountForStages(reflection, stages);
  if (pipelinePriv->bindGroupLayoutCount < requiredCount) {
    return 0;
  }

  for (uint32_t i = 0u; reflection && i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *resource;
    GPUBindGroupLayoutPriv *layoutPriv;

    resource = &reflection->pResources[i];
    if ((resource->visibility & stages) == 0u) {
      continue;
    }
    if (resource->groupIndex >= pipelinePriv->bindGroupLayoutCount) {
      return 0;
    }

    layoutPriv = gpu_layoutPriv(pipelinePriv->bindGroupLayouts[resource->groupIndex]);
    if (!gpu_layoutContainsStageReflectionResource(layoutPriv, resource, stages)) {
      return 0;
    }
    requiredGroupMask |= 1u << resource->groupIndex;
  }

  if (outRequiredGroupMask) {
    *outRequiredGroupMask = requiredGroupMask;
  }
  return 1;
}

GPU_HIDE
int
gpuPipelineLayoutMatchesShaderEntries(GPUPipelineLayout *pipelineLayout,
                                      const GPUShaderLibrary *library,
                                      const char * const *entryPoints,
                                      uint32_t entryPointCount,
                                      GPUShaderStageFlags fallbackStages,
                                      uint32_t *outRequiredGroupMask) {
  GPUShaderReflection reflection;
  GPUResult rc;
  uint32_t combinedGroupMask;
  int ok;

  if (outRequiredGroupMask) {
    *outRequiredGroupMask = 0u;
  }
  if (!pipelineLayout || !library ||
      (entryPointCount > 0u && !entryPoints)) {
    return 0;
  }

  combinedGroupMask = 0u;
  if (gpuShaderLibraryHasEntryResourceInfo(library)) {
    for (uint32_t i = 0u; i < entryPointCount; i++) {
      uint32_t entryGroupMask;

      if (!entryPoints[i]) {
        return 0;
      }

      memset(&reflection, 0, sizeof(reflection));
      rc = gpuGetShaderEntryReflection(library, entryPoints[i], &reflection);
      if (rc != GPU_OK) {
        return 0;
      }

      ok = gpu_pipelineLayoutMatchesShaderReflection(pipelineLayout,
                                                     &reflection,
                                                     fallbackStages,
                                                     &entryGroupMask);
      GPUFreeShaderReflection(&reflection);
      if (!ok) {
        return 0;
      }
      combinedGroupMask |= entryGroupMask;
    }

    if (outRequiredGroupMask) {
      *outRequiredGroupMask = combinedGroupMask;
    }
    return 1;
  }

  memset(&reflection, 0, sizeof(reflection));
  rc = GPUGetShaderReflection(library, &reflection);
  if (rc != GPU_OK) {
    return 0;
  }

  ok = gpu_pipelineLayoutMatchesShaderReflection(pipelineLayout,
                                                 &reflection,
                                                 fallbackStages,
                                                 &combinedGroupMask);
  GPUFreeShaderReflection(&reflection);
  if (ok && outRequiredGroupMask) {
    *outRequiredGroupMask = combinedGroupMask;
  }
  return ok;
}

GPU_EXPORT
GPUResult
GPUCreateBindGroupLayoutsFromReflection(GPUDevice *device,
                                        const GPUShaderLibrary *library,
                                        uint32_t *inoutLayoutCount,
                                        GPUBindGroupLayout **outLayouts) {
  GPUShaderReflection reflection;
  uint32_t requiredCount;
  GPUResult rc;

  if (!library || !inoutLayoutCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(&reflection, 0, sizeof(reflection));
  rc = GPUGetShaderReflection(library, &reflection);
  if (rc != GPU_OK) {
    return rc;
  }

  requiredCount = gpu_reflectionLayoutCount(&reflection);
  if (!outLayouts) {
    *inoutLayoutCount = requiredCount;
    GPUFreeShaderReflection(&reflection);
    return GPU_OK;
  }

  if (*inoutLayoutCount < requiredCount) {
    *inoutLayoutCount = requiredCount;
    GPUFreeShaderReflection(&reflection);
    return GPU_ERROR_INSUFFICIENT_CAPACITY;
  }

  for (uint32_t i = 0; i < requiredCount; i++) {
    outLayouts[i] = NULL;
  }

  for (uint32_t i = 0; i < requiredCount; i++) {
    rc = gpu_createLayoutForReflectionGroup(device, library, &reflection, i, &outLayouts[i]);
    if (rc != GPU_OK) {
      for (uint32_t j = 0; j < i; j++) {
        GPUDestroyBindGroupLayout(outLayouts[j]);
        outLayouts[j] = NULL;
      }
      GPUFreeShaderReflection(&reflection);
      return rc;
    }
  }

  *inoutLayoutCount = requiredCount;
  GPUFreeShaderReflection(&reflection);
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreatePipelineLayoutFromReflection(GPUDevice *device,
                                      const GPUShaderLibrary *library,
                                      uint32_t bindGroupLayoutCount,
                                      GPUBindGroupLayout * const *ppLayouts,
                                      GPUPipelineLayout **outLayout) {
  GPUPipelineLayoutCreateInfo info;
  GPUShaderReflection reflection;
  uint32_t requiredCount;
  GPUResult rc;

  if (!outLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outLayout = NULL;
  if (!library || (bindGroupLayoutCount > 0u && !ppLayouts)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(&reflection, 0, sizeof(reflection));
  rc = GPUGetShaderReflection(library, &reflection);
  if (rc != GPU_OK) {
    return rc;
  }

  requiredCount = gpu_reflectionLayoutCount(&reflection);
  if (bindGroupLayoutCount < requiredCount) {
    GPUFreeShaderReflection(&reflection);
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  for (uint32_t i = 0u; i < requiredCount; i++) {
    if (!gpu_layoutMatchesReflectionGroup(ppLayouts[i], &reflection, i)) {
      GPUFreeShaderReflection(&reflection);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  memset(&info, 0, sizeof(info));
  info.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.bindGroupLayoutCount = bindGroupLayoutCount;
  info.ppBindGroupLayouts = ppLayouts;
  info.pushConstantSizeBytes = reflection.pushConstantSizeBytes;
  info.pushConstantStages = reflection.pushConstantSizeBytes > 0u
                               ? (GPU_SHADER_STAGE_VERTEX_BIT |
                                  GPU_SHADER_STAGE_FRAGMENT_BIT |
                                  GPU_SHADER_STAGE_COMPUTE_BIT)
                               : 0u;
  rc = GPUCreatePipelineLayout(device, &info, outLayout);
  GPUFreeShaderReflection(&reflection);
  return rc;
}

GPU_EXPORT
GPUResult
GPUCreateShaderLayout(GPUDevice *device,
                      const GPUShaderLibrary *library,
                      GPUShaderLayout **outLayout) {
  GPUShaderLayout *layout;
  uint32_t layoutCount;
  GPUResult rc;

  if (!outLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLayout = NULL;
  if (!library) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layout = calloc(1, sizeof(*layout));
  if (!layout) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  layoutCount = 0u;
  rc = GPUCreateBindGroupLayoutsFromReflection(device, library, &layoutCount, NULL);
  if (rc != GPU_OK) {
    free(layout);
    return rc;
  }

  if (layoutCount > 0u) {
    if ((size_t)layoutCount > SIZE_MAX / sizeof(*layout->bindGroupLayouts)) {
      free(layout);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    layout->bindGroupLayouts = calloc(layoutCount, sizeof(*layout->bindGroupLayouts));
    if (!layout->bindGroupLayouts) {
      free(layout);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    rc = GPUCreateBindGroupLayoutsFromReflection(device,
                                                 library,
                                                 &layoutCount,
                                                 layout->bindGroupLayouts);
    if (rc != GPU_OK) {
      GPUDestroyShaderLayout(layout);
      return rc;
    }
  }

  layout->bindGroupLayoutCount = layoutCount;
  rc = GPUCreatePipelineLayoutFromReflection(device,
                                             library,
                                             layout->bindGroupLayoutCount,
                                             layout->bindGroupLayouts,
                                             &layout->pipelineLayout);
  if (rc != GPU_OK) {
    GPUDestroyShaderLayout(layout);
    return rc;
  }

  *outLayout = layout;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyShaderLayout(GPUShaderLayout *layout) {
  if (!layout) {
    return;
  }

  if (layout->pipelineLayout) {
    GPUDestroyPipelineLayout(layout->pipelineLayout);
  }
  if (layout->bindGroupLayouts) {
    for (uint32_t i = 0; i < layout->bindGroupLayoutCount; i++) {
      GPUDestroyBindGroupLayout(layout->bindGroupLayouts[i]);
    }
  }

  free(layout->bindGroupLayouts);
  free(layout);
}

GPU_EXPORT
GPUResult
GPUCreateBindGroup(GPUDevice *device,
                   const GPUBindGroupCreateInfo *info,
                   GPUBindGroup **outGroup) {
  GPUBindGroup *cached;
  GPUBindGroup *group;
  GPUBindGroupPriv *priv;
  GPUBindGroupLayoutPriv *layoutPriv;
  GPUBindGroupLayout *layout;
  const GPUBindGroupEntry *entries;
  GPUApi *api;
  GPUResult result;
  uint32_t count;
  uint32_t i;

  if (!outGroup) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outGroup = NULL;
  if (!device || !info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layout = info->layout;
  entries = info->pEntries;
  count = info->entryCount;
  if (!layout || layout->_device != device || (!entries && count > 0)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layoutPriv = gpu_layoutPriv(layout);
  if (!layoutPriv) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  {
    GPUResult validationResult;

    validationResult = gpu_validateBindGroupEntries(layoutPriv, entries, count);
    if (validationResult != GPU_OK) {
      return validationResult;
    }
  }

  group = calloc(1, sizeof(*group));
  priv = calloc(1, sizeof(*priv));
  if (!group || !priv) {
    free(group);
    free(priv);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if ((size_t)layoutPriv->count > SIZE_MAX / sizeof(*priv->bindings)) {
    free(priv);
    free(group);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (layoutPriv->count > 0) {
    priv->bindings = calloc(layoutPriv->count, sizeof(*priv->bindings));
    if (!priv->bindings) {
      free(priv);
      free(group);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  for (i = 0; i < layoutPriv->count; i++) {
    const GPUBindGroupLayoutEntry *layoutEntry;
    const GPUBindGroupEntry *entry;

    layoutEntry = &layoutPriv->entries[i];
    entry = gpu_findBindGroupEntry(entries,
                                   count,
                                   layoutEntry);
    if (!gpu_bindGroupEntryHasResource(layoutEntry, entry)) {
      free(priv->bindings);
      free(priv);
      free(group);
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    priv->bindings[i].stage = layoutEntry->stage;
    priv->bindings[i].binding = layoutEntry->binding;
    priv->bindings[i].kind = layoutEntry->kind;
    if (entry) {
      priv->bindings[i].buffer = entry->buffer.buffer;
      priv->bindings[i].textureView = entry->textureView;
      priv->bindings[i].sampler = entry->sampler;
      priv->bindings[i].offset = entry->buffer.offset;
      priv->bindings[i].size = entry->buffer.size;
    }
  }

  priv->layout     = layout;
  priv->count      = layoutPriv->count;
  priv->hash       = gpu_bindGroupHash(priv);
  group->_device   = device;
  group->_priv     = priv;
  group->_refCount = 1u;

  cached = gpu_bindGroupCacheFind(device, group);
  if (cached) {
    free(priv->bindings);
    free(priv);
    free(group);
    *outGroup = cached;
    return GPU_OK;
  }

  api = gpuDeviceApi(device);
  if (api && api->descriptor.createBindGroup) {
    result = api->descriptor.createBindGroup(device, group);
    if (result != GPU_OK) {
      GPUDestroyBindGroup(group);
      return result;
    }
  }

  cached = gpu_bindGroupCacheStore(device, group);
  if (cached != group) {
    if (api && api->descriptor.destroyBindGroup) {
      api->descriptor.destroyBindGroup(group);
    }
    free(priv->bindings);
    free(priv);
    free(group);
    group = cached;
  }

  *outGroup = group;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyBindGroup(GPUBindGroup *group) {
  GPUBindGroupPriv *priv;
  GPUApi *api;

  if (!group) {
    return;
  }
  if (!gpu_releaseBindGroup(group)) {
    return;
  }

  api = gpuDeviceApi(group->_device);
  if (api && api->descriptor.destroyBindGroup) {
    api->descriptor.destroyBindGroup(group);
  }

  priv = gpu_groupPriv(group);
  if (priv) {
    free(priv->bindings);
    free(priv);
  }

  free(group);
}

static void
gpuBindRenderBinding(void *ctx, const GPUBindGroupBindingView *binding) {
  GPUBindRenderContext *bindCtx;

  if (!ctx || !binding) {
    return;
  }

  bindCtx = ctx;
  if ((binding->visibility & GPU_SHADER_STAGE_VERTEX_BIT) != 0) {
    if (binding->kind == GPUBindKindBuffer && binding->buffer) {
      gpuSetRenderVertexBuffer(bindCtx->pass,
                               binding->buffer,
                               binding->offset,
                               binding->binding);
    } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
      gpuSetRenderVertexTexture(bindCtx->pass,
                                binding->textureView,
                                binding->binding);
    } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
      gpuSetRenderVertexSampler(bindCtx->pass,
                                binding->sampler,
                                binding->binding);
    }
  }

  if ((binding->visibility & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0) {
    if (binding->kind == GPUBindKindBuffer && binding->buffer) {
      gpuSetRenderFragmentBuffer(bindCtx->pass,
                                 binding->buffer,
                                 binding->offset,
                                 binding->binding);
    } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
      gpuSetRenderFragmentTexture(bindCtx->pass,
                                  binding->textureView,
                                  binding->binding);
    } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
      gpuSetRenderFragmentSampler(bindCtx->pass,
                                  binding->sampler,
                                  binding->binding);
    }
  }
}

GPU_EXPORT
void
GPUBindRenderGroup(GPURenderPassEncoder *pass,
                   uint32_t groupIndex,
                   GPUBindGroup *group,
                   uint32_t dynamicOffsetCount,
                   const uint32_t *pDynamicOffsets) {
  GPUBindRenderContext ctx;
  GPUDevice           *device;
  GPUApi              *api;

  if (!pass || pass->_ended || !group ||
      groupIndex >= GPU_ENCODER_MAX_BIND_GROUPS ||
      !gpuPipelineLayoutAcceptsBindGroup(pass->_pipelineLayout, groupIndex, group)) {
    return;
  }
  device = gpuBindGroupGetDevice(group);
  gpuDeviceRecordBindRequest(device);
  if (dynamicOffsetCount == 0u &&
      pass->_boundGroups[groupIndex] == group &&
      gpuBindGroupDynamicOffsetCount(group) == 0u) {
    return;
  }

  api = gpuDeviceApi(device);
  if (api && api->descriptor.bindRenderGroup) {
    if (gpuValidateBindGroupDynamicOffsets(pass->_pipelineLayout,
                                           groupIndex,
                                           group,
                                           dynamicOffsetCount,
                                           pDynamicOffsets) &&
        api->descriptor.bindRenderGroup(pass,
                                        pass->_pipelineLayout,
                                        groupIndex,
                                        group,
                                        dynamicOffsetCount,
                                        pDynamicOffsets)) {
      pass->_boundGroups[groupIndex] = group;
      pass->_boundGroupLayouts[groupIndex] = gpuBindGroupGetLayout(group);
      gpuDeviceRecordBindEmission(device);
    }
    return;
  }

  ctx.pass = pass;
  if (gpuForEachBindGroupBindingWithDynamicOffsets(pass->_pipelineLayout,
                                                   groupIndex,
                                                   group,
                                                   dynamicOffsetCount,
                                                   pDynamicOffsets,
                                                   gpuBindRenderBinding,
                                                   &ctx)) {
    pass->_boundGroups[groupIndex] = group;
    pass->_boundGroupLayouts[groupIndex] = gpuBindGroupGetLayout(group);
    gpuDeviceRecordBindEmission(device);
  }
}

GPU_HIDE
int
gpuForEachBindGroupBinding(GPUBindGroup *group,
                           GPUBindGroupBindingFn fn,
                           void *ctx) {
  GPUBindGroupPriv       *priv;
  GPUBindGroupLayoutPriv *layout;

  if (!group || !fn) {
    return 0;
  }

  priv   = gpu_groupPriv(group);
  layout = gpu_layoutPriv(priv ? priv->layout : NULL);
  if (!priv || !layout || priv->count < layout->count ||
      (layout->count > 0u && !layout->backendBindings)) {
    return 0;
  }

  for (uint32_t i = 0u; i < layout->count; i++) {
    const GPUBindGroupLayoutEntry *layoutEntry;
    const GPUBindGroupBindingPriv *binding;
    GPUBindGroupBindingView        view;

    layoutEntry = &layout->entries[i];
    binding     = &priv->bindings[i];
    if (!gpu_bindGroupLayoutEntryNeedsBinding(layoutEntry)) {
      continue;
    }

    memset(&view, 0, sizeof(view));
    view.buffer           = binding->buffer;
    view.textureView      = binding->textureView;
    view.sampler          = binding->sampler;
    view.offset           = binding->offset;
    view.size             = binding->size;
    view.visibility       = layoutEntry->visibility;
    view.bindingType      = layoutEntry->bindingType;
    view.binding          = layout->backendBindings[i];
    view.stage            = binding->stage;
    view.kind             = binding->kind;
    view.hasDynamicOffset = layoutEntry->hasDynamicOffset;
    fn(ctx, &view);
  }

  return 1;
}

GPU_HIDE
int
gpuValidateBindGroupDynamicOffsets(GPUPipelineLayout *pipelineLayout,
                                   uint32_t groupIndex,
                                   GPUBindGroup *group,
                                   uint32_t dynamicOffsetCount,
                                   const uint32_t *dynamicOffsets) {
  GPUBindGroupPriv *priv;
  GPUBindGroupLayoutPriv *layout;
  GPUPipelineLayoutPriv *pipeline;
  uint32_t dynamicIndex;

  if (!pipelineLayout || !group ||
      (dynamicOffsetCount > 0u && !dynamicOffsets)) {
    return 0;
  }

  priv = gpu_groupPriv(group);
  layout = gpu_layoutPriv(priv ? priv->layout : NULL);
  pipeline = gpu_pipelineLayoutPriv(pipelineLayout);
  if (!priv || !layout || !pipeline ||
      groupIndex >= pipeline->bindGroupLayoutCount ||
      pipeline->bindGroupLayouts[groupIndex] != priv->layout ||
      (layout->count > 0u &&
       (!pipeline->backendBindings || !pipeline->backendBindings[groupIndex])) ||
      priv->count < layout->count) {
    return 0;
  }

  dynamicIndex = 0u;
  for (uint32_t i = 0; i < layout->count; i++) {
    const GPUBindGroupLayoutEntry *layoutEntry;
    const GPUBindGroupBindingPriv *binding;
    uint64_t effectiveOffset;

    layoutEntry = &layout->entries[i];
    if (!layoutEntry->hasDynamicOffset) {
      continue;
    }

    binding = &priv->bindings[i];
    if (layoutEntry->kind != GPUBindKindBuffer ||
        dynamicIndex >= dynamicOffsetCount ||
        !gpu_u64Add(binding->offset,
                    dynamicOffsets[dynamicIndex],
                    &effectiveOffset) ||
        !gpuBufferRangeValid(binding->buffer, effectiveOffset, binding->size)) {
      return 0;
    }
    dynamicIndex++;
  }

  return dynamicIndex == dynamicOffsetCount;
}

GPU_HIDE
int
gpuForEachBindGroupBindingWithDynamicOffsets(GPUPipelineLayout *pipelineLayout,
                                             uint32_t groupIndex,
                                             GPUBindGroup *group,
                                             uint32_t dynamicOffsetCount,
                                             const uint32_t *pDynamicOffsets,
                                             GPUBindGroupBindingFn fn,
                                             void *ctx) {
  GPUBindGroupPriv *priv;
  GPUBindGroupLayoutPriv *layout;
  GPUPipelineLayoutPriv *pipeline;
  uint32_t dynamicIndex;
  if (!fn ||
      !gpuValidateBindGroupDynamicOffsets(pipelineLayout,
                                          groupIndex,
                                          group,
                                          dynamicOffsetCount,
                                          pDynamicOffsets)) {
    return 0;
  }

  priv = gpu_groupPriv(group);
  layout = gpu_layoutPriv(priv ? priv->layout : NULL);
  pipeline = gpu_pipelineLayoutPriv(pipelineLayout);
  if (!priv || !layout || !pipeline ||
      groupIndex >= pipeline->bindGroupLayoutCount ||
      pipeline->bindGroupLayouts[groupIndex] != priv->layout ||
      (layout->count > 0u &&
       (!pipeline->backendBindings || !pipeline->backendBindings[groupIndex])) ||
      priv->count < layout->count) {
    return 0;
  }

  dynamicIndex = 0u;
  for (uint32_t i = 0; i < layout->count; i++) {
    const GPUBindGroupLayoutEntry *layoutEntry;
    const GPUBindGroupBindingPriv *binding;
    GPUBindGroupBindingView view;
    uint64_t effectiveOffset;

    layoutEntry = &layout->entries[i];
    binding = &priv->bindings[i];
    if (!gpu_bindGroupLayoutEntryNeedsBinding(layoutEntry)) {
      continue;
    }
    effectiveOffset = binding->offset;
    if (layoutEntry->hasDynamicOffset) {
      if (layoutEntry->kind != GPUBindKindBuffer ||
          dynamicIndex >= dynamicOffsetCount ||
          !gpu_u64Add(effectiveOffset,
                      pDynamicOffsets[dynamicIndex],
                      &effectiveOffset)) {
        return 0;
      }
      dynamicIndex++;
    }
    if (layoutEntry->kind == GPUBindKindBuffer &&
        !gpuBufferRangeValid(binding->buffer, effectiveOffset, binding->size)) {
      return 0;
    }

    memset(&view, 0, sizeof(view));
    view.buffer           = binding->buffer;
    view.textureView      = binding->textureView;
    view.sampler          = binding->sampler;
    view.offset           = effectiveOffset;
    view.size             = binding->size;
    view.visibility       = layoutEntry->visibility;
    view.bindingType      = layoutEntry->bindingType;
    view.binding          = pipeline->backendBindings[groupIndex][i];
    view.stage            = binding->stage;
    view.kind             = binding->kind;
    view.hasDynamicOffset = layoutEntry->hasDynamicOffset;
    fn(ctx, &view);
  }

  return dynamicIndex == dynamicOffsetCount;
}
