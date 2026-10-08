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
#include "constants_internal.h"
#include "compute_internal.h"
#include "descr/descriptor_internal.h"
#include "execution_graph_internal.h"
#include "library_internal.h"
#include "pipeline_cache_internal.h"
#include "ray_internal.h"
#include "render/pipeline_internal.h"

#if !defined(_WIN32) && !defined(WIN32)
#  include <pthread.h>
#endif

#define GPU_PIPELINE_CACHE_DEFAULT_ENTRIES 256u
#define GPU_PIPELINE_CACHE_MAX_BUCKETS     4096u
#define GPU_PIPELINE_KEY_HASH_SEED         14695981039346656037ull

#define GPU_PIPELINE_KEY_WRITE(WRITER, VALUE) \
  pipelineKeyWrite((WRITER), &(VALUE), sizeof(VALUE))

typedef enum PipelineCacheEntryType {
  GPU_PIPELINE_CACHE_RENDER = 0,
  GPU_PIPELINE_CACHE_COMPUTE,
  GPU_PIPELINE_CACHE_RAY,
  GPU_PIPELINE_CACHE_GRAPH
} PipelineCacheEntryType;

struct PipelineCacheEntry {
  PipelineCacheEntry       *next;
  PipelineCacheEntry       *hashNext;
  void                     *pipeline;
  size_t                    keySize;
  uint64_t                  keyHash;
  PipelineCacheEntryType    type;
  uint8_t                   keyData[];
};

typedef struct PipelineKeyWriter {
  uint8_t *data;
  size_t   offset;
  size_t   capacity;
  uint64_t hash;
  bool     valid;
} PipelineKeyWriter;

typedef struct PipelineCacheSync {
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION   lock;
  CONDITION_VARIABLE condition;
  HANDLE             worker;
#else
  pthread_mutex_t lock;
  pthread_cond_t  condition;
  pthread_t       worker;
#endif
  bool workerStarted;
} PipelineCacheSync;

typedef enum PipelineCompileJobState {
  GPU_PIPELINE_JOB_QUEUED = 0,
  GPU_PIPELINE_JOB_COMPILING,
  GPU_PIPELINE_JOB_READY,
  GPU_PIPELINE_JOB_FAILED
} PipelineCompileJobState;

struct PipelineCompileJob {
  PipelineCompileJob         *allNext;
  PipelineCompileJob         *queueNext;
  GPURenderPipeline          *pipeline;
  char                       *label;
  char                       *vertexEntry;
  char                       *fragmentEntry;
  char                       *taskEntry;
  char                       *meshEntry;
  GPUColorTargetState        *colorTargets;
  GPUVertexBufferLayout      *bufferLayouts;
  GPUVertexAttribute         *attributes;
  GPUConstant                *values;
  GPUDepthStencilState        depthStencil;
  GPUMeshPipelineEXT          mesh;
  GPUPipelineConstants        constants;
  GPURenderPipelineCreateInfo info;
  uint64_t                    id;
  PipelineCompileJobState     state;
};

static PipelineCacheSync*
pipelineCacheSync(GPUPipelineCache *cache) {
  return cache ? cache->_sync : NULL;
}

static void
pipelineCacheLock(GPUPipelineCache *cache) {
  PipelineCacheSync    *sync;

  sync = pipelineCacheSync(cache);
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&sync->lock);
#else
  pthread_mutex_lock(&sync->lock);
#endif
}

static void
pipelineCacheUnlock(GPUPipelineCache *cache) {
  PipelineCacheSync    *sync;

  sync = pipelineCacheSync(cache);
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&sync->lock);
#else
  pthread_mutex_unlock(&sync->lock);
#endif
}

static void
pipelineCacheSignal(GPUPipelineCache *cache) {
  PipelineCacheSync    *sync;

  sync = pipelineCacheSync(cache);
#if defined(_WIN32) || defined(WIN32)
  WakeAllConditionVariable(&sync->condition);
#else
  pthread_cond_broadcast(&sync->condition);
#endif
}

static void
retainPipeline(PipelineCacheEntryType    type, void *pipeline) {
  switch (type) {
    case GPU_PIPELINE_CACHE_RENDER:
      retainRenderPipeline(pipeline);
      break;
    case GPU_PIPELINE_CACHE_COMPUTE:
      retainComputePipeline(pipeline);
      break;
    case GPU_PIPELINE_CACHE_RAY:
      retainRayTracingPipeline(pipeline);
      break;
    case GPU_PIPELINE_CACHE_GRAPH:
      retainExecutionGraph(pipeline);
      break;
  }
}

static void
gpu_destroyPipeline(PipelineCacheEntryType    type, void *pipeline) {
  switch (type) {
    case GPU_PIPELINE_CACHE_RENDER:
      GPUDestroyRenderPipeline(pipeline);
      break;
    case GPU_PIPELINE_CACHE_COMPUTE:
      GPUDestroyComputePipeline(pipeline);
      break;
    case GPU_PIPELINE_CACHE_RAY:
      GPUDestroyRayTracingPipelineEXT(pipeline);
      break;
    case GPU_PIPELINE_CACHE_GRAPH:
      GPUDestroyExecutionGraphEXT(pipeline);
      break;
  }
}

static void
pipelineKeyWrite(PipelineKeyWriter    *writer,
                 const void           *value,
                 size_t                size) {
  size_t i;

  if (!writer->valid || size > SIZE_MAX - writer->offset) {
    writer->valid = false;
    return;
  }

  if (writer->data && writer->offset <= writer->capacity
      && size <= writer->capacity - writer->offset) {
    memcpy(writer->data + writer->offset, value, size);

    for (i = 0u; i < size; i++) {
      writer->hash ^= ((const uint8_t *)value)[i];
      writer->hash *= 1099511628211ull;
    }
  }

  writer->offset += size;
}

