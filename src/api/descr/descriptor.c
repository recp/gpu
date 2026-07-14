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
#include "../sampler_internal.h"
#include "../texture_internal.h"
#include "descriptor_internal.h"

#include <limits.h>

#if !defined(_WIN32) && !defined(WIN32)
#  include <pthread.h>
#endif

#define GPU_PUSH_CONSTANT_MAX_SIZE_BYTES 4096u

enum {
  GPU_BIND_GROUP_CACHE_CAPACITY       = 256u,
  GPU_BIND_GROUP_CACHE_MASK           = GPU_BIND_GROUP_CACHE_CAPACITY - 1u,
  GPU_BIND_GROUP_CANDIDATE_STACK_SIZE = 16u,
  GPU_BIND_GROUP_MATCH_STACK_WORDS    = 4u
};

/* Cache entries intern live groups without extending resource lifetimes. */

typedef struct GPUBindGroupLayoutPriv {
  GPUBindGroupLayoutEntry *entries;
  uint32_t                *backendBindings;
  uint32_t                 count;
  bool                     hasBackendBindings;
  bool                     bindless;
} GPUBindGroupLayoutPriv;

typedef struct GPUBindGroupBindingPriv {
  union {
    struct {
      GPUBuffer *buffer;
      uint64_t   offset;
      uint64_t   size;
    };

    GPUTextureView *textureView;
    GPUSampler     *sampler;
  };
  uint32_t    binding;
  uint32_t    arrayIndex;
  uint32_t    layoutEntryIndex;
  uint32_t    dynamicOffsetIndex;
  uint32_t    kindIndex;
  GPUBindKind kind;
} GPUBindGroupBindingPriv;

typedef struct GPUBindGroupPriv {
  GPUBindGroupLayout       *layout;
  GPUBindGroupBindingPriv  *bindings;
  uint64_t                 *updateScratch;
  uint64_t                 hash;
  uint32_t                 count;
  uint32_t                 dynamicOffsetCount;
  bool                     bindless;
} GPUBindGroupPriv;

/* The public handle, cache key, and binding list have one lifetime. */
typedef struct GPUBindGroupStorage {
  GPUBindGroup            group;
  GPUBindGroupPriv        priv;
  GPUBindGroupBindingPriv bindings[];
} GPUBindGroupStorage;

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
gpu_layoutPriv(const GPUBindGroupLayout *layout) {
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
    uintptr_t                      resource;

    binding = &priv->bindings[i];
    hash    = GPU_BIND_GROUP_HASH(hash, binding->binding);
    hash    = GPU_BIND_GROUP_HASH(hash, binding->arrayIndex);
    hash    = GPU_BIND_GROUP_HASH(hash, binding->kind);
    switch (binding->kind) {
      case GPUBindKindBuffer:
        resource = (uintptr_t)binding->buffer;
        hash     = GPU_BIND_GROUP_HASH(hash, resource);
        hash     = GPU_BIND_GROUP_HASH(hash, binding->offset);
        hash     = GPU_BIND_GROUP_HASH(hash, binding->size);
        break;
      case GPUBindKindTexture:
        resource = (uintptr_t)binding->textureView;
        hash     = GPU_BIND_GROUP_HASH(hash, resource);
        break;
      case GPUBindKindSampler:
        resource = (uintptr_t)binding->sampler;
        hash     = GPU_BIND_GROUP_HASH(hash, resource);
        break;
      default:
        break;
    }
  }
  return hash;
}

static bool
gpu_bindGroupPrivsEqual(const GPUBindGroupPriv *a,
                        const GPUBindGroupPriv *b) {
  if (!a || !b || a->hash != b->hash ||
      a->layout != b->layout || a->count != b->count) {
    return false;
  }

  for (uint32_t i = 0u; i < a->count; i++) {
    const GPUBindGroupBindingPriv *aBinding;
    const GPUBindGroupBindingPriv *bBinding;

    aBinding = &a->bindings[i];
    bBinding = &b->bindings[i];
    if (aBinding->binding != bBinding->binding ||
        aBinding->arrayIndex != bBinding->arrayIndex ||
        aBinding->kind != bBinding->kind) {
      return false;
    }
    switch (aBinding->kind) {
      case GPUBindKindBuffer:
        if (aBinding->buffer != bBinding->buffer ||
            aBinding->offset != bBinding->offset ||
            aBinding->size != bBinding->size) {
          return false;
        }
        break;
      case GPUBindKindTexture:
        if (aBinding->textureView != bBinding->textureView) {
          return false;
        }
        break;
      case GPUBindKindSampler:
        if (aBinding->sampler != bBinding->sampler) {
          return false;
        }
        break;
      default:
        return false;
    }
  }
  return true;
}

