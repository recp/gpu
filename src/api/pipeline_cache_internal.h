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

typedef struct GPUPipelineCacheEntry GPUPipelineCacheEntry;

struct GPUPipelineCache {
  GPUDevice             *device;
  void                  *_lock;
  GPUPipelineCacheEntry *head;
  GPUPipelineCacheEntry *tail;
  GPUCacheStats          stats;
  uint64_t               maxEntries;
  uint64_t               entryCount;
};

GPU_HIDE
void
gpuRecordPipelineCompile(GPUDevice *device, GPUPipelineCache *cache);

GPU_HIDE
GPUResult
gpuPipelineCacheFindRender(GPUPipelineCache                  *cache,
                           const GPURenderPipelineCreateInfo *info,
                           GPURenderPipeline                **outPipeline);

GPU_HIDE
GPURenderPipeline *
gpuPipelineCacheStoreRender(GPUPipelineCache                  *cache,
                            const GPURenderPipelineCreateInfo *info,
                            GPURenderPipeline                 *pipeline);

GPU_HIDE
GPUResult
gpuPipelineCacheFindCompute(GPUPipelineCache                   *cache,
                            const GPUComputePipelineCreateInfo *info,
                            GPUComputePipeline                **outPipeline);

GPU_HIDE
GPUComputePipeline *
gpuPipelineCacheStoreCompute(GPUPipelineCache                   *cache,
                             const GPUComputePipelineCreateInfo *info,
                             GPUComputePipeline                 *pipeline);

GPU_HIDE
bool
gpuReleaseRenderPipeline(GPURenderPipeline *pipeline);

GPU_HIDE
bool
gpuReleaseComputePipeline(GPUComputePipeline *pipeline);

#endif /* gpu_pipeline_cache_internal_h */
