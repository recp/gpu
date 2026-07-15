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
#include "../impl.h"
#include "../../../api/buffer_internal.h"

#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)

enum {
  GPU_VK_RAY_STACK_GEOMETRY_COUNT = 8u
};

static GPUAccelerationStructureVk *
vk_rayStructure(GPUAccelerationStructureEXT *structure) {
  return structure ? structure->_priv : NULL;
}

static GPUAccelerationStructureEncoderVk *
vk_rayEncoder(GPUAccelerationStructurePassEncoderEXT *pass) {
  return pass ? pass->_priv : NULL;
}

static VkAccelerationStructureTypeKHR
vk_rayType(GPUAccelerationStructureTypeEXT type) {
  return type == GPU_ACCELERATION_STRUCTURE_TOP_LEVEL_EXT
           ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
           : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
}

static VkBuildAccelerationStructureFlagsKHR
vk_rayBuildFlags(GPUAccelerationStructureBuildFlagsEXT flags) {
  VkBuildAccelerationStructureFlagsKHR result;

  result = 0u;
  if ((flags & GPU_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_EXT) != 0u) {
    result |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
  }
  if ((flags & GPU_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_EXT) != 0u) {
    result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  }
  if ((flags & GPU_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_EXT) != 0u) {
    result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
  }
  return result;
}

static VkGeometryInstanceFlagsKHR
vk_rayInstanceFlags(GPUAccelerationStructureInstanceFlagsEXT flags) {
  VkGeometryInstanceFlagsKHR result;

  result = 0u;
  if ((flags &
       GPU_ACCELERATION_STRUCTURE_INSTANCE_DISABLE_CULL_BIT_EXT) != 0u) {
    result |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  }
  if ((flags &
       GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_OPAQUE_BIT_EXT) != 0u) {
    result |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
  }
  if ((flags &
       GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_NON_OPAQUE_BIT_EXT) != 0u) {
    result |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
  }
  return result;
}

static VkDeviceAddress
vk_rayBufferAddress(const GPUBuffer *buffer, uint64_t offset) {
  return buffer && buffer->_gpuAddress <= UINT64_MAX - offset
           ? buffer->_gpuAddress + offset
           : 0u;
}

static uint32_t
vk_rayPrimitiveCount(
  const GPUAccelerationStructureTriangleGeometryEXT *geometry) {
  return geometry->indexBuffer ? geometry->indexCount / 3u
                               : geometry->vertexCount / 3u;
}

static void
vk_rayFillTriangle(
  VkAccelerationStructureGeometryKHR               *dst,
  const GPUAccelerationStructureTriangleGeometryEXT *src) {
  VkAccelerationStructureGeometryTrianglesDataKHR *triangles;

  memset(dst, 0, sizeof(*dst));
  dst->sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
  dst->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  dst->flags =
    (src->flags & GPU_ACCELERATION_STRUCTURE_GEOMETRY_NON_OPAQUE_BIT_EXT) == 0u
      ? VK_GEOMETRY_OPAQUE_BIT_KHR
      : 0u;

  triangles = &dst->geometry.triangles;
  triangles->sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
  triangles->vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT;
  triangles->vertexData.deviceAddress =
    vk_rayBufferAddress(src->vertexBuffer, src->vertexOffset);
  triangles->vertexStride             = src->vertexStride;
  triangles->maxVertex                = src->vertexCount - 1u;
  if (src->indexBuffer) {
    triangles->indexType = src->indexType == GPU_INDEX_TYPE_UINT32
                             ? VK_INDEX_TYPE_UINT32
                             : VK_INDEX_TYPE_UINT16;
    triangles->indexData.deviceAddress =
      vk_rayBufferAddress(src->indexBuffer, src->indexOffset);
  } else {
    triangles->indexType = VK_INDEX_TYPE_NONE_KHR;
  }
}

static void
vk_rayFillInstances(VkAccelerationStructureGeometryKHR *geometry,
                    VkDeviceAddress                      address) {
  memset(geometry, 0, sizeof(*geometry));
  geometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
  geometry->geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  geometry->geometry.instances.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  geometry->geometry.instances.arrayOfPointers = VK_FALSE;
  geometry->geometry.instances.data.deviceAddress = address;
}

