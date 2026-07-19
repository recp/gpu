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
#include "cmdqueue_internal.h"
#include "descr/descriptor_internal.h"
#include "device_internal.h"
#include "library_internal.h"
#include "pipeline_cache_internal.h"
#include "ray_internal.h"

enum {
  GPU_ACCELERATION_STRUCTURE_BUILD_FLAGS_EXT =
    GPU_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_EXT |
    GPU_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_EXT |
    GPU_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_EXT,
  GPU_ACCELERATION_STRUCTURE_GEOMETRY_FLAGS_EXT =
    GPU_ACCELERATION_STRUCTURE_GEOMETRY_NON_OPAQUE_BIT_EXT,
  GPU_ACCELERATION_STRUCTURE_INSTANCE_FLAGS_EXT =
    GPU_ACCELERATION_STRUCTURE_INSTANCE_DISABLE_CULL_BIT_EXT |
    GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_OPAQUE_BIT_EXT |
    GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_NON_OPAQUE_BIT_EXT
};

static const GPUShaderStageFlags GPU_RAY_TRACING_GENERAL_STAGE_FLAGS_EXT =
  GPU_SHADER_STAGE_RAY_GENERATION_BIT |
  GPU_SHADER_STAGE_MISS_BIT |
  GPU_SHADER_STAGE_CALLABLE_BIT;

static const GPUShaderStageFlags GPU_RAY_TRACING_STAGE_FLAGS_EXT =
  GPU_SHADER_STAGE_RAY_GENERATION_BIT |
  GPU_SHADER_STAGE_MISS_BIT |
  GPU_SHADER_STAGE_CALLABLE_BIT |
  GPU_SHADER_STAGE_CLOSEST_HIT_BIT |
  GPU_SHADER_STAGE_ANY_HIT_BIT |
  GPU_SHADER_STAGE_INTERSECTION_BIT;

static bool
gpu_rayTypeValid(GPUAccelerationStructureTypeEXT type) {
  return type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT ||
         type == GPU_ACCELERATION_STRUCTURE_TOP_LEVEL_EXT;
}

static bool
gpu_rayChainValid(const GPUChainedStruct *chain,
                  GPUStructureType        type,
                  size_t                  size) {
  return chain &&
         (chain->sType == GPU_STRUCTURE_TYPE_NONE || chain->sType == type) &&
         (chain->structSize == 0u || chain->structSize >= size) &&
         chain->pNext == NULL;
}

static bool
gpu_rayRangeValid(const GPUBuffer *buffer,
                  uint64_t         offset,
                  uint64_t         count,
                  uint64_t         stride,
                  uint64_t         elementSize) {
  uint64_t sizeBytes;

  if (!buffer || count == 0u || stride < elementSize ||
      count - 1u > (UINT64_MAX - elementSize) / stride) {
    return false;
  }

  sizeBytes = (count - 1u) * stride + elementSize;
  return gpuBufferRangeValid(buffer, offset, sizeBytes);
}

static bool
gpu_rayTriangleValid(GPUDevice *device,
                     const GPUAccelerationStructureTriangleGeometryEXT *geometry) {
  uint64_t indexSize;

  if (!geometry || !geometry->vertexBuffer ||
      geometry->vertexBuffer->device != device ||
      !gpuBufferHasUsage(
        geometry->vertexBuffer,
        GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT) ||
      geometry->vertexFormat != GPU_VERTEX_FORMAT_FLOAT32X3 ||
      geometry->vertexCount == 0u ||
      geometry->vertexStride < 12u ||
      (geometry->flags & ~GPU_ACCELERATION_STRUCTURE_GEOMETRY_FLAGS_EXT) != 0u ||
      !gpu_rayRangeValid(geometry->vertexBuffer,
                         geometry->vertexOffset,
                         geometry->vertexCount,
                         geometry->vertexStride,
                         12u)) {
    return false;
  }

  if (!geometry->indexBuffer) {
    return geometry->vertexCount >= 3u &&
           geometry->vertexCount % 3u == 0u &&
           geometry->indexCount == 0u && geometry->indexOffset == 0u;
  }
  if (geometry->indexBuffer->device != device ||
      !gpuBufferHasUsage(
        geometry->indexBuffer,
        GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT) ||
      geometry->indexCount < 3u || geometry->indexCount % 3u != 0u ||
      (geometry->indexType != GPU_INDEX_TYPE_UINT16 &&
       geometry->indexType != GPU_INDEX_TYPE_UINT32)) {
    return false;
  }

  indexSize = geometry->indexType == GPU_INDEX_TYPE_UINT32 ? 4u : 2u;
  return gpu_rayRangeValid(geometry->indexBuffer,
                           geometry->indexOffset,
                           geometry->indexCount,
                           indexSize,
                           indexSize);
}

