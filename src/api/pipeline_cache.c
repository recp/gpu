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
#include "compute_internal.h"
#include "descr/descriptor_internal.h"
#include "library_internal.h"
#include "pipeline_cache_internal.h"
#include "render/pipeline_internal.h"

#if !defined(_WIN32) && !defined(WIN32)
#  include <pthread.h>
#endif

#define GPU_PIPELINE_CACHE_DEFAULT_ENTRIES 256u
#define GPU_PIPELINE_CACHE_MAX_BUCKETS     4096u
#define GPU_PIPELINE_KEY_HASH_SEED         14695981039346656037ull

typedef enum GPUPipelineCacheEntryType {
  GPU_PIPELINE_CACHE_RENDER = 0,
  GPU_PIPELINE_CACHE_COMPUTE
} GPUPipelineCacheEntryType;

struct GPUPipelineCacheEntry {
  GPUPipelineCacheEntry    *next;
  GPUPipelineCacheEntry    *hashNext;
  void                     *pipeline;
  size_t                    keySize;
  uint64_t                  keyHash;
  GPUPipelineCacheEntryType type;
  uint8_t                   keyData[];
};

typedef struct GPUPipelineKeyWriter {
  uint8_t *data;
  size_t   offset;
  size_t   capacity;
  uint64_t hash;
  bool     valid;
} GPUPipelineKeyWriter;

typedef struct GPUPipelineCacheSync {
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION lock;
  CONDITION_VARIABLE condition;
  HANDLE worker;
#else
  pthread_mutex_t lock;
  pthread_cond_t  condition;
  pthread_t       worker;
#endif
  bool workerStarted;
} GPUPipelineCacheSync;

typedef enum GPUPipelineCompileJobState {
  GPU_PIPELINE_JOB_QUEUED = 0,
  GPU_PIPELINE_JOB_COMPILING,
  GPU_PIPELINE_JOB_READY,
  GPU_PIPELINE_JOB_FAILED
} GPUPipelineCompileJobState;

struct GPUPipelineCompileJob {
  GPUPipelineCompileJob       *allNext;
  GPUPipelineCompileJob       *queueNext;
  GPURenderPipeline           *pipeline;
  char                        *label;
  char                        *vertexEntry;
  char                        *fragmentEntry;
  GPUColorTargetState         *colorTargets;
  GPUVertexBufferLayout       *bufferLayouts;
  GPUVertexAttribute          *attributes;
  GPUDepthStencilState         depthStencil;
  GPURenderPipelineCreateInfo  info;
  uint64_t                     id;
  GPUPipelineCompileJobState   state;
};

static GPUPipelineCacheSync *
gpu_pipelineCacheSync(GPUPipelineCache *cache) {
  return cache ? cache->_sync : NULL;
}

static void
gpu_pipelineCacheLock(GPUPipelineCache *cache) {
  GPUPipelineCacheSync *sync;

  sync = gpu_pipelineCacheSync(cache);
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&sync->lock);
#else
  pthread_mutex_lock(&sync->lock);
#endif
}

static void
gpu_pipelineCacheUnlock(GPUPipelineCache *cache) {
  GPUPipelineCacheSync *sync;

  sync = gpu_pipelineCacheSync(cache);
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&sync->lock);
#else
  pthread_mutex_unlock(&sync->lock);
#endif
}

