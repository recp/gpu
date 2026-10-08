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

#ifndef gpu_pipeline_cache_internal_h
#define gpu_pipeline_cache_internal_h

#include "device_internal.h"

#define GPU_PIPELINE_CACHE_INLINE_KEY_SIZE 256u

typedef struct PipelineCacheEntry    PipelineCacheEntry;
typedef struct PipelineCompileJob    PipelineCompileJob;

typedef struct PipelineCacheKey {
  uint8_t *data;
  size_t   size;
  uint64_t hash;
  bool     ownsData;
  uint8_t  inlineData[GPU_PIPELINE_CACHE_INLINE_KEY_SIZE];
} PipelineCacheKey;

struct GPUPipelineCache {
  GPUDevice              *device;
  void                   *_sync;
  void                   *_priv;
  GPUPipelineCache       *deviceNext;
  PipelineCacheEntry     *head;
  PipelineCacheEntry     *tail;
  PipelineCacheEntry    **buckets;
  PipelineCompileJob     *jobs;
  PipelineCompileJob     *queueHead;
  PipelineCompileJob     *queueTail;
  GPUCacheStats           stats;
  uint64_t                maxEntries;
  uint64_t                entryCount;
  uint64_t                jobCount;
  size_t                  bucketCount;
  bool                    stopWorker;
};

GPU_HIDE
GPUResult
initPipelineCacheDevice(GPUDevice *device);

GPU_HIDE
void
destroyPipelineCacheDevice(GPUDevice *device);

GPU_HIDE
void
recordPipelineCompile(GPUDevice *device, GPUPipelineCache *cache);

GPU_HIDE
void
pipelineCacheReleaseKey(PipelineCacheKey    *key);

GPU_HIDE
GPUResult
pipelineCacheFindRender(GPUPipelineCache                  *cache,
                        const GPURenderPipelineCreateInfo *info,
                        PipelineCacheKey                  *outKey,
                        GPURenderPipeline                **outPipeline);

GPU_HIDE
GPURenderPipeline*
pipelineCacheStoreRender(GPUPipelineCache    *cache,
                         PipelineCacheKey    *key,
                         GPURenderPipeline   *pipeline);

GPU_HIDE
GPUResult
pipelineCacheFindCompute(GPUPipelineCache                   *cache,
                         const GPUComputePipelineCreateInfo *info,
                         PipelineCacheKey                   *outKey,
                         GPUComputePipeline                **outPipeline);

GPU_HIDE
GPUComputePipeline*
pipelineCacheStoreCompute(GPUPipelineCache    *cache,
                          PipelineCacheKey    *key,
                          GPUComputePipeline  *pipeline);

GPU_HIDE
GPUResult
pipelineCacheFindRay(GPUPipelineCache                         *cache,
                     const GPURayTracingPipelineCreateInfoEXT *info,
                     PipelineCacheKey                         *outKey,
                     GPURayTracingPipelineEXT                **outPipeline);

GPU_HIDE
GPURayTracingPipelineEXT*
pipelineCacheStoreRay(GPUPipelineCache         *cache,
                      PipelineCacheKey         *key,
                      GPURayTracingPipelineEXT *pipeline);

GPU_HIDE
GPUResult
pipelineCacheFindGraph(GPUPipelineCache                     *cache,
                       const GPUExecutionGraphCreateInfoEXT *info,
                       PipelineCacheKey                     *outKey,
                       GPUExecutionGraphEXT                **outGraph);

GPU_HIDE
GPUExecutionGraphEXT*
pipelineCacheStoreGraph(GPUPipelineCache     *cache,
                        PipelineCacheKey     *key,
                        GPUExecutionGraphEXT *graph);

GPU_HIDE
bool
releaseRenderPipeline(GPURenderPipeline *pipeline);

GPU_HIDE
void
retainRenderPipeline(GPURenderPipeline *pipeline);

GPU_HIDE
bool
releaseComputePipeline(GPUComputePipeline *pipeline);

GPU_HIDE
void
retainComputePipeline(GPUComputePipeline *pipeline);

#endif /* gpu_pipeline_cache_internal_h */