static bool
gpu_rayInstanceValid(GPUDevice *device,
                     const GPUAccelerationStructureInstanceEXT *instance) {
  if (!instance || !instance->structure ||
      instance->structure->device != device ||
      instance->structure->type !=
        GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT ||
      (instance->flags & ~GPU_ACCELERATION_STRUCTURE_INSTANCE_FLAGS_EXT) != 0u) {
    return false;
  }

  return (instance->flags &
          (GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_OPAQUE_BIT_EXT |
           GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_NON_OPAQUE_BIT_EXT)) !=
         (GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_OPAQUE_BIT_EXT |
          GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_NON_OPAQUE_BIT_EXT);
}

static GPUResult
gpu_validateRayBuildInfo(GPUDevice *device,
                         const GPUAccelerationStructureBuildInfoEXT *info) {
  if (!device || !info ||
      !gpu_rayChainValid(
        &info->chain,
        GPU_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_INFO_EXT,
        sizeof(*info)) ||
      !gpu_rayTypeValid(info->type) ||
      (info->flags & ~GPU_ACCELERATION_STRUCTURE_BUILD_FLAGS_EXT) != 0u ||
      (info->mode != GPU_ACCELERATION_STRUCTURE_BUILD_EXT &&
       info->mode != GPU_ACCELERATION_STRUCTURE_UPDATE_EXT)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->mode == GPU_ACCELERATION_STRUCTURE_BUILD_EXT && info->source) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->mode == GPU_ACCELERATION_STRUCTURE_UPDATE_EXT &&
      (!info->source || info->source->device != device ||
       info->source->type != info->type ||
       (info->flags & GPU_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_EXT) == 0u)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info->type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT) {
    if (!info->bottomLevel.pGeometries ||
        info->bottomLevel.geometryCount == 0u) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0u; i < info->bottomLevel.geometryCount; i++) {
      if (!gpu_rayTriangleValid(device, &info->bottomLevel.pGeometries[i])) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }
    }
  } else {
    if (!info->topLevel.pInstances || info->topLevel.instanceCount == 0u) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0u; i < info->topLevel.instanceCount; i++) {
      if (!gpu_rayInstanceValid(device, &info->topLevel.pInstances[i])) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }
    }
  }

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetAccelerationStructureSizesEXT(
  GPUDevice                                    *device,
  const GPUAccelerationStructureBuildInfoEXT  *info,
  GPUAccelerationStructureSizesEXT            *outSizes) {
  GPUApi    *api;
  GPUResult  result;

  if (!outSizes) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outSizes, 0, sizeof(*outSizes));

  result = gpu_validateRayBuildInfo(device, info);
  if (result != GPU_OK) {
    return result;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_RAY_QUERY)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!(api = gpuDeviceApi(device)) || !api->rayQuery.getSizes) {
    return GPU_ERROR_UNSUPPORTED;
  }

  return api->rayQuery.getSizes(device, info, outSizes);
}

GPU_EXPORT
GPUResult
GPUCreateAccelerationStructureEXT(
  GPUDevice                                    *device,
  const GPUAccelerationStructureCreateInfoEXT *info,
  GPUAccelerationStructureEXT                **outStructure) {
  GPUAccelerationStructureEXT *structure;
  GPUApi                       *api;
  GPUResult                     result;

  if (!outStructure) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outStructure = NULL;
  if (!device || !info || info->sizeBytes == 0u ||
      !gpu_rayTypeValid(info->type) ||
      !gpu_rayChainValid(
        &info->chain,
        GPU_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_EXT,
        sizeof(*info))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_RAY_QUERY)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!(api = gpuDeviceApi(device)) || !api->rayQuery.create) {
    return GPU_ERROR_UNSUPPORTED;
  }

  structure = calloc(1, sizeof(*structure));
  if (!structure) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  structure->device    = device;
  structure->sizeBytes = info->sizeBytes;
  structure->type      = info->type;
  result = api->rayQuery.create(device, info, structure);
  if (result != GPU_OK) {
    free(structure);
    return result;
  }

  *outStructure = structure;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyAccelerationStructureEXT(GPUAccelerationStructureEXT *structure) {
  GPUApi *api;

  if (!structure) {
    return;
  }
  api = gpuDeviceApi(structure->device);
  if (api && api->rayQuery.destroy) {
    api->rayQuery.destroy(structure);
  }
  free(structure);
}

GPU_EXPORT
GPUAccelerationStructurePassEncoderEXT *
GPUBeginAccelerationStructurePassEXT(GPUCommandBuffer *cmdb,
                                     const char       *label) {
  GPUAccelerationStructurePassEncoderEXT *pass;
  GPUDevice                              *device;
  GPUApi                                 *api;

  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder) {
    return NULL;
  }
  device = gpuCommandBufferDevice(cmdb);
  if (!device || !GPUIsFeatureEnabled(device, GPU_FEATURE_RAY_QUERY) ||
      !(api = gpuDeviceApi(device)) || !api->rayQuery.beginPass) {
    return NULL;
  }

  label = gpuDeviceDebugLabel(device, label);
  pass  = api->rayQuery.beginPass(cmdb, label);
  if (!pass) {
    return NULL;
  }
  pass->_api   = api;
  pass->device = device;
  pass->cmdb   = cmdb;
  cmdb->_activeEncoder = true;
  return pass;
}