static GPUBindGroup *
gpu_bindGroupCacheFind(GPUDevice *device,
                       const GPUBindGroupPriv *candidate) {
  GPUBindGroupCache      *cache;
  GPUBindGroupCacheEntry *entry;
  GPUBindGroup           *result;

  cache  = gpu_bindGroupCache(device);
  result = NULL;
  if (!cache || !candidate) {
    return NULL;
  }

  entry = &cache->entries[candidate->hash & GPU_BIND_GROUP_CACHE_MASK];
  gpu_bindGroupCacheLock(cache);
  if (entry->group &&
      gpu_bindGroupPrivsEqual(gpu_groupPriv(entry->group), candidate)) {
    result = entry->group;
    result->_refCount++;
    gpuDeviceCacheCounterAdd(&device->cacheStats.bindGroupHits, 1u);
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
  if (entry->group &&
      gpu_bindGroupPrivsEqual(gpu_groupPriv(entry->group), priv)) {
    result = entry->group;
    result->_refCount++;
    gpuDeviceCacheCounterAdd(&device->cacheStats.bindGroupHits, 1u);
  } else {
    if (entry->group) {
      gpuDeviceCacheCounterAdd(&device->cacheStats.bindGroupCollisions, 1u);
    }
    gpuDeviceCacheCounterAdd(&device->cacheStats.bindGroupMisses, 1u);
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

static GPUBindKind
gpu_layoutEntryKind(const GPUBindGroupLayoutEntry *entry);

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
uint32_t
gpuPipelineLayoutBackendSlotMask(GPUPipelineLayout  *layout,
                                 GPUBindKind         kind,
                                 GPUShaderStageFlags stages) {
  GPUPipelineLayoutPriv *priv;
  uint32_t               mask;

  priv = gpu_pipelineLayoutPriv(layout);
  mask = 0u;
  if (!priv || !priv->backendBindings) {
    return 0u;
  }

  for (uint32_t groupIndex = 0u;
       groupIndex < priv->bindGroupLayoutCount;
       groupIndex++) {
    GPUBindGroupLayoutPriv *group;

    group = gpu_layoutPriv(priv->bindGroupLayouts[groupIndex]);
    if (!group || !priv->backendBindings[groupIndex]) {
      continue;
    }
    for (uint32_t entryIndex = 0u; entryIndex < group->count; entryIndex++) {
      const GPUBindGroupLayoutEntry *entry;
      uint32_t                       binding;

      entry   = &group->entries[entryIndex];
      binding = priv->backendBindings[groupIndex][entryIndex];
      if (gpu_layoutEntryKind(entry) == kind &&
          (entry->visibility & stages) != 0u &&
          binding != UINT32_MAX) {
        for (uint32_t arrayIndex = 0u;
             arrayIndex < entry->arrayCount &&
             binding <= UINT32_MAX - arrayIndex;
             arrayIndex++) {
          uint32_t slot;

          slot = binding + arrayIndex;
          if (slot < 32u) {
            mask |= 1u << slot;
          }
        }
      }
    }
  }

  return mask;
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
const uint32_t *
gpuGetPipelineLayoutBackendBindings(GPUPipelineLayout *layout,
                                    uint32_t groupIndex,
                                    uint32_t *outCount) {
  GPUPipelineLayoutPriv *priv;
  GPUBindGroupLayoutPriv *group;

  priv  = gpu_pipelineLayoutPriv(layout);
  group = priv && groupIndex < priv->bindGroupLayoutCount
            ? gpu_layoutPriv(priv->bindGroupLayouts[groupIndex])
            : NULL;
  if (outCount) {
    *outCount = group ? group->count : 0u;
  }
  if (!group || !priv->backendBindings ||
      !priv->backendBindings[groupIndex]) {
    return NULL;
  }

  return priv->backendBindings[groupIndex];
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
bool
gpuBindGroupLayoutIsBindless(GPUBindGroupLayout *layout) {
  GPUBindGroupLayoutPriv *priv;

  priv = gpu_layoutPriv(layout);
  return priv && priv->bindless;
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
gpu_visibilityIsValid(GPUShaderStageFlags visibility) {
  const GPUShaderStageFlags knownStages = GPU_SHADER_STAGE_VERTEX_BIT |
                                         GPU_SHADER_STAGE_FRAGMENT_BIT |
                                         GPU_SHADER_STAGE_COMPUTE_BIT |
                                         GPU_SHADER_STAGE_TASK_BIT |
                                         GPU_SHADER_STAGE_MESH_BIT;

  return visibility != 0u && (visibility & ~knownStages) == 0u;
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

static GPUBindKind
gpu_layoutEntryKind(const GPUBindGroupLayoutEntry *entry) {
  GPUBindKind kind;

  if (!entry || !gpu_kindFromBindingType(entry->bindingType, &kind)) {
    return GPUBindKindCount;
  }
  return kind;
}

static int
gpu_normalizeLayoutEntry(const GPUBindGroupLayoutEntry *src,
                         GPUBindGroupLayoutEntry *dst) {
  GPUBindKind kind;

  if (!src || !dst) {
    return 0;
  }

  if (!gpu_visibilityIsValid(src->visibility) ||
      !gpu_kindFromBindingType(src->bindingType, &kind)) {
    return 0;
  }

  *dst = *src;
  if (src->arrayCount == 0) {
    dst->arrayCount = 1;
  }
  return 1;
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
                          uint32_t                       count,
                          bool                           bindless) {
  bool hasResourceArray;
  uint32_t i;

  if (!entries && count > 0) {
    return 0;
  }

  hasResourceArray = false;
  for (i = 0; i < count; i++) {
    GPUBindGroupLayoutEntry entry;
    GPUBindKind kind;

    if (!gpu_normalizeLayoutEntry(&entries[i], &entry) ||
        !gpu_kindFromBindingType(entry.bindingType, &kind) ||
        (entry.hasDynamicOffset && kind != GPUBindKindBuffer) ||
        (entry.immutableSampler &&
         (kind != GPUBindKindSampler ||
          !gpu_samplerDescIsValid(&entry.immutableSamplerDesc))) ||
        (bindless && entry.hasDynamicOffset) ||
        gpu_layoutEntryDuplicateExists(entries,
                                       i,
                                       entry.binding)) {
      return 0;
    }
    if (entry.arrayCount > 1u &&
        !(entry.immutableSampler && kind == GPUBindKindSampler)) {
      hasResourceArray = true;
    }
  }

  return !bindless || hasResourceArray;
}

static GPUResult
gpu_bindlessLayoutEnabled(GPUDevice                           *device,
                          const GPUBindGroupLayoutCreateInfo  *info,
                          bool                                *outEnabled,
                          const GPUBindGroupLayout           **outSourceLayout) {
  const GPUBindGroupLayout *sourceLayout;
  const GPUChainedStruct   *chain;
  bool                      enabled;

  if (!device || !info || !outEnabled || !outSourceLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  chain        = info->chain.pNext;
  sourceLayout = NULL;
  enabled      = false;
  while (chain) {
    const GPUBindlessLayoutEXT *bindlessInfo;

    if (chain->sType != GPU_STRUCTURE_TYPE_BINDLESS_LAYOUT_EXT ||
        enabled ||
        (chain->structSize != 0u &&
         chain->structSize < sizeof(GPUBindlessLayoutEXT))) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    bindlessInfo = (const GPUBindlessLayoutEXT *)chain;
    sourceLayout = bindlessInfo->sourceLayout;
    enabled      = true;
    chain        = chain->pNext;
  }

  if (enabled && !GPUIsFeatureEnabled(device, GPU_FEATURE_BINDLESS)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (sourceLayout && sourceLayout->_device != device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outEnabled      = enabled;
  *outSourceLayout = sourceLayout;
  return GPU_OK;
}

static int
gpu_bindGroupEntryHasResource(const GPUBindGroupLayoutEntry *layoutEntry,
                              const GPUBindGroupEntry *entry) {
  GPUBindKind kind;

  if (!layoutEntry) {
    return 0;
  }

  kind = gpu_layoutEntryKind(layoutEntry);

  if (!entry) {
    return layoutEntry->immutableSampler &&
           kind == GPUBindKindSampler;
  }

  switch (kind) {
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
gpu_bindGroupEntryMatchesDevice(GPUDevice                     *device,
                                const GPUBindGroupLayoutEntry *layoutEntry,
                                const GPUBindGroupEntry       *entry) {
  GPUBindKind kind;

  if (!device || !layoutEntry || !entry) {
    return 0;
  }

  kind = gpu_layoutEntryKind(layoutEntry);
  switch (kind) {
    case GPUBindKindBuffer:
      return entry->buffer.buffer && entry->buffer.buffer->device == device;
    case GPUBindKindTexture:
      return entry->textureView && entry->textureView->_texture &&
             entry->textureView->_texture->device == device;
    case GPUBindKindSampler:
      return entry->sampler && entry->sampler->device == device;
    default:
      return 0;
  }
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

  if (!layoutEntry || !entry ||
      gpu_layoutEntryKind(layoutEntry) != GPUBindKindBuffer) {
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

  if (!layoutEntry || !entry ||
      gpu_layoutEntryKind(layoutEntry) != GPUBindKindTexture) {
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
  GPUBindKind layoutKind;

  if (!layoutEntry || !entry ||
      entry->binding != layoutEntry->binding ||
      entry->arrayIndex >= layoutEntry->arrayCount ||
      entry->bindingType != layoutEntry->bindingType) {
    return 0;
  }

  layoutKind = gpu_layoutEntryKind(layoutEntry);
  if (layoutEntry->immutableSampler &&
      layoutKind == GPUBindKindSampler) {
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
                       const GPUBindGroupLayoutEntry *layoutEntry,
                       uint32_t arrayIndex) {
  uint32_t i;

  if (!entries || !layoutEntry) {
    return NULL;
  }

  for (i = 0; i < count; i++) {
    if (entries[i].arrayIndex == arrayIndex &&
        gpu_bindGroupEntryMatchesLayout(layoutEntry, &entries[i])) {
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

  return !(entry->immutableSampler &&
           gpu_layoutEntryKind(entry) == GPUBindKindSampler);
}

static int
gpu_bindGroupRuntimeCount(const GPUBindGroupLayoutPriv *layout,
                          uint32_t                     *outCount) {
  uint32_t count;

  if (!layout || !outCount) {
    return 0;
  }

  count = 0u;
  for (uint32_t i = 0u; i < layout->count; i++) {
    if (!gpu_bindGroupLayoutEntryNeedsBinding(&layout->entries[i])) {
      continue;
    }
    if (layout->entries[i].arrayCount > UINT32_MAX - count) {
      return 0;
    }
    count += layout->entries[i].arrayCount;
  }

  *outCount = count;
  return 1;
}

static int
gpu_bindGroupRuntimeBase(const GPUBindGroupLayoutPriv *layout,
                         uint32_t                      layoutEntryIndex,
                         uint32_t                     *outBase) {
  uint32_t base;

  if (!layout || !outBase || layoutEntryIndex >= layout->count) {
    return 0;
  }

  base = 0u;
  for (uint32_t i = 0u; i < layoutEntryIndex; i++) {
    if (!gpu_bindGroupLayoutEntryNeedsBinding(&layout->entries[i])) {
      continue;
    }
    if (layout->entries[i].arrayCount > UINT32_MAX - base) {
      return 0;
    }
    base += layout->entries[i].arrayCount;
  }

  *outBase = base;
  return 1;
}

static int
gpu_bindGroupDynamicBase(const GPUBindGroupLayoutPriv *layout,
                         uint32_t                      binding,
                         uint32_t                     *outBase) {
  uint32_t base;

  if (!layout || !outBase) {
    return 0;
  }

  base = 0u;
  for (uint32_t i = 0u; i < layout->count; i++) {
    const GPUBindGroupLayoutEntry *entry;

    entry = &layout->entries[i];
    if (!entry->hasDynamicOffset || entry->binding >= binding) {
      continue;
    }
    if (entry->arrayCount > UINT32_MAX - base) {
      return 0;
    }
    base += entry->arrayCount;
  }

  *outBase = base;
  return 1;
}

static GPUResult
gpu_validateBindGroupEntries(const GPUBindGroupLayoutPriv *layoutPriv,
                             const GPUBindGroupEntry       *entries,
                             uint32_t                       count,
                             bool                           allowPartial) {
  uint64_t *matched;
  uint64_t  stackMatched[GPU_BIND_GROUP_MATCH_STACK_WORDS] = {0};
  size_t    matchedWordCount;
  uint32_t  runtimeCount;

  if (!layoutPriv || (!entries && count > 0)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!gpu_bindGroupRuntimeCount(layoutPriv, &runtimeCount) ||
      count > runtimeCount || (!allowPartial && count != runtimeCount)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  matched          = stackMatched;
  matchedWordCount = runtimeCount / 64u;
  if (runtimeCount % 64u != 0u) {
    matchedWordCount++;
  }
  if (matchedWordCount > GPU_ARRAY_LEN(stackMatched)) {
    matched = calloc(matchedWordCount, sizeof(*matched));
    if (!matched) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }
  }

  for (uint32_t i = 0; i < count; i++) {
    uint64_t matchMask;
    size_t   matchWord;
    uint32_t matchIndex;
    uint32_t matchBase;
    uint32_t matchSlot;

    matchIndex = UINT32_MAX;
    for (uint32_t j = 0; j < layoutPriv->count; j++) {
      if (!gpu_bindGroupEntryMatchesLayout(&layoutPriv->entries[j],
                                           &entries[i])) {
        continue;
      }
      matchIndex = j;
      break;
    }

    if (matchIndex == UINT32_MAX ||
        !gpu_bindGroupRuntimeBase(layoutPriv, matchIndex, &matchBase) ||
        entries[i].arrayIndex > UINT32_MAX - matchBase) {
      goto invalid;
    }

    matchSlot = matchBase + entries[i].arrayIndex;
    matchWord = matchSlot / 64u;
    matchMask = 1ull << (matchSlot % 64u);
    if ((matched[matchWord] & matchMask) != 0u) {
      goto invalid;
    }
    matched[matchWord] |= matchMask;
  }

  if (matched != stackMatched) {
    free(matched);
  }
  return GPU_OK;

invalid:
  if (matched != stackMatched) {
    free(matched);
  }
  return GPU_ERROR_INVALID_ARGUMENT;
}

static GPUResult
gpu_createBindGroupLayout(GPUDevice *device,
                          const GPUBindGroupLayoutCreateInfo *info,
                          const uint32_t *backendBindings,
                          GPUBindGroupLayout **outLayout) {
  GPUBindGroupLayout *layout;
  GPUBindGroupLayoutPriv *priv;
  const GPUBindGroupLayoutPriv *sourcePriv;
  const GPUBindGroupLayout *sourceLayout;
  const GPUBindGroupLayoutEntry *entries;
  GPUApi *api;
  GPUResult result;
  uint32_t count;
  bool bindless;

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

  result = gpu_bindlessLayoutEnabled(device,
                                     info,
                                     &bindless,
                                     &sourceLayout);
  if (result != GPU_OK) {
    return result;
  }

  entries    = info->pEntries;
  count      = info->entryCount;
  sourcePriv = sourceLayout ? gpu_layoutPriv(sourceLayout) : NULL;
  if (sourceLayout) {
    if (!sourcePriv || backendBindings || count != 0u || entries) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    entries         = sourcePriv->entries;
    count           = sourcePriv->count;
    backendBindings = sourcePriv->backendBindings;
  }
  if (!gpu_validateLayoutEntries(entries, count, bindless)) {
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
  priv->bindless            = bindless;
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
GPUGetBindGroupLayoutEntries(const GPUBindGroupLayout *layout,
                             uint32_t                 *outCount) {
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
      if (gpu_layoutEntryKind(&layout->entries[entryIndex]) == kind &&
          (layout->entries[entryIndex].visibility & visibility) != 0u &&
          priv->backendBindings[groupIndex][entryIndex] <= slot) {
        uint32_t base;

        base = priv->backendBindings[groupIndex][entryIndex];
        if (slot - base < layout->entries[entryIndex].arrayCount) {
          return 1;
        }
      }
    }
  }

  return 0;
}

static int
gpu_pipelineRangeIsUsed(const GPUPipelineLayoutPriv *priv,
                        GPUBindKind                  kind,
                        uint32_t                     firstSlot,
                        uint32_t                     slotCount,
                        GPUShaderStageFlags          visibility) {
  if (slotCount == 0u || firstSlot > UINT32_MAX - (slotCount - 1u)) {
    return 1;
  }

  for (uint32_t i = 0u; i < slotCount; i++) {
    if (gpu_pipelineSlotIsUsed(priv,
                               kind,
                               firstSlot + i,
                               visibility)) {
      return 1;
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
    GPU_SHADER_STAGE_COMPUTE_BIT,
    GPU_SHADER_STAGE_TASK_BIT,
    GPU_SHADER_STAGE_MESH_BIT
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
          uint32_t endA;
          uint32_t endB;

          if (gpu_layoutEntryKind(a) != gpu_layoutEntryKind(b)) {
            continue;
          }
          if (backendA > UINT32_MAX - a->arrayCount ||
              backendB > UINT32_MAX - b->arrayCount) {
            return 0;
          }
          endA = backendA + a->arrayCount;
          endB = backendB + b->arrayCount;
          if (backendA >= endB || backendB >= endA) {
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
gpu_compileDX12PipelineBindings(GPUPipelineLayoutPriv *priv) {
  static const GPUBindKind kinds[] = {
    GPUBindKindBuffer,
    GPUBindKindTexture,
    GPUBindKindSampler
  };

  for (uint32_t groupIndex = 0u;
       groupIndex < priv->bindGroupLayoutCount;
       groupIndex++) {
    GPUBindGroupLayoutPriv *layout;

    layout = gpu_layoutPriv(priv->bindGroupLayouts[groupIndex]);
    if (!layout || layout->hasBackendBindings) {
      continue;
    }

    for (uint32_t kindIndex = 0u;
         kindIndex < GPU_ARRAY_LEN(kinds);
         kindIndex++) {
      uint32_t cursor;

      cursor = 0u;
      for (;;) {
        uint32_t selectedEntry;
        uint32_t selectedBinding;
        uint32_t selectedCount;

        selectedEntry   = UINT32_MAX;
        selectedBinding = UINT32_MAX;
        for (uint32_t entryIndex = 0u;
             entryIndex < layout->count;
             entryIndex++) {
          const GPUBindGroupLayoutEntry *entry;

          entry = &layout->entries[entryIndex];
          if (gpu_layoutEntryKind(entry) != kinds[kindIndex] ||
              priv->backendBindings[groupIndex][entryIndex] != UINT32_MAX) {
            continue;
          }
          if (selectedEntry == UINT32_MAX ||
              entry->binding < selectedBinding) {
            selectedEntry   = entryIndex;
            selectedBinding = entry->binding;
          }
        }

        if (selectedEntry == UINT32_MAX) {
          break;
        }

        selectedCount = layout->entries[selectedEntry].arrayCount;
        if (selectedCount == 0u || selectedCount > UINT32_MAX - cursor) {
          return GPU_ERROR_UNSUPPORTED;
        }
        priv->backendBindings[groupIndex][selectedEntry] = cursor;
        cursor += selectedCount;
      }
    }
  }

  return GPU_OK;
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
            (entry->arrayCount >
               gpu_metalBindingLimit(gpu_layoutEntryKind(entry)) ||
             backendBinding >
               gpu_metalBindingLimit(gpu_layoutEntryKind(entry)) -
                 entry->arrayCount)) {
          gpu_clearPipelineBindings(priv);
          return GPU_ERROR_UNSUPPORTED;
        }
        priv->backendBindings[groupIndex][entryIndex] = backendBinding;
      }
    }
  }

  if (backend == GPU_BACKEND_DX12) {
    return gpu_compileDX12PipelineBindings(priv);
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
      uint32_t selectedCount = 0u;
      GPUShaderStageFlags selectedVisibility = 0u;

      for (uint32_t groupIndex = 0;
           groupIndex < priv->bindGroupLayoutCount;
           groupIndex++) {
        GPUBindGroupLayoutPriv *layout =
          gpu_layoutPriv(priv->bindGroupLayouts[groupIndex]);

        for (uint32_t entryIndex = 0; layout && entryIndex < layout->count; entryIndex++) {
          const GPUBindGroupLayoutEntry *entry = &layout->entries[entryIndex];

          if (gpu_layoutEntryKind(entry) != kinds[kindIndex] ||
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
      selectedCount = gpu_layoutPriv(priv->bindGroupLayouts[selectedGroup])
                        ->entries[selectedEntry].arrayCount;

      while (gpu_pipelineRangeIsUsed(priv,
                                     kinds[kindIndex],
                                     backendBinding,
                                     selectedCount,
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
      {
        uint32_t limit;

        limit = gpu_metalBindingLimit(kinds[kindIndex]);
        if (selectedCount > limit || backendBinding > limit - selectedCount) {
          gpu_clearPipelineBindings(priv);
          return GPU_ERROR_UNSUPPORTED;
        }
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
                                       api ? api->backend : GPU_BACKEND_DEFAULT);
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
                                          GPUShaderStageFlags stages,
                                          uint32_t *outEntryIndex) {
  uint32_t resourceArrayCount;
  GPUShaderStageFlags requiredVisibility;

  if (outEntryIndex) {
    *outEntryIndex = UINT32_MAX;
  }
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
      if (outEntryIndex) {
        *outEntryIndex = i;
      }
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
  uint64_t matchedMask;
  uint32_t expectedCount;

  priv = gpu_layoutPriv(layout);
  expectedCount = gpu_reflectionResourceCountForGroup(reflection, groupIndex);
  if (!priv || priv->count != expectedCount ||
      expectedCount > sizeof(matchedMask) * CHAR_BIT) {
    return 0;
  }

  if (expectedCount == 0u) {
    return 1;
  }

  matchedMask = 0u;
  for (uint32_t i = 0u; i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *resource;
    int found;

    resource = &reflection->pResources[i];
    if (resource->groupIndex != groupIndex) {
      continue;
    }

    found = 0;
    for (uint32_t j = 0u; j < priv->count; j++) {
      uint64_t bit = UINT64_C(1) << j;

      if ((matchedMask & bit) == 0u &&
          gpu_layoutMatchesReflectionResource(&priv->entries[j], resource)) {
        matchedMask |= bit;
        found = 1;
        break;
      }
    }

    if (!found) {
      return 0;
    }
  }

  return 1;
}

static int
gpu_pipelineLayoutMatchesShaderResources(GPUPipelineLayout *pipelineLayout,
                                         const GPUShaderLibrary *library,
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

  if (!reflection ||
      (reflection->resourceCount > 0u && !reflection->pResources)) {
    return 0;
  }

  requiredGroupMask = 0u;
  requiredCount     = 0u;
  for (uint32_t i = 0u; i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *resource = &reflection->pResources[i];

    if ((resource->visibility & stages) != 0u &&
        resource->groupIndex >= requiredCount) {
      requiredCount = resource->groupIndex + 1u;
    }
  }
  if (pipelinePriv->bindGroupLayoutCount < requiredCount) {
    return 0;
  }

  for (uint32_t i = 0u; i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *resource = &reflection->pResources[i];
    GPUBindGroupLayoutPriv *layoutPriv;
    uint32_t backendBinding;
    uint32_t entryIndex;

    if ((resource->visibility & stages) == 0u) {
      continue;
    }
    if (resource->groupIndex >= pipelinePriv->bindGroupLayoutCount) {
      return 0;
    }

    layoutPriv = gpu_layoutPriv(pipelinePriv->bindGroupLayouts[resource->groupIndex]);
    if (!gpu_layoutContainsStageReflectionResource(layoutPriv,
                                                   resource,
                                                   stages,
                                                   &entryIndex)) {
      return 0;
    }
    backendBinding = resource->binding;
    if (!gpuGetShaderResourceBackendBinding(library,
                                            resource,
                                            &backendBinding) ||
        !pipelinePriv->backendBindings ||
        !pipelinePriv->backendBindings[resource->groupIndex] ||
        pipelinePriv->backendBindings[resource->groupIndex][entryIndex] !=
          backendBinding) {
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
  const GPUShaderReflection *reflection;
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
      GPUShaderReflection entryReflection;
      GPUShaderStageFlags entryStage;
      uint32_t entryGroupMask;

      if (!entryPoints[i]) {
        return 0;
      }

      if (!gpuShaderEntryView(library,
                              entryPoints[i],
                              &entryStage,
                              &entryReflection)) {
        return 0;
      }

      ok = gpu_pipelineLayoutMatchesShaderResources(pipelineLayout,
                                                    library,
                                                    &entryReflection,
                                                    entryStage,
                                                    &entryGroupMask);
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

  reflection = gpuShaderReflectionView(library);
  if (!reflection) {
    return 0;
  }

  ok = gpu_pipelineLayoutMatchesShaderResources(pipelineLayout,
                                                library,
                                                reflection,
                                                fallbackStages,
                                                &combinedGroupMask);
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
  const GPUShaderReflection *reflection;
  uint32_t requiredCount;
  GPUResult rc;

  if (!library || !inoutLayoutCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  reflection = gpuShaderReflectionView(library);
  if (!reflection) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  requiredCount = gpu_reflectionLayoutCount(reflection);
  if (!outLayouts) {
    *inoutLayoutCount = requiredCount;
    return GPU_OK;
  }

  if (*inoutLayoutCount < requiredCount) {
    *inoutLayoutCount = requiredCount;
    return GPU_ERROR_INSUFFICIENT_CAPACITY;
  }

  for (uint32_t i = 0; i < requiredCount; i++) {
    outLayouts[i] = NULL;
  }

  for (uint32_t i = 0; i < requiredCount; i++) {
    rc = gpu_createLayoutForReflectionGroup(device,
                                            library,
                                            reflection,
                                            i,
                                            &outLayouts[i]);
    if (rc != GPU_OK) {
      for (uint32_t j = 0; j < i; j++) {
        GPUDestroyBindGroupLayout(outLayouts[j]);
        outLayouts[j] = NULL;
      }
      return rc;
    }
  }

  *inoutLayoutCount = requiredCount;
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
  const GPUShaderReflection *reflection;
  uint32_t requiredCount;

  if (!outLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outLayout = NULL;
  if (!library || (bindGroupLayoutCount > 0u && !ppLayouts)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  reflection = gpuShaderReflectionView(library);
  if (!reflection) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  requiredCount = gpu_reflectionLayoutCount(reflection);
  if (bindGroupLayoutCount < requiredCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  for (uint32_t i = 0u; i < requiredCount; i++) {
    if (!gpu_layoutMatchesReflectionGroup(ppLayouts[i], reflection, i)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  memset(&info, 0, sizeof(info));
  info.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.bindGroupLayoutCount = bindGroupLayoutCount;
  info.ppBindGroupLayouts = ppLayouts;
  info.pushConstantSizeBytes = reflection->pushConstantSizeBytes;
  info.pushConstantStages = reflection->pushConstantSizeBytes > 0u
                               ? (GPU_SHADER_STAGE_VERTEX_BIT |
                                  GPU_SHADER_STAGE_FRAGMENT_BIT |
                                  GPU_SHADER_STAGE_COMPUTE_BIT |
                                  GPU_SHADER_STAGE_TASK_BIT |
                                  GPU_SHADER_STAGE_MESH_BIT)
                               : 0u;
  return GPUCreatePipelineLayout(device, &info, outLayout);
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
  if (!device || !library) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layout = calloc(1, sizeof(*layout));
  if (!layout) {
    return GPU_ERROR_OUT_OF_MEMORY;
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
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    layout->bindGroupLayouts = calloc(layoutCount,
                                      sizeof(*layout->bindGroupLayouts));
    if (!layout->bindGroupLayouts) {
      free(layout);
      return GPU_ERROR_OUT_OF_MEMORY;
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
  GPUBindGroup             *cached;
  GPUBindGroup             *group;
  GPUBindGroupPriv         *priv;
  GPUBindGroupBindingPriv  *candidateBindings;
  GPUBindGroupLayoutPriv   *layoutPriv;
  GPUBindGroupLayout       *layout;
  GPUBindGroupStorage      *storage;
  const GPUBindGroupEntry  *entries;
  GPUBindGroupBindingPriv   stackBindings[
                             GPU_BIND_GROUP_CANDIDATE_STACK_SIZE] = {0};
  GPUBindGroupPriv          candidate = {0};
  GPUApi                   *api;
  GPUResult                 result;
  size_t                    storageSize;
  size_t                    scratchWordCount;
  uint32_t                  count;
  uint32_t                  cursor;
  uint32_t                  kindCounts[GPUBindKindCount] = {0};
  uint32_t                  runtimeCount;
  bool                      heapCandidate;

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

    validationResult = gpu_validateBindGroupEntries(layoutPriv,
                                                    entries,
                                                    count,
                                                    layoutPriv->bindless);
    if (validationResult != GPU_OK) {
      return validationResult;
    }
  }

  if (!gpu_bindGroupRuntimeCount(layoutPriv, &runtimeCount)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if ((size_t)runtimeCount >
      (SIZE_MAX - sizeof(*storage)) / sizeof(*candidateBindings)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  heapCandidate = runtimeCount > GPU_BIND_GROUP_CANDIDATE_STACK_SIZE;
  if (heapCandidate) {
    candidateBindings = calloc(runtimeCount, sizeof(*candidateBindings));
    if (!candidateBindings) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }
  } else {
    candidateBindings = stackBindings;
  }

  cursor = 0u;
  for (uint32_t i = 0u; i < layoutPriv->count; i++) {
    const GPUBindGroupLayoutEntry *layoutEntry;
    uint32_t dynamicBase;

    layoutEntry = &layoutPriv->entries[i];
    if (!gpu_bindGroupLayoutEntryNeedsBinding(layoutEntry)) {
      continue;
    }
    dynamicBase = UINT32_MAX;
    if (layoutEntry->hasDynamicOffset &&
        !gpu_bindGroupDynamicBase(layoutPriv,
                                  layoutEntry->binding,
                                  &dynamicBase)) {
      if (heapCandidate) {
        free(candidateBindings);
      }
      return GPU_ERROR_BACKEND_FAILURE;
    }

    for (uint32_t arrayIndex = 0u;
         arrayIndex < layoutEntry->arrayCount;
         arrayIndex++) {
      const GPUBindGroupEntry *entry;
      GPUBindGroupBindingPriv *binding;

      entry = gpu_findBindGroupEntry(entries,
                                     count,
                                     layoutEntry,
                                     arrayIndex);
      if (cursor >= runtimeCount) {
        if (heapCandidate) {
          free(candidateBindings);
        }
        return GPU_ERROR_INVALID_ARGUMENT;
      }

      binding                     = &candidateBindings[cursor++];
      binding->binding            = layoutEntry->binding;
      binding->arrayIndex         = arrayIndex;
      binding->layoutEntryIndex   = i;
      binding->dynamicOffsetIndex = layoutEntry->hasDynamicOffset
                                      ? dynamicBase + arrayIndex
                                      : UINT32_MAX;
      binding->kind               = gpu_layoutEntryKind(layoutEntry);
      if (binding->kind >= GPUBindKindCount) {
        if (heapCandidate) {
          free(candidateBindings);
        }
        return GPU_ERROR_INVALID_ARGUMENT;
      }
      binding->kindIndex = kindCounts[binding->kind]++;
      if (!entry) {
        if (layoutPriv->bindless) {
          continue;
        }
        if (heapCandidate) {
          free(candidateBindings);
        }
        return GPU_ERROR_INVALID_ARGUMENT;
      }
      switch (binding->kind) {
        case GPUBindKindBuffer:
          binding->buffer = entry->buffer.buffer;
          binding->offset = entry->buffer.offset;
          binding->size   = entry->buffer.size;
          break;
        case GPUBindKindTexture:
          binding->textureView = entry->textureView;
          break;
        case GPUBindKindSampler:
          binding->sampler = entry->sampler;
          break;
        default:
          if (heapCandidate) {
            free(candidateBindings);
          }
          return GPU_ERROR_INVALID_ARGUMENT;
      }
    }
  }

  candidate.layout             = layout;
  candidate.bindings           = candidateBindings;
  candidate.count              = runtimeCount;
  candidate.dynamicOffsetCount = 0u;
  candidate.bindless           = layoutPriv->bindless;
  for (uint32_t i = 0u; i < candidate.count; i++) {
    if (candidate.bindings[i].dynamicOffsetIndex != UINT32_MAX) {
      candidate.dynamicOffsetCount++;
    }
  }
  candidate.hash = candidate.bindless ? 0u : gpu_bindGroupHash(&candidate);

  if (!candidate.bindless) {
    cached = gpu_bindGroupCacheFind(device, &candidate);
    if (cached) {
      if (heapCandidate) {
        free(candidateBindings);
      }
      *outGroup = cached;
      return GPU_OK;
    }
  }

  scratchWordCount = candidate.bindless
                       ? ((size_t)runtimeCount + 63u) / 64u
                       : 0u;
  storageSize = sizeof(*storage) +
                (size_t)runtimeCount * sizeof(*storage->bindings);
  if (scratchWordCount >
      (SIZE_MAX - storageSize) / sizeof(*priv->updateScratch)) {
    if (heapCandidate) {
      free(candidateBindings);
    }
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  storageSize += scratchWordCount * sizeof(*priv->updateScratch);
  storage = calloc(1, storageSize);
  if (!storage) {
    if (heapCandidate) {
      free(candidateBindings);
    }
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  group = &storage->group;
  priv  = &storage->priv;
  *priv = candidate;
  if (runtimeCount > 0u) {
    priv->bindings = storage->bindings;
    memcpy(storage->bindings,
           candidateBindings,
           (size_t)runtimeCount * sizeof(*storage->bindings));
  } else {
    priv->bindings = NULL;
  }
  if (scratchWordCount > 0u) {
    priv->updateScratch = (uint64_t *)&storage->bindings[runtimeCount];
  }
  if (heapCandidate) {
    free(candidateBindings);
  }

  group->_device   = device;
  group->_priv     = priv;
  group->_refCount = 1u;

  api = gpuDeviceApi(device);
  if (api && api->descriptor.createBindGroup) {
    result = api->descriptor.createBindGroup(device, group);
    if (result != GPU_OK) {
      GPUDestroyBindGroup(group);
      return result;
    }
  }

  if (!priv->bindless) {
    cached = gpu_bindGroupCacheStore(device, group);
    if (cached != group) {
      if (api && api->descriptor.destroyBindGroup) {
        api->descriptor.destroyBindGroup(group);
      }
      free((GPUBindGroupStorage *)group);
      group = cached;
    }
  }

  *outGroup = group;
  return GPU_OK;
}

static GPUBindGroupBindingPriv *
gpu_bindingForEntry(GPUBindGroup                  *group,
                    const GPUBindGroupEntry       *entry,
                    GPUBindGroupLayoutPriv       **outLayout,
                    uint32_t                      *outLayoutEntryIndex) {
  GPUBindGroupPriv       *priv;
  GPUBindGroupLayoutPriv *layout;

  priv   = gpu_groupPriv(group);
  layout = gpu_layoutPriv(priv ? priv->layout : NULL);
  if (!priv || !layout || !entry) {
    return NULL;
  }

  for (uint32_t i = 0u; i < layout->count; i++) {
    uint32_t base;
    uint32_t index;

    if (!gpu_bindGroupEntryMatchesLayout(&layout->entries[i], entry) ||
        !gpu_bindGroupRuntimeBase(layout, i, &base) ||
        entry->arrayIndex > UINT32_MAX - base) {
      continue;
    }

    index = base + entry->arrayIndex;
    if (index >= priv->count ||
        priv->bindings[index].layoutEntryIndex != i) {
      return NULL;
    }
    if (outLayout) {
      *outLayout = layout;
    }
    if (outLayoutEntryIndex) {
      *outLayoutEntryIndex = i;
    }
    return &priv->bindings[index];
  }

  return NULL;
}

static void
gpu_clearBindGroupUpdateScratch(GPUBindGroup            *group,
                                uint32_t                 entryCount,
                                const GPUBindGroupEntry *entries) {
  GPUBindGroupPriv *priv;

  priv = gpu_groupPriv(group);
  if (!priv || !priv->updateScratch || !entries) {
    return;
  }

  for (uint32_t i = 0u; i < entryCount; i++) {
    GPUBindGroupBindingPriv *binding;
    size_t                   index;

    binding = gpu_bindingForEntry(group, &entries[i], NULL, NULL);
    if (!binding) {
      continue;
    }
    index = (size_t)(binding - priv->bindings);
    priv->updateScratch[index >> 6u] &= ~(1ull << (index & 63u));
  }
}

static GPUResult
gpu_validateBindGroupUpdateEntries(GPUBindGroup            *group,
                                   uint32_t                 entryCount,
                                   const GPUBindGroupEntry *entries) {
  GPUBindGroupPriv *priv;
  uint32_t          markedCount;

  priv = gpu_groupPriv(group);
  if (!priv || entryCount > priv->count ||
      (entryCount > 0u && (!entries || !priv->updateScratch))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  markedCount = 0u;
  for (uint32_t i = 0u; i < entryCount; i++) {
    GPUBindGroupBindingPriv *binding;
    GPUBindGroupLayoutPriv  *layout;
    uint64_t                 bit;
    size_t                   index;
    uint32_t                 layoutEntryIndex;

    binding = gpu_bindingForEntry(group,
                                  &entries[i],
                                  &layout,
                                  &layoutEntryIndex);
    if (!binding || !layout || layoutEntryIndex >= layout->count ||
        !gpu_bindGroupEntryMatchesDevice(group->_device,
                                         &layout->entries[layoutEntryIndex],
                                         &entries[i])) {
      goto invalid;
    }
    index = (size_t)(binding - priv->bindings);
    bit   = 1ull << (index & 63u);
    if ((priv->updateScratch[index >> 6u] & bit) != 0u) {
      goto invalid;
    }
    priv->updateScratch[index >> 6u] |= bit;
    markedCount++;
  }

  gpu_clearBindGroupUpdateScratch(group, markedCount, entries);
  return GPU_OK;

invalid:
  gpu_clearBindGroupUpdateScratch(group, markedCount, entries);
  return GPU_ERROR_INVALID_ARGUMENT;
}

GPU_EXPORT
GPUResult
GPUUpdateBindGroupEXT(GPUBindGroup            *group,
                      uint32_t                 entryCount,
                      const GPUBindGroupEntry *pEntries) {
  GPUBindGroupPriv       *priv;
  GPUBindGroupLayoutPriv *layout;
  GPUApi                 *api;
  GPUResult               result;

  priv   = gpu_groupPriv(group);
  layout = gpu_layoutPriv(priv ? priv->layout : NULL);
  if (!group || !priv || !layout || !priv->bindless ||
      (entryCount > 0u && !pEntries)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!GPUIsFeatureEnabled(group->_device, GPU_FEATURE_BINDLESS)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = gpu_validateBindGroupUpdateEntries(group,
                                              entryCount,
                                              pEntries);
  if (result != GPU_OK) {
    return result;
  }

  api = gpuDeviceApi(group->_device);
  if (api && api->descriptor.updateBindGroup &&
      !api->descriptor.updateBindGroup(group, entryCount, pEntries)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  for (uint32_t i = 0u; i < entryCount; i++) {
    GPUBindGroupBindingPriv *binding;

    binding = gpu_bindingForEntry(group, &pEntries[i], NULL, NULL);
    if (!binding) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    switch (binding->kind) {
      case GPUBindKindBuffer:
        binding->buffer = pEntries[i].buffer.buffer;
        binding->offset = pEntries[i].buffer.offset;
        binding->size   = pEntries[i].buffer.size;
        break;
      case GPUBindKindTexture:
        binding->textureView = pEntries[i].textureView;
        break;
      case GPUBindKindSampler:
        binding->sampler = pEntries[i].sampler;
        break;
      default:
        return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyBindGroup(GPUBindGroup *group) {
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

  free((GPUBindGroupStorage *)group);
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
                               binding->binding + binding->arrayIndex);
    } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
      gpuSetRenderVertexTexture(bindCtx->pass,
                                binding->textureView,
                                binding->binding + binding->arrayIndex);
    } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
      gpuSetRenderVertexSampler(bindCtx->pass,
                                binding->sampler,
                                binding->binding + binding->arrayIndex);
    }
  }

  if ((binding->visibility & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0) {
    if (binding->kind == GPUBindKindBuffer && binding->buffer) {
      gpuSetRenderFragmentBuffer(bindCtx->pass,
                                 binding->buffer,
                                 binding->offset,
                                 binding->binding + binding->arrayIndex);
    } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
      gpuSetRenderFragmentTexture(bindCtx->pass,
                                  binding->textureView,
                                  binding->binding + binding->arrayIndex);
    } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
      gpuSetRenderFragmentSampler(bindCtx->pass,
                                  binding->sampler,
                                  binding->binding + binding->arrayIndex);
    }
  }

  if ((binding->visibility & GPU_SHADER_STAGE_TASK_BIT) != 0) {
    if (binding->kind == GPUBindKindBuffer && binding->buffer) {
      gpuSetRenderTaskBuffer(bindCtx->pass,
                             binding->buffer,
                             binding->offset,
                             binding->binding + binding->arrayIndex);
    } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
      gpuSetRenderTaskTexture(bindCtx->pass,
                              binding->textureView,
                              binding->binding + binding->arrayIndex);
    } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
      gpuSetRenderTaskSampler(bindCtx->pass,
                              binding->sampler,
                              binding->binding + binding->arrayIndex);
    }
  }

  if ((binding->visibility & GPU_SHADER_STAGE_MESH_BIT) != 0) {
    if (binding->kind == GPUBindKindBuffer && binding->buffer) {
      gpuSetRenderMeshBuffer(bindCtx->pass,
                             binding->buffer,
                             binding->offset,
                             binding->binding + binding->arrayIndex);
    } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
      gpuSetRenderMeshTexture(bindCtx->pass,
                              binding->textureView,
                              binding->binding + binding->arrayIndex);
    } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
      gpuSetRenderMeshSampler(bindCtx->pass,
                              binding->sampler,
                              binding->binding + binding->arrayIndex);
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
  if (gpuBindGroupShadowMatches(
        pass->_boundGroups[groupIndex],
        pass->_boundDynamicOffsetCounts[groupIndex],
        pass->_boundDynamicOffsets[groupIndex],
        group,
        dynamicOffsetCount,
        pDynamicOffsets)) {
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
      gpuStoreBindGroupShadow(
        &pass->_boundDynamicOffsetCounts[groupIndex],
        pass->_boundDynamicOffsets[groupIndex],
        dynamicOffsetCount,
        pDynamicOffsets);
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
    gpuStoreBindGroupShadow(
      &pass->_boundDynamicOffsetCounts[groupIndex],
      pass->_boundDynamicOffsets[groupIndex],
      dynamicOffsetCount,
      pDynamicOffsets);
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
  if (!priv || !layout ||
      (layout->count > 0u && !layout->backendBindings)) {
    return 0;
  }

  for (uint32_t i = 0u; i < priv->count; i++) {
    const GPUBindGroupLayoutEntry *layoutEntry;
    const GPUBindGroupBindingPriv *binding;
    GPUBindGroupBindingView        view;

    if (priv->bindings[i].layoutEntryIndex >= layout->count) {
      return 0;
    }
    layoutEntry = &layout->entries[priv->bindings[i].layoutEntryIndex];
    binding     = &priv->bindings[i];

    memset(&view, 0, sizeof(view));
    switch (binding->kind) {
      case GPUBindKindBuffer:
        view.buffer = binding->buffer;
        view.offset = binding->offset;
        view.size   = binding->size;
        break;
      case GPUBindKindTexture:
        view.textureView = binding->textureView;
        break;
      case GPUBindKindSampler:
        view.sampler = binding->sampler;
        break;
      default:
        return 0;
    }
    view.visibility       = layoutEntry->visibility;
    view.bindingType      = layoutEntry->bindingType;
    view.binding          = layout->backendBindings[binding->layoutEntryIndex];
    view.arrayIndex       = binding->arrayIndex;
    view.kindIndex        = binding->kindIndex;
    view.kind             = binding->kind;
    view.hasDynamicOffset = layoutEntry->hasDynamicOffset;
    fn(ctx, &view);
  }

  return 1;
}

GPU_HIDE
int
gpuForEachBindGroupEntry(GPUBindGroup            *group,
                         uint32_t                 entryCount,
                         const GPUBindGroupEntry *entries,
                         GPUBindGroupBindingFn    fn,
                         void                    *ctx) {
  if (!group || !fn || (entryCount > 0u && !entries)) {
    return 0;
  }

  for (uint32_t i = 0u; i < entryCount; i++) {
    GPUBindGroupBindingPriv *binding;
    GPUBindGroupLayoutPriv  *layout;
    GPUBindGroupBindingView  view;
    uint32_t                 layoutEntryIndex;

    binding = gpu_bindingForEntry(group,
                                  &entries[i],
                                  &layout,
                                  &layoutEntryIndex);
    if (!binding || !layout || !layout->backendBindings ||
        layoutEntryIndex >= layout->count) {
      return 0;
    }

    memset(&view, 0, sizeof(view));
    switch (binding->kind) {
      case GPUBindKindBuffer:
        view.buffer = entries[i].buffer.buffer;
        view.offset = entries[i].buffer.offset;
        view.size   = entries[i].buffer.size;
        break;
      case GPUBindKindTexture:
        view.textureView = entries[i].textureView;
        break;
      case GPUBindKindSampler:
        view.sampler = entries[i].sampler;
        break;
      default:
        return 0;
    }
    view.visibility       = layout->entries[layoutEntryIndex].visibility;
    view.bindingType      = layout->entries[layoutEntryIndex].bindingType;
    view.binding          = layout->backendBindings[layoutEntryIndex];
    view.arrayIndex       = binding->arrayIndex;
    view.kindIndex        = binding->kindIndex;
    view.kind             = binding->kind;
    view.hasDynamicOffset = false;
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
      dynamicOffsetCount != priv->dynamicOffsetCount) {
    return 0;
  }

  dynamicIndex = 0u;
  for (uint32_t i = 0u; i < priv->count; i++) {
    const GPUBindGroupBindingPriv *binding;
    uint64_t effectiveOffset;

    binding = &priv->bindings[i];
    if (binding->layoutEntryIndex >= layout->count) {
      return 0;
    }
    if (binding->dynamicOffsetIndex == UINT32_MAX) {
      continue;
    }

    if (binding->kind != GPUBindKindBuffer ||
        binding->dynamicOffsetIndex >= dynamicOffsetCount ||
        !gpu_u64Add(binding->offset,
                    dynamicOffsets[binding->dynamicOffsetIndex],
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
  GPUBindGroupPriv       *priv;
  GPUBindGroupLayoutPriv *layout;
  GPUPipelineLayoutPriv  *pipeline;
  uint32_t                dynamicIndex;

  if (!fn ||
      !gpuValidateBindGroupDynamicOffsets(pipelineLayout,
                                          groupIndex,
                                          group,
                                          dynamicOffsetCount,
                                          pDynamicOffsets)) {
    return 0;
  }

  priv     = gpu_groupPriv(group);
  layout   = gpu_layoutPriv(priv ? priv->layout : NULL);
  pipeline = gpu_pipelineLayoutPriv(pipelineLayout);
  if (!priv || !layout || !pipeline ||
      groupIndex >= pipeline->bindGroupLayoutCount ||
      pipeline->bindGroupLayouts[groupIndex] != priv->layout ||
      (layout->count > 0u &&
       (!pipeline->backendBindings || !pipeline->backendBindings[groupIndex])) ||
      dynamicOffsetCount != priv->dynamicOffsetCount) {
    return 0;
  }

  dynamicIndex = 0u;
  for (uint32_t i = 0u; i < priv->count; i++) {
    const GPUBindGroupLayoutEntry *layoutEntry;
    const GPUBindGroupBindingPriv *binding;
    GPUBindGroupBindingView view;
    uint64_t effectiveOffset;

    binding = &priv->bindings[i];
    if (binding->layoutEntryIndex >= layout->count) {
      return 0;
    }
    layoutEntry = &layout->entries[binding->layoutEntryIndex];
    effectiveOffset = binding->kind == GPUBindKindBuffer
                        ? binding->offset
                        : 0u;
    if (binding->dynamicOffsetIndex != UINT32_MAX) {
      if (binding->kind != GPUBindKindBuffer ||
          binding->dynamicOffsetIndex >= dynamicOffsetCount ||
          !gpu_u64Add(effectiveOffset,
                      pDynamicOffsets[binding->dynamicOffsetIndex],
                      &effectiveOffset)) {
        return 0;
      }
      dynamicIndex++;
    }
    if (binding->kind == GPUBindKindBuffer &&
        ((!binding->buffer && !priv->bindless) ||
         (binding->buffer &&
          !gpuBufferRangeValid(binding->buffer,
                               effectiveOffset,
                               binding->size)))) {
      return 0;
    }

    memset(&view, 0, sizeof(view));
    switch (binding->kind) {
      case GPUBindKindBuffer:
        view.buffer = binding->buffer;
        view.offset = effectiveOffset;
        view.size   = binding->size;
        break;
      case GPUBindKindTexture:
        view.textureView = binding->textureView;
        break;
      case GPUBindKindSampler:
        view.sampler = binding->sampler;
        break;
      default:
        return 0;
    }
    view.visibility       = layoutEntry->visibility;
    view.bindingType      = layoutEntry->bindingType;
    view.binding          = pipeline->backendBindings[groupIndex]
                                                     [binding->layoutEntryIndex];
    view.arrayIndex       = binding->arrayIndex;
    view.kindIndex        = binding->kindIndex;
    view.kind             = binding->kind;
    view.hasDynamicOffset = layoutEntry->hasDynamicOffset;
    fn(ctx, &view);
  }

  return dynamicIndex == dynamicOffsetCount;
}
