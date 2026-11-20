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

#include "constants_internal.h"
#include "../common.h"
#include "compile_pool_internal.h"
#include "buffer_internal.h"
#include "cmdqueue_internal.h"
#include "compute_internal.h"
#include "descr/descriptor_internal.h"
#include "device_internal.h"
#include "image_buffer_internal.h"
#include "library_internal.h"
#include "pipeline_cache_internal.h"
#include "query_internal.h"
#include "ray_internal.h"

#include <stddef.h>

#if !GPU_BUILD_WITH_VALIDATION
#  define computeValidationError(pass, message) ((void)0)
#  define computeBindingsComplete(pass) true
#endif

typedef struct BindComputeContext {
  GPUComputePassEncoder *pass;
} BindComputeContext;

static GPUDevice*
computePassDevice(const GPUComputePassEncoder *pass) {
  if (!pass) {
    return NULL;
  }

  if (pass->_device) {
    return pass->_device;
  }

  if (pass->_cmdb && pass->_cmdb->_queue) {
    return pass->_cmdb->_queue->_device;
  }

  return pass->_pipelineLayout ? pass->_pipelineLayout->_device : NULL;
}

static GPUApi*
computePassApi(const GPUComputePassEncoder *pass) {
  if (pass && pass->_api) {
    return pass->_api;
  }

  return deviceApi(computePassDevice(pass));
}

#if GPU_BUILD_WITH_VALIDATION
static void
computeValidationError(const GPUComputePassEncoder *pass,
                       const char                  *message) {
  deviceRecordValidationError(computePassDevice(pass), message);
}

static inline bool
computeBindingsComplete(const GPUComputePassEncoder *pass) {
  if (!deviceValidationEnabled(computePassDevice(pass))) {
    return true;
  }

  return pipelineLayoutMaskIsBound(pass->_pipelineLayout,
                                   pass->_boundGroupLayouts,
                                   GPU_ENCODER_MAX_BIND_GROUPS,
                                   pass->_requiredBindGroupMask);
}
#endif

static bool
validPushConstantRange(uint32_t    limit,
                       uint32_t    offset,
                       uint32_t    sizeBytes,
                       const void *data) {
  if (sizeBytes == 0u) {
    return true;
  }

  if (!data || offset > limit || ((offset | sizeBytes) & 3u) != 0u) {
    return false;
  }

  return sizeBytes <= limit - offset;
}

static bool
validIndirectBatch(GPUBuffer *argsBuffer,
                   uint64_t   argsOffset,
                   uint32_t   commandCount,
                   uint32_t   strideBytes,
                   uint32_t   commandSize) {
  uint64_t maxCommandIndex;
  uint64_t lastCommandOffset;

  if (!bufferHasUsage(argsBuffer, GPU_BUFFER_USAGE_INDIRECT)
      || (argsOffset & 3u) != 0u
      || commandCount == 0u
      || strideBytes < commandSize
      || (strideBytes & 3u) != 0u
      || commandSize > UINT64_MAX - argsOffset) {
    return false;
  }

  maxCommandIndex = (uint64_t)commandCount - 1u;

  if (maxCommandIndex > (UINT64_MAX - argsOffset - commandSize) / strideBytes) {
    return false;
  }

  lastCommandOffset = argsOffset + maxCommandIndex * strideBytes;

  return bufferRangeValid(argsBuffer, lastCommandOffset, commandSize);
}

static GPUComputePipelineState*
compileComputePipelineState(GPUDevice *device, GPUComputePipeline *pipeline) {
  GPUApi *api;

  if (!(api = deviceApi(device)) || !api->compute.newComputeState) {
    return NULL;
  }

  return api->compute.newComputeState(device, pipeline);
}

static GPUResult
computePipelineExtensions(GPUDevice                                 *device,
                          const GPUComputePipelineCreateInfo        *info,
                          const GPUIntersectionFunctionPipelineEXT **outIntersection) {
  const GPUChainedStruct                    *chain;
  const GPUIntersectionFunctionPipelineEXT *intersection;

  if (!device || !info || !outIntersection) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  intersection = NULL;

  for (chain = info->chain.pNext; chain; chain = chain->pNext) {
    if (chain->sType == GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS) {
      continue;
    }

    if (chain->sType != GPU_STRUCTURE_TYPE_INTERSECTION_FUNCTION_PIPELINE_EXT
        || intersection
        || (chain->structSize != 0u
            && chain->structSize < sizeof(GPUIntersectionFunctionPipelineEXT))) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    intersection = (const GPUIntersectionFunctionPipelineEXT *)chain;
  }

  if (intersection
      && !GPUIsFeatureEnabled(device,
                              GPU_FEATURE_INTERSECTION_FUNCTION_TABLE)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outIntersection = intersection;

  return GPU_OK;
}

static void
bindComputeBinding(void *ctx, const GPUBindGroupBindingView *binding) {
  BindComputeContext    *bindCtx;

  if (!ctx || !binding
      || (binding->visibility & GPU_SHADER_STAGE_COMPUTE_BIT) == 0u) {
    return;
  }

  bindCtx = ctx;

  if (binding->kind == GPUBindKindBuffer && binding->buffer) {
    setComputeBuffer(bindCtx->pass,
                     binding->buffer,
                     binding->offset,
                     binding->binding + binding->arrayIndex);
  } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
    setComputeTexture(bindCtx->pass,
                      binding->textureView,
                      binding->binding + binding->arrayIndex);
  } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
    setComputeSampler(bindCtx->pass,
                      binding->sampler,
                      binding->binding + binding->arrayIndex);
  } else if (binding->kind == GPUBindKindAccelerationStructure
             && binding->accelerationStructure) {
    setComputeAccelerationStructure(bindCtx->pass,
                                    binding->accelerationStructure,
                                    binding->binding + binding->arrayIndex);
  }
}

GPU_HIDE
void
setComputeBuffer(GPUComputePassEncoder *pass,
                 GPUBuffer             *buf,
                 uint64_t               off,
                 uint32_t               index) {
  GPUApi *api;

  if (!pass || pass->_ended || !buf) {
    return;
  }

  if (!(api = computePassApi(pass)) || !api->compute.buffer) {
    return;
  }

  api->compute.buffer(pass, buf, off, index);
}

GPU_HIDE
void
setComputeTexture(GPUComputePassEncoder *pass,
                  GPUTextureView        *view,
                  uint32_t               index) {
  GPUApi *api;

  if (!pass || pass->_ended || !view) {
    return;
  }

  if (!(api = computePassApi(pass)) || !api->compute.texture) {
    return;
  }

  api->compute.texture(pass, view, index);
}