GPU_EXPORT
GPUResult
GPUBuildAccelerationStructureEXT(
  GPUAccelerationStructurePassEncoderEXT     *pass,
  GPUAccelerationStructureEXT                *dst,
  const GPUAccelerationStructureBuildInfoEXT *info,
  GPUBuffer                                   *scratchBuffer,
  uint64_t                                     scratchOffset) {
  GPUResult result;

  if (!pass || pass->ended || !dst || dst->device != pass->device ||
      !scratchBuffer || scratchBuffer->device != pass->device ||
      !gpuBufferHasUsage(
        scratchBuffer,
        GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT) ||
      !gpuBufferOffsetValid(scratchBuffer, scratchOffset) ||
      scratchOffset == scratchBuffer->sizeBytes) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = gpu_validateRayBuildInfo(pass->device, info);
  if (result != GPU_OK) {
    return result;
  }
  if (dst->type != info->type) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!pass->_api || !pass->_api->rayQuery.build) {
    return GPU_ERROR_UNSUPPORTED;
  }

  return pass->_api->rayQuery.build(pass,
                                    dst,
                                    info,
                                    scratchBuffer,
                                    scratchOffset);
}

GPU_EXPORT
void
GPUEndAccelerationStructurePassEXT(
  GPUAccelerationStructurePassEncoderEXT *pass) {
  if (!pass || pass->ended) {
    return;
  }
  if (pass->_api && pass->_api->rayQuery.endPass) {
    pass->_api->rayQuery.endPass(pass);
  }
  pass->ended = true;
  if (pass->cmdb) {
    pass->cmdb->_activeEncoder = false;
  }
}

static bool
gpu_rayTracingGeneralStageValid(GPUShaderStageFlags stage) {
  return stage != 0u &&
         (stage & (stage - 1u)) == 0u &&
         (stage & GPU_RAY_TRACING_GENERAL_STAGE_FLAGS_EXT) != 0u;
}

static bool
gpu_rayTracingEntryStage(const GPUShaderLibrary *library,
                         const char             *entry,
                         GPUShaderStageFlags     requested,
                         GPUShaderStageFlags     expected,
                         GPUShaderStageFlags    *outStage) {
  GPUShaderStageFlags reflected;

  if (!library || !entry || !entry[0] || !outStage) {
    return false;
  }

  reflected = 0u;
  if (gpuGetShaderLibraryEntryStage(library, entry, &reflected)) {
    if ((requested && requested != reflected) ||
        (expected && expected != reflected)) {
      return false;
    }
    *outStage = reflected;
    return true;
  }

  if (requested && (!expected || requested == expected)) {
    *outStage = requested;
    return true;
  }
  if (expected) {
    *outStage = expected;
    return true;
  }
  return false;
}

static bool
gpu_rayTracingGroupValid(const GPUShaderLibrary          *library,
                         const GPURayTracingShaderGroupEXT *group,
                         GPUShaderStageFlags              *outGeneralStage) {
  GPUShaderStageFlags stage;

  if (!library || !group || !outGeneralStage) {
    return false;
  }
  *outGeneralStage = 0u;

  switch (group->type) {
    case GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT:
      if (!group->generalEntry || group->closestHitEntry ||
          group->anyHitEntry || group->intersectionEntry) {
        return false;
      }
      if (!gpu_rayTracingEntryStage(library,
                                    group->generalEntry,
                                    group->generalStage,
                                    0u,
                                    &stage) ||
          !gpu_rayTracingGeneralStageValid(stage)) {
        return false;
      }
      *outGeneralStage = stage;
      return true;

    case GPU_RAY_TRACING_SHADER_GROUP_TRIANGLES_HIT_EXT:
      if (group->generalEntry || group->intersectionEntry ||
          group->generalStage ||
          (!group->closestHitEntry && !group->anyHitEntry)) {
        return false;
      }
      if (group->closestHitEntry &&
          !gpu_rayTracingEntryStage(library,
                                    group->closestHitEntry,
                                    0u,
                                    GPU_SHADER_STAGE_CLOSEST_HIT_BIT,
                                    &stage)) {
        return false;
      }
      return !group->anyHitEntry ||
             gpu_rayTracingEntryStage(library,
                                      group->anyHitEntry,
                                      0u,
                                      GPU_SHADER_STAGE_ANY_HIT_BIT,
                                      &stage);

    case GPU_RAY_TRACING_SHADER_GROUP_PROCEDURAL_HIT_EXT:
      if (group->generalEntry || group->generalStage ||
          !group->intersectionEntry) {
        return false;
      }
      if (!gpu_rayTracingEntryStage(library,
                                    group->intersectionEntry,
                                    0u,
                                    GPU_SHADER_STAGE_INTERSECTION_BIT,
                                    &stage) ||
          (group->closestHitEntry &&
           !gpu_rayTracingEntryStage(library,
                                     group->closestHitEntry,
                                     0u,
                                     GPU_SHADER_STAGE_CLOSEST_HIT_BIT,
                                     &stage)) ||
          (group->anyHitEntry &&
           !gpu_rayTracingEntryStage(library,
                                     group->anyHitEntry,
                                     0u,
                                     GPU_SHADER_STAGE_ANY_HIT_BIT,
                                     &stage))) {
        return false;
      }
      return true;
  }

  return false;
}