static void
gpu_pipelineCacheSignal(GPUPipelineCache *cache) {
  GPUPipelineCacheSync *sync;

  sync = gpu_pipelineCacheSync(cache);
#if defined(_WIN32) || defined(WIN32)
  WakeAllConditionVariable(&sync->condition);
#else
  pthread_cond_broadcast(&sync->condition);
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
gpu_retainComputePipeline(GPUComputePipeline *pipeline) {
#if defined(_WIN32) || defined(WIN32)
  InterlockedIncrement((volatile LONG *)&pipeline->_refCount);
#else
  __atomic_add_fetch(&pipeline->_refCount, 1u, __ATOMIC_RELAXED);
#endif
}

GPU_HIDE
bool
gpuReleaseComputePipeline(GPUComputePipeline *pipeline) {
#if defined(_WIN32) || defined(WIN32)
  return InterlockedDecrement((volatile LONG *)&pipeline->_refCount) == 0;
#else
  return __atomic_sub_fetch(&pipeline->_refCount, 1u, __ATOMIC_ACQ_REL) == 0u;
#endif
}

static void
gpu_retainPipeline(GPUPipelineCacheEntryType type, void *pipeline) {
  if (type == GPU_PIPELINE_CACHE_RENDER) {
    gpu_retainRenderPipeline(pipeline);
  } else {
    gpu_retainComputePipeline(pipeline);
  }
}

static void
gpu_destroyPipeline(GPUPipelineCacheEntryType type, void *pipeline) {
  if (type == GPU_PIPELINE_CACHE_RENDER) {
    GPUDestroyRenderPipeline(pipeline);
  } else {
    GPUDestroyComputePipeline(pipeline);
  }
}

static void
gpu_pipelineKeyWrite(GPUPipelineKeyWriter *writer,
                     const void           *value,
                     size_t                size) {
  if (!writer->valid || size > SIZE_MAX - writer->offset) {
    writer->valid = false;
    return;
  }
  if (writer->data && writer->offset <= writer->capacity &&
      size <= writer->capacity - writer->offset) {
    memcpy(writer->data + writer->offset, value, size);
    for (size_t i = 0u; i < size; i++) {
      writer->hash ^= ((const uint8_t *)value)[i];
      writer->hash *= 1099511628211ull;
    }
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
gpu_pipelineKeyWriteRenderInfo(GPUPipelineKeyWriter              *writer,
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

static void
gpu_pipelineKeyWriteComputeInfo(GPUPipelineKeyWriter               *writer,
                                const GPUComputePipelineCreateInfo *info) {
  uintptr_t layout;
  uintptr_t library;

  layout  = (uintptr_t)info->layout;
  library = (uintptr_t)info->library;
  GPU_PIPELINE_KEY_WRITE(writer, layout);
  GPU_PIPELINE_KEY_WRITE(writer, library);
  gpu_pipelineKeyWriteString(writer, info->entryPoint);
}

static bool
gpu_pipelineKeyPrepare(GPUPipelineCacheKey *key, size_t size) {
  key->size = size;
  if (size <= sizeof(key->inlineData)) {
    key->data = key->inlineData;
    return true;
  }

  key->data = malloc(size);
  if (!key->data) {
    return false;
  }
  key->ownsData = true;
  return true;
}

GPU_HIDE
void
gpuPipelineCacheReleaseKey(GPUPipelineCacheKey *key) {
  if (!key) {
    return;
  }
  if (key->ownsData) {
    free(key->data);
  }
  key->data     = NULL;
  key->size     = 0u;
  key->hash     = 0u;
  key->ownsData = false;
}

static bool
gpu_buildRenderPipelineKey(const GPURenderPipelineCreateInfo *info,
                           GPUPipelineCacheKey                *outKey) {
  GPUPipelineKeyWriter writer;

  outKey->data     = NULL;
  outKey->size     = 0u;
  outKey->hash     = 0u;
  outKey->ownsData = false;
  if (info->chain.pNext) {
    return false;
  }

  writer.data     = outKey->inlineData;
  writer.offset   = 0u;
  writer.capacity = sizeof(outKey->inlineData);
  writer.hash     = GPU_PIPELINE_KEY_HASH_SEED;
  writer.valid    = true;
  gpu_pipelineKeyWriteRenderInfo(&writer, info);
  if (!writer.valid || writer.offset == 0u) {
    return false;
  }
  if (writer.offset <= writer.capacity) {
    outKey->data = outKey->inlineData;
    outKey->size = writer.offset;
    outKey->hash = writer.hash;
    return true;
  }

  if (!gpu_pipelineKeyPrepare(outKey, writer.offset)) {
    return false;
  }
  writer.data     = outKey->data;
  writer.offset   = 0u;
  writer.capacity = outKey->size;
  writer.hash     = GPU_PIPELINE_KEY_HASH_SEED;
  writer.valid    = true;
  gpu_pipelineKeyWriteRenderInfo(&writer, info);
  if (!writer.valid || writer.offset != outKey->size) {
    gpuPipelineCacheReleaseKey(outKey);
    return false;
  }
  outKey->hash = writer.hash;
  return true;
}

static bool
gpu_buildComputePipelineKey(const GPUComputePipelineCreateInfo *info,
                            GPUPipelineCacheKey                 *outKey) {
  GPUPipelineKeyWriter writer;

  outKey->data     = NULL;
  outKey->size     = 0u;
  outKey->hash     = 0u;
  outKey->ownsData = false;
  if (info->chain.pNext) {
    return false;
  }

  writer.data     = outKey->inlineData;
  writer.offset   = 0u;
  writer.capacity = sizeof(outKey->inlineData);
  writer.hash     = GPU_PIPELINE_KEY_HASH_SEED;
  writer.valid    = true;
  gpu_pipelineKeyWriteComputeInfo(&writer, info);
  if (!writer.valid || writer.offset == 0u) {
    return false;
  }
  if (writer.offset <= writer.capacity) {
    outKey->data = outKey->inlineData;
    outKey->size = writer.offset;
    outKey->hash = writer.hash;
    return true;
  }

  if (!gpu_pipelineKeyPrepare(outKey, writer.offset)) {
    return false;
  }
  writer.data     = outKey->data;
  writer.offset   = 0u;
  writer.capacity = outKey->size;
  writer.hash     = GPU_PIPELINE_KEY_HASH_SEED;
  writer.valid    = true;
  gpu_pipelineKeyWriteComputeInfo(&writer, info);
  if (!writer.valid || writer.offset != outKey->size) {
    gpuPipelineCacheReleaseKey(outKey);
    return false;
  }
  outKey->hash = writer.hash;
  return true;
}

static GPUPipelineCacheEntry *
gpu_pipelineCacheFindEntry(GPUPipelineCache          *cache,
                           const GPUPipelineCacheKey *key,
                           GPUPipelineCacheEntryType  type) {
  GPUPipelineCacheEntry *entry;
  size_t                 bucket;

  bucket = (size_t)key->hash & (cache->bucketCount - 1u);
  for (entry = cache->buckets[bucket]; entry; entry = entry->hashNext) {
    if (entry->type == type && entry->keyHash == key->hash &&
        entry->keySize == key->size &&
        memcmp(entry->keyData, key->data, key->size) == 0) {
      return entry;
    }
  }
  return NULL;
}

static void
gpu_pipelineCacheRemoveEntry(GPUPipelineCache      *cache,
                             GPUPipelineCacheEntry *entry) {
  GPUPipelineCacheEntry **link;
  size_t                  bucket;

  bucket = (size_t)entry->keyHash & (cache->bucketCount - 1u);
  for (link = &cache->buckets[bucket]; *link; link = &(*link)->hashNext) {
    if (*link == entry) {
      *link = entry->hashNext;
      return;
    }
  }
}

static void *
gpu_pipelineCacheFind(GPUPipelineCache          *cache,
                      const GPUPipelineCacheKey *key,
                      GPUPipelineCacheEntryType  type) {
  GPUPipelineCacheEntry *entry;
  void                   *pipeline;

  pipeline = NULL;
  gpu_pipelineCacheLock(cache);
  entry = gpu_pipelineCacheFindEntry(cache, key, type);
  if (entry) {
    pipeline = entry->pipeline;
    gpu_retainPipeline(type, pipeline);
    cache->stats.pipelineHits++;
    gpuDeviceCacheCounterAdd(&cache->device->cacheStats.pipelineHits, 1u);
  }
  gpu_pipelineCacheUnlock(cache);
  return pipeline;
}

static void *
gpu_pipelineCacheStore(GPUPipelineCache    *cache,
                       GPUPipelineCacheKey *key,
                       GPUPipelineCacheEntryType type,
                       void                *pipeline) {
  GPUPipelineCacheEntry *entry;
  GPUPipelineCacheEntry *evicted;
  void                   *result;
  size_t                  bucket;

  if (key->size > SIZE_MAX - sizeof(*entry)) {
    gpuRecordPipelineCompile(cache->device, cache);
    gpuPipelineCacheReleaseKey(key);
    return pipeline;
  }
  entry = malloc(sizeof(*entry) + key->size);
  if (!entry) {
    gpuRecordPipelineCompile(cache->device, cache);
    gpuPipelineCacheReleaseKey(key);
    return pipeline;
  }
  entry->next     = NULL;
  entry->hashNext = NULL;
  entry->pipeline = pipeline;
  entry->keySize  = key->size;
  entry->keyHash  = key->hash;
  entry->type     = type;
  memcpy(entry->keyData, key->data, key->size);
  evicted = NULL;
  result  = pipeline;

  gpu_pipelineCacheLock(cache);
  {
    GPUPipelineCacheEntry *existing;

    existing = gpu_pipelineCacheFindEntry(cache, key, type);
    if (existing) {
      result = existing->pipeline;
      gpu_retainPipeline(type, result);
      cache->stats.pipelineHits++;
      cache->stats.pipelineCompiles++;
      gpuDeviceCacheCounterAdd(&cache->device->cacheStats.pipelineHits, 1u);
      gpuDeviceCacheCounterAdd(&cache->device->cacheStats.pipelineCompiles, 1u);
      gpu_pipelineCacheUnlock(cache);
      free(entry);
      gpu_destroyPipeline(type, pipeline);
      gpuPipelineCacheReleaseKey(key);
      return result;
    }
  }

  if (cache->entryCount == cache->maxEntries) {
    evicted     = cache->head;
    cache->head = evicted->next;
    gpu_pipelineCacheRemoveEntry(cache, evicted);
    if (!cache->head) {
      cache->tail = NULL;
    }
    cache->entryCount--;
  }
  gpu_retainPipeline(type, pipeline);
  if (cache->tail) {
    cache->tail->next = entry;
  } else {
    cache->head = entry;
  }
  bucket                 = (size_t)entry->keyHash &
                           (cache->bucketCount - 1u);
  entry->hashNext        = cache->buckets[bucket];
  cache->buckets[bucket] = entry;
  cache->tail            = entry;
  cache->entryCount++;
  cache->stats.pipelineMisses++;
  cache->stats.pipelineCompiles++;
  gpuDeviceCacheCounterAdd(&cache->device->cacheStats.pipelineMisses, 1u);
  gpuDeviceCacheCounterAdd(&cache->device->cacheStats.pipelineCompiles, 1u);
  gpu_pipelineCacheUnlock(cache);

  gpuPipelineCacheReleaseKey(key);
  if (evicted) {
    gpu_destroyPipeline(evicted->type, evicted->pipeline);
    free(evicted);
  }
  return result;
}

GPU_HIDE
GPUResult
gpuPipelineCacheFindRender(GPUPipelineCache                  *cache,
                           const GPURenderPipelineCreateInfo *info,
                           GPUPipelineCacheKey               *outKey,
                           GPURenderPipeline                **outPipeline) {
  *outPipeline = NULL;
  if (!gpu_buildRenderPipelineKey(info, outKey)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  *outPipeline = gpu_pipelineCacheFind(cache,
                                       outKey,
                                       GPU_PIPELINE_CACHE_RENDER);
  return GPU_OK;
}

GPU_HIDE
GPURenderPipeline *
gpuPipelineCacheStoreRender(GPUPipelineCache    *cache,
                            GPUPipelineCacheKey *key,
                            GPURenderPipeline   *pipeline) {
  return gpu_pipelineCacheStore(cache,
                                key,
                                GPU_PIPELINE_CACHE_RENDER,
                                pipeline);
}

GPU_HIDE
GPUResult
gpuPipelineCacheFindCompute(GPUPipelineCache                   *cache,
                            const GPUComputePipelineCreateInfo *info,
                            GPUPipelineCacheKey                *outKey,
                            GPUComputePipeline                **outPipeline) {
  *outPipeline = NULL;
  if (!gpu_buildComputePipelineKey(info, outKey)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  *outPipeline = gpu_pipelineCacheFind(cache,
                                       outKey,
                                       GPU_PIPELINE_CACHE_COMPUTE);
  return GPU_OK;
}

GPU_HIDE
GPUComputePipeline *
gpuPipelineCacheStoreCompute(GPUPipelineCache    *cache,
                             GPUPipelineCacheKey *key,
                             GPUComputePipeline  *pipeline) {
  return gpu_pipelineCacheStore(cache,
                                key,
                                GPU_PIPELINE_CACHE_COMPUTE,
                                pipeline);
}

static void
gpu_deviceCacheLock(GPUDevice *device) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(device->_pipelineCacheLock);
#else
  pthread_mutex_lock(device->_pipelineCacheLock);
#endif
}

static void
gpu_deviceCacheUnlock(GPUDevice *device) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(device->_pipelineCacheLock);
#else
  pthread_mutex_unlock(device->_pipelineCacheLock);
#endif
}

GPU_HIDE
GPUResult
gpuInitPipelineCacheDevice(GPUDevice *device) {
  void *lock;

  if (!device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
#if defined(_WIN32) || defined(WIN32)
  lock = calloc(1, sizeof(CRITICAL_SECTION));
  if (!lock) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  InitializeCriticalSection(lock);
#else
  lock = calloc(1, sizeof(pthread_mutex_t));
  if (!lock) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  if (pthread_mutex_init(lock, NULL) != 0) {
    free(lock);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif
  device->_pipelineCacheLock     = lock;
  device->_nextPipelineCompileId = 1u;
  return GPU_OK;
}

GPU_HIDE
void
gpuDestroyPipelineCacheDevice(GPUDevice *device) {
  if (!device || !device->_pipelineCacheLock) {
    return;
  }
#if defined(_WIN32) || defined(WIN32)
  DeleteCriticalSection(device->_pipelineCacheLock);
#else
  pthread_mutex_destroy(device->_pipelineCacheLock);
#endif
  free(device->_pipelineCacheLock);
  device->_pipelineCacheLock = NULL;
}

static char *
gpu_pipelineCacheDupString(const char *value) {
  size_t size;
  char  *copy;

  if (!value) {
    return NULL;
  }
  size = strlen(value) + 1u;
  copy = malloc(size);
  if (copy) {
    memcpy(copy, value, size);
  }
  return copy;
}

static void
gpu_destroyPipelineJob(GPUPipelineCompileJob *job) {
  if (!job) {
    return;
  }
  GPUDestroyRenderPipeline(job->pipeline);
  free(job->attributes);
  free(job->bufferLayouts);
  free(job->colorTargets);
  free(job->fragmentEntry);
  free(job->vertexEntry);
  free(job->label);
  free(job);
}

static GPUPipelineCompileJob *
gpu_createPipelineJob(GPUPipelineCache                  *cache,
                      const GPURenderPipelineCreateInfo *info) {
  GPUPipelineCompileJob *job;
  uint32_t               attributeCount;
  uint32_t               cursor;

  job = calloc(1, sizeof(*job));
  if (!job) {
    return NULL;
  }
  job->label         = gpu_pipelineCacheDupString(info->label);
  job->vertexEntry   = gpu_pipelineCacheDupString(info->vertexEntry);
  job->fragmentEntry = gpu_pipelineCacheDupString(info->fragmentEntry);
  if ((info->label && !job->label) || !job->vertexEntry ||
      !job->fragmentEntry) {
    gpu_destroyPipelineJob(job);
    return NULL;
  }

  if (info->colorTargetCount > 0u) {
    job->colorTargets = malloc((size_t)info->colorTargetCount *
                               sizeof(*job->colorTargets));
    if (!job->colorTargets) {
      gpu_destroyPipelineJob(job);
      return NULL;
    }
    memcpy(job->colorTargets,
           info->pColorTargets,
           (size_t)info->colorTargetCount * sizeof(*job->colorTargets));
  }

  attributeCount = 0u;
  for (uint32_t i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    if (info->vertex.pBufferLayouts[i].attributeCount >
        UINT32_MAX - attributeCount) {
      gpu_destroyPipelineJob(job);
      return NULL;
    }
    attributeCount += info->vertex.pBufferLayouts[i].attributeCount;
  }
  if (info->vertex.bufferLayoutCount > 0u) {
    job->bufferLayouts = calloc(info->vertex.bufferLayoutCount,
                                sizeof(*job->bufferLayouts));
    if (!job->bufferLayouts) {
      gpu_destroyPipelineJob(job);
      return NULL;
    }
  }
  if (attributeCount > 0u) {
    job->attributes = malloc((size_t)attributeCount * sizeof(*job->attributes));
    if (!job->attributes) {
      gpu_destroyPipelineJob(job);
      return NULL;
    }
  }

  cursor = 0u;
  for (uint32_t i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    job->bufferLayouts[i] = info->vertex.pBufferLayouts[i];
    job->bufferLayouts[i].pAttributes =
      job->bufferLayouts[i].attributeCount > 0u
        ? &job->attributes[cursor]
        : NULL;
    if (job->bufferLayouts[i].attributeCount > 0u) {
      memcpy(&job->attributes[cursor],
             info->vertex.pBufferLayouts[i].pAttributes,
             (size_t)job->bufferLayouts[i].attributeCount *
               sizeof(*job->attributes));
      cursor += job->bufferLayouts[i].attributeCount;
    }
  }

  job->info                       = *info;
  job->info.label                 = job->label;
  job->info.cache                 = cache;
  job->info.vertexEntry           = job->vertexEntry;
  job->info.fragmentEntry         = job->fragmentEntry;
  job->info.pColorTargets         = job->colorTargets;
  job->info.vertex.pBufferLayouts = job->bufferLayouts;
  if (info->pDepthStencilState) {
    job->depthStencil            = *info->pDepthStencilState;
    job->info.pDepthStencilState = &job->depthStencil;
  }
  job->state = GPU_PIPELINE_JOB_QUEUED;
  return job;
}

static bool
gpu_pipelineInfoCanCopy(const GPURenderPipelineCreateInfo *info) {
  if (!info || !info->layout || !info->library || !info->vertexEntry ||
      !info->fragmentEntry || info->chain.pNext ||
      (info->colorTargetCount > 0u && !info->pColorTargets) ||
      (info->vertex.bufferLayoutCount > 0u &&
       !info->vertex.pBufferLayouts)) {
    return false;
  }
  for (uint32_t i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    if (info->vertex.pBufferLayouts[i].attributeCount > 0u &&
        !info->vertex.pBufferLayouts[i].pAttributes) {
      return false;
    }
  }
  return true;
}

static void
gpu_pipelineCacheWorkerRun(GPUPipelineCache *cache) {
  GPUPipelineCacheSync *sync;

  sync = gpu_pipelineCacheSync(cache);
  for (;;) {
    GPUPipelineCompileJob *job;
    GPURenderPipeline     *pipeline;
    GPUResult              result;

    gpu_pipelineCacheLock(cache);
    while (!cache->stopWorker && !cache->queueHead) {
#if defined(_WIN32) || defined(WIN32)
      SleepConditionVariableCS(&sync->condition, &sync->lock, INFINITE);
#else
      pthread_cond_wait(&sync->condition, &sync->lock);
#endif
    }
    if (cache->stopWorker) {
      gpu_pipelineCacheUnlock(cache);
      return;
    }
    job              = cache->queueHead;
    cache->queueHead = job->queueNext;
    if (!cache->queueHead) {
      cache->queueTail = NULL;
    }
    job->queueNext = NULL;
    job->state     = GPU_PIPELINE_JOB_COMPILING;
    gpu_pipelineCacheUnlock(cache);

    pipeline = NULL;
    result   = GPUCreateRenderPipeline(cache->device, &job->info, &pipeline);

    gpu_pipelineCacheLock(cache);
    job->pipeline = pipeline;
    job->state    = result == GPU_OK
                      ? GPU_PIPELINE_JOB_READY
                      : GPU_PIPELINE_JOB_FAILED;
    gpu_pipelineCacheSignal(cache);
    gpu_pipelineCacheUnlock(cache);
  }
}

#if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI
gpu_pipelineCacheWorker(void *context) {
  gpu_pipelineCacheWorkerRun(context);
  return 0u;
}
#else
static void *
gpu_pipelineCacheWorker(void *context) {
  gpu_pipelineCacheWorkerRun(context);
  return NULL;
}
#endif

static bool
gpu_pipelineCacheStartWorker(GPUPipelineCache *cache) {
  GPUPipelineCacheSync *sync;

  sync = gpu_pipelineCacheSync(cache);
  if (sync->workerStarted) {
    return true;
  }
#if defined(_WIN32) || defined(WIN32)
  sync->worker = CreateThread(NULL,
                              0u,
                              gpu_pipelineCacheWorker,
                              cache,
                              0u,
                              NULL);
  sync->workerStarted = sync->worker != NULL;
#else
  sync->workerStarted = pthread_create(&sync->worker,
                                       NULL,
                                       gpu_pipelineCacheWorker,
                                       cache) == 0;
#endif
  return sync->workerStarted;
}

static size_t
gpu_pipelineCacheBucketCount(uint64_t maxEntries) {
  size_t count;
  size_t target;

  target = maxEntries > GPU_PIPELINE_CACHE_MAX_BUCKETS
             ? GPU_PIPELINE_CACHE_MAX_BUCKETS
             : (size_t)maxEntries;
  count = 1u;
  while (count < target) {
    count <<= 1u;
  }
  return count;
}

GPU_EXPORT
GPUResult
GPUCreatePipelineCache(GPUDevice                         * __restrict device,
                       const GPUPipelineCacheCreateInfo  * __restrict info,
                       GPUPipelineCache                 ** __restrict outCache) {
  GPUPipelineCache     *cache;
  GPUPipelineCacheSync *sync;
  GPUApi               *api;
  GPUResult             result;
  uint64_t              maxEntries;
  size_t                bucketCount;

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
  if (info->enableDiskCache && (!info->cachePath || !info->cachePath[0])) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuDeviceApi(device);
  if (info->enableDiskCache &&
      (!api || !api->pipelineCache.create || !api->pipelineCache.destroy)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  maxEntries  = info->maxEntries > 0u
                  ? info->maxEntries
                  : GPU_PIPELINE_CACHE_DEFAULT_ENTRIES;
  bucketCount = gpu_pipelineCacheBucketCount(maxEntries);
  cache = calloc(1, sizeof(*cache));
  if (!cache) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  cache->buckets = calloc(bucketCount, sizeof(*cache->buckets));
  if (!cache->buckets) {
    free(cache);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  sync = calloc(1, sizeof(*sync));
  if (!sync) {
    free(cache->buckets);
    free(cache);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&sync->lock);
  InitializeConditionVariable(&sync->condition);
#else
  if (pthread_mutex_init(&sync->lock, NULL) != 0) {
    free(sync);
    free(cache->buckets);
    free(cache);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (pthread_cond_init(&sync->condition, NULL) != 0) {
    pthread_mutex_destroy(&sync->lock);
    free(sync);
    free(cache->buckets);
    free(cache);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif

  if (info->enableDiskCache) {
    result = api->pipelineCache.create(device, info, cache);
    if (result != GPU_OK) {
#if defined(_WIN32) || defined(WIN32)
      DeleteCriticalSection(&sync->lock);
#else
      pthread_cond_destroy(&sync->condition);
      pthread_mutex_destroy(&sync->lock);
#endif
      free(sync);
      free(cache->buckets);
      free(cache);
      return result;
    }
  }

  cache->device      = device;
  cache->_sync       = sync;
  cache->maxEntries  = maxEntries;
  cache->bucketCount = bucketCount;
  gpu_deviceCacheLock(device);
  cache->deviceNext       = device->_pipelineCaches;
  device->_pipelineCaches = cache;
  gpu_deviceCacheUnlock(device);
  *outCache = cache;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyPipelineCache(GPUPipelineCache *cache) {
  GPUPipelineCacheEntry *entry;
  GPUPipelineCompileJob *job;
  GPUPipelineCacheSync  *sync;
  GPUPipelineCache     **link;

  if (!cache) {
    return;
  }

  sync = gpu_pipelineCacheSync(cache);
  gpu_deviceCacheLock(cache->device);
  for (link = &cache->device->_pipelineCaches; *link; link = &(*link)->deviceNext) {
    if (*link == cache) {
      *link = cache->deviceNext;
      break;
    }
  }
  gpu_deviceCacheUnlock(cache->device);

  gpu_pipelineCacheLock(cache);
  cache->stopWorker = true;
  gpu_pipelineCacheSignal(cache);
  gpu_pipelineCacheUnlock(cache);
  if (sync->workerStarted) {
#if defined(_WIN32) || defined(WIN32)
    WaitForSingleObject(sync->worker, INFINITE);
    CloseHandle(sync->worker);
#else
    pthread_join(sync->worker, NULL);
#endif
  }

  if (cache->_priv) {
    GPUApi *api;

    api = gpuDeviceApi(cache->device);
    if (api && api->pipelineCache.destroy) {
      api->pipelineCache.destroy(cache);
    }
  }

  gpu_pipelineCacheLock(cache);
  entry             = cache->head;
  job               = cache->jobs;
  cache->head       = NULL;
  cache->tail       = NULL;
  cache->jobs       = NULL;
  cache->queueHead  = NULL;
  cache->queueTail  = NULL;
  cache->entryCount = 0u;
  cache->jobCount   = 0u;
  gpu_pipelineCacheUnlock(cache);

#if defined(_WIN32) || defined(WIN32)
  DeleteCriticalSection(&sync->lock);
#else
  pthread_cond_destroy(&sync->condition);
  pthread_mutex_destroy(&sync->lock);
#endif
  free(sync);

  while (entry) {
    GPUPipelineCacheEntry *next;

    next = entry->next;
    gpu_destroyPipeline(entry->type, entry->pipeline);
    free(entry);
    entry = next;
  }
  while (job) {
    GPUPipelineCompileJob *next;

    next = job->allNext;
    gpu_destroyPipelineJob(job);
    job = next;
  }
  free(cache->buckets);
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
  if (!device || !cache || cache->device != device ||
      !gpu_pipelineInfoCanCopy(info) || info->layout->_device != device ||
      info->library->_device != device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  {
    GPUPipelineCompileJob *job;

    job = gpu_createPipelineJob(cache, info);
    if (!job) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    gpu_deviceCacheLock(device);
    job->id = device->_nextPipelineCompileId++;
    if (job->id == 0u) {
      job->id = device->_nextPipelineCompileId++;
    }
    gpu_pipelineCacheLock(cache);
    if (cache->jobCount == cache->maxEntries) {
      gpu_pipelineCacheUnlock(cache);
      gpu_deviceCacheUnlock(device);
      gpu_destroyPipelineJob(job);
      return GPU_ERROR_INSUFFICIENT_CAPACITY;
    }
    if (!gpu_pipelineCacheStartWorker(cache)) {
      gpu_pipelineCacheUnlock(cache);
      gpu_deviceCacheUnlock(device);
      gpu_destroyPipelineJob(job);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    job->allNext = cache->jobs;
    cache->jobs  = job;
    if (cache->queueTail) {
      cache->queueTail->queueNext = job;
    } else {
      cache->queueHead = job;
    }
    cache->queueTail  = job;
    cache->jobCount++;
    outHandle->id = job->id;
    gpu_pipelineCacheSignal(cache);
    gpu_pipelineCacheUnlock(cache);
    gpu_deviceCacheUnlock(device);
  }
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUPollRenderPipelineCompile(GPUDevice                 * __restrict device,
                             GPUPipelineCompileHandle               handle,
                             GPUPipelineCompileStatus  * __restrict outStatus,
                             GPURenderPipeline        ** __restrict outPipeline) {
  if (!outStatus || !outPipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outStatus   = GPU_PIPELINE_COMPILE_FAILED;
  *outPipeline = NULL;
  if (!device || !device->_pipelineCacheLock || handle.id == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  gpu_deviceCacheLock(device);
  for (GPUPipelineCache *cache = device->_pipelineCaches;
       cache;
       cache = cache->deviceNext) {
    GPUPipelineCompileJob **link;
    GPUPipelineCompileJob  *job;

    gpu_pipelineCacheLock(cache);
    for (link = &cache->jobs; *link; link = &(*link)->allNext) {
      if ((*link)->id == handle.id) {
        break;
      }
    }
    job = *link;
    if (!job) {
      gpu_pipelineCacheUnlock(cache);
      continue;
    }
    if (job->state == GPU_PIPELINE_JOB_QUEUED ||
        job->state == GPU_PIPELINE_JOB_COMPILING) {
      *outStatus = GPU_PIPELINE_COMPILE_PENDING;
      gpu_pipelineCacheUnlock(cache);
      gpu_deviceCacheUnlock(device);
      return GPU_OK;
    }

    *link = job->allNext;
    cache->jobCount--;
    if (job->state == GPU_PIPELINE_JOB_READY) {
      *outStatus    = GPU_PIPELINE_COMPILE_READY;
      *outPipeline  = job->pipeline;
      job->pipeline = NULL;
    }
    gpu_pipelineCacheUnlock(cache);
    gpu_deviceCacheUnlock(device);
    gpu_destroyPipelineJob(job);
    return GPU_OK;
  }
  gpu_deviceCacheUnlock(device);
  return GPU_ERROR_INVALID_ARGUMENT;
}

GPU_HIDE
void
gpuRecordPipelineCompile(GPUDevice *device, GPUPipelineCache *cache) {
  if (!cache) {
    if (device) {
      gpuDeviceCacheCounterAdd(&device->cacheStats.pipelineCompiles, 1u);
    }
    return;
  }

  gpu_pipelineCacheLock(cache);
  cache->stats.pipelineMisses++;
  cache->stats.pipelineCompiles++;
  if (cache->device) {
    gpuDeviceCacheCounterAdd(&cache->device->cacheStats.pipelineMisses, 1u);
    gpuDeviceCacheCounterAdd(&cache->device->cacheStats.pipelineCompiles, 1u);
  }
  gpu_pipelineCacheUnlock(cache);
}