GPU_HIDE
void
setComputeSampler(GPUComputePassEncoder *pass,
                  GPUSampler            *sampler,
                  uint32_t               index) {
  GPUApi *api;

  if (!pass || pass->_ended || !sampler) {
    return;
  }

  if (!(api = computePassApi(pass)) || !api->compute.sampler) {
    return;
  }

  api->compute.sampler(pass, sampler, index);
}

GPU_HIDE
void
setComputeAccelerationStructure(GPUComputePassEncoder       *pass,
                                GPUAccelerationStructureEXT *structure,
                                uint32_t                     index) {
  GPUApi *api;

  if (!pass || pass->_ended || !structure) {
    return;
  }

  if (!(api = computePassApi(pass))
      || !api->compute.accelerationStructure) {
    return;
  }

  api->compute.accelerationStructure(pass, structure, index);
}

/* what can be told before compiling */

static GPUResult
computePipelineChecks(GPUDevice                                 *device,
                      const GPUComputePipelineCreateInfo        *info,
                      const GPUIntersectionFunctionPipelineEXT **outIntersection) {
  GPUShaderExecutionGraphEntryInfo graphEntry;
  GPUShaderStageFlags              stage;
  uint32_t                         executionFlags;
  GPUResult                        result;

  if (!device || !info || !info->layout || !info->library || !info->entryPoint
      || info->layout->_device != device || info->library->_device != device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info->cache && info->cache->device != device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE
      && info->chain.sType != GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = computePipelineExtensions(device, info, outIntersection);

  if (result != GPU_OK) {
    return result;
  }

  if (shaderEntryView(info->library, info->entryPoint, &stage, NULL, &executionFlags)
      && (stage != GPU_SHADER_STAGE_COMPUTE_BIT
          || (executionFlags & GPUShaderEntryExecutionTileBit))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (getShaderLibraryExecutionGraphEntry(info->library,
                                          info->entryPoint,
                                          &graphEntry)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return GPU_OK;
}

static GPUResult
createComputePipelineOnce(GPUDevice                          *__restrict device,
                          const GPUComputePipelineCreateInfo *__restrict info,
                          GPUComputePipeline                **__restrict outPipeline) {
  GPUPipelineCacheKey                      cacheKey;
  const char                              *entries[1];
  GPUComputePipelineState                 *state;
  GPUComputePipeline                      *pipeline;
  GPUShaderFunction                       *function;
  const GPUPipelineConstants              *constants;
  GPUApi                                  *api;
  const GPUIntersectionFunctionPipelineEXT *intersection;
  GPUResult                                result;
  uint32_t                                 requiredBindGroupMask;

  if (!outPipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outPipeline = NULL;
  memset(&cacheKey, 0, sizeof(cacheKey));

  if ((result = computePipelineChecks(device, info, &intersection)) != GPU_OK) {
    return result;
  }

  if (info->cache && !intersection) {
    result = pipelineCacheFindCompute(info->cache,
                                      info,
                                      &cacheKey,
                                      &pipeline);

    if (result != GPU_OK) {
      return result;
    }

    if (pipeline) {
      pipelineCacheReleaseKey(&cacheKey);

      *outPipeline = pipeline;
      return GPU_OK;
    }
  }

  constants  = pipelineConstants(info->chain.pNext);
  entries[0] = info->entryPoint;

  if (!pipelineLayoutMatchesShaderEntries(info->layout,
                                          info->library,
                                          entries,
                                          (uint32_t)GPU_ARRAY_LEN(entries),
                                          GPU_SHADER_STAGE_COMPUTE_BIT,
                                          &requiredBindGroupMask)) {
    pipelineCacheReleaseKey(&cacheKey);
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = deviceApi(device))) {
    pipelineCacheReleaseKey(&cacheKey);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (intersection && api->compute.createPipeline) {
    pipelineCacheReleaseKey(&cacheKey);
    return GPU_ERROR_UNSUPPORTED;
  }

  if (api->compute.createPipeline) {
    if (!(pipeline = calloc(1, sizeof(*pipeline)))) {
      pipelineCacheReleaseKey(&cacheKey);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    pipeline->_api      = api;
    pipeline->_device   = device;
    pipeline->_refCount = 1u;

    pipeline->_requiredBindGroupMask = requiredBindGroupMask;

    result = api->compute.createPipeline(device, info, pipeline);

    if (result != GPU_OK) {
      free(pipeline);
      pipelineCacheReleaseKey(&cacheKey);
      return result;
    }

    state = pipeline->_state;

    if (!state) {
      GPUDestroyComputePipeline(pipeline);
      pipelineCacheReleaseKey(&cacheKey);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  } else {
    if (!api->compute.newComputePipeline || !api->compute.setFunction) {
      pipelineCacheReleaseKey(&cacheKey);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    if (!(function = shaderVariant(info->library, info->entryPoint, constants))) {
      pipelineCacheReleaseKey(&cacheKey);
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    if (!(pipeline = api->compute.newComputePipeline())) {
      destroyShaderFunction(info->library, function);
      pipelineCacheReleaseKey(&cacheKey);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    pipeline->_api      = api;
    pipeline->_device   = device;
    pipeline->_refCount = 1u;

    api->compute.setFunction(pipeline, function);
    destroyShaderFunction(info->library, function);

    if (intersection) {
      result = attachComputeIntersectionFunctions(device,
                                                  info->library,
                                                  intersection,
                                                  pipeline);
      if (result != GPU_OK) {
        GPUDestroyComputePipeline(pipeline);
        pipelineCacheReleaseKey(&cacheKey);
        return result;
      }
    }

    pipeline->_cache = info->cache;
    state            = compileComputePipelineState(device, pipeline);
    pipeline->_cache = NULL;

    if (!state) {
      GPUDestroyComputePipeline(pipeline);
      pipelineCacheReleaseKey(&cacheKey);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  if (!getShaderLibraryComputeWorkgroupSize(info->library,
                                            info->entryPoint,
                                            state->workgroupSize)) {
    state->workgroupSize[0] = 1u;
    state->workgroupSize[1] = 1u;
    state->workgroupSize[2] = 1u;
  }

  pipeline->_layout = info->layout;

  pipeline->_requiredBindGroupMask = requiredBindGroupMask;

  getPipelineLayoutPushConstants(info->layout,
                                 &pipeline->_pushConstantSizeBytes,
                                 &pipeline->_pushConstantStages);

  if (info->cache && !intersection) {
    pipeline = pipelineCacheStoreCompute(info->cache, &cacheKey, pipeline);
  } else {
    recordPipelineCompile(device, info->cache);
  }

  *outPipeline = pipeline;

  return GPU_OK;
}

/* a failed compute pipeline, reported once naming it (api/pipeline_report.c) */
static GPUResult
computePipelineReport(GPUDevice *device, const GPUComputePipelineCreateInfo *info, GPUResult result) {
  static const char *const stages[1] = {"compute"};

  if (result != GPU_OK && device && info)
    pipelineFailed(device, result, "compute pipeline", info->label, info->library, stages, &info->entryPoint, 1u);

  return result;
}

/* every compute pipeline, made here or by a compile job */
static GPUResult
createComputePipeline(GPUDevice                          *__restrict device,
                      const GPUComputePipelineCreateInfo *__restrict info,
                      GPUComputePipeline                **__restrict outPipeline) {
  pipelineDriverMessage(NULL);
  return computePipelineReport(device, info, createComputePipelineOnce(device, info, outPipeline));
}

/* a compute pipeline compiled on the device's threads: the caller gets a
 * placeholder at once, whose compiled pipeline its uses wait for. with a
 * cache, identical creates made while it compiles get placeholders of the
 * same job (inflight.refs counts them) */

typedef struct ComputeCompileJob {
  GPUCompileTask               task;
  GPUComputePipelineCreateInfo info;
  GPUPreparedConstants         prepared;
  GPUPipelineCacheInflight     inflight;
  GPUDevice                   *device;
  GPUComputePipeline          *result;
  char                        *entryPoint;
  char                        *label;
  GPUResult                    status;
  bool                         shared; /* in its cache's inflight list */
} ComputeCompileJob;

static void
computeCompileRun(GPUCompileTask *task) {
  ComputeCompileJob *job;
  uint32_t           joined;

  job         = (ComputeCompileJob *)task;
  job->status = createComputePipeline(job->device, &job->info, &job->result);

  /* later creates find it in the cache; each placeholder that joined holds
   * the pipeline as the first one does */
  if (job->shared) {
    joined = pipelineCacheEndCompute(job->info.cache, &job->inflight);

    for (; job->status == GPU_OK && joined; joined--)
      retainComputePipeline(job->result);
  }
}

/* the last placeholder of a job lets it go */
static bool
computeCompileRelease(ComputeCompileJob *job) {
#if defined(_MSC_VER) && !defined(__clang__)
  return InterlockedDecrement((volatile LONG *)&job->inflight.refs) == 0;
#else
  return __atomic_sub_fetch(&job->inflight.refs, 1u, __ATOMIC_ACQ_REL) == 0u;
#endif
}

static void
computeCompileFree(ComputeCompileJob *job) {
  free(job->prepared.values);
  free(job->entryPoint);
  free(job->label);
  free(job);
}

static char*
computeCompileString(const char *value) {
  char  *copy;
  size_t length;

  if (!value)
    return NULL;

  length = strlen(value) + 1u;
  if ((copy = malloc(length)))
    memcpy(copy, value, length);

  return copy;
}

/* info: constants prepared, cache resolved; NULL placeholder: compile here */

static GPUResult
computeCompileDeferred(GPUDevice                          *device,
                       const GPUComputePipelineCreateInfo *info,
                       GPUComputePipeline                **outPipeline) {
  const GPUIntersectionFunctionPipelineEXT *intersection;
  const char                               *entries[1];
  GPUComputePipeline                       *shell;
  GPUComputePipeline                       *cached;
  GPUPipelineCacheInflight                 *running;
  ComputeCompileJob                        *job;
  GPUResult                                 result;
  uint32_t                                  requiredBindGroupMask;
  bool                                      submit;

  if ((result = computePipelineChecks(device, info, &intersection)) != GPU_OK)
    return computePipelineReport(device, info, result);

  /* intersection functions keep caller memory: compiled here */
  if (intersection)
    return createComputePipeline(device, info, outPipeline);

  entries[0] = info->entryPoint;
  if (!pipelineLayoutMatchesShaderEntries(info->layout, info->library, entries, 1u,
                                          GPU_SHADER_STAGE_COMPUTE_BIT, &requiredBindGroupMask))
    return computePipelineReport(device, info, GPU_ERROR_INVALID_ARGUMENT);

  if (!(job = calloc(1u, sizeof(*job))) || !(shell = calloc(1u, sizeof(*shell)))) {
    free(job);
    return computePipelineReport(device, info, GPU_ERROR_OUT_OF_MEMORY);
  }

  if ((result = prepareConstants(info->library, info->chain.pNext, true, &job->prepared)) != GPU_OK
      || !(job->entryPoint = computeCompileString(info->entryPoint))
      || (info->label && !(job->label = computeCompileString(info->label)))) {
    computeCompileFree(job);
    free(shell);
    return computePipelineReport(device, info, result != GPU_OK ? result : GPU_ERROR_OUT_OF_MEMORY);
  }

  job->info             = *info;
  job->info.chain.pNext = job->prepared.chain;
  job->info.entryPoint  = job->entryPoint;
  job->info.label       = job->label;
  job->device           = device;
  job->task.run         = computeCompileRun;
  job->inflight.refs    = 1u;
  submit                = true;

  /* one compiled is in the cache; one compiling (two renderers asking for
   * one pipeline) is shared, not compiled again */
  if (job->info.cache
      && pipelineCacheBeginCompute(job->info.cache, &job->info, &job->inflight,
                                   &cached, &running) == GPU_OK) {
    if (cached || running) {
      computeCompileFree(job);

      if (cached) {
        free(shell);
        *outPipeline = cached;
        return GPU_OK;
      }

      job    = (ComputeCompileJob *)(void *)((char *)running - offsetof(ComputeCompileJob, inflight));
      submit = false;
    } else {
      job->shared = true;
    }
  }

  shell->_api                   = deviceApi(device);
  shell->_device                = device;
  shell->_layout                = info->layout;
  shell->_requiredBindGroupMask = requiredBindGroupMask;
  shell->_refCount              = 1u;
  shell->_compile               = job;

  if (submit)
    compilePoolSubmit(device, &job->task);
  *outPipeline = shell;

  return GPU_OK;
}

GPU_HIDE
GPUComputePipeline*
computePipelineResolve(GPUComputePipeline *pipeline) {
  ComputeCompileJob *job;

  if (!pipeline || !(job = pipeline->_compile))
    return pipeline;

  compilePoolWait(pipeline->_device, &job->task);

  return job->status == GPU_OK ? job->result : NULL;
}

GPU_EXPORT
GPUResult
GPUSetPipelineCompileThreads(GPUDevice *device, uint32_t threadCount) {
  return device ? compilePoolConfigure(device, threadCount) : GPU_ERROR_INVALID_ARGUMENT;
}

GPU_EXPORT
GPUResult
GPUHoldPipelineCompiles(GPUDevice *device, bool hold) {
  if (!device)
    return GPU_ERROR_INVALID_ARGUMENT;

  compilePoolHold(device, hold);
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUReleaseHeldPipelines(GPUDevice *device) {
  if (!device)
    return GPU_ERROR_INVALID_ARGUMENT;

  compilePoolRelease(device);
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUWaitComputePipeline(GPUComputePipeline *pipeline) {
  if (!pipeline)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (!pipeline->_compile)
    return pipeline->_api && pipeline->_api->compute.waitPipeline
           ? pipeline->_api->compute.waitPipeline(pipeline)
           : GPU_OK;

  (void)computePipelineResolve(pipeline);

  return pipeline->_compile->status;
}

/* texel reads per bound range: what a pipeline reading through views needs to run the plain module instead. Its
 * layout twins are made with it; the plain library and pipeline at the first dispatch with a range a view can't
 * hold (a group marked texelPlain), then kept */
typedef struct ComputeTexelTwin {
  GPUDevice          *device;
  void               *bytecode;
  uint64_t            bytecodeSize;
  char               *entryPoint;
  GPUConstant        *constants;
  uint32_t            constantCount;
  bool                disableDiskCache;
  GPUPipelineLayout  *layout;                                   /* the pipeline layout over the plain group layouts */
  GPUBindGroupLayout *groupLayouts[GPU_ENCODER_MAX_BIND_GROUPS]; /* the plain copies; NULL where none had views */
  GPUShaderLibrary   *library;
  GPUComputePipeline *pipeline;
} ComputeTexelTwin;

static void
computeTexelTwinFree(ComputeTexelTwin *twin) {
  uint32_t i;

  if (!twin)
    return;

  if (twin->pipeline)
    GPUDestroyComputePipeline(twin->pipeline);
  if (twin->library)
    GPUDestroyShaderLibrary(twin->library);
  if (twin->layout)
    GPUDestroyPipelineLayout(twin->layout);
  for (i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++)
    if (twin->groupLayouts[i])
      GPUDestroyBindGroupLayout(twin->groupLayouts[i]);

  free(twin->bytecode);
  free(twin->entryPoint);
  free(twin->constants);
  free(twin);
}

static ComputeTexelTwin *
computeTexelTwinPrepare(GPUDevice *device, const GPUComputePipelineCreateInfo *info) {
  GPUPipelineLayoutCreateInfo   layoutInfo = {0};
  GPUBindGroupLayout           *groups[GPU_ENCODER_MAX_BIND_GROUPS];
  const GPUPipelineLayoutPriv  *pipeline;
  const GPUPipelineConstants   *constants = NULL;
  const GPUChainedStruct       *chain;
  ComputeTexelTwin             *twin;
  const void                   *bytecode;
  uint64_t                      size;
  bool                          noDiskCache;
  uint32_t                      i;

  if (!(pipeline = info->layout->_priv) || pipeline->bindGroupLayoutCount > GPU_ENCODER_MAX_BIND_GROUPS
      || !shaderLibraryBytecode(info->library, &bytecode, &size, &noDiskCache) || !size
      || !(twin = calloc(1u, sizeof(*twin))))
    return NULL;

  for (chain = info->chain.pNext; chain; chain = chain->pNext)
    if (chain->sType == GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS)
      constants = (const GPUPipelineConstants *)chain;

  twin->device           = device;
  twin->bytecodeSize     = size;
  twin->disableDiskCache = noDiskCache;
  twin->constantCount    = constants ? constants->constantCount : 0u;

  if (!(twin->bytecode = malloc((size_t)size)) || !(twin->entryPoint = malloc(strlen(info->entryPoint) + 1u))
      || (twin->constantCount && (!constants->pConstants
                                  || !(twin->constants = malloc(twin->constantCount * sizeof(GPUConstant)))))) {
    computeTexelTwinFree(twin);
    return NULL;
  }

  memcpy(twin->bytecode, bytecode, (size_t)size);
  memcpy(twin->entryPoint, info->entryPoint, strlen(info->entryPoint) + 1u);

  if (twin->constantCount)
    memcpy(twin->constants, constants->pConstants, twin->constantCount * sizeof(GPUConstant));

  for (i = 0u; i < pipeline->bindGroupLayoutCount; i++) {
    groups[i] = pipeline->bindGroupLayouts[i];

    if (groups[i] && bindGroupLayoutHasTexelViews(groups[i])) {
      if (!(twin->groupLayouts[i] = bindGroupLayoutPlainCopy(groups[i]))) {
        computeTexelTwinFree(twin);
        return NULL;
      }

      groups[i] = twin->groupLayouts[i];
    }
  }

  layoutInfo.chain.sType           = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize      = sizeof(layoutInfo);
  layoutInfo.ppBindGroupLayouts    = groups;
  layoutInfo.bindGroupLayoutCount  = pipeline->bindGroupLayoutCount;
  layoutInfo.pushConstantSizeBytes = pipeline->pushConstantSizeBytes;
  layoutInfo.pushConstantStages    = pipeline->pushConstantStages;

  if (GPUCreatePipelineLayout(device, &layoutInfo, &twin->layout) != GPU_OK) {
    twin->layout = NULL;
    computeTexelTwinFree(twin);
    return NULL;
  }

  return twin;
}

/* the twin's pipeline, made at the first dispatch that needs it (the library and pipeline together, once) */
static GPUComputePipeline *
computeTexelTwinPipeline(ComputeTexelTwin *twin) {
  GPUComputePipelineCreateInfo info      = {0};
  GPUPipelineConstants         constants = {0};
  GPUShaderLibrary            *library   = NULL;
  GPUComputePipeline          *pipeline  = NULL;
  GPUComputePipeline          *expected;

#if defined(_MSC_VER) && !defined(__clang__)
  if ((pipeline = InterlockedCompareExchangePointer((PVOID volatile *)&twin->pipeline, NULL, NULL)))
    return computePipelineResolve(pipeline);
#else
  if ((pipeline = __atomic_load_n(&twin->pipeline, __ATOMIC_ACQUIRE)))
    return computePipelineResolve(pipeline);
#endif

  if (shaderLibraryCreatePlain(twin->device, twin->bytecode, twin->bytecodeSize, twin->disableDiskCache, &library)
        != GPU_OK || !library)
    return NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.library          = library;
  info.layout           = twin->layout;
  info.entryPoint       = twin->entryPoint;

  if (twin->constantCount) {
    constants.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS;
    constants.chain.structSize = sizeof(constants);
    constants.pConstants       = twin->constants;
    constants.constantCount    = twin->constantCount;
    info.chain.pNext           = &constants.chain;
  }

  if (GPUCreateComputePipeline(twin->device, &info, &pipeline) != GPU_OK || !pipeline) {
    GPUDestroyShaderLibrary(library);
    return NULL;
  }

  expected = NULL;
#if defined(_MSC_VER) && !defined(__clang__)
  if ((expected = InterlockedCompareExchangePointer((PVOID volatile *)&twin->pipeline, pipeline, NULL)) != NULL) {
#else
  if (!__atomic_compare_exchange_n(&twin->pipeline, &expected, pipeline, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
#endif
    GPUDestroyComputePipeline(pipeline);
    GPUDestroyShaderLibrary(library);
    return computePipelineResolve(expected);
  }

  twin->library = library; /* published with the pipeline: one creator gets here */
  return computePipelineResolve(pipeline);
}

/* before a dispatch: a required group whose range a view can't hold runs the plain twin, every bound group as its
 * plain twin; once none does, the pipeline and groups the app bound come back */
static bool
computeTexelRoute(GPUComputePassEncoder *pass, GPUApi *api) {
  ComputeTexelTwin   *twin = pass->_texelTwin;
  GPUComputePipeline *pipeline;
  GPUBindGroup       *group;
  uint32_t            mask, g;
  bool                plain;

  mask  = pass->_requiredBindGroupMask;
  plain = twin && (pass->_texelPlainGroups & mask) != 0u;

  if (!plain && !pass->_texelTwinBound)
    return true;

  if (!api->compute.setComputePipelineState || !api->descriptor.bindComputeGroup)
    return false;

  if (plain) {
    if (!(pipeline = computeTexelTwinPipeline(twin)) || !pipeline->_state) {
      computeValidationError(pass, "dispatch skipped: no plain pipeline for a range texel views can't hold");
      return false;
    }

    api->compute.setComputePipelineState(pass, pipeline->_state);

    for (g = 0u; g < GPU_ENCODER_MAX_BIND_GROUPS; g++) {
      if (!(mask & (1u << g)) || !(group = pass->_boundGroups[g]))
        continue;

      if (twin->groupLayouts[g] && !(group = bindGroupPlainTwin(group, twin->groupLayouts[g]))) {
        computeValidationError(pass, "dispatch skipped: no plain bind group for a range texel views can't hold");
        return false;
      }

      api->descriptor.bindComputeGroup(pass, twin->layout, g, group, pass->_boundDynamicOffsetCounts[g],
                                       pass->_boundDynamicOffsets[g]);
    }

    pass->_texelTwinBound = true;
    return true;
  }

  /* back to what the app bound */
  api->compute.setComputePipelineState(pass, ((GPUComputePipeline *)pass->_pipeline)->_state);

  for (g = 0u; g < GPU_ENCODER_MAX_BIND_GROUPS; g++) {
    if ((mask & (1u << g)) && (group = pass->_boundGroups[g]))
      api->descriptor.bindComputeGroup(pass, pass->_pipelineLayout, g, group, pass->_boundDynamicOffsetCounts[g],
                                       pass->_boundDynamicOffsets[g]);
  }

  pass->_texelTwinBound = false;
  return true;
}

GPU_EXPORT
void
GPUDestroyComputePipeline(GPUComputePipeline *pipeline) {
  ComputeCompileJob *job;
  GPUApi            *api;

  if (!pipeline) {
    return;
  }

  if ((job = pipeline->_compile)) {
    if (!releaseComputePipeline(pipeline))
      return;

    computeTexelTwinFree(pipeline->_texelTwin);
    pipeline->_texelTwin = NULL;

    /* the compile finished (or run here), then this placeholder's hold on
     * what it made goes, and the job with the last one */
    (void)computePipelineResolve(pipeline);
    if (job->status == GPU_OK)
      GPUDestroyComputePipeline(job->result);
    if (computeCompileRelease(job))
      computeCompileFree(job);
    free(pipeline);
    return;
  }

  if (!releaseComputePipeline(pipeline)) {
    return;
  }

  computeTexelTwinFree(pipeline->_texelTwin);
  pipeline->_texelTwin = NULL;

  if ((api = pipeline->_api) && api->compute.destroyComputePipeline) {
    api->compute.destroyComputePipeline(pipeline);
    return;
  }

  free(pipeline);
}

GPU_EXPORT
GPUComputePassEncoder*
GPUBeginComputePass(GPUCommandBuffer *cmdb, const char *label) {
  GPUComputePassCreateInfo info = {0};

  info.label = label;

  return GPUBeginComputePassWithInfo(cmdb, &info);
}

GPU_EXPORT
GPUComputePassEncoder*
GPUBeginComputePassWithInfo(GPUCommandBuffer               *cmdb,
                            const GPUComputePassCreateInfo *info) {
  GPUComputePassCreateInfo backendInfo;
  GPUComputePassEncoder   *pass;
  GPUDevice               *device;
  GPUApi                  *api;
  bool                     wroteBeginTimestamp;

  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder || !info
      || (info->chain.sType != GPU_STRUCTURE_TYPE_NONE
          && info->chain.sType != GPU_STRUCTURE_TYPE_COMPUTE_PASS_CREATE_INFO)
      || (info->chain.structSize != 0u
          && info->chain.structSize < sizeof(*info))
      || info->chain.pNext) {
    return NULL;
  }

  device = cmdb->_queue ? cmdb->_queue->_device : NULL;

  if (!validPassTimestampWrites(info->timestampWrites, device)
      || !(api = deviceApi(device)) || !api->compute.computeCommandEncoder) {
    return NULL;
  }

  backendInfo         = *info;
  backendInfo.label   = deviceDebugLabel(device, info->label);
  wroteBeginTimestamp = false;

  if (info->timestampWrites && api->cmdbuf.writeTimestamp) {
    api->cmdbuf.writeTimestamp(cmdb,
                               info->timestampWrites->querySet,
                               info->timestampWrites->beginIndex,
                               true);
    wroteBeginTimestamp = true;
  }

  if (!(pass = api->compute.computeCommandEncoder(cmdb, &backendInfo))) {
    if (wroteBeginTimestamp) {
      api->cmdbuf.writeTimestamp(cmdb,
                                 info->timestampWrites->querySet,
                                 info->timestampWrites->endIndex,
                                 false);
    }
    return NULL;
  }

  pass->_api               = api;
  pass->_device            = device;
  pass->_cmdb              = cmdb;
  pass->_stats             = device->runtimeConfig.enableStats ? &device->currentFrameStats : NULL;
  pass->_timestampQuerySet = info->timestampWrites ? info->timestampWrites->querySet : NULL;
  pass->_timestampEndIndex = info->timestampWrites ? info->timestampWrites->endIndex : 0u;

  cmdb->_activeEncoder = true;

  return pass;
}

GPU_EXPORT
void
GPUBindComputePipeline(GPUComputePassEncoder *pass,
                       GPUComputePipeline    *pipeline) {
  GPUComputePipelineState *state;
  GPUApi                  *api;
  ComputeTexelTwin        *twin = pipeline ? pipeline->_texelTwin : NULL;

  if (pipeline && pipeline->_compile && !(pipeline = computePipelineResolve(pipeline))) {
    if (pass)
      computeValidationError(pass, "GPUBindComputePipeline skipped: its background compile failed");
    return;
  }

  if (!pass || pass->_ended || !pipeline || !pipeline->_state) {
    return;
  }

  if (pipeline->_device != computePassDevice(pass)
      || pipeline->_api != computePassApi(pass)) {
    computeValidationError(pass,
                           "GPUBindComputePipeline skipped: device mismatch");
    return;
  }

  if (!(api = computePassApi(pass))
      || !api->compute.setComputePipelineState) {
    return;
  }

  frameStatsRecordBindRequest(pass->_stats);

  if (pass->_pipeline == pipeline) {
    return;
  }

  if (pass->_pipelineLayout != pipeline->_layout) {
    memset(pass->_boundGroups, 0, sizeof(pass->_boundGroups));
    memset(pass->_boundGroupLayouts, 0, sizeof(pass->_boundGroupLayouts));
    memset(pass->_boundDynamicOffsetCounts,
           0,
           sizeof(pass->_boundDynamicOffsetCounts));
    pass->_imageBufferGroups = 0u;
    pass->_texelPlainGroups  = 0u;
  }

  pass->_texelTwin = twin;

  pass->_pipelineLayout = pipeline->_layout;

  state = pipeline->_state;

  api->compute.setComputePipelineState(pass, state);
  frameStatsRecordBindEmission(pass->_stats);

  pass->_hasPipeline           = true;
  pass->_executionGraph        = false;
  pass->_pipeline              = pipeline;
  pass->_requiredBindGroupMask = pipeline->_requiredBindGroupMask;
  pass->_pushConstantSizeBytes = pipeline->_pushConstantSizeBytes;
  pass->_pushConstantStages    = pipeline->_pushConstantStages & GPU_SHADER_STAGE_COMPUTE_BIT;
  pass->_pushConstantsEmitted  = false;

  if (pass->_pushConstantSizeBytes > 0u) {
    memset(pass->_pushConstants, 0, pass->_pushConstantSizeBytes);
  }
}

/* groups holding emulated read-write textures (image_buffer_internal.h): a
 * device that never made one's buffer answers at once, a group keeps what it
 * found until another is made */

static inline bool
computeGroupImageBuffers(GPUComputePassEncoder *pass, GPUBindGroup *group) {
  const GPUBindGroupPriv *priv;

  if (!pass->_device || !pass->_device->_imageBufferGeneration || !(priv = group->_priv))
    return false;

  return priv->imageBufferGeneration == pass->_device->_imageBufferGeneration ? priv->imageBuffers
                                                                              : bindGroupImageBuffersFind(group);
}

static inline void
computeNoteTexelPlain(GPUComputePassEncoder *pass, uint32_t groupIndex, GPUBindGroup *group) {
  if (group && group->_priv && ((GPUBindGroupPriv *)group->_priv)->texelPlain)
    pass->_texelPlainGroups |= 1u << groupIndex;
  else
    pass->_texelPlainGroups &= ~(1u << groupIndex);
}

static inline void
computeNoteImageBuffers(GPUComputePassEncoder *pass, uint32_t groupIndex, GPUBindGroup *group) {
  if (computeGroupImageBuffers(pass, group))
    pass->_imageBufferGroups |= 1u << groupIndex;
  else
    pass->_imageBufferGroups &= ~(1u << groupIndex);
}

static void
rebindComputeGroup(GPUComputePassEncoder *pass, GPUApi *api, uint32_t groupIndex) {
  BindComputeContext ctx;

  if (api->descriptor.bindComputeGroup) {
    (void)api->descriptor.bindComputeGroup(pass,
                                           pass->_pipelineLayout,
                                           groupIndex,
                                           pass->_boundGroups[groupIndex],
                                           pass->_boundDynamicOffsetCounts[groupIndex],
                                           pass->_boundDynamicOffsets[groupIndex]);
    return;
  }

  ctx.pass = pass;
  (void)forEachBindGroupBindingWithDynamicOffsets(pass->_pipelineLayout,
                                                  groupIndex,
                                                  pass->_boundGroups[groupIndex],
                                                  pass->_boundDynamicOffsetCounts[groupIndex],
                                                  pass->_boundDynamicOffsets[groupIndex],
                                                  bindComputeBinding,
                                                  &ctx);
}

/* a dispatch whose emulated textures need a copy first: the pass ends, the
 * copies go, and the pass goes on in a new encoder with the same state */

static void
computeImageBuffers(GPUComputePassEncoder *pass, GPUApi *api) {
  GPUComputePassCreateInfo info;
  GPUComputePassEncoder    saved;
  GPUComputePipeline      *pipeline;
  GPUCommandBuffer        *cmdb;
  void                    *priv;
  uint32_t                 group;

  cmdb = pass->_cmdb;

  if (!cmdb || !api->compute.endEncoding || !api->compute.computeCommandEncoder
      || !imageBufferComputeNeedsSync(pass)) {
    imageBufferComputeWrote(pass);
    return;
  }

  saved = *pass;
  api->compute.endEncoding(pass);
  cmdb->_activeEncoder = false;

  imageBufferComputeSync(&saved, cmdb);

  memset(&info, 0, sizeof(info));
  info.label = "gpu image buffer";

  /* backends keep one compute encoder per command buffer: the same one comes back */

  if (api->compute.computeCommandEncoder(cmdb, &info) != pass) {
    *pass        = saved;
    pass->_ended = true;
    return;
  }

  priv        = pass->_priv;
  *pass       = saved;
  pass->_priv = priv;
  cmdb->_activeEncoder = true;

  if ((pipeline = pass->_pipeline) && pipeline->_state && api->compute.setComputePipelineState)
    api->compute.setComputePipelineState(pass, pipeline->_state);

  for (group = 0u; group < GPU_ENCODER_MAX_BIND_GROUPS; group++) {
    if (pass->_boundGroups[group])
      rebindComputeGroup(pass, api, group);
  }

  if (pass->_pushConstantsEmitted && api->compute.pushConstants)
    api->compute.pushConstants(pass, pass->_pushConstants, pass->_pushConstantSizeBytes);

  imageBufferComputeWrote(pass);
}

GPU_EXPORT
void
GPUBindComputeGroup(GPUComputePassEncoder *pass,
                    uint32_t               groupIndex,
                    GPUBindGroup          *bindGroup,
                    uint32_t               dynamicOffsetCount,
                    const uint32_t        *pDynamicOffsets) {
  BindComputeContext    ctx;
  GPUApi               *api;

  if (!pass || pass->_ended || !bindGroup
      || groupIndex >= GPU_ENCODER_MAX_BIND_GROUPS) {
    return;
  }

#if GPU_BUILD_WITH_VALIDATION
  if (bindGroupGetDevice(bindGroup) != computePassDevice(pass)
      || !pipelineLayoutAcceptsBindGroup(pass->_pipelineLayout,
                                         groupIndex,
                                         bindGroup)) {
    return;
  }
#endif

  frameStatsRecordBindRequest(pass->_stats);

  if (bindGroupShadowMatches(pass->_boundGroups[groupIndex],
                             pass->_boundDynamicOffsetCounts[groupIndex],
                             pass->_boundDynamicOffsets[groupIndex],
                             bindGroup,
                             dynamicOffsetCount,
                             pDynamicOffsets)) {
    return;
  }

  api = computePassApi(pass);

  /* texel reads: under the plain twin's pipeline a group binds against its layout; the app's group goes in under
   * the app's pipeline (the next dispatch binds what it runs either way) */
  if (pass->_texelTwinBound && pass->_pipeline && api && api->compute.setComputePipelineState)
    api->compute.setComputePipelineState(pass, ((GPUComputePipeline *)pass->_pipeline)->_state);

  if (api && api->descriptor.bindComputeGroup) {
#if GPU_BUILD_WITH_VALIDATION
    if (!validateBindGroupDynamicOffsets(pass->_pipelineLayout,
                                         groupIndex,
                                         bindGroup,
                                         dynamicOffsetCount,
                                         pDynamicOffsets)) {
      return;
    }
#endif

    if (api->descriptor.bindComputeGroup(pass,
                                         pass->_pipelineLayout,
                                         groupIndex,
                                         bindGroup,
                                         dynamicOffsetCount,
                                         pDynamicOffsets)) {
      if (pass->_boundGroups[groupIndex] != bindGroup) {
        pass->_boundGroupLayouts[groupIndex] = bindGroupGetLayout(bindGroup);
        computeNoteImageBuffers(pass, groupIndex, bindGroup);
        computeNoteTexelPlain(pass, groupIndex, bindGroup);
      }

      pass->_boundGroups[groupIndex] = bindGroup;
      storeBindGroupShadow(&pass->_boundDynamicOffsetCounts[groupIndex],
                           pass->_boundDynamicOffsets[groupIndex],
                           dynamicOffsetCount,
                           pDynamicOffsets);
      frameStatsRecordBindEmission(pass->_stats);
    }

    return;
  }

  ctx.pass = pass;

  if (forEachBindGroupBindingWithDynamicOffsets(pass->_pipelineLayout,
                                                groupIndex,
                                                bindGroup,
                                                dynamicOffsetCount,
                                                pDynamicOffsets,
                                                bindComputeBinding,
                                                &ctx)) {
    if (pass->_boundGroups[groupIndex] != bindGroup) {
      pass->_boundGroupLayouts[groupIndex] = bindGroupGetLayout(bindGroup);
      computeNoteImageBuffers(pass, groupIndex, bindGroup);
      computeNoteTexelPlain(pass, groupIndex, bindGroup);
    }

    pass->_boundGroups[groupIndex] = bindGroup;
    storeBindGroupShadow(&pass->_boundDynamicOffsetCounts[groupIndex],
                         pass->_boundDynamicOffsets[groupIndex],
                         dynamicOffsetCount,
                         pDynamicOffsets);
    frameStatsRecordBindEmission(pass->_stats);
  }
}

GPU_EXPORT
void
GPUSetComputePushConstants(GPUComputePassEncoder *pass,
                           uint32_t               offset,
                           uint32_t               sizeBytes,
                           const void            *data) {
  GPUApi *api;

  if (!pass || pass->_ended || !pass->_hasPipeline
      || pass->_pushConstantSizeBytes == 0u
      || pass->_pushConstantStages == 0u) {
    return;
  }

  if (!validPushConstantRange(pass->_pushConstantSizeBytes,
                              offset,
                              sizeBytes,
                              data)) {
    return;
  }

  if (sizeBytes == 0u) {
    return;
  }

  frameStatsRecordStateRequest(pass->_stats);

  if (pass->_pushConstantsEmitted
      && memcmp(pass->_pushConstants + offset, data, sizeBytes) == 0) {
    return;
  }

  if (!(api = computePassApi(pass)) || !api->compute.pushConstants) {
    return;
  }

  memcpy(pass->_pushConstants + offset, data, sizeBytes);
  api->compute.pushConstants(pass,
                             pass->_pushConstants,
                             pass->_pushConstantSizeBytes);
  pass->_pushConstantsEmitted = true;
  frameStatsRecordStateEmission(pass->_stats);
}

GPU_EXPORT
void
GPUDispatch(GPUComputePassEncoder *pass,
            uint32_t               x,
            uint32_t               y,
            uint32_t               z) {
  GPUApi *api;

  if (!pass || pass->_ended) {
    return;
  }

  if (!pass->_hasPipeline || pass->_executionGraph) {
    computeValidationError(pass, "GPUDispatch skipped: no compute pipeline bound");
    return;
  }

  if (!computeBindingsComplete(pass)) {
    computeValidationError(pass, "GPUDispatch skipped: missing compute bind group");
    return;
  }

  if (x == 0 || y == 0 || z == 0) {
    computeValidationError(pass, "GPUDispatch skipped: zero dispatch size");
    return;
  }

  if (!(api = computePassApi(pass)) || !api->compute.dispatch) {
    return;
  }

  if (pass->_imageBufferGroups & pass->_requiredBindGroupMask) {
    computeImageBuffers(pass, api);
  }

  /* texel reads: a range a view can't hold runs the plain twin */
  if ((pass->_texelPlainGroups || pass->_texelTwinBound) && !computeTexelRoute(pass, api)) {
    return;
  }

  api->compute.dispatch(pass, x, y, z);
}

GPU_EXPORT
void
GPUDispatchIndirect(GPUComputePassEncoder *pass,
                    GPUBuffer             *argsBuffer,
                    uint64_t               argsOffset) {
  GPUApi *api;

  if (!pass || pass->_ended) {
    return;
  }

  if (!pass->_hasPipeline || pass->_executionGraph) {
    computeValidationError(pass, "GPUDispatchIndirect skipped: no compute pipeline bound");
    return;
  }

  if (!computeBindingsComplete(pass)) {
    computeValidationError(pass, "GPUDispatchIndirect skipped: missing compute bind group");
    return;
  }

  if (!bufferHasUsage(argsBuffer, GPU_BUFFER_USAGE_INDIRECT)
      || (argsOffset & 3u) != 0u
      || !bufferRangeValid(argsBuffer, argsOffset, 12u)) {
    computeValidationError(pass, "GPUDispatchIndirect skipped: invalid indirect buffer");
    return;
  }

  if (!(api = computePassApi(pass)) || !api->compute.dispatchIndirect) {
    return;
  }

  if (pass->_imageBufferGroups & pass->_requiredBindGroupMask) {
    computeImageBuffers(pass, api);
  }

  /* texel reads: a range a view can't hold runs the plain twin */
  if ((pass->_texelPlainGroups || pass->_texelTwinBound) && !computeTexelRoute(pass, api)) {
    return;
  }

  api->compute.dispatchIndirect(pass, argsBuffer, argsOffset);
}

GPU_EXPORT
void
GPUMultiDispatchIndirect(GPUComputePassEncoder *pass,
                         GPUBuffer             *argsBuffer,
                         uint64_t               argsOffset,
                         uint32_t               dispatchCount,
                         uint32_t               strideBytes) {
  GPUApi  *api;
  uint32_t i;

  if (!pass || pass->_ended) {
    return;
  }

  if (!pass->_hasPipeline || pass->_executionGraph) {
    computeValidationError(pass,
                           "GPUMultiDispatchIndirect skipped: no compute pipeline bound");
    return;
  }

  if (!computeBindingsComplete(pass)) {
    computeValidationError(pass,
                           "GPUMultiDispatchIndirect skipped: missing compute bind group");
    return;
  }

  if (!validIndirectBatch(argsBuffer,
                          argsOffset,
                          dispatchCount,
                          strideBytes,
                          12u)) {
    computeValidationError(pass,
                           "GPUMultiDispatchIndirect skipped: invalid indirect batch");
    return;
  }

  if (!(api = computePassApi(pass))) {
    return;
  }

  if (pass->_imageBufferGroups & pass->_requiredBindGroupMask) {
    computeImageBuffers(pass, api);
  }

  /* texel reads: a range a view can't hold runs the plain twin */
  if ((pass->_texelPlainGroups || pass->_texelTwinBound) && !computeTexelRoute(pass, api)) {
    return;
  }

  if (api->compute.multiDispatchIndirect
      && api->compute.multiDispatchIndirect(pass,
                                            argsBuffer,
                                            argsOffset,
                                            dispatchCount,
                                            strideBytes)) {
    return;
  }

  if (!api->compute.dispatchIndirect) {
    return;
  }

  for (i = 0; i < dispatchCount; i++) {
    api->compute.dispatchIndirect(pass,
                                  argsBuffer,
                                  argsOffset + (uint64_t)i * strideBytes);
  }
}

GPU_EXPORT
void
GPUEndComputePass(GPUComputePassEncoder *pass) {
  GPUApi *api;

  if (!pass || pass->_ended) {
    return;
  }

  pass->_ended = true;

  if (pass->_cmdb) {
    pass->_cmdb->_activeEncoder = false;
  }

  if (!(api = computePassApi(pass)) || !api->compute.endEncoding) {
    return;
  }

  api->compute.endEncoding(pass);

  if (pass->_timestampQuerySet && api->cmdbuf.writeTimestamp) {
    api->cmdbuf.writeTimestamp(pass->_cmdb,
                               pass->_timestampQuerySet,
                               pass->_timestampEndIndex,
                               false);
  }
}

GPU_EXPORT
GPUResult
GPUCreateComputePipeline(GPUDevice                          *device,
                         const GPUComputePipelineCreateInfo *info,
                         GPUComputePipeline                **outPipeline) {
  GPUComputePipelineCreateInfo snapshot;
  GPUPreparedConstants         prepared;
  GPUResult                    result;

  if (!outPipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outPipeline = NULL;

  if (!info || !info->library) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = prepareConstants(info->library, info->chain.pNext, true, &prepared);

  if (result != GPU_OK) {
    return result;
  }

  snapshot             = *info;
  snapshot.chain.pNext = prepared.chain;
  snapshot.library     = shaderLibraryForLayout(info->library, info->layout);
  if (!snapshot.cache && device)
    snapshot.cache = device->_defaultPipelineCache;
  result               = compilePoolThreaded(device)
                         ? computeCompileDeferred(device, &snapshot, outPipeline)
                         : createComputePipeline(device, &snapshot, outPipeline);
  free(prepared.values);

  /* reads through views: what a range they can't hold needs (no twin: such a dispatch is skipped) */
  if (result == GPU_OK && *outPipeline && snapshot.library == info->library && info->layout
      && shaderLibraryTexelReads(info->library))
    (*outPipeline)->_texelTwin = computeTexelTwinPrepare(device, info);

  return result;
}