static bool
vk_rayEnsureGeometryCapacity(GPUAccelerationStructureVk *native,
                             uint32_t                     count) {
  VkAccelerationStructureGeometryKHR       *geometries;
  VkAccelerationStructureBuildRangeInfoKHR *ranges;
  uint32_t                                  capacity;

  if (native->geometryCapacity >= count && native->geometries &&
      native->ranges) {
    return true;
  }
  capacity = native->geometryCapacity ? native->geometryCapacity : 4u;
  while (capacity < count) {
    if (capacity > UINT32_MAX / 2u) {
      capacity = count;
      break;
    }
    capacity *= 2u;
  }
  if ((size_t)capacity > SIZE_MAX / sizeof(*geometries) ||
      (size_t)capacity > SIZE_MAX / sizeof(*ranges)) {
    return false;
  }

  geometries = calloc(capacity, sizeof(*geometries));
  ranges     = calloc(capacity, sizeof(*ranges));
  if (!geometries || !ranges) {
    free(geometries);
    free(ranges);
    return false;
  }
  free(native->geometries);
  free(native->ranges);
  native->geometries       = geometries;
  native->ranges           = ranges;
  native->geometryCapacity = capacity;
  return true;
}

static bool
vk_rayEnsureInstanceBuffer(GPUDevice                       *device,
                           GPUAccelerationStructureVk      *native,
                           uint64_t                         sizeBytes) {
  GPUBufferCreateInfo info = {0};
  GPUBuffer          *buffer;
  uint64_t            capacity;

  if (native->instanceBuffer && native->instanceCapacity >= sizeBytes) {
    return true;
  }
  capacity = native->instanceCapacity ? native->instanceCapacity : 256u;
  while (capacity < sizeBytes) {
    if (capacity > UINT64_MAX / 2u) {
      capacity = sizeBytes;
      break;
    }
    capacity *= 2u;
  }

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "gpu-ray-instance-buffer";
  info.sizeBytes        = capacity;
  info.usage            = GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT;
  buffer                = NULL;
  if (vk_createHostBuffer(device, &info, &buffer) != GPU_OK || !buffer) {
    return false;
  }
  vk_destroyBuffer(native->instanceBuffer);
  native->instanceBuffer   = buffer;
  native->instanceCapacity = capacity;
  return true;
}

static bool
vk_rayPrepareBLAS(GPUAccelerationStructureVk                 *native,
                  const GPUAccelerationStructureBuildInfoEXT *info) {
  uint32_t count;

  count = info->bottomLevel.geometryCount;
  if (!vk_rayEnsureGeometryCapacity(native, count)) {
    return false;
  }
  for (uint32_t i = 0u; i < count; i++) {
    vk_rayFillTriangle(&native->geometries[i],
                       &info->bottomLevel.pGeometries[i]);
    memset(&native->ranges[i], 0, sizeof(native->ranges[i]));
    native->ranges[i].primitiveCount =
      vk_rayPrimitiveCount(&info->bottomLevel.pGeometries[i]);
  }
  return true;
}

static bool
vk_rayPrepareTLAS(GPUDevice                                  *device,
                  GPUAccelerationStructureVk                 *native,
                  const GPUAccelerationStructureBuildInfoEXT *info) {
  VkAccelerationStructureInstanceKHR *instances;
  GPUBufferVk                         *instanceBuffer;
  VkMappedMemoryRange                  range = {0};
  uint64_t                             sizeBytes;

  sizeBytes = (uint64_t)info->topLevel.instanceCount *
              sizeof(VkAccelerationStructureInstanceKHR);
  if (!vk_rayEnsureGeometryCapacity(native, 1u) ||
      !vk_rayEnsureInstanceBuffer(device, native, sizeBytes)) {
    return false;
  }
  instanceBuffer = native->instanceBuffer->_priv;
  if (!instanceBuffer || !instanceBuffer->mapped) {
    return false;
  }

  instances = instanceBuffer->mapped;
  memset(instances, 0, (size_t)sizeBytes);
  for (uint32_t i = 0u; i < info->topLevel.instanceCount; i++) {
    const GPUAccelerationStructureInstanceEXT *src;
    GPUAccelerationStructureVk                *structure;

    src       = &info->topLevel.pInstances[i];
    structure = vk_rayStructure(src->structure);
    if (!structure || !structure->address) {
      return false;
    }
    memcpy(instances[i].transform.matrix,
           src->transform,
           sizeof(instances[i].transform.matrix));
    instances[i].instanceCustomIndex                    = 0u;
    instances[i].mask                                   = src->mask
                                                            ? src->mask
                                                            : 0xffu;
    instances[i].instanceShaderBindingTableRecordOffset = 0u;
    instances[i].flags = vk_rayInstanceFlags(src->flags);
    instances[i].accelerationStructureReference = structure->address;
  }
  if (!instanceBuffer->coherent) {
    range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = instanceBuffer->memory;
    range.size   = VK_WHOLE_SIZE;
    if (vkFlushMappedMemoryRanges(instanceBuffer->device,
                                  1u,
                                  &range) != VK_SUCCESS) {
      return false;
    }
  }

  vk_rayFillInstances(&native->geometries[0],
                      native->instanceBuffer->_gpuAddress);
  memset(&native->ranges[0], 0, sizeof(native->ranges[0]));
  native->ranges[0].primitiveCount = info->topLevel.instanceCount;
  return true;
}

