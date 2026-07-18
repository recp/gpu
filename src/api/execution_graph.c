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
gpu_graphChainValid(const GPUChainedStruct *chain,
                    GPUStructureType        type,
                    size_t                  size) {
  return chain &&
         (chain->sType == GPU_STRUCTURE_TYPE_NONE || chain->sType == type) &&
         (chain->structSize == 0u || chain->structSize >= size);
}

static void
gpu_releaseExecutionGraph(GPUExecutionGraphEXT *graph) {
  GPUApi *api;

  if (!graph || graph->refCount == 0u || --graph->refCount != 0u) {
    return;
  }
  api = graph->_api;
  if (api && api->executionGraph.destroy) {
    api->executionGraph.destroy(graph);
  }
  free(graph);
}

static bool
gpu_graphRequirementsValid(
  const GPUExecutionGraphMemoryRequirementsEXT *requirements) {
  if (!requirements || requirements->minSizeBytes > requirements->maxSizeBytes) {
    return false;
  }
  if (requirements->maxSizeBytes == 0u) {
    return requirements->minSizeBytes == 0u;
  }

  return requirements->sizeGranularityBytes > 0u;
}

static bool
gpu_graphMemorySizeValid(
  const GPUExecutionGraphMemoryRequirementsEXT *requirements,
  uint64_t                                      sizeBytes) {
  if (!gpu_graphRequirementsValid(requirements)) {
    return false;
  }
  if (requirements->maxSizeBytes == 0u) {
    return sizeBytes == 0u;
  }
  if (sizeBytes < requirements->minSizeBytes ||
      sizeBytes > requirements->maxSizeBytes) {
    return false;
  }

  return (sizeBytes - requirements->minSizeBytes) %
           requirements->sizeGranularityBytes == 0u;
}

static bool
gpu_graphEntryValid(const GPUExecutionGraphEntryEXT *entry) {
  uint32_t alignment;

  if (!entry) {
    return false;
  }
  alignment = entry->recordAlignmentBytes;
  return alignment > 0u && (alignment & (alignment - 1u)) == 0u &&
         (entry->recordSizeBytes == 0u ||
          entry->recordSizeBytes % alignment == 0u);
}

static bool
gpu_graphInputLayoutValid(const GPUExecutionGraphEntryEXT *entry,
                          uint64_t                         strideBytes) {
  return gpu_graphEntryValid(entry) &&
         (strideBytes == 0u ||
          (strideBytes >= entry->recordSizeBytes &&
           strideBytes % entry->recordAlignmentBytes == 0u));
}

