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
#include "device_internal.h"
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
      geometry->vertexCount < 3u || geometry->vertexCount % 3u != 0u ||
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
    return geometry->indexCount == 0u && geometry->indexOffset == 0u;
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
