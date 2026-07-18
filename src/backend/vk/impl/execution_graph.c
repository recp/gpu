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
#include "pipeline_cache.h"

#ifdef VK_AMDX_shader_enqueue

typedef struct GPUExecutionGraphEntryVk {
  const char                 *entryPoint;
  GPUExecutionGraphEntryEXT   entry;
  uint64_t                    nameHash;
  uint32_t                    nameLength;
} GPUExecutionGraphEntryVk;

typedef struct GPUExecutionGraphVk {
  GPUDeviceVk               *gpuDevice;
  GPUShaderLayoutVk          shaderLayout;
  VkPipeline                 pipeline;
  GPUExecutionGraphEntryVk  *entries;
  uint32_t                   entryCount;
} GPUExecutionGraphVk;

typedef struct GPUExecutionGraphInstanceVk {
  GPUCommandBufferVk       *recordingCommandBuffer;
  VkDispatchGraphInfoAMDX  *hostInfos;
  VkDevice                  device;
  VkBuffer                  scratchBuffer;
  VkDeviceMemory            scratchMemory;
  VkDeviceAddress           scratchAddress;
  uint64_t                  scratchOffset;
  uint32_t                  inputCapacity;
  bool                      initialized;
} GPUExecutionGraphInstanceVk;

struct GPUExecutionGraphInputChunkVk {
  struct GPUExecutionGraphInputChunkVk *next;
  uint8_t                              *mapped;
  VkDevice                              device;
  VkBuffer                              buffer;
  VkDeviceMemory                        memory;
  VkDeviceAddress                       address;
  uint64_t                              capacity;
  uint64_t                              offset;
};

enum {
  GPU_VK_GRAPH_INPUT_CHUNK_CAPACITY = 4096u
};