static void
pipelineKeyWriteString(PipelineKeyWriter    *writer, const char *value) {
  pipelineKeyWrite(writer, value, strlen(value) + 1u);
}

static void
pipelineKeyWriteOptionalString(PipelineKeyWriter    *writer,
                               const char           *value) {
  bool present;

  present = value != NULL;
  GPU_PIPELINE_KEY_WRITE(writer, present);

  if (present) {
    pipelineKeyWriteString(writer, value);
  }
}

static void
pipelineKeyWriteDepthStencil(PipelineKeyWriter          *writer,
                             const GPUDepthStencilState *state) {
  GPUDepthStencilState        empty = {0};
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
pipelineKeyWriteConstants(PipelineKeyWriter      *writer,
                          const GPUChainedStruct *chain) {
  const GPUPipelineConstants *constants;
  const GPUConstant          *value;
  uint32_t                    count;
  uint32_t                    i;

  constants = pipelineConstants(chain);
  count     = constants ? constants->constantCount : 0u;
  GPU_PIPELINE_KEY_WRITE(writer, count);

  for (i = 0u; i < count; i++) {
    value = &constants->pConstants[i];
    GPU_PIPELINE_KEY_WRITE(writer, value->id);
    GPU_PIPELINE_KEY_WRITE(writer, value->type);

    switch (value->type) {
      case GPU_CONSTANT_BOOL: GPU_PIPELINE_KEY_WRITE(writer, value->value.boolean); break;
      case GPU_CONSTANT_I32:  GPU_PIPELINE_KEY_WRITE(writer, value->value.i32); break;
      case GPU_CONSTANT_U32:  GPU_PIPELINE_KEY_WRITE(writer, value->value.u32); break;
      case GPU_CONSTANT_F32:  GPU_PIPELINE_KEY_WRITE(writer, value->value.f32); break;
      default: writer->valid = false; break;
    }
  }
}

static void
pipelineKeyWriteRenderInfo(PipelineKeyWriter                 *writer,
                           const GPURenderPipelineCreateInfo *info) {
  const GPUMeshPipelineEXT    *mesh;
  const GPUVertexBufferLayout *bufferLayout;
  const GPUVertexAttribute    *attribute;
  const GPUColorTargetState   *target;
  uintptr_t                    layout;
  uintptr_t                    library;
  uint32_t                     pipelineType;
  uint32_t                     i;
  uint32_t                     j;
  uint32_t                     k;

  layout       = (uintptr_t)info->layout;
  library      = (uintptr_t)info->library;
  mesh         = pipelineMesh(info->chain.pNext);
  pipelineType = mesh ? GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT
                      : GPU_STRUCTURE_TYPE_NONE;
  GPU_PIPELINE_KEY_WRITE(writer, layout);
  GPU_PIPELINE_KEY_WRITE(writer, library);
  GPU_PIPELINE_KEY_WRITE(writer, pipelineType);
  pipelineKeyWriteConstants(writer, info->chain.pNext);

  if (mesh) {
    pipelineKeyWriteString(writer, mesh->taskEntry ? mesh->taskEntry : "");
    pipelineKeyWriteString(writer, mesh->meshEntry);
    GPU_PIPELINE_KEY_WRITE(writer, mesh->payloadSizeBytes);
  } else {
    pipelineKeyWriteString(writer, info->vertexEntry);
  }

  pipelineKeyWriteString(writer, info->fragmentEntry);
  GPU_PIPELINE_KEY_WRITE(writer, info->vertex.bufferLayoutCount);

  for (i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    bufferLayout = &info->vertex.pBufferLayouts[i];
    GPU_PIPELINE_KEY_WRITE(writer, bufferLayout->strideBytes);
    GPU_PIPELINE_KEY_WRITE(writer, bufferLayout->stepMode);
    GPU_PIPELINE_KEY_WRITE(writer, bufferLayout->attributeCount);

    for (j = 0u; j < bufferLayout->attributeCount; j++) {
      attribute = &bufferLayout->pAttributes[j];
      GPU_PIPELINE_KEY_WRITE(writer, attribute->format);
      GPU_PIPELINE_KEY_WRITE(writer, attribute->offset);
      GPU_PIPELINE_KEY_WRITE(writer, attribute->shaderLocation);
    }
  }

  GPU_PIPELINE_KEY_WRITE(writer, info->colorTargetCount);

  for (k = 0u; k < info->colorTargetCount; k++) {
    target = &info->pColorTargets[k];
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
  pipelineKeyWriteDepthStencil(writer, info->pDepthStencilState);
  GPU_PIPELINE_KEY_WRITE(writer, info->primitiveTopology);
  GPU_PIPELINE_KEY_WRITE(writer, info->cullMode);
  GPU_PIPELINE_KEY_WRITE(writer, info->frontFace);
  GPU_PIPELINE_KEY_WRITE(writer, info->multisample.sampleCount);
  GPU_PIPELINE_KEY_WRITE(writer, info->multisample.sampleMask);
  GPU_PIPELINE_KEY_WRITE(writer, info->multisample.alphaToCoverageEnable);
}

static void
pipelineKeyWriteComputeInfo(PipelineKeyWriter                  *writer,
                            const GPUComputePipelineCreateInfo *info) {
  uintptr_t layout;
  uintptr_t library;

  layout  = (uintptr_t)info->layout;
  library = (uintptr_t)info->library;
  GPU_PIPELINE_KEY_WRITE(writer, layout);
  GPU_PIPELINE_KEY_WRITE(writer, library);
  pipelineKeyWriteString(writer, info->entryPoint);
  pipelineKeyWriteConstants(writer, info->chain.pNext);
}

static void
pipelineKeyWriteRayInfo(PipelineKeyWriter                        *writer,
                        const GPURayTracingPipelineCreateInfoEXT *info) {
  const GPURayTracingShaderGroupEXT *group;
  uintptr_t                          layout;
  uintptr_t                          library;
  uint32_t                           i;

  layout  = (uintptr_t)info->layout;
  library = (uintptr_t)info->library;
  GPU_PIPELINE_KEY_WRITE(writer, layout);
  GPU_PIPELINE_KEY_WRITE(writer, library);
  GPU_PIPELINE_KEY_WRITE(writer, info->groupCount);

  for (i = 0u; i < info->groupCount; i++) {
    group = &info->pGroups[i];
    GPU_PIPELINE_KEY_WRITE(writer, group->type);
    GPU_PIPELINE_KEY_WRITE(writer, group->generalStage);
    pipelineKeyWriteOptionalString(writer, group->generalEntry);
    pipelineKeyWriteOptionalString(writer, group->closestHitEntry);
    pipelineKeyWriteOptionalString(writer, group->anyHitEntry);
    pipelineKeyWriteOptionalString(writer, group->intersectionEntry);
  }

  GPU_PIPELINE_KEY_WRITE(writer, info->maxRecursionDepth);
  GPU_PIPELINE_KEY_WRITE(writer, info->maxPayloadSizeBytes);
  GPU_PIPELINE_KEY_WRITE(writer, info->maxHitAttributeSizeBytes);
}

static void
pipelineKeyWriteGraphInfo(PipelineKeyWriter                    *writer,
                          const GPUExecutionGraphCreateInfoEXT *info) {
  uintptr_t layout;
  uintptr_t library;

  layout  = (uintptr_t)info->layout;
  library = (uintptr_t)info->library;
  GPU_PIPELINE_KEY_WRITE(writer, layout);
  GPU_PIPELINE_KEY_WRITE(writer, library);
  pipelineKeyWriteOptionalString(writer, info->graphName);
}

static bool
pipelineKeyPrepare(PipelineCacheKey    *key, size_t size) {
  key->size = size;

  if (size <= sizeof(key->inlineData)) {
    key->data = key->inlineData;
    return true;
  }

  if (!(key->data = malloc(size))) {
    return false;
  }

  key->ownsData = true;

  return true;
}

static bool
buildRenderPipelineKey(const GPURenderPipelineCreateInfo *info,
                       PipelineCacheKey                  *outKey) {
  PipelineKeyWriter    writer;

  outKey->data     = NULL;
  outKey->size     = 0u;
  outKey->hash     = 0u;
  outKey->ownsData = false;

  writer.data     = outKey->inlineData;
  writer.offset   = 0u;
  writer.capacity = sizeof(outKey->inlineData);
  writer.hash     = GPU_PIPELINE_KEY_HASH_SEED;
  writer.valid    = true;
  pipelineKeyWriteRenderInfo(&writer, info);

  if (!writer.valid || writer.offset == 0u) {
    return false;
  }

  if (writer.offset <= writer.capacity) {
    outKey->data = outKey->inlineData;
    outKey->size = writer.offset;
    outKey->hash = writer.hash;
    return true;
  }

  if (!pipelineKeyPrepare(outKey, writer.offset)) {
    return false;
  }

  writer.data     = outKey->data;
  writer.offset   = 0u;
  writer.capacity = outKey->size;
  writer.hash     = GPU_PIPELINE_KEY_HASH_SEED;
  writer.valid    = true;
  pipelineKeyWriteRenderInfo(&writer, info);

  if (!writer.valid || writer.offset != outKey->size) {
    pipelineCacheReleaseKey(outKey);
    return false;
  }

  outKey->hash = writer.hash;

  return true;
}

static bool
buildComputePipelineKey(const GPUComputePipelineCreateInfo *info,
                        PipelineCacheKey                   *outKey) {
  PipelineKeyWriter    writer;

  outKey->data     = NULL;
  outKey->size     = 0u;
  outKey->hash     = 0u;
  outKey->ownsData = false;

  writer.data     = outKey->inlineData;
  writer.offset   = 0u;
  writer.capacity = sizeof(outKey->inlineData);
  writer.hash     = GPU_PIPELINE_KEY_HASH_SEED;
  writer.valid    = true;
  pipelineKeyWriteComputeInfo(&writer, info);

  if (!writer.valid || writer.offset == 0u) {
    return false;
  }

  if (writer.offset <= writer.capacity) {
    outKey->data = outKey->inlineData;
    outKey->size = writer.offset;
    outKey->hash = writer.hash;
    return true;
  }

  if (!pipelineKeyPrepare(outKey, writer.offset)) {
    return false;
  }

  writer.data     = outKey->data;
  writer.offset   = 0u;
  writer.capacity = outKey->size;
  writer.hash     = GPU_PIPELINE_KEY_HASH_SEED;
  writer.valid    = true;
  pipelineKeyWriteComputeInfo(&writer, info);

  if (!writer.valid || writer.offset != outKey->size) {
    pipelineCacheReleaseKey(outKey);
    return false;
  }

  outKey->hash = writer.hash;

  return true;
}

static bool
buildRayPipelineKey(const GPURayTracingPipelineCreateInfoEXT *info,
                    PipelineCacheKey                         *outKey) {
  PipelineKeyWriter    writer;

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
  pipelineKeyWriteRayInfo(&writer, info);

  if (!writer.valid || writer.offset == 0u) {
    return false;
  }

  if (writer.offset <= writer.capacity) {
    outKey->data = outKey->inlineData;
    outKey->size = writer.offset;
    outKey->hash = writer.hash;
    return true;
  }

  if (!pipelineKeyPrepare(outKey, writer.offset)) {
    return false;
  }

  writer.data     = outKey->data;
  writer.offset   = 0u;
  writer.capacity = outKey->size;
  writer.hash     = GPU_PIPELINE_KEY_HASH_SEED;
  writer.valid    = true;
  pipelineKeyWriteRayInfo(&writer, info);

  if (!writer.valid || writer.offset != outKey->size) {
    pipelineCacheReleaseKey(outKey);
    return false;
  }

  outKey->hash = writer.hash;

  return true;
}

static bool
buildGraphPipelineKey(const GPUExecutionGraphCreateInfoEXT *info,
                      PipelineCacheKey                     *outKey) {
  PipelineKeyWriter    writer;

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
  pipelineKeyWriteGraphInfo(&writer, info);

  if (!writer.valid || writer.offset == 0u) {
    return false;
  }

  if (writer.offset <= writer.capacity) {
    outKey->data = outKey->inlineData;
    outKey->size = writer.offset;
    outKey->hash = writer.hash;
    return true;
  }

  if (!pipelineKeyPrepare(outKey, writer.offset)) {
    return false;
  }

  writer.data     = outKey->data;
  writer.offset   = 0u;
  writer.capacity = outKey->size;
  writer.hash     = GPU_PIPELINE_KEY_HASH_SEED;
  writer.valid    = true;
  pipelineKeyWriteGraphInfo(&writer, info);

  if (!writer.valid || writer.offset != outKey->size) {
    pipelineCacheReleaseKey(outKey);
    return false;
  }

  outKey->hash = writer.hash;

  return true;
}

static PipelineCacheEntry*
pipelineCacheFindEntry(GPUPipelineCache          *cache,
                       const PipelineCacheKey    *key,
                       PipelineCacheEntryType     type) {
  PipelineCacheEntry    *entry;
  size_t                 bucket;

  bucket = (size_t)key->hash & (cache->bucketCount - 1u);

  for (entry = cache->buckets[bucket]; entry; entry = entry->hashNext) {
    if (entry->type == type && entry->keyHash == key->hash
        && entry->keySize == key->size
        && memcmp(entry->keyData, key->data, key->size) == 0) {
      return entry;
    }
  }

  return NULL;
}

static void
pipelineCacheRemoveEntry(GPUPipelineCache      *cache,
                         PipelineCacheEntry    *entry) {
  PipelineCacheEntry    **link;
  size_t                  bucket;

  bucket = (size_t)entry->keyHash & (cache->bucketCount - 1u);

  for (link = &cache->buckets[bucket]; *link; link = &(*link)->hashNext) {
    if (*link == entry) {
      *link = entry->hashNext;
      return;
    }
  }
}

static void*
pipelineCacheFind(GPUPipelineCache          *cache,
                  const PipelineCacheKey    *key,
                  PipelineCacheEntryType     type) {
  PipelineCacheEntry    *entry;
  void                  *pipeline;

  pipeline = NULL;
  pipelineCacheLock(cache);

  if ((entry = pipelineCacheFindEntry(cache, key, type))) {
    pipeline = entry->pipeline;
    retainPipeline(type, pipeline);
    cache->stats.pipelineHits++;
    deviceCacheCounterAdd(&cache->device->cacheStats.pipelineHits, 1u);
  }

  pipelineCacheUnlock(cache);

  return pipeline;
}

static void*
pipelineCacheStore(GPUPipelineCache         *cache,
                   PipelineCacheKey         *key,
                   PipelineCacheEntryType    type,
                   void                     *pipeline) {
  PipelineCacheEntry    *entry;
  PipelineCacheEntry    *evicted;
  void                  *result;
  PipelineCacheEntry    *existing;
  size_t                 bucket;

  if (key->size > SIZE_MAX - sizeof(*entry)) {
    recordPipelineCompile(cache->device, cache);
    pipelineCacheReleaseKey(key);
    return pipeline;
  }

  if (!(entry = malloc(sizeof(*entry) + key->size))) {
    recordPipelineCompile(cache->device, cache);
    pipelineCacheReleaseKey(key);
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

  pipelineCacheLock(cache);

  if ((existing = pipelineCacheFindEntry(cache, key, type))) {
    result = existing->pipeline;
    retainPipeline(type, result);
    cache->stats.pipelineHits++;
    cache->stats.pipelineCompiles++;
    deviceCacheCounterAdd(&cache->device->cacheStats.pipelineHits, 1u);
    deviceCacheCounterAdd(&cache->device->cacheStats.pipelineCompiles, 1u);
    pipelineCacheUnlock(cache);
    free(entry);
    gpu_destroyPipeline(type, pipeline);
    pipelineCacheReleaseKey(key);
    return result;
  }

  if (cache->entryCount == cache->maxEntries) {
    evicted     = cache->head;
    cache->head = evicted->next;
    pipelineCacheRemoveEntry(cache, evicted);

    if (!cache->head) {
      cache->tail = NULL;
    }

    cache->entryCount--;
  }

  retainPipeline(type, pipeline);

  if (cache->tail) {
    cache->tail->next = entry;
  } else {
    cache->head = entry;
  }

  bucket                 = (size_t)entry->keyHash & (cache->bucketCount - 1u);
  entry->hashNext        = cache->buckets[bucket];
  cache->buckets[bucket] = entry;
  cache->tail            = entry;
  cache->entryCount++;
  cache->stats.pipelineMisses++;
  cache->stats.pipelineCompiles++;
  deviceCacheCounterAdd(&cache->device->cacheStats.pipelineMisses, 1u);
  deviceCacheCounterAdd(&cache->device->cacheStats.pipelineCompiles, 1u);
  pipelineCacheUnlock(cache);

  pipelineCacheReleaseKey(key);

  if (evicted) {
    gpu_destroyPipeline(evicted->type, evicted->pipeline);
    free(evicted);
  }

  return result;
}

static void
deviceCacheLock(GPUDevice *device) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(device->_pipelineCacheLock);
#else
  pthread_mutex_lock(device->_pipelineCacheLock);
#endif
}

static void
deviceCacheUnlock(GPUDevice *device) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(device->_pipelineCacheLock);
#else
  pthread_mutex_unlock(device->_pipelineCacheLock);
#endif
}

static char*
pipelineCacheDupString(const char *value) {
  char  *copy;
  size_t size;

  if (!value) {
    return NULL;
  }

  size = strlen(value) + 1u;

  if ((copy = malloc(size))) {
    memcpy(copy, value, size);
  }

  return copy;
}

static void
destroyPipelineJob(PipelineCompileJob    *job) {
  if (!job) {
    return;
  }

  GPUDestroyRenderPipeline(job->pipeline);
  free(job->values);
  free(job->attributes);
  free(job->bufferLayouts);
  free(job->colorTargets);
  free(job->meshEntry);
  free(job->taskEntry);
  free(job->fragmentEntry);
  free(job->vertexEntry);
  free(job->label);
  free(job);
}

static PipelineCompileJob*
createPipelineJob(GPUPipelineCache                  *cache,
                  const GPURenderPipelineCreateInfo *info) {
  PipelineCompileJob        *job;
  const GPUPipelineConstants *constants;
  const GPUMeshPipelineEXT   *mesh;
  uint32_t                    attributeCount;
  uint32_t                    cursor;
  uint32_t                    i;
  uint32_t                    k;

  if (!(job = calloc(1, sizeof(*job)))) {
    return NULL;
  }

  job->label         = pipelineCacheDupString(info->label);
  job->vertexEntry   = pipelineCacheDupString(info->vertexEntry);
  job->fragmentEntry = pipelineCacheDupString(info->fragmentEntry);

  if ((info->label && !job->label)
      || (info->vertexEntry && !job->vertexEntry) || !job->fragmentEntry) {
    destroyPipelineJob(job);
    return NULL;
  }

  mesh      = pipelineMesh(info->chain.pNext);
  constants = pipelineConstants(info->chain.pNext);

  if (constants && constants->constantCount > 0u) {
    if (!(job->values = malloc((size_t)constants->constantCount * sizeof(*job->values)))) {
      destroyPipelineJob(job);
      return NULL;
    }

    memcpy(job->values, constants->pConstants,
           (size_t)constants->constantCount * sizeof(*job->values));
    job->constants            = *constants;
    job->constants.pConstants = job->values;
  }

  if (mesh) {
    job->taskEntry = pipelineCacheDupString(mesh->taskEntry);
    job->meshEntry = pipelineCacheDupString(mesh->meshEntry);

    if ((mesh->taskEntry && !job->taskEntry) || !job->meshEntry) {
      destroyPipelineJob(job);
      return NULL;
    }

    job->mesh             = *mesh;
    job->mesh.chain.pNext = NULL;
    job->mesh.taskEntry   = job->taskEntry;
    job->mesh.meshEntry   = job->meshEntry;
  }

  if (info->colorTargetCount > 0u) {
    if (!(job->colorTargets = malloc((size_t)info->colorTargetCount * sizeof(*job->colorTargets)))) {
      destroyPipelineJob(job);
      return NULL;
    }

    memcpy(job->colorTargets,
           info->pColorTargets,
           (size_t)info->colorTargetCount * sizeof(*job->colorTargets));
  }

  attributeCount = 0u;

  for (i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    if (info->vertex.pBufferLayouts[i].attributeCount > UINT32_MAX - attributeCount) {
      destroyPipelineJob(job);
      return NULL;
    }

    attributeCount += info->vertex.pBufferLayouts[i].attributeCount;
  }

  if (info->vertex.bufferLayoutCount > 0u) {
    if (!(job->bufferLayouts = calloc(info->vertex.bufferLayoutCount,
                                      sizeof(*job->bufferLayouts)))) {
      destroyPipelineJob(job);
      return NULL;
    }
  }

  if (attributeCount > 0u) {
    if (!(job->attributes = malloc((size_t)attributeCount * sizeof(*job->attributes)))) {
      destroyPipelineJob(job);
      return NULL;
    }
  }

  cursor = 0u;

  for (k = 0u; k < info->vertex.bufferLayoutCount; k++) {
    job->bufferLayouts[k]             = info->vertex.pBufferLayouts[k];
    job->bufferLayouts[k].pAttributes = job->bufferLayouts[k].attributeCount > 0u ? &job->attributes[cursor] : NULL;

    if (job->bufferLayouts[k].attributeCount > 0u) {
      memcpy(&job->attributes[cursor],
             info->vertex.pBufferLayouts[k].pAttributes,
             (size_t)job->bufferLayouts[k].attributeCount * sizeof(*job->attributes));
      cursor += job->bufferLayouts[k].attributeCount;
    }
  }

  job->info                       = *info;
  job->info.label                 = job->label;
  job->info.cache                 = cache;
  job->info.vertexEntry           = job->vertexEntry;
  job->info.fragmentEntry         = job->fragmentEntry;
  job->info.pColorTargets         = job->colorTargets;
  job->info.vertex.pBufferLayouts = job->bufferLayouts;
  job->info.chain.pNext           = mesh ? &job->mesh.chain : NULL;

  if (job->values) {
    job->constants.chain.pNext = job->info.chain.pNext;
    job->info.chain.pNext      = &job->constants.chain;
  }

  if (info->pDepthStencilState) {
    job->depthStencil            = *info->pDepthStencilState;
    job->info.pDepthStencilState = &job->depthStencil;
  }

  job->state = GPU_PIPELINE_JOB_QUEUED;

  return job;
}

static bool
pipelineInfoCanCopy(const GPURenderPipelineCreateInfo *info) {
  const GPUChainedStruct   *extension;
  const GPUMeshPipelineEXT *mesh;
  uint32_t                  i;

  if (!info || !info->layout || !info->library || !info->fragmentEntry
      || (info->colorTargetCount > 0u && !info->pColorTargets)
      || (info->vertex.bufferLayoutCount > 0u
          && !info->vertex.pBufferLayouts)) {
    return false;
  }

  mesh = pipelineMesh(info->chain.pNext);

  for (extension = info->chain.pNext; extension; extension = extension->pNext) {
    if (extension->sType != GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT
        && extension->sType != GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS) {
      return false;
    }
  }

  if (mesh ? !mesh->meshEntry : !info->vertexEntry) {
    return false;
  }

  for (i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    if (info->vertex.pBufferLayouts[i].attributeCount > 0u
        && !info->vertex.pBufferLayouts[i].pAttributes) {
      return false;
    }
  }

  return true;
}

static void
pipelineCacheWorkerRun(GPUPipelineCache *cache) {
  PipelineCacheSync     *sync;
  PipelineCompileJob    *job;
  GPURenderPipeline     *pipeline;
  GPUResult              result;

  sync = pipelineCacheSync(cache);

  for (;;) {
    pipelineCacheLock(cache);

    while (!cache->stopWorker && !cache->queueHead) {
#if defined(_WIN32) || defined(WIN32)
      SleepConditionVariableCS(&sync->condition, &sync->lock, INFINITE);
#else
      pthread_cond_wait(&sync->condition, &sync->lock);
#endif
    }

    if (cache->stopWorker) {
      pipelineCacheUnlock(cache);
      return;
    }

    job              = cache->queueHead;

    if (!(cache->queueHead = job->queueNext)) {
      cache->queueTail = NULL;
    }

    job->queueNext = NULL;
    job->state     = GPU_PIPELINE_JOB_COMPILING;
    pipelineCacheUnlock(cache);

    pipeline = NULL;
    result   = createRenderPipeline(cache->device, &job->info, &pipeline);

    pipelineCacheLock(cache);
    job->pipeline = pipeline;
    job->state    = result == GPU_OK ? GPU_PIPELINE_JOB_READY : GPU_PIPELINE_JOB_FAILED;
    pipelineCacheSignal(cache);
    pipelineCacheUnlock(cache);
  }
}

#if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI
pipelineCacheWorker(void *context) {
  pipelineCacheWorkerRun(context);

  return 0u;
}
#else
static void*
pipelineCacheWorker(void *context) {
  pipelineCacheWorkerRun(context);

  return NULL;
}
#endif

static bool
pipelineCacheStartWorker(GPUPipelineCache *cache) {
  PipelineCacheSync    *sync;

  sync = pipelineCacheSync(cache);

  if (sync->workerStarted) {
    return true;
  }
#if defined(_WIN32) || defined(WIN32)
  sync->worker        = CreateThread(NULL,
                                     0u,
                                     pipelineCacheWorker,
                                     cache,
                                     0u,
                                     NULL);
  sync->workerStarted = sync->worker != NULL;
#else
  sync->workerStarted = pthread_create(&sync->worker,
                                       NULL,
                                       pipelineCacheWorker,
                                       cache) == 0;
#endif
  return sync->workerStarted;
}

static size_t
pipelineCacheBucketCount(uint64_t maxEntries) {
  size_t count;
  size_t target;

  target = maxEntries > GPU_PIPELINE_CACHE_MAX_BUCKETS ? GPU_PIPELINE_CACHE_MAX_BUCKETS : (size_t)maxEntries;
  count  = 1u;

  while (count < target) {
    count <<= 1u;
  }

  return count;
}

GPU_HIDE
void
retainRenderPipeline(GPURenderPipeline *pipeline) {
#if defined(_WIN32) || defined(WIN32)
  InterlockedIncrement((volatile LONG *)&pipeline->_refCount);
#else
  __atomic_add_fetch(&pipeline->_refCount, 1u, __ATOMIC_RELAXED);
#endif
}

GPU_HIDE
bool
releaseRenderPipeline(GPURenderPipeline *pipeline) {
#if defined(_WIN32) || defined(WIN32)
  return InterlockedDecrement((volatile LONG *)&pipeline->_refCount) == 0;
#else
  return __atomic_sub_fetch(&pipeline->_refCount, 1u, __ATOMIC_ACQ_REL) == 0u;
#endif
}

GPU_HIDE
void
retainComputePipeline(GPUComputePipeline *pipeline) {
#if defined(_WIN32) || defined(WIN32)
  InterlockedIncrement((volatile LONG *)&pipeline->_refCount);
#else
  __atomic_add_fetch(&pipeline->_refCount, 1u, __ATOMIC_RELAXED);
#endif
}

GPU_HIDE
bool
releaseComputePipeline(GPUComputePipeline *pipeline) {
#if defined(_WIN32) || defined(WIN32)
  return InterlockedDecrement((volatile LONG *)&pipeline->_refCount) == 0;
#else
  return __atomic_sub_fetch(&pipeline->_refCount, 1u, __ATOMIC_ACQ_REL) == 0u;
#endif
}

GPU_HIDE
void
pipelineCacheReleaseKey(PipelineCacheKey    *key) {
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

GPU_HIDE
GPUResult
pipelineCacheFindRender(GPUPipelineCache                  *cache,
                        const GPURenderPipelineCreateInfo *info,
                        PipelineCacheKey                  *outKey,
                        GPURenderPipeline                **outPipeline) {
  *outPipeline = NULL;

  if (!buildRenderPipelineKey(info, outKey)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  *outPipeline = pipelineCacheFind(cache,
                                   outKey,
                                   GPU_PIPELINE_CACHE_RENDER);

  return GPU_OK;
}

GPU_HIDE
GPURenderPipeline*
pipelineCacheStoreRender(GPUPipelineCache    *cache,
                         PipelineCacheKey    *key,
                         GPURenderPipeline   *pipeline) {
  return pipelineCacheStore(cache,
                            key,
                            GPU_PIPELINE_CACHE_RENDER,
                            pipeline);
}

GPU_HIDE
GPUResult
pipelineCacheFindCompute(GPUPipelineCache                   *cache,
                         const GPUComputePipelineCreateInfo *info,
                         PipelineCacheKey                   *outKey,
                         GPUComputePipeline                **outPipeline) {
  *outPipeline = NULL;

  if (!buildComputePipelineKey(info, outKey)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  *outPipeline = pipelineCacheFind(cache,
                                   outKey,
                                   GPU_PIPELINE_CACHE_COMPUTE);

  return GPU_OK;
}

GPU_HIDE
GPUComputePipeline*
pipelineCacheStoreCompute(GPUPipelineCache    *cache,
                          PipelineCacheKey    *key,
                          GPUComputePipeline  *pipeline) {
  return pipelineCacheStore(cache,
                            key,
                            GPU_PIPELINE_CACHE_COMPUTE,
                            pipeline);
}

GPU_HIDE
GPUResult
pipelineCacheFindRay(GPUPipelineCache                         *cache,
                     const GPURayTracingPipelineCreateInfoEXT *info,
                     PipelineCacheKey                         *outKey,
                     GPURayTracingPipelineEXT                **outPipeline) {
  *outPipeline = NULL;

  if (!buildRayPipelineKey(info, outKey)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  *outPipeline = pipelineCacheFind(cache,
                                   outKey,
                                   GPU_PIPELINE_CACHE_RAY);

  return GPU_OK;
}

GPU_HIDE
GPURayTracingPipelineEXT*
pipelineCacheStoreRay(GPUPipelineCache         *cache,
                      PipelineCacheKey         *key,
                      GPURayTracingPipelineEXT *pipeline) {
  return pipelineCacheStore(cache,
                            key,
                            GPU_PIPELINE_CACHE_RAY,
                            pipeline);
}

GPU_HIDE
GPUResult
pipelineCacheFindGraph(GPUPipelineCache                     *cache,
                       const GPUExecutionGraphCreateInfoEXT *info,
                       PipelineCacheKey                     *outKey,
                       GPUExecutionGraphEXT                **outGraph) {
  *outGraph = NULL;

  if (!buildGraphPipelineKey(info, outKey)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  *outGraph = pipelineCacheFind(cache,
                                outKey,
                                GPU_PIPELINE_CACHE_GRAPH);

  return GPU_OK;
}

GPU_HIDE
GPUExecutionGraphEXT*
pipelineCacheStoreGraph(GPUPipelineCache     *cache,
                        PipelineCacheKey     *key,
                        GPUExecutionGraphEXT *graph) {
  return pipelineCacheStore(cache,
                            key,
                            GPU_PIPELINE_CACHE_GRAPH,
                            graph);
}

GPU_HIDE
GPUResult
initPipelineCacheDevice(GPUDevice *device) {
  void *lock;

  if (!device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
#if defined(_WIN32) || defined(WIN32)
  if (!(lock = calloc(1, sizeof(CRITICAL_SECTION)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  InitializeCriticalSection(lock);
#else
  if (!(lock = calloc(1, sizeof(pthread_mutex_t)))) {
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
destroyPipelineCacheDevice(GPUDevice *device) {
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

GPU_HIDE
void
recordPipelineCompile(GPUDevice *device, GPUPipelineCache *cache) {
  if (!cache) {
    if (device) {
      deviceCacheCounterAdd(&device->cacheStats.pipelineCompiles, 1u);
    }
    return;
  }

  pipelineCacheLock(cache);
  cache->stats.pipelineMisses++;
  cache->stats.pipelineCompiles++;

  if (cache->device) {
    deviceCacheCounterAdd(&cache->device->cacheStats.pipelineMisses, 1u);
    deviceCacheCounterAdd(&cache->device->cacheStats.pipelineCompiles, 1u);
  }

  pipelineCacheUnlock(cache);
}

static GPUResult
compileRenderPipelineAsync(GPUDevice                         *__restrict device,
                              GPUPipelineCache                  *__restrict cache,
                              const GPURenderPipelineCreateInfo *__restrict info,
                              GPUPipelineCompileHandle          *__restrict outHandle) {
  PipelineCompileJob    *job;

  if (!outHandle) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  outHandle->id = 0;

  if (!device || !cache || cache->device != device
      || !pipelineInfoCanCopy(info) || info->layout->_device != device
      || info->library->_device != device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(job = createPipelineJob(cache, info))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  deviceCacheLock(device);
  job->id = device->_nextPipelineCompileId++;

  if (job->id == 0u) {
    job->id = device->_nextPipelineCompileId++;
  }

  pipelineCacheLock(cache);

  if (cache->jobCount == cache->maxEntries) {
    pipelineCacheUnlock(cache);
    deviceCacheUnlock(device);
    destroyPipelineJob(job);
    return GPU_ERROR_INSUFFICIENT_CAPACITY;
  }

  if (!pipelineCacheStartWorker(cache)) {
    pipelineCacheUnlock(cache);
    deviceCacheUnlock(device);
    destroyPipelineJob(job);
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
  pipelineCacheSignal(cache);
  pipelineCacheUnlock(cache);
  deviceCacheUnlock(device);

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreatePipelineCache(GPUDevice                        *__restrict device,
                       const GPUPipelineCacheCreateInfo *__restrict info,
                       GPUPipelineCache                **__restrict outCache) {
  GPUPipelineCache     *cache;
  PipelineCacheSync    *sync;
  Api                  *api;
  uint64_t              maxEntries;
  size_t                bucketCount;
  GPUResult             result;

  if (!outCache) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outCache = NULL;

  if (!device || !info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE
      && info->chain.sType != GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info->enableDiskCache && (!info->cachePath || !info->cachePath[0])) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  api = deviceApi(device);

  if (info->enableDiskCache
      && (!api || !api->pipelineCache.create || !api->pipelineCache.destroy)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  maxEntries  = info->maxEntries > 0u ? info->maxEntries : GPU_PIPELINE_CACHE_DEFAULT_ENTRIES;
  bucketCount = pipelineCacheBucketCount(maxEntries);

  if (!(cache = calloc(1, sizeof(*cache)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  if (!(cache->buckets = calloc(bucketCount, sizeof(*cache->buckets)))) {
    free(cache);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  if (!(sync = calloc(1, sizeof(*sync)))) {
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
  deviceCacheLock(device);
  cache->deviceNext       = device->_pipelineCaches;
  device->_pipelineCaches = cache;
  deviceCacheUnlock(device);

  *outCache = cache;

  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyPipelineCache(GPUPipelineCache *cache) {
  PipelineCacheEntry    *entry;
  PipelineCompileJob    *job;
  PipelineCacheSync     *sync;
  GPUPipelineCache     **link;
  Api                   *api;
  PipelineCacheEntry    *nextEntry;
  PipelineCompileJob    *nextJob;

  if (!cache) {
    return;
  }

  sync = pipelineCacheSync(cache);
  deviceCacheLock(cache->device);

  for (link = &cache->device->_pipelineCaches; *link; link = &(*link)->deviceNext) {
    if (*link == cache) {
      *link = cache->deviceNext;
      break;
    }
  }

  deviceCacheUnlock(cache->device);

  pipelineCacheLock(cache);
  cache->stopWorker = true;
  pipelineCacheSignal(cache);
  pipelineCacheUnlock(cache);

  if (sync->workerStarted) {
#if defined(_WIN32) || defined(WIN32)
    WaitForSingleObject(sync->worker, INFINITE);
    CloseHandle(sync->worker);
#else
    pthread_join(sync->worker, NULL);
#endif
  }

  if (cache->_priv) {
    api = deviceApi(cache->device);

    if (api && api->pipelineCache.destroy) {
      api->pipelineCache.destroy(cache);
    }
  }

  pipelineCacheLock(cache);
  entry             = cache->head;
  job               = cache->jobs;
  cache->head       = NULL;
  cache->tail       = NULL;
  cache->jobs       = NULL;
  cache->queueHead  = NULL;
  cache->queueTail  = NULL;
  cache->entryCount = 0u;
  cache->jobCount   = 0u;
  pipelineCacheUnlock(cache);

#if defined(_WIN32) || defined(WIN32)
  DeleteCriticalSection(&sync->lock);
#else
  pthread_cond_destroy(&sync->condition);
  pthread_mutex_destroy(&sync->lock);
#endif
  free(sync);

  while (entry) {
    nextEntry = entry->next;
    gpu_destroyPipeline(entry->type, entry->pipeline);
    free(entry);
    entry = nextEntry;
  }

  while (job) {
    nextJob = job->allNext;
    destroyPipelineJob(job);
    job = nextJob;
  }

  free(cache->buckets);
  free(cache);
}

GPU_EXPORT
GPUResult
GPUPrewarmRenderPipelines(GPUDevice                         *__restrict device,
                          GPUPipelineCache                  *__restrict cache,
                          uint32_t                                      count,
                          const GPURenderPipelineCreateInfo *__restrict infos) {
  GPURenderPipelineCreateInfo info;
  GPURenderPipeline          *pipeline;
  uint32_t                    i;
  GPUResult                   result;

  if (!device || !cache || cache->device != device || (count > 0u && !infos)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  for (i = 0u; i < count; i++) {
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
GPUPollRenderPipelineCompile(GPUDevice                *__restrict device,
                             GPUPipelineCompileHandle             handle,
                             GPUPipelineCompileStatus *__restrict outStatus,
                             GPURenderPipeline       **__restrict outPipeline) {
  GPUPipelineCache       *cache;
  PipelineCompileJob    **link;
  PipelineCompileJob     *job;

  if (!outStatus || !outPipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outStatus   = GPU_PIPELINE_COMPILE_FAILED;
  *outPipeline = NULL;

  if (!device || !device->_pipelineCacheLock || handle.id == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  deviceCacheLock(device);

  for (cache = device->_pipelineCaches; cache; cache = cache->deviceNext) {
    pipelineCacheLock(cache);

    for (link = &cache->jobs; *link; link = &(*link)->allNext) {
      if ((*link)->id == handle.id) {
        break;
      }
    }

    if (!(job = *link)) {
      pipelineCacheUnlock(cache);
      continue;
    }

    if (job->state == GPU_PIPELINE_JOB_QUEUED
        || job->state == GPU_PIPELINE_JOB_COMPILING) {
      *outStatus = GPU_PIPELINE_COMPILE_PENDING;
      pipelineCacheUnlock(cache);
      deviceCacheUnlock(device);
      return GPU_OK;
    }

    *link = job->allNext;
    cache->jobCount--;

    if (job->state == GPU_PIPELINE_JOB_READY) {
      *outStatus    = GPU_PIPELINE_COMPILE_READY;
      *outPipeline  = job->pipeline;
      job->pipeline = NULL;
    }

    pipelineCacheUnlock(cache);
    deviceCacheUnlock(device);
    destroyPipelineJob(job);
    return GPU_OK;
  }

  deviceCacheUnlock(device);

  return GPU_ERROR_INVALID_ARGUMENT;
}

GPU_EXPORT
GPUResult
GPUCompileRenderPipelineAsync(GPUDevice                         *device,
                              GPUPipelineCache                  *cache,
                              const GPURenderPipelineCreateInfo *info,
                              GPUPipelineCompileHandle          *outHandle) {
  GPURenderPipelineCreateInfo snapshot;
  PreparedConstants           prepared;
  GPUResult                   result;

  if (!outHandle) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  outHandle->id = 0u;

  if (!info || !info->library) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = prepareConstants(info->library, info->chain.pNext, false, &prepared);

  if (result != GPU_OK) {
    return result;
  }

  snapshot             = *info;
  snapshot.chain.pNext = prepared.chain;
  result               = compileRenderPipelineAsync(device, cache, &snapshot, outHandle);
  free(prepared.values);

  return result;
}