GPU_HIDE
GPUResult
vk_getAccelerationStructureSizes(
  GPUDevice                                    *device,
  const GPUAccelerationStructureBuildInfoEXT  *info,
  GPUAccelerationStructureSizesEXT            *outSizes) {
  GPUDeviceVk                              *deviceVk;
  VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {0};
  VkAccelerationStructureBuildSizesInfoKHR sizes = {0};
  VkAccelerationStructureGeometryKHR stackGeometries[
    GPU_VK_RAY_STACK_GEOMETRY_COUNT];
  uint32_t stackCounts[GPU_VK_RAY_STACK_GEOMETRY_COUNT];
  VkAccelerationStructureGeometryKHR *geometries;
  uint32_t                           *counts;
  uint32_t                            geometryCount;
  bool                                heap;

  deviceVk = device ? device->_priv : NULL;
  if (!deviceVk || !deviceVk->rayQuery ||
      !deviceVk->getAccelerationStructureBuildSizes) {
    return GPU_ERROR_UNSUPPORTED;
  }

  geometryCount = info->type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT
                    ? info->bottomLevel.geometryCount
                    : 1u;
  heap       = geometryCount > GPU_VK_RAY_STACK_GEOMETRY_COUNT;
  geometries = heap ? calloc(geometryCount, sizeof(*geometries))
                    : stackGeometries;
  counts     = heap ? calloc(geometryCount, sizeof(*counts)) : stackCounts;
  if (!geometries || !counts) {
    free(heap ? geometries : NULL);
    free(heap ? counts : NULL);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  if (info->type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT) {
    for (uint32_t i = 0u; i < geometryCount; i++) {
      vk_rayFillTriangle(&geometries[i],
                         &info->bottomLevel.pGeometries[i]);
      counts[i] = vk_rayPrimitiveCount(&info->bottomLevel.pGeometries[i]);
    }
  } else {
    vk_rayFillInstances(&geometries[0], 0u);
    counts[0] = info->topLevel.instanceCount;
  }

  buildInfo.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
  buildInfo.type          = vk_rayType(info->type);
  buildInfo.flags         = vk_rayBuildFlags(info->flags);
  buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  buildInfo.geometryCount = geometryCount;
  buildInfo.pGeometries   = geometries;
  sizes.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
  deviceVk->getAccelerationStructureBuildSizes(
    deviceVk->device,
    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
    &buildInfo,
    counts,
    &sizes
  );

  if (heap) {
    free(geometries);
    free(counts);
  }
  outSizes->accelerationStructureSize = sizes.accelerationStructureSize;
  outSizes->buildScratchSize          = sizes.buildScratchSize;
  outSizes->updateScratchSize         = sizes.updateScratchSize;
  return sizes.accelerationStructureSize > 0u
           ? GPU_OK
           : GPU_ERROR_BACKEND_FAILURE;
}

static void
vk_rayDestroyState(GPUAccelerationStructureVk *native) {
  if (!native) {
    return;
  }
  vk_destroyBuffer(native->instanceBuffer);
  if (native->device && native->structure && native->gpuDevice &&
      native->gpuDevice->destroyAccelerationStructure) {
    native->gpuDevice->destroyAccelerationStructure(native->device,
                                                     native->structure,
                                                     NULL);
  }
  if (native->device && native->buffer) {
    vkDestroyBuffer(native->device, native->buffer, NULL);
  }
  if (native->device && native->memory) {
    vkFreeMemory(native->device, native->memory, NULL);
  }
  free(native->geometries);
  free(native->ranges);
  free(native);
}

GPU_HIDE
GPUResult
vk_createAccelerationStructure(
  GPUDevice                                    *device,
  const GPUAccelerationStructureCreateInfoEXT *info,
  GPUAccelerationStructureEXT                 *structure) {
  GPUDeviceVk                       *deviceVk;
  GPUAccelerationStructureVk       *native;
  VkBufferCreateInfo                bufferInfo = {0};
  VkMemoryAllocateFlagsInfo         allocationFlags = {0};
  VkMemoryAllocateInfo              allocationInfo = {0};
  VkMemoryRequirements              requirements;
  VkMemoryPropertyFlags             memoryFlags;
  VkAccelerationStructureCreateInfoKHR createInfo = {0};
  VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {0};
  uint32_t                           memoryTypeIndex;

  deviceVk = device ? device->_priv : NULL;
  if (!deviceVk || !deviceVk->rayQuery ||
      !deviceVk->createAccelerationStructure ||
      !deviceVk->getAccelerationStructureAddress) {
    return GPU_ERROR_UNSUPPORTED;
  }
  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->gpuDevice = deviceVk;
  native->device    = deviceVk->device;

  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size  = info->sizeBytes;
  bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(native->device,
                     &bufferInfo,
                     NULL,
                     &native->buffer) != VK_SUCCESS) {
    vk_rayDestroyState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  vkGetBufferMemoryRequirements(native->device,
                                native->buffer,
                                &requirements);
  if (!vk_findMemoryType(device,
                         requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         0u,
                         &memoryTypeIndex,
                         &memoryFlags)) {
    vk_rayDestroyState(native);
    return GPU_ERROR_UNSUPPORTED;
  }
  GPU__UNUSED(memoryFlags);

  allocationFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
  allocationFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.pNext = &allocationFlags;
  allocationInfo.allocationSize  = requirements.size;
  allocationInfo.memoryTypeIndex = memoryTypeIndex;
  if (vkAllocateMemory(native->device,
                       &allocationInfo,
                       NULL,
                       &native->memory) != VK_SUCCESS ||
      vkBindBufferMemory(native->device,
                         native->buffer,
                         native->memory,
                         0u) != VK_SUCCESS) {
    vk_rayDestroyState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  createInfo.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
  createInfo.buffer = native->buffer;
  createInfo.size   = info->sizeBytes;
  createInfo.type   = vk_rayType(info->type);
  if (deviceVk->createAccelerationStructure(deviceVk->device,
                                            &createInfo,
                                            NULL,
                                            &native->structure) != VK_SUCCESS) {
    vk_rayDestroyState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  addressInfo.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
  addressInfo.accelerationStructure = native->structure;
  native->address = deviceVk->getAccelerationStructureAddress(
    deviceVk->device,
    &addressInfo
  );
  if (!native->address) {
    vk_rayDestroyState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vk_setDebugName(device,
                  VK_OBJECT_TYPE_BUFFER,
                  (uint64_t)native->buffer,
                  info->label);
  vk_setDebugName(device,
                  VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR,
                  (uint64_t)native->structure,
                  info->label);
  structure->_priv = native;
  return GPU_OK;
}

GPU_HIDE
void
vk_destroyAccelerationStructure(GPUAccelerationStructureEXT *structure) {
  GPUAccelerationStructureVk *native;

  native = vk_rayStructure(structure);
  vk_rayDestroyState(native);
  if (structure) {
    structure->_priv = NULL;
  }
}

GPU_HIDE
GPUAccelerationStructurePassEncoderEXT *
vk_beginAccelerationStructurePass(GPUCommandBuffer *cmdb, const char *label) {
  GPUCommandBufferVk                      *command;
  GPUAccelerationStructurePassEncoderEXT *pass;
  GPUAccelerationStructureEncoderVk      *native;
  GPUDevice                               *device;

  command = cmdb ? cmdb->_priv : NULL;
  device  = cmdb && cmdb->_queue ? cmdb->_queue->_device : NULL;
  if (!command || !command->command || !device) {
    return NULL;
  }

  pass   = &command->rayQueryEncoder;
  native = &command->rayQueryState;
  memset(pass, 0, sizeof(*pass));
  memset(native, 0, sizeof(*native));
  native->command = command->command;
  native->debugLabelActive = vk_beginDebugLabel(device,
                                                 native->command,
                                                 label);
  pass->_priv = native;
  return pass;
}

static void
vk_rayBuildBarrier(VkCommandBuffer command) {
  VkMemoryBarrier barrier = {0};
  VkPipelineStageFlags dstStages;

  barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                          VK_ACCESS_SHADER_READ_BIT;
  dstStages = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
              VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
#ifdef VK_EXT_mesh_shader
  dstStages |= VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT |
               VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT;
#endif
  vkCmdPipelineBarrier(
    command,
    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
    dstStages,
    0u,
    1u,
    &barrier,
    0u,
    NULL,
    0u,
    NULL
  );
}

GPU_HIDE
GPUResult
vk_buildAccelerationStructure(
  GPUAccelerationStructurePassEncoderEXT     *pass,
  GPUAccelerationStructureEXT                *dst,
  const GPUAccelerationStructureBuildInfoEXT *info,
  GPUBuffer                                   *scratchBuffer,
  uint64_t                                     scratchOffset) {
  GPUAccelerationStructureEncoderVk *encoder;
  GPUAccelerationStructureVk        *native;
  GPUAccelerationStructureVk        *source;
  GPUDeviceVk                        *deviceVk;
  VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {0};
  const VkAccelerationStructureBuildRangeInfoKHR *ranges;
  VkDeviceAddress                     scratchAddress;
  uint32_t                            geometryCount;
  bool                                prepared;

  encoder  = vk_rayEncoder(pass);
  native   = vk_rayStructure(dst);
  source   = info->source ? vk_rayStructure(info->source) : NULL;
  deviceVk = pass && pass->device ? pass->device->_priv : NULL;
  if (!encoder || !encoder->command || !native || !deviceVk ||
      !deviceVk->buildAccelerationStructures) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  prepared = info->type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT
               ? vk_rayPrepareBLAS(native, info)
               : vk_rayPrepareTLAS(pass->device, native, info);
  if (!prepared) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  scratchAddress = vk_rayBufferAddress(scratchBuffer, scratchOffset);
  if (!scratchAddress ||
      (deviceVk->accelerationStructureScratchAlignment > 1u &&
       scratchAddress % deviceVk->accelerationStructureScratchAlignment != 0u)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  geometryCount = info->type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT
                    ? info->bottomLevel.geometryCount
                    : 1u;
  buildInfo.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
  buildInfo.type  = vk_rayType(info->type);
  buildInfo.flags = vk_rayBuildFlags(info->flags);
  buildInfo.mode  = info->mode == GPU_ACCELERATION_STRUCTURE_UPDATE_EXT
                      ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                      : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  buildInfo.srcAccelerationStructure = source ? source->structure : VK_NULL_HANDLE;
  buildInfo.dstAccelerationStructure = native->structure;
  buildInfo.geometryCount             = geometryCount;
  buildInfo.pGeometries               = native->geometries;
  buildInfo.scratchData.deviceAddress = scratchAddress;
  ranges = native->ranges;
  deviceVk->buildAccelerationStructures(encoder->command,
                                        1u,
                                        &buildInfo,
                                        &ranges);
  vk_rayBuildBarrier(encoder->command);
  return GPU_OK;
}

GPU_HIDE
void
vk_endAccelerationStructurePass(
  GPUAccelerationStructurePassEncoderEXT *pass) {
  GPUAccelerationStructureEncoderVk *native;

  native = vk_rayEncoder(pass);
  if (!native) {
    return;
  }
  if (native->debugLabelActive) {
    vk_endDebugLabel(pass->device, native->command);
  }
  native->command          = VK_NULL_HANDLE;
  native->debugLabelActive = false;
}

GPU_HIDE
void
vk_initRayQuery(GPUApiRayQuery *api) {
  api->getSizes  = vk_getAccelerationStructureSizes;
  api->create    = vk_createAccelerationStructure;
  api->destroy   = vk_destroyAccelerationStructure;
  api->beginPass = vk_beginAccelerationStructurePass;
  api->build     = vk_buildAccelerationStructure;
  api->endPass   = vk_endAccelerationStructurePass;
}

#else

GPU_HIDE
void
vk_initRayQuery(GPUApiRayQuery *api) {
  memset(api, 0, sizeof(*api));
}

#endif