static uint32_t
gpu_rayTracingGroupEntryCount(const GPURayTracingShaderGroupEXT *group) {
  if (!group) {
    return 0u;
  }
  return (group->generalEntry ? 1u : 0u) +
         (group->closestHitEntry ? 1u : 0u) +
         (group->anyHitEntry ? 1u : 0u) +
         (group->intersectionEntry ? 1u : 0u);
}

static void
gpu_rayTracingGroupEntries(const GPURayTracingShaderGroupEXT *group,
                           const char                       **entries,
                           uint32_t                          *index) {
  if (group->generalEntry) entries[(*index)++] = group->generalEntry;
  if (group->closestHitEntry) entries[(*index)++] = group->closestHitEntry;
  if (group->anyHitEntry) entries[(*index)++] = group->anyHitEntry;
  if (group->intersectionEntry) entries[(*index)++] = group->intersectionEntry;
}

static void
gpu_accumulateRayInterface(const GPUShaderLibrary *library,
                           const char             *entry,
                           GPUShaderStageFlags     stage,
                           uint32_t               *maxPayloadSizeBytes,
                           uint32_t               *maxHitAttributeSizeBytes,
                           bool                   *payloadMetadataMissing,
                           bool                   *hitMetadataMissing,
                           bool                   *callableMetadataMissing) {
  uint32_t payloadSizeBytes;
  uint32_t hitAttributeSizeBytes;
  uint32_t callableDataSizeBytes;
  bool     requirePayload;
  bool     requireHitAttribute;
  bool     requireCallableData;
  bool     reflected;

  if (!entry) {
    return;
  }
  requirePayload = stage == GPU_SHADER_STAGE_MISS_BIT ||
                   stage == GPU_SHADER_STAGE_CLOSEST_HIT_BIT ||
                   stage == GPU_SHADER_STAGE_ANY_HIT_BIT;
  requireHitAttribute = stage == GPU_SHADER_STAGE_CLOSEST_HIT_BIT ||
                        stage == GPU_SHADER_STAGE_ANY_HIT_BIT;
  requireCallableData = stage == GPU_SHADER_STAGE_CALLABLE_BIT;
  reflected = gpuGetShaderLibraryRayInterfaceInfo(
    library,
    entry,
    stage,
    &payloadSizeBytes,
    &hitAttributeSizeBytes,
    &callableDataSizeBytes
  ) != 0;
  if (requirePayload && (!reflected || payloadSizeBytes == 0u)) {
    *payloadMetadataMissing = true;
  } else if (reflected && payloadSizeBytes > *maxPayloadSizeBytes) {
    *maxPayloadSizeBytes = payloadSizeBytes;
  }
  if (requireHitAttribute && (!reflected || hitAttributeSizeBytes == 0u)) {
    *hitMetadataMissing = true;
  } else if (reflected &&
             hitAttributeSizeBytes > *maxHitAttributeSizeBytes) {
    *maxHitAttributeSizeBytes = hitAttributeSizeBytes;
  }
  if (requireCallableData && reflected && callableDataSizeBytes == 0u) {
    *callableMetadataMissing = true;
  }
}

