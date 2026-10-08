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
#include "buffer_internal.h"
#include "compute_internal.h"
#include "descr/descriptor_internal.h"
#include "device_internal.h"
#include "execution_graph_internal.h"
#include "library_internal.h"
#include "pipeline_cache_internal.h"

#include <us/compiler.h>

static bool
graphChainValid(const GPUChainedStruct *chain,
                GPUStructureType        type,
                size_t                  size) {
  return chain
         && (chain->sType == GPU_STRUCTURE_TYPE_NONE || chain->sType == type)
         && (chain->structSize == 0u || chain->structSize >= size);
}

static void
releaseExecutionGraph(GPUExecutionGraphEXT *graph) {
  Api    *api;

  if (!graph) {
    return;
  }

#if defined(_WIN32) || defined(WIN32)
  if (InterlockedDecrement((volatile LONG *)&graph->refCount) != 0) {
    return;
  }
#else
  if (__atomic_sub_fetch(&graph->refCount, 1u, __ATOMIC_ACQ_REL) != 0u) {
    return;
  }
#endif

  api = graph->_api;

  if (api && api->executionGraph.destroy) {
    api->executionGraph.destroy(graph);
  }

  free(graph);
}

static bool
graphRequirementsValid(const GPUExecutionGraphMemoryRequirementsEXT *requirements) {
  if (!requirements || requirements->minSizeBytes > requirements->maxSizeBytes) {
    return false;
  }

  if (requirements->maxSizeBytes == 0u) {
    return requirements->minSizeBytes == 0u;
  }

  return requirements->sizeGranularityBytes > 0u;
}

static bool
graphMemorySizeValid(const GPUExecutionGraphMemoryRequirementsEXT *requirements,
                     uint64_t                                      sizeBytes) {
  if (!graphRequirementsValid(requirements)) {
    return false;
  }

  if (requirements->maxSizeBytes == 0u) {
    return sizeBytes == 0u;
  }

  if (sizeBytes < requirements->minSizeBytes
      || sizeBytes > requirements->maxSizeBytes) {
    return false;
  }

  return (sizeBytes - requirements->minSizeBytes) % requirements->sizeGranularityBytes == 0u;
}

static bool
graphEntryValid(const GPUExecutionGraphEntryEXT *entry) {
  uint32_t alignment;

  if (!entry) {
    return false;
  }

  alignment = entry->recordAlignmentBytes;

  return alignment > 0u && (alignment & (alignment - 1u)) == 0u
         && (entry->recordSizeBytes == 0u || entry->recordSizeBytes % alignment == 0u);
}

static bool
graphInputLayoutValid(const GPUExecutionGraphEntryEXT *entry,
                      uint64_t                         strideBytes) {
  return graphEntryValid(entry)
         && (strideBytes == 0u
             || (strideBytes >= entry->recordSizeBytes
                 && strideBytes % entry->recordAlignmentBytes == 0u));
}

static bool
graphInputSize(const GPUExecutionGraphEntryEXT *entry,
               uint32_t                         recordCount,
               uint64_t                         strideBytes,
               uint64_t                        *outSizeBytes) {
  uint64_t effectiveStride;
  uint64_t lastRecord;

  if (!outSizeBytes || recordCount == 0u
      || !graphInputLayoutValid(entry, strideBytes)) {
    return false;
  }

  if (entry->recordSizeBytes == 0u) {
    *outSizeBytes = 0u;
    return true;
  }

  if (recordCount == 1u) {
    *outSizeBytes = entry->recordSizeBytes;
    return true;
  }

  effectiveStride = strideBytes ? strideBytes : entry->recordSizeBytes;
  lastRecord      = (uint64_t)recordCount - 1u;

  if (lastRecord > (UINT64_MAX - entry->recordSizeBytes) / effectiveStride) {
    return false;
  }

  *outSizeBytes = lastRecord * effectiveStride + entry->recordSizeBytes;

  return true;
}

static GPUDevice*
graphPassDevice(const GPUComputePassEncoder *pass) {
  return pass ? pass->_device : NULL;
}

static Api*
graphPassApi(const GPUComputePassEncoder *pass) {
  return pass && pass->_api ? pass->_api : deviceApi(graphPassDevice(pass));
}

static void
graphValidationError(const GPUComputePassEncoder *pass,
                     const char                  *message) {
#if GPU_BUILD_WITH_VALIDATION
  deviceRecordValidationError(graphPassDevice(pass), message);
#else
  GPU__UNUSED(pass);
  GPU__UNUSED(message);
#endif
}