static uint64_t
vk_graphNameHash(const char *name, size_t length) {
  uint64_t hash;

  hash = UINT64_C(1469598103934665603);
  for (size_t i = 0u; i < length; i++) {
    hash ^= (uint8_t)name[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static GPUResult
vk_graphResult(VkResult result) {
  return result == VK_ERROR_OUT_OF_HOST_MEMORY ||
         result == VK_ERROR_OUT_OF_DEVICE_MEMORY
           ? GPU_ERROR_OUT_OF_MEMORY
           : GPU_ERROR_BACKEND_FAILURE;
}

static void
vk_destroyExecutionGraphState(GPUExecutionGraphVk *native) {
  if (!native) {
    return;
  }
  if (native->gpuDevice && native->gpuDevice->device && native->pipeline) {
    vkDestroyPipeline(native->gpuDevice->device, native->pipeline, NULL);
  }
  vk_destroyShaderLayout(&native->shaderLayout);
  free(native);
}

static GPUResult
vk_createExecutionGraph(GPUDevice                            *device,
                        const GPUExecutionGraphCreateInfoEXT *info,
                        GPUExecutionGraphEXT                 *graph) {
  GPUShaderExecutionGraphEntryInfo reflected[USL_RUNTIME_MAX_ENTRY_POINTS];
  VkPipelineShaderStageNodeCreateInfoAMDX nodeInfos[USL_RUNTIME_MAX_ENTRY_POINTS];
  VkPipelineShaderStageCreateInfo stages[USL_RUNTIME_MAX_ENTRY_POINTS];
  GPUShaderLibraryVk            *library;
  GPUExecutionGraphVk           *native;
  VkExecutionGraphPipelineCreateInfoAMDX pipelineInfo = {0};
  VkExecutionGraphPipelineScratchSizeAMDX scratch = {0};
  VkPipelineCache                cache;
  uint8_t                       *storage;
  size_t                         allocationSize;
  size_t                         namesSize;
  uint32_t                       entryCount;
  uint32_t                       programEntryCount;
  VkResult                       result;

  library = info && info->library ? info->library->_priv : NULL;
  entryCount = info
                 ? gpuGetShaderLibraryExecutionGraphEntryCount(info->library)
                 : 0u;
  if (!device || !device->_priv || !info || !graph || !library ||
      !library->module || entryCount == 0u ||
      entryCount > USL_RUNTIME_MAX_ENTRY_POINTS ||
      (info->graphName && strcmp(info->graphName, "main") != 0)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  namesSize         = 0u;
  programEntryCount = 0u;
  for (uint32_t i = 0u; i < entryCount; i++) {
    size_t nameSize;

    if (!gpuGetShaderLibraryExecutionGraphEntryAt(info->library,
                                                  i,
                                                  &reflected[i]) ||
        !reflected[i].entryPoint || !reflected[i].entryPoint[0] ||
        !reflected[i].nodeName || !reflected[i].nodeName[0] ||
        reflected[i].recordSizeBytes != 0u) {
      return GPU_ERROR_UNSUPPORTED;
    }
    nameSize = strlen(reflected[i].entryPoint) + 1u;
    if (namesSize > SIZE_MAX - nameSize) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    namesSize += nameSize;
    programEntryCount += reflected[i].programEntry;
  }
  if (programEntryCount == 0u ||
      entryCount > (SIZE_MAX - sizeof(*native) - namesSize) /
                     sizeof(*native->entries)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  allocationSize = sizeof(*native) +
                   entryCount * sizeof(*native->entries) + namesSize;
  native = calloc(1, allocationSize);
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->gpuDevice  = device->_priv;
  native->entries    = (GPUExecutionGraphEntryVk *)(native + 1);
  native->entryCount = entryCount;
  storage = (uint8_t *)(native->entries + entryCount);

  if (!native->gpuDevice->executionGraph ||
      !native->gpuDevice->createExecutionGraphPipelines ||
      vk_createShaderLayout(device,
                            info->layout,
                            info->library,
                            &native->shaderLayout) != GPU_OK) {
    vk_destroyExecutionGraphState(native);
    return GPU_ERROR_UNSUPPORTED;
  }

  memset(stages, 0, entryCount * sizeof(stages[0]));
  memset(nodeInfos, 0, entryCount * sizeof(nodeInfos[0]));
  for (uint32_t i = 0u; i < entryCount; i++) {
    size_t nameSize;

    nodeInfos[i].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NODE_CREATE_INFO_AMDX;
    nodeInfos[i].pName = reflected[i].nodeName;
    nodeInfos[i].index = reflected[i].nodeIndex;
    stages[i].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[i].pNext = &nodeInfos[i];
    stages[i].stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stages[i].module = library->module;
    stages[i].pName  = reflected[i].entryPoint;

    nameSize = strlen(reflected[i].entryPoint) + 1u;
    memcpy(storage, reflected[i].entryPoint, nameSize);
    native->entries[i].entryPoint = (const char *)storage;
    native->entries[i].nameLength = (uint32_t)(nameSize - 1u);
    native->entries[i].nameHash = vk_graphNameHash(
      reflected[i].entryPoint,
      nameSize - 1u
    );
    storage += nameSize;
  }

  pipelineInfo.sType =
    VK_STRUCTURE_TYPE_EXECUTION_GRAPH_PIPELINE_CREATE_INFO_AMDX;
  pipelineInfo.stageCount        = entryCount;
  pipelineInfo.pStages           = stages;
  pipelineInfo.layout            = native->shaderLayout.layout;
  pipelineInfo.basePipelineIndex = -1;
#ifdef VK_EXT_descriptor_buffer
  if (native->shaderLayout.descriptorBuffer) {
    pipelineInfo.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
  }
#endif
  cache  = vk_lockCache(info->cache);
  result = native->gpuDevice->createExecutionGraphPipelines(
    native->gpuDevice->device,
    cache,
    1u,
    &pipelineInfo,
    NULL,
    &native->pipeline
  );
  vk_unlockCache(info->cache);
  if (result != VK_SUCCESS || !native->pipeline) {
    vk_destroyExecutionGraphState(native);
    return vk_graphResult(result);
  }

  for (uint32_t i = 0u; i < entryCount; i++) {
    result = native->gpuDevice->getExecutionGraphPipelineNodeIndex(
      native->gpuDevice->device,
      native->pipeline,
      &nodeInfos[i],
      &native->entries[i].entry.index
    );
    if (result != VK_SUCCESS) {
      vk_destroyExecutionGraphState(native);
      return vk_graphResult(result);
    }
    native->entries[i].entry.recordSizeBytes = reflected[i].recordSizeBytes;
    native->entries[i].entry.recordAlignmentBytes = 1u;
  }

  scratch.sType =
    VK_STRUCTURE_TYPE_EXECUTION_GRAPH_PIPELINE_SCRATCH_SIZE_AMDX;
  result = native->gpuDevice->getExecutionGraphPipelineScratchSize(
    native->gpuDevice->device,
    native->pipeline,
    &scratch
  );
  if (result != VK_SUCCESS || scratch.minSize > scratch.maxSize ||
      (scratch.maxSize > 0u && scratch.sizeGranularity == 0u)) {
    vk_destroyExecutionGraphState(native);
    return result == VK_SUCCESS ? GPU_ERROR_BACKEND_FAILURE
                                : vk_graphResult(result);
  }

  graph->memoryRequirements.minSizeBytes         = scratch.minSize;
  graph->memoryRequirements.maxSizeBytes         = scratch.maxSize;
  graph->memoryRequirements.sizeGranularityBytes = scratch.sizeGranularity;
  graph->_priv = native;
  return GPU_OK;
}

static void
vk_destroyExecutionGraph(GPUExecutionGraphEXT *graph) {
  if (!graph) {
    return;
  }
  vk_destroyExecutionGraphState(graph->_priv);
  graph->_priv = NULL;
}

static uint64_t
vk_graphAlignedOffset(VkDeviceAddress address, uint64_t alignment) {
  uint64_t remainder;

  remainder = alignment > 0u ? address % alignment : 0u;
  return remainder ? alignment - remainder : 0u;
}

static GPUResult
vk_createGraphBuffer(GPUDevice            *device,
                     uint64_t              sizeBytes,
                     VkBufferUsageFlags    usage,
                     VkMemoryPropertyFlags requiredMemory,
                     VkBuffer             *outBuffer,
                     VkDeviceMemory       *outMemory,
                     VkDeviceAddress      *outAddress,
                     void                **outMapped) {
  GPUDeviceVk                 *deviceVk;
  VkBufferCreateInfo           bufferInfo = {0};
  VkMemoryAllocateFlagsInfo    allocationFlags = {0};
  VkMemoryAllocateInfo         allocationInfo = {0};
  VkBufferDeviceAddressInfo    addressInfo = {0};
  VkMemoryRequirements         requirements;
  VkMemoryPropertyFlags        memoryFlags;
  uint32_t                     memoryTypeIndex;
  VkResult                     result;

  deviceVk = device ? device->_priv : NULL;
  if (!deviceVk || !outBuffer || !outMemory || !outAddress ||
      sizeBytes == 0u || sizeBytes > UINT64_MAX - 1u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outBuffer  = VK_NULL_HANDLE;
  *outMemory  = VK_NULL_HANDLE;
  *outAddress = 0u;
  if (outMapped) {
    *outMapped = NULL;
  }

  bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size        = sizeBytes;
  bufferInfo.usage       = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  result = vkCreateBuffer(deviceVk->device,
                          &bufferInfo,
                          NULL,
                          outBuffer);
  if (result != VK_SUCCESS) {
    return vk_graphResult(result);
  }
  vkGetBufferMemoryRequirements(deviceVk->device,
                                *outBuffer,
                                &requirements);
  if (!vk_findMemoryType(device,
                         requirements.memoryTypeBits,
                         requiredMemory,
                         0u,
                         &memoryTypeIndex,
                         &memoryFlags)) {
    vkDestroyBuffer(deviceVk->device, *outBuffer, NULL);
    *outBuffer = VK_NULL_HANDLE;
    return GPU_ERROR_UNSUPPORTED;
  }

  allocationFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
  allocationFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  allocationInfo.sType  = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.pNext  = &allocationFlags;
  allocationInfo.allocationSize  = requirements.size;
  allocationInfo.memoryTypeIndex = memoryTypeIndex;
  result = vkAllocateMemory(deviceVk->device,
                            &allocationInfo,
                            NULL,
                            outMemory);
  if (result != VK_SUCCESS ||
      vkBindBufferMemory(deviceVk->device,
                         *outBuffer,
                         *outMemory,
                         0u) != VK_SUCCESS) {
    vkDestroyBuffer(deviceVk->device, *outBuffer, NULL);
    if (*outMemory) {
      vkFreeMemory(deviceVk->device, *outMemory, NULL);
    }
    *outMemory = VK_NULL_HANDLE;
    *outBuffer = VK_NULL_HANDLE;
    return result != VK_SUCCESS ? vk_graphResult(result)
                                : GPU_ERROR_BACKEND_FAILURE;
  }
  if (outMapped &&
      vkMapMemory(deviceVk->device,
                  *outMemory,
                  0u,
                  VK_WHOLE_SIZE,
                  0u,
                  outMapped) != VK_SUCCESS) {
    vkDestroyBuffer(deviceVk->device, *outBuffer, NULL);
    vkFreeMemory(deviceVk->device, *outMemory, NULL);
    *outMemory = VK_NULL_HANDLE;
    *outBuffer = VK_NULL_HANDLE;
    return GPU_ERROR_BACKEND_FAILURE;
  }

  addressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
  addressInfo.buffer = *outBuffer;
  *outAddress = deviceVk->getBufferDeviceAddress(deviceVk->device,
                                                  &addressInfo);
  if (!*outAddress) {
    if (outMapped && *outMapped) {
      vkUnmapMemory(deviceVk->device, *outMemory);
      *outMapped = NULL;
    }
    vkDestroyBuffer(deviceVk->device, *outBuffer, NULL);
    vkFreeMemory(deviceVk->device, *outMemory, NULL);
    *outMemory = VK_NULL_HANDLE;
    *outBuffer = VK_NULL_HANDLE;
    return GPU_ERROR_BACKEND_FAILURE;
  }
  GPU__UNUSED(memoryFlags);
  return GPU_OK;
}

static bool
vk_graphInputOffset(GPUExecutionGraphInputChunkVk *chunk,
                    uint64_t                       alignment,
                    uint64_t                       sizeBytes,
                    uint64_t                      *outOffset) {
  uint64_t address;
  uint64_t padding;
  uint64_t offset;

  if (!chunk || !outOffset || alignment == 0u ||
      chunk->offset > UINT64_MAX - chunk->address) {
    return false;
  }
  address = chunk->address + chunk->offset;
  padding = (alignment - address % alignment) % alignment;
  if (chunk->offset > UINT64_MAX - padding) {
    return false;
  }
  offset = chunk->offset + padding;
  if (offset > chunk->capacity || sizeBytes > chunk->capacity - offset) {
    return false;
  }
  *outOffset = offset;
  return true;
}

static bool
vk_reserveGraphInput(GPUComputePassEncoder *pass,
                     uint64_t               sizeBytes,
                     uint64_t               alignment,
                     void                 **outMapped,
                     VkDeviceAddress       *outAddress) {
  GPUCommandBufferVk            *command;
  GPUDevice                     *device;
  GPUDeviceVk                   *deviceVk;
  GPUExecutionGraphInputChunkVk *chunk;
  VkDeviceAddress                address;
  void                          *mapped;
  uint64_t                       capacity;
  uint64_t                       offset;
  GPUResult                      result;

  command  = pass && pass->_cmdb ? pass->_cmdb->_priv : NULL;
  device   = pass ? pass->_device : NULL;
  deviceVk = device ? device->_priv : NULL;
  if (!command || !deviceVk || sizeBytes == 0u || alignment == 0u ||
      !outMapped || !outAddress) {
    return false;
  }

  for (chunk = command->graphInputChunks; chunk; chunk = chunk->next) {
    if (vk_graphInputOffset(chunk, alignment, sizeBytes, &offset)) {
      chunk->offset = offset + sizeBytes;
      *outMapped    = chunk->mapped + offset;
      *outAddress   = chunk->address + offset;
      return true;
    }
  }

  if (sizeBytes > UINT64_MAX - (alignment - 1u)) {
    return false;
  }
  capacity = sizeBytes + alignment - 1u;
  if (capacity < GPU_VK_GRAPH_INPUT_CHUNK_CAPACITY) {
    capacity = GPU_VK_GRAPH_INPUT_CHUNK_CAPACITY;
  }
  chunk = calloc(1, sizeof(*chunk));
  if (!chunk) {
    return false;
  }
  mapped = NULL;
  result = vk_createGraphBuffer(device,
                                capacity,
                                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &chunk->buffer,
                                &chunk->memory,
                                &address,
                                &mapped);
  if (result != GPU_OK) {
    free(chunk);
    return false;
  }
  chunk->device   = deviceVk->device;
  chunk->mapped   = mapped;
  chunk->address  = address;
  chunk->capacity = capacity;
  if (!vk_graphInputOffset(chunk, alignment, sizeBytes, &offset)) {
    vkUnmapMemory(chunk->device, chunk->memory);
    vkDestroyBuffer(chunk->device, chunk->buffer, NULL);
    vkFreeMemory(chunk->device, chunk->memory, NULL);
    free(chunk);
    return false;
  }

  chunk->offset             = offset + sizeBytes;
  chunk->next               = command->graphInputChunks;
  command->graphInputChunks = chunk;
  gpuDeviceRecordHotPathAlloc(device, sizeof(*chunk) + capacity);
  *outMapped  = chunk->mapped + offset;
  *outAddress = chunk->address + offset;
  return true;
}

static void
vk_destroyExecutionGraphInstanceState(GPUExecutionGraphInstanceVk *native) {
  if (!native) {
    return;
  }
  if (native->device && native->scratchBuffer) {
    vkDestroyBuffer(native->device, native->scratchBuffer, NULL);
  }
  if (native->device && native->scratchMemory) {
    vkFreeMemory(native->device, native->scratchMemory, NULL);
  }
  free(native);
}

static GPUResult
vk_createExecutionGraphInstance(
  GPUDevice                                    *device,
  const GPUExecutionGraphInstanceCreateInfoEXT *info,
  GPUExecutionGraphInstanceEXT                 *instance) {
  GPUExecutionGraphVk         *graph;
  GPUExecutionGraphInstanceVk *native;
  GPUDeviceVk                 *deviceVk;
  VkDeviceAddress              address;
  uint64_t                     scratchSize;
  GPUResult                    result;

  graph    = info && info->graph ? info->graph->_priv : NULL;
  deviceVk = device ? device->_priv : NULL;
  if (!deviceVk || !graph || !instance || graph->entryCount == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  native = calloc(1,
                  sizeof(*native) +
                  graph->entryCount * sizeof(*native->hostInfos));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->device        = deviceVk->device;
  native->hostInfos     = (VkDispatchGraphInfoAMDX *)(native + 1);
  native->inputCapacity = graph->entryCount;

  if (info->memorySizeBytes > UINT64_MAX - 63u) {
    vk_destroyExecutionGraphInstanceState(native);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  scratchSize = info->memorySizeBytes + 63u;
  result = vk_createGraphBuffer(device,
                                scratchSize,
                                VK_BUFFER_USAGE_EXECUTION_GRAPH_SCRATCH_BIT_AMDX,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                &native->scratchBuffer,
                                &native->scratchMemory,
                                &address,
                                NULL);
  if (result != GPU_OK) {
    vk_destroyExecutionGraphInstanceState(native);
    return result;
  }
  native->scratchOffset  = vk_graphAlignedOffset(address, 64u);
  native->scratchAddress = address + native->scratchOffset;

  instance->_priv = native;
  return GPU_OK;
}

static void
vk_destroyExecutionGraphInstance(GPUExecutionGraphInstanceEXT *instance) {
  if (!instance) {
    return;
  }
  vk_destroyExecutionGraphInstanceState(instance->_priv);
  instance->_priv = NULL;
}

static GPUResult
vk_getExecutionGraphEntry(const GPUExecutionGraphEXT *graph,
                          const char                 *entryName,
                          GPUExecutionGraphEntryEXT  *outEntry) {
  GPUExecutionGraphVk *native;
  uint64_t             hash;
  size_t               length;

  native = graph ? graph->_priv : NULL;
  if (!native || !entryName || !outEntry) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  length = strlen(entryName);
  if (length > UINT32_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  hash = vk_graphNameHash(entryName, length);
  for (uint32_t i = 0u; i < native->entryCount; i++) {
    const GPUExecutionGraphEntryVk *entry;

    entry = &native->entries[i];
    if (entry->nameHash == hash && entry->nameLength == (uint32_t)length &&
        memcmp(entry->entryPoint, entryName, length) == 0) {
      *outEntry = entry->entry;
      return GPU_OK;
    }
  }
  return GPU_ERROR_INVALID_ARGUMENT;
}

static void
vk_bindExecutionGraph(GPUComputePassEncoder *pass,
                      GPUExecutionGraphEXT  *graph) {
  GPUComputeEncoderVk *encoder;
  GPUExecutionGraphVk *native;

  encoder = pass ? pass->_priv : NULL;
  native  = graph ? graph->_priv : NULL;
  if (!encoder || !encoder->command || !native || !native->pipeline) {
    return;
  }
  vkCmdBindPipeline(encoder->command,
                    VK_PIPELINE_BIND_POINT_EXECUTION_GRAPH_AMDX,
                    native->pipeline);
  if (encoder->descriptors.pipelineLayout !=
      native->shaderLayout.baseLayout) {
    memset(encoder->descriptors.groups, 0, sizeof(encoder->descriptors.groups));
  }
  vk_bindShaderSamplers(encoder->command,
                        VK_PIPELINE_BIND_POINT_EXECUTION_GRAPH_AMDX,
                        &native->shaderLayout);
  encoder->pipelineLayout             = native->shaderLayout.layout;
  encoder->descriptors.pipelineLayout = native->shaderLayout.baseLayout;
  encoder->bindPoint                  =
    VK_PIPELINE_BIND_POINT_EXECUTION_GRAPH_AMDX;
  encoder->executionGraph             = graph;
  encoder->executionGraphInstance     = NULL;
}

static bool
vk_executionGraphEntryMatches(const GPUExecutionGraphVk       *graph,
                              const GPUExecutionGraphEntryEXT *entry) {
  if (!graph || !entry) {
    return false;
  }
  for (uint32_t i = 0u; i < graph->entryCount; i++) {
    const GPUExecutionGraphEntryEXT *expected;

    expected = &graph->entries[i].entry;
    if (expected->index == entry->index) {
      return expected->recordSizeBytes == entry->recordSizeBytes &&
             expected->recordAlignmentBytes == entry->recordAlignmentBytes;
    }
  }
  return false;
}

static bool
vk_trackGraphInitialization(GPUComputePassEncoder        *pass,
                            GPUExecutionGraphInstanceEXT *instance,
                            GPUExecutionGraphInstanceVk  *native) {
  GPUCommandBufferVk *command;

  if (!pass || !instance || !native || native->initialized) {
    return false;
  }
  command = pass->_cmdb ? pass->_cmdb->_priv : NULL;
  if (!command) {
    return true;
  }
  if (native->recordingCommandBuffer == command) {
    return false;
  }
  if (command->graphInitializationCount >=
      GPU_VK_GRAPH_INIT_TRACK_COUNT) {
    return true;
  }

  command->graphInitializations[command->graphInitializationCount++] = instance;
  native->recordingCommandBuffer = command;
  return true;
}

static bool
vk_prepareExecutionGraphInstance(GPUComputePassEncoder        *pass,
                                 GPUExecutionGraphInstanceEXT *instance) {
  GPUComputeEncoderVk         *encoder;
  GPUExecutionGraphVk         *graph;
  GPUExecutionGraphInstanceVk *native;

  encoder = pass ? pass->_priv : NULL;
  graph   = instance && instance->graph ? instance->graph->_priv : NULL;
  native  = instance ? instance->_priv : NULL;
  if (!encoder || !encoder->command || !graph || !native ||
      encoder->executionGraph != instance->graph) {
    return false;
  }
  if (vk_trackGraphInitialization(pass, instance, native)) {
    VkMemoryBarrier barrier = {0};

    graph->gpuDevice->initializeGraphScratchMemory(
      encoder->command,
      graph->pipeline,
      native->scratchAddress,
      instance->memorySizeBytes
    );
    barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(encoder->command,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0u,
                         1u,
                         &barrier,
                         0u,
                         NULL,
                         0u,
                         NULL);
  }
  encoder->executionGraphInstance = instance;
  return true;
}

static void
vk_dispatchExecutionGraph(GPUComputePassEncoder           *pass,
                          GPUExecutionGraphInstanceEXT    *instance,
                          uint32_t                         inputCount,
                          const GPUExecutionGraphInputEXT *inputs) {
  GPUComputeEncoderVk         *encoder;
  GPUExecutionGraphVk         *graph;
  GPUExecutionGraphInstanceVk *native;
  VkDispatchGraphCountInfoAMDX countInfo = {0};

  encoder = pass ? pass->_priv : NULL;
  graph   = instance && instance->graph ? instance->graph->_priv : NULL;
  native  = instance ? instance->_priv : NULL;
  if (!encoder || !graph || !native || !inputs || inputCount == 0u ||
      inputCount > native->inputCapacity ||
      !vk_prepareExecutionGraphInstance(pass, instance)) {
    return;
  }
  for (uint32_t i = 0u; i < inputCount; i++) {
    if (!vk_executionGraphEntryMatches(graph, &inputs[i].entry)) {
      return;
    }
    native->hostInfos[i].nodeIndex      = inputs[i].entry.index;
    native->hostInfos[i].payloadCount   = inputs[i].recordCount;
    native->hostInfos[i].payloads.hostAddress = inputs[i].pRecords;
    native->hostInfos[i].payloadStride = inputs[i].recordStrideBytes
                                           ? inputs[i].recordStrideBytes
                                           : inputs[i].entry.recordSizeBytes;
  }
  countInfo.count             = inputCount;
  countInfo.infos.hostAddress = native->hostInfos;
  countInfo.stride            = sizeof(*native->hostInfos);
  graph->gpuDevice->dispatchGraph(encoder->command,
                                  native->scratchAddress,
                                  instance->memorySizeBytes,
                                  &countInfo);
}

static void
vk_dispatchExecutionGraphBuffer(
  GPUComputePassEncoder                 *pass,
  GPUExecutionGraphInstanceEXT          *instance,
  uint32_t                               inputCount,
  const GPUExecutionGraphBufferInputEXT *inputs) {
  GPUComputeEncoderVk         *encoder;
  GPUExecutionGraphVk         *graph;
  GPUExecutionGraphInstanceVk *native;
  VkDispatchGraphInfoAMDX     *gpuInfos;
  VkDispatchGraphCountInfoAMDX countInfo = {0};
  VkDeviceAddress              inputAddress;
  uint64_t                     alignment;
  uint64_t                     tableSize;

  encoder = pass ? pass->_priv : NULL;
  graph   = instance && instance->graph ? instance->graph->_priv : NULL;
  native  = instance ? instance->_priv : NULL;
  if (!encoder || !graph || !native || !inputs || inputCount == 0u ||
      inputCount > native->inputCapacity) {
    return;
  }
  alignment = graph->gpuDevice->executionGraphDispatchAddressAlignment;
  for (uint32_t i = 0u; i < inputCount; i++) {
    VkDeviceAddress address;

    address = inputs[i].records
                ? inputs[i].records->_gpuAddress + inputs[i].recordOffset
                : 0u;
    if (!address || address % alignment != 0u ||
        !vk_executionGraphEntryMatches(graph, &inputs[i].entry)) {
      return;
    }
  }

  tableSize = (uint64_t)inputCount * sizeof(*gpuInfos);
  if (!vk_reserveGraphInput(pass,
                            tableSize,
                            alignment,
                            (void **)&gpuInfos,
                            &inputAddress) ||
      !vk_prepareExecutionGraphInstance(pass, instance)) {
    return;
  }
  for (uint32_t i = 0u; i < inputCount; i++) {
    VkDeviceAddress address;

    address = inputs[i].records->_gpuAddress + inputs[i].recordOffset;
    gpuInfos[i].nodeIndex      = inputs[i].entry.index;
    gpuInfos[i].payloadCount   = inputs[i].recordCount;
    gpuInfos[i].payloads.deviceAddress = address;
    gpuInfos[i].payloadStride = inputs[i].recordStrideBytes
                                  ? inputs[i].recordStrideBytes
                                  : inputs[i].entry.recordSizeBytes;
  }
  countInfo.count               = inputCount;
  countInfo.infos.deviceAddress = inputAddress;
  countInfo.stride              = sizeof(*gpuInfos);
  graph->gpuDevice->dispatchGraphIndirect(encoder->command,
                                          native->scratchAddress,
                                          instance->memorySizeBytes,
                                          &countInfo);
}

#endif

GPU_HIDE
void
vk_resetGraphInitializations(GPUCommandBufferVk *command) {
#ifdef VK_AMDX_shader_enqueue
  if (!command) {
    return;
  }
  for (uint32_t i = 0u; i < command->graphInitializationCount; i++) {
    GPUExecutionGraphInstanceEXT *instance;
    GPUExecutionGraphInstanceVk  *native;

    instance = command->graphInitializations[i];
    native   = instance ? instance->_priv : NULL;
    if (native && native->recordingCommandBuffer == command) {
      native->recordingCommandBuffer = NULL;
    }
    command->graphInitializations[i] = NULL;
  }
  command->graphInitializationCount = 0u;
  for (GPUExecutionGraphInputChunkVk *chunk = command->graphInputChunks;
       chunk;
       chunk = chunk->next) {
    chunk->offset = 0u;
  }
#else
  GPU__UNUSED(command);
#endif
}

GPU_HIDE
void
vk_destroyGraphInputScratch(GPUCommandBufferVk *command) {
#ifdef VK_AMDX_shader_enqueue
  GPUExecutionGraphInputChunkVk *chunk;

  chunk = command ? command->graphInputChunks : NULL;
  while (chunk) {
    GPUExecutionGraphInputChunkVk *next;

    next = chunk->next;
    if (chunk->device && chunk->memory && chunk->mapped) {
      vkUnmapMemory(chunk->device, chunk->memory);
    }
    if (chunk->device && chunk->buffer) {
      vkDestroyBuffer(chunk->device, chunk->buffer, NULL);
    }
    if (chunk->device && chunk->memory) {
      vkFreeMemory(chunk->device, chunk->memory, NULL);
    }
    free(chunk);
    chunk = next;
  }
  if (command) {
    command->graphInputChunks = NULL;
  }
#else
  GPU__UNUSED(command);
#endif
}

GPU_HIDE
void
vk_submitGraphInitializations(GPUCommandBufferVk *command) {
#ifdef VK_AMDX_shader_enqueue
  if (!command) {
    return;
  }
  for (uint32_t i = 0u; i < command->graphInitializationCount; i++) {
    GPUExecutionGraphInstanceEXT *instance;
    GPUExecutionGraphInstanceVk  *native;

    instance = command->graphInitializations[i];
    native   = instance ? instance->_priv : NULL;
    if (native && native->recordingCommandBuffer == command) {
      native->recordingCommandBuffer = NULL;
      native->initialized            = true;
    }
    command->graphInitializations[i] = NULL;
  }
  command->graphInitializationCount = 0u;
#else
  GPU__UNUSED(command);
#endif
}

GPU_HIDE
void
vk_initExecutionGraph(GPUApiExecutionGraph *api) {
#ifdef VK_AMDX_shader_enqueue
  api->create          = vk_createExecutionGraph;
  api->destroy         = vk_destroyExecutionGraph;
  api->createInstance  = vk_createExecutionGraphInstance;
  api->destroyInstance = vk_destroyExecutionGraphInstance;
  api->getEntry        = vk_getExecutionGraphEntry;
  api->bind            = vk_bindExecutionGraph;
  api->dispatch        = vk_dispatchExecutionGraph;
  api->dispatchBuffer  = vk_dispatchExecutionGraphBuffer;
#else
  GPU__UNUSED(api);
#endif
}
