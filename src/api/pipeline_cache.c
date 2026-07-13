/*
 * Copyright (C) 2026 Recep Aslantas
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

#include "../common.h"
#include "pipeline_cache_internal.h"
#include "render/pipeline_internal.h"

#if !defined(_WIN32) && !defined(WIN32)
#  include <pthread.h>
#endif

#define GPU_PIPELINE_CACHE_DEFAULT_ENTRIES 256u

typedef struct GPUPipelineCacheKey {
  uint8_t *data;
  size_t   size;
  uint64_t hash;
} GPUPipelineCacheKey;

struct GPUPipelineCacheEntry {
  GPUPipelineCacheEntry *next;
  GPURenderPipeline     *pipeline;
  uint8_t               *keyData;
  size_t                 keySize;
  uint64_t               keyHash;
};

typedef struct GPUPipelineKeyWriter {
  uint8_t *data;
  size_t   offset;
  bool     valid;
} GPUPipelineKeyWriter;

static void
gpu_pipelineCacheLock(GPUPipelineCache *cache) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(cache->_lock);
#else
  pthread_mutex_lock(cache->_lock);
#endif
}

static void
gpu_pipelineCacheUnlock(GPUPipelineCache *cache) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(cache->_lock);
#else
  pthread_mutex_unlock(cache->_lock);
#endif
}

static void
gpu_retainRenderPipeline(GPURenderPipeline *pipeline) {
#if defined(_WIN32) || defined(WIN32)
  InterlockedIncrement((volatile LONG *)&pipeline->_refCount);
#else
  __atomic_add_fetch(&pipeline->_refCount, 1u, __ATOMIC_RELAXED);
#endif
}

GPU_HIDE
bool
gpuReleaseRenderPipeline(GPURenderPipeline *pipeline) {
#if defined(_WIN32) || defined(WIN32)
  return InterlockedDecrement((volatile LONG *)&pipeline->_refCount) == 0;
#else
  return __atomic_sub_fetch(&pipeline->_refCount, 1u, __ATOMIC_ACQ_REL) == 0u;
#endif
}

static void
gpu_pipelineKeyWrite(GPUPipelineKeyWriter *writer,
                     const void           *value,
                     size_t                size) {
  if (!writer->valid || size > SIZE_MAX - writer->offset) {
    writer->valid = false;
    return;
  }
  if (writer->data && size > 0u) {
    memcpy(writer->data + writer->offset, value, size);
  }
  writer->offset += size;
}

#define GPU_PIPELINE_KEY_WRITE(WRITER, VALUE) \
  gpu_pipelineKeyWrite((WRITER), &(VALUE), sizeof(VALUE))

static void
gpu_pipelineKeyWriteString(GPUPipelineKeyWriter *writer, const char *value) {
  gpu_pipelineKeyWrite(writer, value, strlen(value) + 1u);
}

static void
gpu_pipelineKeyWriteDepthStencil(GPUPipelineKeyWriter       *writer,
                                 const GPUDepthStencilState *state) {
  GPUDepthStencilState empty = {0};
  const GPUDepthStencilState *value;

  value = state ? state : &empty;
  GPU_PIPELINE_KEY_WRITE(writer, value->depthTestEnable);
  GPU_PIPELINE_KEY_WRITE(writer, value->depthWriteEnable);
  GPU_PIPELINE_KEY_WRITE(writer, value->depthCompare);
  GPU_PIPELINE_KEY_WRITE(writer, value->stencilTestEnable);
  GPU_PIPELINE_KEY_WRITE(writer, value->front.compare);
  GPU_PIPELINE_KEY_WRITE(writer, value->front.failOp);
  GPU_PIPELINE_KEY_WRITE(writer, value->front.depthFailOp);
  GPU_PIPELINE_KEY_WRITE(writer, value->front.passOp);
  GPU_PIPELINE_KEY_WRITE(writer, value->back.compare);
  GPU_PIPELINE_KEY_WRITE(writer, value->back.failOp);
  GPU_PIPELINE_KEY_WRITE(writer, value->back.depthFailOp);
  GPU_PIPELINE_KEY_WRITE(writer, value->back.passOp);
  GPU_PIPELINE_KEY_WRITE(writer, value->stencilReadMask);
  GPU_PIPELINE_KEY_WRITE(writer, value->stencilWriteMask);
}

static void
gpu_pipelineKeyWriteInfo(GPUPipelineKeyWriter              *writer,
                         const GPURenderPipelineCreateInfo *info) {
  uintptr_t layout;
  uintptr_t library;

  layout  = (uintptr_t)info->layout;
  library = (uintptr_t)info->library;
  GPU_PIPELINE_KEY_WRITE(writer, layout);
  GPU_PIPELINE_KEY_WRITE(writer, library);
  gpu_pipelineKeyWriteString(writer, info->vertexEntry);
  gpu_pipelineKeyWriteString(writer, info->fragmentEntry);
  GPU_PIPELINE_KEY_WRITE(writer, info->vertex.bufferLayoutCount);
  for (uint32_t i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    const GPUVertexBufferLayout *bufferLayout;

    bufferLayout = &info->vertex.pBufferLayouts[i];
    GPU_PIPELINE_KEY_WRITE(writer, bufferLayout->strideBytes);
    GPU_PIPELINE_KEY_WRITE(writer, bufferLayout->stepMode);
    GPU_PIPELINE_KEY_WRITE(writer, bufferLayout->attributeCount);
    for (uint32_t j = 0u; j < bufferLayout->attributeCount; j++) {
      const GPUVertexAttribute *attribute;

      attribute = &bufferLayout->pAttributes[j];
      GPU_PIPELINE_KEY_WRITE(writer, attribute->format);
      GPU_PIPELINE_KEY_WRITE(writer, attribute->offset);
      GPU_PIPELINE_KEY_WRITE(writer, attribute->shaderLocation);
    }
  }
  GPU_PIPELINE_KEY_WRITE(writer, info->colorTargetCount);
  for (uint32_t i = 0u; i < info->colorTargetCount; i++) {
    const GPUColorTargetState *target;

    target = &info->pColorTargets[i];
    GPU_PIPELINE_KEY_WRITE(writer, target->format);
    GPU_PIPELINE_KEY_WRITE(writer, target->blend.enabled);
    GPU_PIPELINE_KEY_WRITE(writer, target->blend.color.srcFactor);
    GPU_PIPELINE_KEY_WRITE(writer, target->blend.color.dstFactor);
    GPU_PIPELINE_KEY_WRITE(writer, target->blend.color.op);
    GPU_PIPELINE_KEY_WRITE(writer, target->blend.alpha.srcFactor);
    GPU_PIPELINE_KEY_WRITE(writer, target->blend.alpha.dstFactor);
    GPU_PIPELINE_KEY_WRITE(writer, target->blend.alpha.op);
    GPU_PIPELINE_KEY_WRITE(writer, target->blend.writeMask);
  }
  GPU_PIPELINE_KEY_WRITE(writer, info->depthStencilFormat);
  gpu_pipelineKeyWriteDepthStencil(writer, info->pDepthStencilState);
  GPU_PIPELINE_KEY_WRITE(writer, info->primitiveTopology);
  GPU_PIPELINE_KEY_WRITE(writer, info->cullMode);
  GPU_PIPELINE_KEY_WRITE(writer, info->frontFace);
  GPU_PIPELINE_KEY_WRITE(writer, info->multisample.sampleCount);
  GPU_PIPELINE_KEY_WRITE(writer, info->multisample.sampleMask);
  GPU_PIPELINE_KEY_WRITE(writer, info->multisample.alphaToCoverageEnable);
}

static uint64_t
gpu_pipelineKeyHash(const uint8_t *data, size_t size) {
  uint64_t hash;

  hash = 14695981039346656037ull;
  for (size_t i = 0u; i < size; i++) {
    hash ^= data[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

static bool
gpu_buildRenderPipelineKey(const GPURenderPipelineCreateInfo *info,
                           GPUPipelineCacheKey                *outKey) {
  GPUPipelineKeyWriter writer;

  memset(outKey, 0, sizeof(*outKey));
  if (info->chain.pNext) {
    return false;
  }

  writer.data   = NULL;
  writer.offset = 0u;
  writer.valid  = true;
  gpu_pipelineKeyWriteInfo(&writer, info);
  if (!writer.valid || writer.offset == 0u) {
    return false;
  }

  outKey->data = malloc(writer.offset);
  if (!outKey->data) {
    return false;
  }
  outKey->size  = writer.offset;
  writer.data   = outKey->data;
  writer.offset = 0u;
  writer.valid  = true;
  gpu_pipelineKeyWriteInfo(&writer, info);
  if (!writer.valid || writer.offset != outKey->size) {
    free(outKey->data);
    memset(outKey, 0, sizeof(*outKey));
    return false;
  }
  outKey->hash = gpu_pipelineKeyHash(outKey->data, outKey->size);
  return true;
}

static GPUPipelineCacheEntry *
gpu_pipelineCacheFindEntry(GPUPipelineCache          *cache,
                           const GPUPipelineCacheKey *key) {
  GPUPipelineCacheEntry *entry;

  for (entry = cache->head; entry; entry = entry->next) {
    if (entry->keyHash == key->hash && entry->keySize == key->size &&
        memcmp(entry->keyData, key->data, key->size) == 0) {
      return entry;
    }
  }
  return NULL;
}

static GPURenderPipeline *
gpu_pipelineCacheFind(GPUPipelineCache          *cache,
                      const GPUPipelineCacheKey *key) {
  GPUPipelineCacheEntry *entry;
  GPURenderPipeline     *pipeline;

  pipeline = NULL;
  gpu_pipelineCacheLock(cache);
  entry = gpu_pipelineCacheFindEntry(cache, key);
  if (entry) {
    pipeline = entry->pipeline;
    gpu_retainRenderPipeline(pipeline);
    cache->stats.pipelineHits++;
    cache->device->cacheStats.pipelineHits++;
  }
  gpu_pipelineCacheUnlock(cache);
  return pipeline;
}

static GPURenderPipeline *
gpu_pipelineCacheStore(GPUPipelineCache    *cache,
                       GPUPipelineCacheKey *key,
                       GPURenderPipeline   *pipeline) {
  GPUPipelineCacheEntry *entry;
  GPUPipelineCacheEntry *evicted;
  GPURenderPipeline     *result;

  entry = calloc(1, sizeof(*entry));
  if (!entry) {
    gpu_pipelineCacheLock(cache);
    cache->stats.pipelineMisses++;
    cache->stats.pipelineCompiles++;
    cache->device->cacheStats.pipelineMisses++;
    cache->device->cacheStats.pipelineCompiles++;
    gpu_pipelineCacheUnlock(cache);
    free(key->data);
    memset(key, 0, sizeof(*key));
    return pipeline;
  }
  entry->pipeline = pipeline;
  entry->keyData  = key->data;
  entry->keySize  = key->size;
  entry->keyHash  = key->hash;
  evicted         = NULL;
  result          = pipeline;

  gpu_pipelineCacheLock(cache);
  {
    GPUPipelineCacheEntry *existing;

    existing = gpu_pipelineCacheFindEntry(cache, key);
    if (existing) {
      result = existing->pipeline;
      gpu_retainRenderPipeline(result);
      cache->stats.pipelineHits++;
      cache->stats.pipelineCompiles++;
      cache->device->cacheStats.pipelineHits++;
      cache->device->cacheStats.pipelineCompiles++;
      gpu_pipelineCacheUnlock(cache);
      free(entry->keyData);
      free(entry);
      GPUDestroyRenderPipeline(pipeline);
      memset(key, 0, sizeof(*key));
      return result;
    }
  }

  if (cache->entryCount == cache->maxEntries) {
    evicted     = cache->head;
    cache->head = evicted->next;
    if (!cache->head) {
      cache->tail = NULL;
    }
    cache->entryCount--;
  }
  gpu_retainRenderPipeline(pipeline);
  if (cache->tail) {
    cache->tail->next = entry;
  } else {
    cache->head = entry;
  }
  cache->tail = entry;
  cache->entryCount++;
  cache->stats.pipelineMisses++;
  cache->stats.pipelineCompiles++;
  cache->device->cacheStats.pipelineMisses++;
  cache->device->cacheStats.pipelineCompiles++;
  gpu_pipelineCacheUnlock(cache);

  memset(key, 0, sizeof(*key));
  if (evicted) {
    GPUDestroyRenderPipeline(evicted->pipeline);
    free(evicted->keyData);
    free(evicted);
  }
  return result;
}

GPU_HIDE
GPUResult
gpuPipelineCacheFindRender(GPUPipelineCache                  *cache,
                           const GPURenderPipelineCreateInfo *info,
                           GPURenderPipeline                **outPipeline) {
  GPUPipelineCacheKey key;

  *outPipeline = NULL;
  if (!gpu_buildRenderPipelineKey(info, &key)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  *outPipeline = gpu_pipelineCacheFind(cache, &key);
  free(key.data);
  return GPU_OK;
}

GPU_HIDE
GPURenderPipeline *
gpuPipelineCacheStoreRender(GPUPipelineCache                  *cache,
                            const GPURenderPipelineCreateInfo *info,
                            GPURenderPipeline                 *pipeline) {
  GPUPipelineCacheKey key;

  if (!gpu_buildRenderPipelineKey(info, &key)) {
    gpuRecordPipelineCompile(cache->device, cache);
    return pipeline;
  }
  return gpu_pipelineCacheStore(cache, &key, pipeline);
}

GPU_EXPORT
GPUResult
GPUCreatePipelineCache(GPUDevice                         * __restrict device,
                       const GPUPipelineCacheCreateInfo  * __restrict info,
                       GPUPipelineCache                 ** __restrict outCache) {
  GPUPipelineCache *cache;
  void             *lock;

  if (!outCache) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outCache = NULL;

  if (!device || !info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->enableDiskCache) {
    return GPU_ERROR_UNSUPPORTED;
  }

  cache = calloc(1, sizeof(*cache));
  if (!cache) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

#if defined(_WIN32) || defined(WIN32)
  lock = calloc(1, sizeof(CRITICAL_SECTION));
  if (!lock) {
    free(cache);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  InitializeCriticalSection(lock);
#else
  lock = calloc(1, sizeof(pthread_mutex_t));
  if (!lock) {
    free(cache);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  if (pthread_mutex_init(lock, NULL) != 0) {
    free(lock);
    free(cache);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif

  cache->device     = device;
  cache->_lock      = lock;
  cache->maxEntries = info->maxEntries > 0u
                        ? info->maxEntries
                        : GPU_PIPELINE_CACHE_DEFAULT_ENTRIES;
  *outCache = cache;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyPipelineCache(GPUPipelineCache *cache) {
  GPUPipelineCacheEntry *entry;

  if (!cache) {
    return;
  }

  gpu_pipelineCacheLock(cache);
  entry             = cache->head;
  cache->head       = NULL;
  cache->tail       = NULL;
  cache->entryCount = 0u;
  gpu_pipelineCacheUnlock(cache);

#if defined(_WIN32) || defined(WIN32)
  DeleteCriticalSection(cache->_lock);
#else
  pthread_mutex_destroy(cache->_lock);
#endif
  free(cache->_lock);

  while (entry) {
    GPUPipelineCacheEntry *next;

    next = entry->next;
    GPUDestroyRenderPipeline(entry->pipeline);
    free(entry->keyData);
    free(entry);
    entry = next;
  }
  free(cache);
}

GPU_EXPORT
GPUResult
GPUPrewarmRenderPipelines(GPUDevice                        * __restrict device,
                          GPUPipelineCache                 * __restrict cache,
                          uint32_t                                      count,
                          const GPURenderPipelineCreateInfo * __restrict infos) {
  if (!device || !cache || cache->device != device || (count > 0u && !infos)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  for (uint32_t i = 0u; i < count; i++) {
    GPURenderPipelineCreateInfo info;
    GPURenderPipeline          *pipeline;
    GPUResult                   result;

    info       = infos[i];
    info.cache = cache;
    pipeline   = NULL;
    result     = GPUCreateRenderPipeline(device, &info, &pipeline);
    if (result != GPU_OK) {
      return result;
    }
    GPUDestroyRenderPipeline(pipeline);
  }
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCompileRenderPipelineAsync(GPUDevice                         * __restrict device,
                              GPUPipelineCache                  * __restrict cache,
                              const GPURenderPipelineCreateInfo * __restrict info,
                              GPUPipelineCompileHandle          * __restrict outHandle) {
  if (!outHandle) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  outHandle->id = 0;
  if (!device || !cache || cache->device != device || !info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return GPU_ERROR_UNSUPPORTED;
}

GPU_EXPORT
GPUResult
GPUPollRenderPipelineCompile(GPUDevice                 * __restrict device,
                             GPUPipelineCompileHandle               handle,
                             GPUPipelineCompileStatus  * __restrict outStatus,
                             GPURenderPipeline        ** __restrict outPipeline) {
  GPU__UNUSED(handle);

  if (!outStatus || !outPipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outStatus = GPU_PIPELINE_COMPILE_FAILED;
  *outPipeline = NULL;
  if (!device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return GPU_ERROR_UNSUPPORTED;
}

GPU_HIDE
void
gpuRecordPipelineCompile(GPUDevice *device, GPUPipelineCache *cache) {
  if (!cache) {
    if (device) {
      device->cacheStats.pipelineCompiles++;
    }
    return;
  }

  gpu_pipelineCacheLock(cache);
  cache->stats.pipelineMisses++;
  cache->stats.pipelineCompiles++;
  if (cache->device) {
    cache->device->cacheStats.pipelineMisses++;
    cache->device->cacheStats.pipelineCompiles++;
  }
  gpu_pipelineCacheUnlock(cache);
}