static bool
graphBindingsComplete(const GPUComputePassEncoder *pass) {
#if GPU_BUILD_WITH_VALIDATION
  GPUDevice *device;

  device = graphPassDevice(pass);

  if (!deviceValidationEnabled(device)) {
    return true;
  }

  return pipelineLayoutMaskIsBound(pass->_pipelineLayout,
                                   pass->_boundGroupLayouts,
                                   GPU_ENCODER_MAX_BIND_GROUPS,
                                   pass->_requiredBindGroupMask);
#else
  GPU__UNUSED(pass);

  return true;
#endif
}

static bool
graphDispatchReady(GPUComputePassEncoder        *pass,
                   GPUExecutionGraphInstanceEXT *instance,
                   const char                   *name) {
  if (!pass || pass->_ended || !instance) {
    return false;
  }

  if (!pass->_hasPipeline || !pass->_executionGraph) {
    graphValidationError(pass, name);
    return false;
  }

  if (instance->device != graphPassDevice(pass)
      || instance->_api != graphPassApi(pass)
      || instance->graph != (GPUExecutionGraphEXT *)pass->_pipeline) {
    graphValidationError(pass,
                         "execution graph dispatch skipped: instance mismatch");
    return false;
  }

  if (!graphBindingsComplete(pass)) {
    graphValidationError(pass,
                         "execution graph dispatch skipped: missing bind group");
    return false;
  }

  return true;
}

GPU_HIDE
void
retainExecutionGraph(GPUExecutionGraphEXT *graph) {
  if (!graph) {
    return;
  }

#if defined(_WIN32) || defined(WIN32)
  InterlockedIncrement((volatile LONG *)&graph->refCount);
#else
  __atomic_add_fetch(&graph->refCount, 1u, __ATOMIC_RELAXED);
#endif
}