static bool
gpu_graphInputSize(const GPUExecutionGraphEntryEXT *entry,
                   uint32_t                         recordCount,
                   uint64_t                         strideBytes,
                   uint64_t                        *outSizeBytes) {
  uint64_t effectiveStride;
  uint64_t lastRecord;

  if (!outSizeBytes || recordCount == 0u ||
      !gpu_graphInputLayoutValid(entry, strideBytes)) {
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
  lastRecord = (uint64_t)recordCount - 1u;
  if (lastRecord >
      (UINT64_MAX - entry->recordSizeBytes) / effectiveStride) {
    return false;
  }
  *outSizeBytes = lastRecord * effectiveStride + entry->recordSizeBytes;
  return true;
}

static GPUDevice *
gpu_graphPassDevice(const GPUComputePassEncoder *pass) {
  return pass ? pass->_device : NULL;
}

static GPUApi *
gpu_graphPassApi(const GPUComputePassEncoder *pass) {
  return pass && pass->_api ? pass->_api : gpuDeviceApi(gpu_graphPassDevice(pass));
}

static void
gpu_graphValidationError(const GPUComputePassEncoder *pass,
                         const char                  *message) {
#if GPU_BUILD_WITH_VALIDATION
  gpuDeviceRecordValidationError(gpu_graphPassDevice(pass), message);
#else
  GPU__UNUSED(pass);
  GPU__UNUSED(message);
#endif
}

static bool
gpu_graphBindingsComplete(const GPUComputePassEncoder *pass) {
#if GPU_BUILD_WITH_VALIDATION
  GPUDevice *device;

  device = gpu_graphPassDevice(pass);
  if (!gpuDeviceValidationEnabled(device)) {
    return true;
  }
  return gpuPipelineLayoutMaskIsBound(pass->_pipelineLayout,
                                      pass->_boundGroupLayouts,
                                      GPU_ENCODER_MAX_BIND_GROUPS,
                                      pass->_requiredBindGroupMask);
#else
  GPU__UNUSED(pass);
  return true;
#endif
}

GPU_EXPORT
GPUResult
GPUCreateExecutionGraphEXT(GPUDevice                            *device,
                           const GPUExecutionGraphCreateInfoEXT *info,
                           GPUExecutionGraphEXT                **outGraph) {
  GPUExecutionGraphEXT *graph;
  GPUApi               *api;
  const char           *entryPoints[USL_RUNTIME_MAX_ENTRY_POINTS];
  uint32_t              entryCount;
  GPUResult             result;

  if (!outGraph) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outGraph = NULL;
  if (!device || !info || !info->library || !info->layout ||
      (info->graphName && info->graphName[0] == '\0') ||
      !gpu_graphChainValid(&info->chain,
                           GPU_STRUCTURE_TYPE_EXECUTION_GRAPH_CREATE_INFO_EXT,
                           sizeof(*info)) ||
      info->library->_device != device ||
      info->layout->_device != device ||
      (info->cache && info->cache->device != device)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_EXECUTION_GRAPH)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  entryCount = gpuGetShaderLibraryExecutionGraphEntryCount(info->library);
  if (entryCount == 0u || entryCount > GPU_ARRAY_LEN(entryPoints)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  for (uint32_t i = 0u; i < entryCount; i++) {
    GPUShaderExecutionGraphEntryInfo entry;

    if (!gpuGetShaderLibraryExecutionGraphEntryAt(info->library, i, &entry)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    entryPoints[i] = entry.entryPoint;
  }
  api = gpuDeviceApi(device);
  if (!api || info->library->_api != api || !api->executionGraph.create) {
    return GPU_ERROR_UNSUPPORTED;
  }

  graph = calloc(1, sizeof(*graph));
  if (!graph) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  graph->_api    = api;
  graph->device  = device;
  graph->library = info->library;
  graph->layout  = info->layout;
  graph->refCount = 1u;
  if (!gpuPipelineLayoutMatchesShaderEntries(info->layout,
                                             info->library,
                                             entryPoints,
                                             entryCount,
                                             GPU_SHADER_STAGE_COMPUTE_BIT,
                                             &graph->requiredBindGroupMask)) {
    free(graph);
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  gpuGetPipelineLayoutPushConstants(info->layout,
                                    &graph->pushConstantSizeBytes,
                                    &graph->pushConstantStages);

  result = api->executionGraph.create(device, info, graph);
  if (result != GPU_OK ||
      !gpu_graphRequirementsValid(&graph->memoryRequirements)) {
    gpu_releaseExecutionGraph(graph);
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }

  *outGraph = graph;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyExecutionGraphEXT(GPUExecutionGraphEXT *graph) {
  gpu_releaseExecutionGraph(graph);
}

GPU_EXPORT
GPUResult
GPUGetExecutionGraphMemoryRequirementsEXT(
  const GPUExecutionGraphEXT             *graph,
  GPUExecutionGraphMemoryRequirementsEXT *outRequirements) {
  if (!graph || !outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outRequirements = graph->memoryRequirements;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateExecutionGraphInstanceEXT(
  GPUDevice                                     *device,
  const GPUExecutionGraphInstanceCreateInfoEXT  *info,
  GPUExecutionGraphInstanceEXT                 **outInstance) {
  GPUExecutionGraphInstanceCreateInfoEXT resolvedInfo;
  GPUExecutionGraphInstanceEXT          *instance;
  GPUApi                                *api;
  uint64_t                               memorySizeBytes;
  GPUResult                              result;

  if (!outInstance) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outInstance = NULL;
  if (!device || !info || !info->graph || info->graph->device != device ||
      !gpu_graphChainValid(
        &info->chain,
        GPU_STRUCTURE_TYPE_EXECUTION_GRAPH_INSTANCE_CREATE_INFO_EXT,
        sizeof(*info))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = info->graph->_api;
  if (!api || api != gpuDeviceApi(device) ||
      !api->executionGraph.createInstance) {
    return GPU_ERROR_UNSUPPORTED;
  }

  memorySizeBytes = info->memorySizeBytes > 0u
                      ? info->memorySizeBytes
                      : info->graph->memoryRequirements.minSizeBytes;
  if (!gpu_graphMemorySizeValid(&info->graph->memoryRequirements,
                                memorySizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  instance = calloc(1, sizeof(*instance));
  if (!instance) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  instance->_api            = api;
  instance->device          = device;
  instance->graph           = info->graph;
  instance->memorySizeBytes = memorySizeBytes;
  resolvedInfo              = *info;
  resolvedInfo.memorySizeBytes = memorySizeBytes;
  result = api->executionGraph.createInstance(device,
                                               &resolvedInfo,
                                               instance);
  if (result != GPU_OK) {
    free(instance);
    return result;
  }
  info->graph->refCount++;
  *outInstance = instance;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyExecutionGraphInstanceEXT(GPUExecutionGraphInstanceEXT *instance) {
  GPUExecutionGraphEXT *graph;
  GPUApi                *api;

  if (!instance) {
    return;
  }
  graph = instance->graph;
  api   = instance->_api;
  if (api && api->executionGraph.destroyInstance) {
    api->executionGraph.destroyInstance(instance);
  }
  free(instance);
  gpu_releaseExecutionGraph(graph);
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
  if (!gpu_graphEntryValid(outEntry)) {
    memset(outEntry, 0, sizeof(*outEntry));
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return GPU_OK;
}

GPU_EXPORT
void
GPUBindExecutionGraphEXT(GPUComputePassEncoder *pass,
                         GPUExecutionGraphEXT  *graph) {
  GPUApi *api;

  if (!pass || pass->_ended || !graph) {
    return;
  }
  if (graph->device != gpu_graphPassDevice(pass) ||
      graph->_api != gpu_graphPassApi(pass)) {
    gpu_graphValidationError(pass,
                             "GPUBindExecutionGraphEXT skipped: device mismatch");
    return;
  }
  api = graph->_api;
  if (!api || !api->executionGraph.bind) {
    return;
  }

  gpuFrameStatsRecordBindRequest(pass->_stats);
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

  api->executionGraph.bind(pass, graph);
  gpuFrameStatsRecordBindEmission(pass->_stats);
  pass->_pipeline                = graph;
  pass->_pipelineLayout          = graph->layout;
  pass->_requiredBindGroupMask   = graph->requiredBindGroupMask;
  pass->_pushConstantSizeBytes   = graph->pushConstantSizeBytes;
  pass->_pushConstantStages      = graph->pushConstantStages &
                                   GPU_SHADER_STAGE_COMPUTE_BIT;
  pass->_hasPipeline             = true;
  pass->_executionGraph          = true;
  pass->_pushConstantsEmitted    = false;
  if (pass->_pushConstantSizeBytes > 0u) {
    memset(pass->_pushConstants, 0, pass->_pushConstantSizeBytes);
  }
}

static bool
gpu_graphDispatchReady(GPUComputePassEncoder        *pass,
                       GPUExecutionGraphInstanceEXT *instance,
                       const char                   *name) {
  if (!pass || pass->_ended || !instance) {
    return false;
  }
  if (!pass->_hasPipeline || !pass->_executionGraph) {
    gpu_graphValidationError(pass, name);
    return false;
  }
  if (instance->device != gpu_graphPassDevice(pass) ||
      instance->_api != gpu_graphPassApi(pass) ||
      instance->graph != (GPUExecutionGraphEXT *)pass->_pipeline) {
    gpu_graphValidationError(pass,
                             "execution graph dispatch skipped: instance mismatch");
    return false;
  }
  if (!gpu_graphBindingsComplete(pass)) {
    gpu_graphValidationError(pass,
                             "execution graph dispatch skipped: missing bind group");
    return false;
  }

  return true;
}

GPU_EXPORT
void
GPUDispatchExecutionGraphEXT(GPUComputePassEncoder           *pass,
                             GPUExecutionGraphInstanceEXT    *instance,
                             uint32_t                         inputCount,
                             const GPUExecutionGraphInputEXT *pInputs) {
  GPUApi *api;

  if (!gpu_graphDispatchReady(
        pass,
        instance,
        "GPUDispatchExecutionGraphEXT skipped: no execution graph bound")) {
    return;
  }
  if (inputCount == 0u || !pInputs) {
    gpu_graphValidationError(pass,
                             "GPUDispatchExecutionGraphEXT skipped: no inputs");
    return;
  }
  for (uint32_t i = 0u; i < inputCount; i++) {
    const GPUExecutionGraphInputEXT *input;
    uint64_t                         sizeBytes;

    input = &pInputs[i];
    if (!gpu_graphInputSize(&input->entry,
                            input->recordCount,
                            input->recordStrideBytes,
                            &sizeBytes) ||
        (sizeBytes > 0u &&
         (!input->pRecords ||
          ((uintptr_t)input->pRecords &
           (input->entry.recordAlignmentBytes - 1u)) != 0u))) {
      gpu_graphValidationError(
        pass,
        "GPUDispatchExecutionGraphEXT skipped: invalid input"
      );
      return;
    }
  }
  api = gpu_graphPassApi(pass);
  if (!api || !api->executionGraph.dispatch) {
    return;
  }

  api->executionGraph.dispatch(pass, instance, inputCount, pInputs);
}

GPU_EXPORT
void
GPUDispatchExecutionGraphBufferEXT(
  GPUComputePassEncoder                 *pass,
  GPUExecutionGraphInstanceEXT          *instance,
  uint32_t                               inputCount,
  const GPUExecutionGraphBufferInputEXT *pInputs) {
  GPUApi *api;

  if (!gpu_graphDispatchReady(
        pass,
        instance,
        "GPUDispatchExecutionGraphBufferEXT skipped: no execution graph bound")) {
    return;
  }
  if (inputCount == 0u || !pInputs) {
    gpu_graphValidationError(
      pass,
      "GPUDispatchExecutionGraphBufferEXT skipped: no inputs"
    );
    return;
  }
  for (uint32_t i = 0u; i < inputCount; i++) {
    const GPUExecutionGraphBufferInputEXT *input;
    uint64_t                               sizeBytes;

    input = &pInputs[i];
    if (!input->records || input->records->device != instance->device ||
        !gpuBufferHasUsage(input->records,
                           GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT |
                           GPU_BUFFER_USAGE_INDIRECT) ||
        !gpu_graphInputSize(&input->entry,
                            input->recordCount,
                            input->recordStrideBytes,
                            &sizeBytes) ||
        (input->recordOffset &
         (input->entry.recordAlignmentBytes - 1u)) != 0u ||
        (sizeBytes > 0u
           ? !gpuBufferRangeValid(input->records,
                                  input->recordOffset,
                                  sizeBytes)
           : !gpuBufferOffsetValid(input->records, input->recordOffset))) {
      gpu_graphValidationError(
        pass,
        "GPUDispatchExecutionGraphBufferEXT skipped: invalid input"
      );
      return;
    }
  }
  api = gpu_graphPassApi(pass);
  if (!api || !api->executionGraph.dispatchBuffer) {
    return;
  }

  api->executionGraph.dispatchBuffer(pass, instance, inputCount, pInputs);
}