static bool
gpu_resolveRayInterfaceLimits(
  const GPUShaderLibrary            *library,
  const GPURayTracingShaderGroupEXT *groups,
  const GPUShaderStageFlags         *generalStages,
  uint32_t                           groupCount,
  uint32_t                           requestedPayloadSizeBytes,
  uint32_t                           requestedHitAttributeSizeBytes,
  uint32_t                          *outPayloadSizeBytes,
  uint32_t                          *outHitAttributeSizeBytes) {
  uint32_t reflectedPayloadSizeBytes;
  uint32_t reflectedHitAttributeSizeBytes;
  bool     payloadMetadataMissing;
  bool     hitMetadataMissing;
  bool     callableMetadataMissing;

  reflectedPayloadSizeBytes      = 0u;
  reflectedHitAttributeSizeBytes = 0u;
  payloadMetadataMissing         = false;
  hitMetadataMissing             = false;
  callableMetadataMissing        = false;

  for (uint32_t i = 0u; i < groupCount; i++) {
    const GPURayTracingShaderGroupEXT *group;

    group = &groups[i];
    if (group->type == GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT) {
      GPUShaderStageFlags stage;

      stage = generalStages[i];
      gpu_accumulateRayInterface(library,
                                 group->generalEntry,
                                 stage,
                                 &reflectedPayloadSizeBytes,
                                 &reflectedHitAttributeSizeBytes,
                                 &payloadMetadataMissing,
                                 &hitMetadataMissing,
                                 &callableMetadataMissing);
      continue;
    }

    gpu_accumulateRayInterface(library,
                               group->closestHitEntry,
                               GPU_SHADER_STAGE_CLOSEST_HIT_BIT,
                               &reflectedPayloadSizeBytes,
                               &reflectedHitAttributeSizeBytes,
                               &payloadMetadataMissing,
                               &hitMetadataMissing,
                               &callableMetadataMissing);
    gpu_accumulateRayInterface(library,
                               group->anyHitEntry,
                               GPU_SHADER_STAGE_ANY_HIT_BIT,
                               &reflectedPayloadSizeBytes,
                               &reflectedHitAttributeSizeBytes,
                               &payloadMetadataMissing,
                               &hitMetadataMissing,
                               &callableMetadataMissing);
    gpu_accumulateRayInterface(library,
                               group->intersectionEntry,
                               GPU_SHADER_STAGE_INTERSECTION_BIT,
                               &reflectedPayloadSizeBytes,
                               &reflectedHitAttributeSizeBytes,
                               &payloadMetadataMissing,
                               &hitMetadataMissing,
                               &callableMetadataMissing);
  }

  if ((requestedPayloadSizeBytes == 0u && payloadMetadataMissing) ||
      (requestedHitAttributeSizeBytes == 0u && hitMetadataMissing) ||
      callableMetadataMissing ||
      (requestedPayloadSizeBytes > 0u &&
       requestedPayloadSizeBytes < reflectedPayloadSizeBytes) ||
      (requestedHitAttributeSizeBytes > 0u &&
       requestedHitAttributeSizeBytes < reflectedHitAttributeSizeBytes)) {
    return false;
  }

  *outPayloadSizeBytes = requestedPayloadSizeBytes > 0u
                           ? requestedPayloadSizeBytes
                           : reflectedPayloadSizeBytes;
  *outHitAttributeSizeBytes = requestedHitAttributeSizeBytes > 0u
                                ? requestedHitAttributeSizeBytes
                                : reflectedHitAttributeSizeBytes;
  return true;
}

GPU_HIDE
void
gpuRetainRayTracingPipeline(GPURayTracingPipelineEXT *pipeline) {
  if (!pipeline) {
    return;
  }
#if defined(_WIN32) || defined(WIN32)
  InterlockedIncrement((volatile LONG *)&pipeline->refCount);
#else
  __atomic_add_fetch(&pipeline->refCount, 1u, __ATOMIC_RELAXED);
#endif
}

static void
gpu_releaseRayTracingPipeline(GPURayTracingPipelineEXT *pipeline) {
  GPUApi *api;

  if (!pipeline) {
    return;
  }
#if defined(_WIN32) || defined(WIN32)
  if (InterlockedDecrement((volatile LONG *)&pipeline->refCount) != 0) {
    return;
  }
#else
  if (__atomic_sub_fetch(&pipeline->refCount, 1u, __ATOMIC_ACQ_REL) != 0u) {
    return;
  }
#endif
  api = pipeline->_api;
  if (api && api->rayTracing.destroyPipeline) {
    api->rayTracing.destroyPipeline(pipeline);
  }
  free(pipeline->generalStages);
  free(pipeline->groupTypes);
  free(pipeline);
}