GPU_EXPORT
GPUResult
GPUCreateExecutionGraphEXT(GPUDevice                            *device,
                           const GPUExecutionGraphCreateInfoEXT *info,
                           GPUExecutionGraphEXT                **outGraph) {
  PipelineCacheKey      cacheKey;
  const char           *entryPoints[USL_RUNTIME_MAX_ENTRY_POINTS];
  GPUExecutionGraphEXT *graph;
  GPUExecutionGraphEXT *cachedGraph;
  Api                  *api;
  uint32_t              entryCount;
  uint32_t              i;
  GPUResult             result;

  if (!outGraph) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outGraph = NULL;
  memset(&cacheKey, 0, sizeof(cacheKey));

  if (!device || !info || !info->library || !info->layout
      || (info->graphName && info->graphName[0] == '\0')
      || !graphChainValid(&info->chain,
                          GPU_STRUCTURE_TYPE_EXECUTION_GRAPH_CREATE_INFO_EXT,
                          sizeof(*info))
      || info->library->_device != device
      || info->layout->_device != device
      || (info->cache && info->cache->device != device)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_EXECUTION_GRAPH)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  entryCount = getShaderLibraryExecutionGraphEntryCount(info->library);

  if (entryCount == 0u || entryCount > GPU_ARRAY_LEN(entryPoints)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  for (i = 0u; i < entryCount; i++) {
    ShaderExecutionGraphEntryInfo    entry;

    if (!getShaderLibraryExecutionGraphEntryAt(info->library, i, &entry)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    entryPoints[i] = entry.entryPoint;
  }

  if (!(api = deviceApi(device)) || info->library->_api != api || !api->executionGraph.create) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!(graph = calloc(1, sizeof(*graph)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  graph->_api     = api;
  graph->device   = device;
  graph->library  = info->library;
  graph->layout   = info->layout;
  graph->refCount = 1u;

  if (!pipelineLayoutMatchesShaderEntries(info->layout,
                                          info->library,
                                          entryPoints,
                                          entryCount,
                                          GPU_SHADER_STAGE_COMPUTE_BIT,
                                          &graph->requiredBindGroupMask)) {
    free(graph);
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  getPipelineLayoutPushConstants(info->layout,
                                 &graph->pushConstantSizeBytes,
                                 &graph->pushConstantStages);

  if (info->cache && !info->chain.pNext) {
    cachedGraph = NULL;
    result      = pipelineCacheFindGraph(info->cache,
                                         info,
                                         &cacheKey,
                                         &cachedGraph);

    if (result != GPU_OK) {
      free(graph);
      return result;
    }

    if (cachedGraph) {
      pipelineCacheReleaseKey(&cacheKey);
      free(graph);
      *outGraph = cachedGraph;
      return GPU_OK;
    }
  }

  result = api->executionGraph.create(device, info, graph);

  if (result != GPU_OK
      || !graphRequirementsValid(&graph->memoryRequirements)) {
    pipelineCacheReleaseKey(&cacheKey);
    releaseExecutionGraph(graph);
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }

  if (info->cache && !info->chain.pNext) {
    graph = pipelineCacheStoreGraph(info->cache, &cacheKey, graph);
  } else {
    recordPipelineCompile(device, info->cache);
  }

  *outGraph = graph;

  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyExecutionGraphEXT(GPUExecutionGraphEXT *graph) {
  releaseExecutionGraph(graph);
}

GPU_EXPORT
GPUResult
GPUGetExecutionGraphMemoryRequirementsEXT(const GPUExecutionGraphEXT             *graph,
                                          GPUExecutionGraphMemoryRequirementsEXT *outRequirements) {
  if (!graph || !outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outRequirements = graph->memoryRequirements;

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateExecutionGraphInstanceEXT(GPUDevice                                    *device,
                                   const GPUExecutionGraphInstanceCreateInfoEXT *info,
                                   GPUExecutionGraphInstanceEXT                **outInstance) {
  GPUExecutionGraphInstanceCreateInfoEXT resolvedInfo;
  GPUExecutionGraphInstanceEXT          *instance;
  Api                                   *api;
  uint64_t                               memorySizeBytes;
  GPUResult                              result;

  if (!outInstance) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outInstance = NULL;

  if (!device || !info || !info->graph || info->graph->device != device
      || !graphChainValid(&info->chain,
                          GPU_STRUCTURE_TYPE_EXECUTION_GRAPH_INSTANCE_CREATE_INFO_EXT,
                          sizeof(*info))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  api = info->graph->_api;

  if (!api || api != deviceApi(device)
      || !api->executionGraph.createInstance) {
    return GPU_ERROR_UNSUPPORTED;
  }

  memorySizeBytes = info->memorySizeBytes > 0u
                      ? info->memorySizeBytes
                      : info->graph->memoryRequirements.minSizeBytes;

  if (!graphMemorySizeValid(&info->graph->memoryRequirements,
                            memorySizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(instance = calloc(1, sizeof(*instance)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  instance->_api            = api;
  instance->device          = device;
  instance->graph           = info->graph;
  instance->memorySizeBytes = memorySizeBytes;

  resolvedInfo                 = *info;
  resolvedInfo.memorySizeBytes = memorySizeBytes;

  result = api->executionGraph.createInstance(device,
                                              &resolvedInfo,
                                              instance);

  if (result != GPU_OK) {
    free(instance);
    return result;
  }

  retainExecutionGraph(info->graph);

  *outInstance = instance;

  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyExecutionGraphInstanceEXT(GPUExecutionGraphInstanceEXT *instance) {
  GPUExecutionGraphEXT *graph;
  Api                  *api;

  if (!instance) {
    return;
  }

  graph = instance->graph;
  api   = instance->_api;

  if (api && api->executionGraph.destroyInstance) {
    api->executionGraph.destroyInstance(instance);
  }

  free(instance);
  releaseExecutionGraph(graph);
}

GPU_EXPORT
GPUResult
GPUGetExecutionGraphEntryEXT(const GPUExecutionGraphEXT *graph,
                             const char                 *entryName,
                             GPUExecutionGraphEntryEXT  *outEntry) {
  GPUResult result;

  if (!graph || !entryName || entryName[0] == '\0' || !outEntry) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outEntry, 0, sizeof(*outEntry));

  if (!graph->_api || !graph->_api->executionGraph.getEntry) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = graph->_api->executionGraph.getEntry(graph, entryName, outEntry);

  if (result != GPU_OK) {
    memset(outEntry, 0, sizeof(*outEntry));
    return result;
  }

  if (!graphEntryValid(outEntry)) {
    memset(outEntry, 0, sizeof(*outEntry));
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return GPU_OK;
}

GPU_EXPORT
void
GPUBindExecutionGraphEXT(GPUComputePassEncoder *pass,
                         GPUExecutionGraphEXT  *graph) {
  Api    *api;

  if (!pass || pass->_ended || !graph) {
    return;
  }

  if (graph->device != graphPassDevice(pass)
      || graph->_api != graphPassApi(pass)) {
    graphValidationError(pass,
                         "GPUBindExecutionGraphEXT skipped: device mismatch");
    return;
  }

  api = graph->_api;

  if (!api || !api->executionGraph.bind) {
    return;
  }

  frameStatsRecordBindRequest(pass->_stats);

  if (pass->_executionGraph && pass->_pipeline == graph) {
    return;
  }

  if (pass->_pipelineLayout != graph->layout) {
    memset(pass->_boundGroups, 0, sizeof(pass->_boundGroups));
    memset(pass->_boundGroupLayouts, 0, sizeof(pass->_boundGroupLayouts));
    memset(pass->_boundDynamicOffsetCounts,
           0,
           sizeof(pass->_boundDynamicOffsetCounts));
  }

  pass->_pipelineLayout = graph->layout;

  api->executionGraph.bind(pass, graph);
  frameStatsRecordBindEmission(pass->_stats);

  pass->_pipeline              = graph;
  pass->_requiredBindGroupMask = graph->requiredBindGroupMask;
  pass->_pushConstantSizeBytes = graph->pushConstantSizeBytes;
  pass->_pushConstantStages    = graph->pushConstantStages & GPU_SHADER_STAGE_COMPUTE_BIT;
  pass->_hasPipeline           = true;
  pass->_executionGraph        = true;
  pass->_pushConstantsEmitted  = false;

  if (pass->_pushConstantSizeBytes > 0u) {
    memset(pass->_pushConstants, 0, pass->_pushConstantSizeBytes);
  }
}

GPU_EXPORT
void
GPUDispatchExecutionGraphEXT(GPUComputePassEncoder           *pass,
                             GPUExecutionGraphInstanceEXT    *instance,
                             uint32_t                         inputCount,
                             const GPUExecutionGraphInputEXT *pInputs) {
  const GPUExecutionGraphInputEXT *input;
  Api                             *api;
  uint64_t                         sizeBytes;
  uint32_t                         i;

  if (!graphDispatchReady(pass,
                          instance,
                          "GPUDispatchExecutionGraphEXT skipped: no execution graph bound")) {
    return;
  }

  if (inputCount == 0u || !pInputs) {
    graphValidationError(pass,
                         "GPUDispatchExecutionGraphEXT skipped: no inputs");
    return;
  }

  for (i = 0u; i < inputCount; i++) {
    input = &pInputs[i];

    if (!graphInputSize(&input->entry,
                        input->recordCount,
                        input->recordStrideBytes,
                        &sizeBytes)
        || (sizeBytes > 0u
            && (!input->pRecords
                || ((uintptr_t)input->pRecords & (input->entry.recordAlignmentBytes - 1u)) != 0u))) {
      graphValidationError(pass,
                           "GPUDispatchExecutionGraphEXT skipped: invalid input");
      return;
    }
  }

  if (!(api = graphPassApi(pass)) || !api->executionGraph.dispatch) {
    return;
  }

  api->executionGraph.dispatch(pass, instance, inputCount, pInputs);
}

GPU_EXPORT
void
GPUDispatchExecutionGraphBufferEXT(GPUComputePassEncoder                 *pass,
                                   GPUExecutionGraphInstanceEXT          *instance,
                                   uint32_t                               inputCount,
                                   const GPUExecutionGraphBufferInputEXT *pInputs) {
  const GPUExecutionGraphBufferInputEXT *input;
  Api                                   *api;
  uint64_t                               sizeBytes;
  uint32_t                               i;

  if (!graphDispatchReady(pass,
                          instance,
                          "GPUDispatchExecutionGraphBufferEXT skipped: no execution graph bound")) {
    return;
  }

  if (inputCount == 0u || !pInputs) {
    graphValidationError(pass,
                         "GPUDispatchExecutionGraphBufferEXT skipped: no inputs");
    return;
  }

  for (i = 0u; i < inputCount; i++) {
    input = &pInputs[i];

    if (!input->records || input->records->device != instance->device
        || !bufferHasUsage(input->records,
                           GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT | GPU_BUFFER_USAGE_INDIRECT)
        || !graphInputSize(&input->entry,
                           input->recordCount,
                           input->recordStrideBytes,
                           &sizeBytes)
        || (input->recordOffset & (input->entry.recordAlignmentBytes - 1u)) != 0u
        || (sizeBytes > 0u
              ? !bufferRangeValid(input->records,
                                  input->recordOffset,
                                  sizeBytes)
              : !bufferOffsetValid(input->records, input->recordOffset))) {
      graphValidationError(pass,
                           "GPUDispatchExecutionGraphBufferEXT skipped: invalid input");
      return;
    }
  }

  if (!(api = graphPassApi(pass)) || !api->executionGraph.dispatchBuffer) {
    return;
  }

  api->executionGraph.dispatchBuffer(pass, instance, inputCount, pInputs);
}