GPU_EXPORT
GPUResult
GPUCreateRayTracingPipelineEXT(GPUDevice                                *device,
                               const GPURayTracingPipelineCreateInfoEXT *info,
                               GPURayTracingPipelineEXT                **outPipeline) {
  GPURayTracingPipelineEXT           *pipeline;
  GPURayTracingShaderGroupTypeEXT    *groupTypes;
  GPUShaderStageFlags                *generalStages;
  const char                        **entries;
  GPUApi                             *api;
  GPURayTracingPipelineCreateInfoEXT resolvedInfo;
  GPUPipelineCacheKey                 cacheKey;
  uint32_t                           entryCount;
  uint32_t                           entryIndex;
  uint32_t                           requiredBindGroupMask;
  uint32_t                           maxPayloadSizeBytes;
  uint32_t                           maxHitAttributeSizeBytes;
  GPUResult                          result;
  bool                               hasRayGeneration;

  if (!outPipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outPipeline = NULL;
  memset(&cacheKey, 0, sizeof(cacheKey));
  if (!device || !info || !info->library || !info->layout ||
      !info->pGroups || info->groupCount == 0u ||
      info->maxRecursionDepth == 0u ||
      !gpu_rayChainValid(
        &info->chain,
        GPU_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_EXT,
        sizeof(*info)) ||
      info->library->_device != device ||
      info->layout->_device != device ||
      (info->cache && info->cache->device != device)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_RAY_TRACING_PIPELINE)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!device->rayTracingLimits.maxRecursionDepth ||
      info->maxRecursionDepth > device->rayTracingLimits.maxRecursionDepth) {
    return GPU_ERROR_UNSUPPORTED;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->rayTracing.createPipeline) {
    return GPU_ERROR_UNSUPPORTED;
  }

  entryCount       = 0u;
  hasRayGeneration = false;
  for (uint32_t i = 0u; i < info->groupCount; i++) {
    uint32_t count;

    count = gpu_rayTracingGroupEntryCount(&info->pGroups[i]);
    if (count == 0u || entryCount > UINT32_MAX - count) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    entryCount += count;
  }
  if ((size_t)entryCount > SIZE_MAX / sizeof(*entries) ||
      (size_t)info->groupCount > SIZE_MAX / sizeof(*generalStages) ||
      (size_t)info->groupCount > SIZE_MAX / sizeof(*groupTypes)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  entries       = calloc(entryCount, sizeof(*entries));
  generalStages = calloc(info->groupCount, sizeof(*generalStages));
  groupTypes    = calloc(info->groupCount, sizeof(*groupTypes));
  if (!entries || !generalStages || !groupTypes) {
    free(groupTypes);
    free(generalStages);
    free(entries);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  entryIndex = 0u;
  for (uint32_t i = 0u; i < info->groupCount; i++) {
    if (!gpu_rayTracingGroupValid(info->library,
                                  &info->pGroups[i],
                                  &generalStages[i])) {
      free(groupTypes);
      free(generalStages);
      free(entries);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    groupTypes[i] = info->pGroups[i].type;
    hasRayGeneration |= generalStages[i] ==
                          GPU_SHADER_STAGE_RAY_GENERATION_BIT;
    gpu_rayTracingGroupEntries(&info->pGroups[i], entries, &entryIndex);
  }
  if (!hasRayGeneration ||
      !gpuPipelineLayoutMatchesShaderEntries(info->layout,
                                             info->library,
                                             entries,
                                             entryCount,
                                             GPU_RAY_TRACING_STAGE_FLAGS_EXT,
                                             &requiredBindGroupMask)) {
    free(groupTypes);
    free(generalStages);
    free(entries);
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!gpu_resolveRayInterfaceLimits(info->library,
                                     info->pGroups,
                                     generalStages,
                                     info->groupCount,
                                     info->maxPayloadSizeBytes,
                                     info->maxHitAttributeSizeBytes,
                                     &maxPayloadSizeBytes,
                                     &maxHitAttributeSizeBytes)) {
    free(groupTypes);
    free(generalStages);
    free(entries);
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (device->rayTracingLimits.maxHitAttributeSizeBytes &&
      maxHitAttributeSizeBytes >
        device->rayTracingLimits.maxHitAttributeSizeBytes) {
    free(groupTypes);
    free(generalStages);
    free(entries);
    return GPU_ERROR_UNSUPPORTED;
  }
  if (info->cache && !info->chain.pNext) {
    result = gpuPipelineCacheFindRay(info->cache,
                                     info,
                                     &cacheKey,
                                     &pipeline);
    if (result != GPU_OK) {
      free(groupTypes);
      free(generalStages);
      free(entries);
      return result;
    }
    if (pipeline) {
      gpuPipelineCacheReleaseKey(&cacheKey);
      free(groupTypes);
      free(generalStages);
      free(entries);
      *outPipeline = pipeline;
      return GPU_OK;
    }
  }
  free(entries);

  pipeline = calloc(1, sizeof(*pipeline));
  if (!pipeline) {
    gpuPipelineCacheReleaseKey(&cacheKey);
    free(groupTypes);
    free(generalStages);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  pipeline->_api                     = api;
  pipeline->device                   = device;
  pipeline->layout                   = info->layout;
  pipeline->groupTypes               = groupTypes;
  pipeline->generalStages            = generalStages;
  pipeline->requiredBindGroupMask    = requiredBindGroupMask;
  pipeline->groupCount               = info->groupCount;
  pipeline->maxPayloadSizeBytes      = maxPayloadSizeBytes;
  pipeline->maxHitAttributeSizeBytes = maxHitAttributeSizeBytes;
  pipeline->refCount                 = 1u;
  resolvedInfo                          = *info;
  resolvedInfo.maxPayloadSizeBytes      = maxPayloadSizeBytes;
  resolvedInfo.maxHitAttributeSizeBytes = maxHitAttributeSizeBytes;
  result = api->rayTracing.createPipeline(device, &resolvedInfo, pipeline);
  if (result != GPU_OK) {
    gpuPipelineCacheReleaseKey(&cacheKey);
    gpu_releaseRayTracingPipeline(pipeline);
    return result;
  }

  if (info->cache && !info->chain.pNext) {
    pipeline = gpuPipelineCacheStoreRay(info->cache, &cacheKey, pipeline);
  } else {
    gpuRecordPipelineCompile(device, info->cache);
  }
  *outPipeline = pipeline;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyRayTracingPipelineEXT(GPURayTracingPipelineEXT *pipeline) {
  gpu_releaseRayTracingPipeline(pipeline);
}

static bool
gpu_shaderTableRecordValid(const GPURayTracingPipelineEXT *pipeline,
                           const GPUShaderTableRecordEXT   *record,
                           GPURayTracingShaderGroupTypeEXT  type,
                           GPUShaderStageFlags              stage) {
  uint32_t index;

  if (!pipeline || !record || record->groupIndex >= pipeline->groupCount) {
    return false;
  }
  index = record->groupIndex;
  return pipeline->groupTypes[index] == type &&
         (!stage || pipeline->generalStages[index] == stage);
}

GPU_EXPORT
GPUResult
GPUCreateShaderTableEXT(GPUDevice                         *device,
                        const GPUShaderTableCreateInfoEXT *info,
                        GPUShaderTableEXT                **outTable) {
  GPUShaderTableEXT *table;
  GPUApi            *api;
  GPUResult          result;

  if (!outTable) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outTable = NULL;
  if (!device || !info || !info->pipeline ||
      info->pipeline->device != device ||
      !gpu_rayChainValid(&info->chain,
                         GPU_STRUCTURE_TYPE_SHADER_TABLE_CREATE_INFO_EXT,
                         sizeof(*info)) ||
      !gpu_shaderTableRecordValid(
        info->pipeline,
        info->pRayGenerationRecord,
        GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT,
        GPU_SHADER_STAGE_RAY_GENERATION_BIT) ||
      (info->missRecordCount > 0u && !info->pMissRecords) ||
      (info->hitGroupRecordCount > 0u && !info->pHitGroupRecords) ||
      (info->callableRecordCount > 0u && !info->pCallableRecords)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  for (uint32_t i = 0u; i < info->missRecordCount; i++) {
    if (!gpu_shaderTableRecordValid(
          info->pipeline,
          &info->pMissRecords[i],
          GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT,
          GPU_SHADER_STAGE_MISS_BIT)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }
  for (uint32_t i = 0u; i < info->hitGroupRecordCount; i++) {
    const GPUShaderTableRecordEXT *record;

    record = &info->pHitGroupRecords[i];
    if (record->groupIndex >= info->pipeline->groupCount ||
        info->pipeline->groupTypes[record->groupIndex] ==
          GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }
  for (uint32_t i = 0u; i < info->callableRecordCount; i++) {
    if (!gpu_shaderTableRecordValid(
          info->pipeline,
          &info->pCallableRecords[i],
          GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT,
          GPU_SHADER_STAGE_CALLABLE_BIT)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  api = info->pipeline->_api;
  if (!api || !api->rayTracing.createShaderTable) {
    return GPU_ERROR_UNSUPPORTED;
  }
  table = calloc(1, sizeof(*table));
  if (!table) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  table->_api     = api;
  table->device   = device;
  table->pipeline = info->pipeline;
  result = api->rayTracing.createShaderTable(device, info, table);
  if (result != GPU_OK) {
    free(table);
    return result;
  }
  gpuRetainRayTracingPipeline(info->pipeline);
  *outTable = table;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyShaderTableEXT(GPUShaderTableEXT *table) {
  GPURayTracingPipelineEXT *pipeline;
  GPUApi                   *api;

  if (!table) {
    return;
  }
  pipeline = table->pipeline;
  api      = table->_api;
  if (api && api->rayTracing.destroyShaderTable) {
    api->rayTracing.destroyShaderTable(table);
  }
  free(table);
  gpu_releaseRayTracingPipeline(pipeline);
}

GPU_EXPORT
GPURayTracingPassEncoderEXT *
GPUBeginRayTracingPassEXT(GPUCommandBuffer *cmdb, const char *label) {
  GPURayTracingPassEncoderEXT *pass;
  GPUDevice                   *device;
  GPUApi                      *api;

  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder) {
    return NULL;
  }
  device = gpuCommandBufferDevice(cmdb);
  if (!device ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_RAY_TRACING_PIPELINE) ||
      !(api = gpuDeviceApi(device)) || !api->rayTracing.beginPass) {
    return NULL;
  }

  label = gpuDeviceDebugLabel(device, label);
  pass  = api->rayTracing.beginPass(cmdb, label);
  if (!pass) {
    return NULL;
  }
  pass->_api   = api;
  pass->device = device;
  pass->cmdb   = cmdb;
  pass->stats  = device->runtimeConfig.enableStats
                   ? &device->currentFrameStats
                   : NULL;
  cmdb->_activeEncoder = true;
  return pass;
}

GPU_EXPORT
void
GPUBindRayTracingPipelineEXT(GPURayTracingPassEncoderEXT *pass,
                             GPURayTracingPipelineEXT    *pipeline) {
  GPUApi *api;

  if (!pass || pass->ended || !pipeline ||
      pipeline->device != pass->device || pipeline->_api != pass->_api ||
      !(api = pass->_api) || !api->rayTracing.bindPipeline) {
    return;
  }
  gpuFrameStatsRecordBindRequest(pass->stats);
  if (pass->_pipeline == pipeline) {
    return;
  }
  if (pass->pipelineLayout != pipeline->layout) {
    memset(pass->boundGroups, 0, sizeof(pass->boundGroups));
    memset(pass->boundGroupLayouts, 0, sizeof(pass->boundGroupLayouts));
    memset(pass->boundDynamicOffsetCounts,
           0,
           sizeof(pass->boundDynamicOffsetCounts));
  }
  api->rayTracing.bindPipeline(pass, pipeline);
  gpuFrameStatsRecordBindEmission(pass->stats);
  pass->_pipeline             = pipeline;
  pass->pipelineLayout        = pipeline->layout;
  pass->requiredBindGroupMask = pipeline->requiredBindGroupMask;
  pass->hasPipeline           = true;
}

GPU_EXPORT
void
GPUBindRayTracingGroupEXT(GPURayTracingPassEncoderEXT *pass,
                          uint32_t                      groupIndex,
                          GPUBindGroup                 *group,
                          uint32_t                      dynamicOffsetCount,
                          const uint32_t               *pDynamicOffsets) {
  GPUApi *api;

  if (!pass || pass->ended || !pass->hasPipeline || !group ||
      groupIndex >= GPU_ENCODER_MAX_BIND_GROUPS ||
      gpuBindGroupGetDevice(group) != pass->device ||
      !gpuPipelineLayoutAcceptsBindGroup(pass->pipelineLayout,
                                         groupIndex,
                                         group) ||
      gpuBindGroupShadowMatches(
        pass->boundGroups[groupIndex],
        pass->boundDynamicOffsetCounts[groupIndex],
        pass->boundDynamicOffsets[groupIndex],
        group,
        dynamicOffsetCount,
        pDynamicOffsets)) {
    if (pass && !pass->ended) {
      gpuFrameStatsRecordBindRequest(pass->stats);
    }
    return;
  }
  api = pass->_api;
  if (!api || !api->rayTracing.bindGroup ||
      !gpuValidateBindGroupDynamicOffsets(pass->pipelineLayout,
                                          groupIndex,
                                          group,
                                          dynamicOffsetCount,
                                          pDynamicOffsets)) {
    return;
  }

  gpuFrameStatsRecordBindRequest(pass->stats);
  if (api->rayTracing.bindGroup(pass,
                                pass->pipelineLayout,
                                groupIndex,
                                group,
                                dynamicOffsetCount,
                                pDynamicOffsets)) {
    if (pass->boundGroups[groupIndex] != group) {
      pass->boundGroupLayouts[groupIndex] = gpuBindGroupGetLayout(group);
    }
    pass->boundGroups[groupIndex] = group;
    gpuStoreBindGroupShadow(
      &pass->boundDynamicOffsetCounts[groupIndex],
      pass->boundDynamicOffsets[groupIndex],
      dynamicOffsetCount,
      pDynamicOffsets);
    gpuFrameStatsRecordBindEmission(pass->stats);
  }
}

GPU_EXPORT
void
GPUDispatchRaysEXT(GPURayTracingPassEncoderEXT *pass,
                   GPUShaderTableEXT           *table,
                   uint32_t                     width,
                   uint32_t                     height,
                   uint32_t                     depth) {
  GPUApi *api;

  if (!pass || pass->ended || !pass->hasPipeline || !table ||
      table->device != pass->device || table->pipeline != pass->_pipeline ||
      width == 0u || height == 0u || depth == 0u ||
      !(api = pass->_api) || !api->rayTracing.dispatch) {
    return;
  }
  if (!gpuRayDispatchFits(width,
                          height,
                          depth,
                          pass->device->rayTracingLimits.maxDispatchSize,
                          pass->device->rayTracingLimits.maxDispatchCount)) {
    gpuDeviceRecordValidationError(
      pass->device,
      "GPUDispatchRaysEXT skipped: dispatch exceeds device limits");
    return;
  }
#if GPU_BUILD_WITH_VALIDATION
  if (!gpuPipelineLayoutMaskIsBound(pass->pipelineLayout,
                                    pass->boundGroupLayouts,
                                    GPU_ENCODER_MAX_BIND_GROUPS,
                                    pass->requiredBindGroupMask)) {
    gpuDeviceRecordValidationError(
      pass->device,
      "GPUDispatchRaysEXT skipped: required bind group is missing");
    return;
  }
#endif
  api->rayTracing.dispatch(pass, table, width, height, depth);
}

GPU_EXPORT
void
GPUEndRayTracingPassEXT(GPURayTracingPassEncoderEXT *pass) {
  if (!pass || pass->ended) {
    return;
  }
  if (pass->_api && pass->_api->rayTracing.endPass) {
    pass->_api->rayTracing.endPass(pass);
  }
  pass->ended = true;
  if (pass->cmdb) {
    pass->cmdb->_activeEncoder = false;
  }
}
