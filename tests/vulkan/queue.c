#include <gpu/gpu.h>

#include "backend/vk/common.h"
#include "api/buffer_internal.h"
#include "api/cmdqueue_internal.h"
#include "api/descr/descriptor_internal.h"
#include "api/device_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  VULKAN_COMMAND_BATCH_SIZE       = 4u,
  VULKAN_TRANSFER_TEST_BYTES      = 64u * 1024u,
  VULKAN_BUFFER_UPLOADS_PER_SLOT  = GPU_VK_BUFFER_TRANSFER_CAPACITY /
                                    VULKAN_TRANSFER_TEST_BYTES,
  VULKAN_TEXTURE_UPLOADS_PER_SLOT = GPU_VK_TEXTURE_TRANSFER_CAPACITY /
                                    VULKAN_TRANSFER_TEST_BYTES,
  VULKAN_WARM_ITERATIONS          = 256u
};

typedef struct CompletionProbe {
  GPUCommandBuffer *cmdb;
  uint32_t          count;
} CompletionProbe;

_Static_assert(sizeof(GPUPipelineStatisticsResult) == 11u * sizeof(uint64_t),
               "pipeline statistics result layout changed");

#define VULKAN_CHECK(label, expression)                                      \
  do {                                                                       \
    if (ok && !(expression)) {                                               \
      fprintf(stderr, "vulkan queue check failed: %s\n", label);            \
      ok = 0;                                                                \
    }                                                                        \
  } while (0)

static int
feature_set_contains(const GPUFeatureSet *set, GPUFeature feature) {
  if (!set || !set->pFeatures) {
    return 0;
  }

  for (uint32_t i = 0; i < set->featureCount; i++) {
    if (set->pFeatures[i] == feature) {
      return 1;
    }
  }
  return 0;
}

static void
on_complete(void *sender, GPUCommandBuffer *cmdb) {
  CompletionProbe *probe;

  probe       = sender;
  probe->cmdb = cmdb;
  probe->count++;
}

static int
descriptor_binding_path(GPUDevice *device,
                        GPUQueue  *queue,
                        GPUFence  *fence) {
  GPUDeviceVk                   *deviceVk;
  GPUBindGroupLayoutEntry        layoutEntry = {0};
  GPUBindGroupLayoutCreateInfo   layoutInfo = {0};
  GPUBindGroupEntry              groupEntry = {0};
  GPUBindGroupCreateInfo         groupInfo = {0};
  GPUPipelineLayoutCreateInfo    pipelineInfo = {0};
  GPUQueueSubmitInfo             submitInfo = {0};
  GPUBufferCreateInfo            bufferInfo = {0};
  GPUBindGroupLayout            *layouts[1];
  GPUBindGroupLayout            *layout;
  GPUBindGroupLayoutVk          *layoutVk;
  GPUPipelineLayout             *pipelineLayout;
  GPUPipelineLayoutVk           *pipelineLayoutVk;
  GPUBindGroup                  *group;
  GPUBindGroupVk                *groupVk;
  GPUBuffer                     *buffer;
  GPUCommandBuffer              *cmdb;
  GPUComputePassEncoder         *pass;
  GPUComputeEncoderVk           *passVk;
  int                            ok;

  deviceVk = device ? device->_priv : NULL;
  layout   = NULL;
  group    = NULL;
  buffer   = NULL;
  pipelineLayout = NULL;
  cmdb     = NULL;
  ok       = 0;
  if (!deviceVk) {
    return 0;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "vulkan-descriptor-buffer";
  bufferInfo.sizeBytes        = 256u;
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    goto cleanup;
  }

  layoutEntry.binding      = 0u;
  layoutEntry.bindingType  = GPU_BINDING_UNIFORM_BUFFER;
  layoutEntry.visibility   = GPU_SHADER_STAGE_COMPUTE_BIT;
  layoutEntry.arrayCount   = 1u;
  layoutInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label            = "vulkan-descriptor-layout";
  layoutInfo.entryCount       = 1u;
  layoutInfo.pEntries         = &layoutEntry;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK ||
      !layout) {
    goto cleanup;
  }

  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  groupEntry.buffer.buffer = buffer;
  groupEntry.buffer.size   = bufferInfo.sizeBytes;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "vulkan-descriptor-group";
  groupInfo.layout           = layout;
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &groupEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    goto cleanup;
  }

  layouts[0] = layout;
  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.bindGroupLayoutCount = 1u;
  pipelineInfo.ppBindGroupLayouts   = layouts;
  if (GPUCreatePipelineLayout(device,
                              &pipelineInfo,
                              &pipelineLayout) != GPU_OK ||
      !pipelineLayout ||
      GPUAcquireCommandBuffer(queue,
                              "vulkan-descriptor-bind",
                              &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb,
                                            "vulkan-descriptor-bind"))) {
    goto cleanup;
  }

  pipelineLayoutVk = pipelineLayout->_native;
  passVk           = pass->_priv;
  if (!pipelineLayoutVk || !passVk) {
    GPUEndComputePass(pass);
    goto cleanup;
  }
  pass->_pipelineLayout          = pipelineLayout;
  passVk->pipelineLayout         = pipelineLayoutVk->layout;
  passVk->descriptorPipelineLayout = pipelineLayoutVk;
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  ok = pass->_boundGroups[0] == group;
  GPUEndComputePass(pass);
  if (!ok) {
    goto cleanup;
  }

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, 5000000000ull) != GPU_OK) {
    ok = 0;
    goto cleanup;
  }

  layoutVk = layout->_native;
  groupVk  = group->_native;
  ok = layoutVk && groupVk &&
       layoutVk->descriptorBuffer == deviceVk->descriptorBuffer;
#ifdef VK_EXT_descriptor_buffer
  if (ok && deviceVk->descriptorBuffer) {
    ok = pipelineLayoutVk->descriptorBuffer && groupVk->descriptorChunk &&
         groupVk->set && buffer->_gpuAddress != 0u;
  } else
#endif
  if (ok) {
    ok = groupVk->set != VK_NULL_HANDLE;
  }

cleanup:
  GPUDestroyBindGroup(group);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(layout);
  GPUDestroyBuffer(buffer);
  return ok;
}

static int
wait_queue(GPUQueue *queue, GPUFence *fence) {
  GPUQueueSubmitInfo submitInfo = {0};
  GPUCommandBuffer  *cmdb;

  cmdb = NULL;
  if (!queue || !fence ||
      GPUAcquireCommandBuffer(queue, "vulkan-transfer-wait", &cmdb) != GPU_OK ||
      !cmdb) {
    return 0;
  }

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  return GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
         GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
}

static int
frame_time_roundtrip(GPUDevice *device,
                     GPUQueue  *queue,
                     GPUFence  *fence) {
  GPUFrameStats       stats;
  GPUQueueSubmitInfo  submitInfo = {0};
  GPUCommandBuffer   *cmdb;

  GPUResetStats(device);
  cmdb = NULL;
  if (GPUAcquireCommandBuffer(queue, "vulkan-frame-time", &cmdb) != GPU_OK ||
      !cmdb) {
    return 0;
  }

  cmdb->_recordsGPUFrameTime    = true;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, 5000000000ull) != GPU_OK ||
      GPUGetLastFrameStats(device, &stats) != GPU_OK) {
    return 0;
  }

  return stats.gpuFrameMs > 0.0;
}

static int
buffer_transfers_reuse(GPUDevice       *device,
                       GPUQueue        *queue,
                       GPUFence        *fence) {
  GPUQueueVk         *native;
  GPUBufferCreateInfo bufferInfo = {0};
  GPUTransferSlotVk   slots[GPU_VK_TRANSFER_SLOT_COUNT];
  GPUBuffer          *buffer;
  GPUBufferVk        *bufferNative;
  GPUBuffer          *readback;
  GPUBufferVk        *readbackNative;
  VkBuffer            readbackBuffer;
  VkDeviceMemory      readbackMemory;
  void               *readbackMapped;
  uint64_t            readbackCapacity;
  static uint8_t      upload[VULKAN_TRANSFER_TEST_BYTES];
  uint32_t            value;
  uint32_t            copied;
  int                 ok;

  native = queue ? queue->_priv : NULL;
  buffer = NULL;
  if (!native || !device) {
    return 0;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "vulkan-sync-buffer-transfer";
  bufferInfo.sizeBytes        = sizeof(upload);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    return 0;
  }

  bufferNative = buffer->_priv;
  ok = bufferNative && !bufferNative->mapped;
  for (uint32_t i = 0u;
       ok && i < GPU_VK_TRANSFER_SLOT_COUNT *
                  VULKAN_BUFFER_UPLOADS_PER_SLOT;
       i++) {
    value = UINT32_C(0x12340000) + i;
    memcpy(upload, &value, sizeof(value));
    ok = GPUQueueWriteBuffer(queue,
                             buffer,
                             0u,
                             upload,
                             sizeof(upload)) == GPU_OK;
  }
  copied = 0u;
  ok = ok && GPUQueueReadBuffer(queue,
                                buffer,
                                0u,
                                &copied,
                                sizeof(copied)) == GPU_OK &&
       copied == value && !native->transferOpen;
  if (!ok) {
    (void)wait_queue(queue, fence);
    GPUDestroyBuffer(buffer);
    return 0;
  }

  for (uint32_t i = 0u; i < GPU_VK_TRANSFER_SLOT_COUNT; i++) {
    GPUBufferVk *upload;

    slots[i] = native->transferSlots[i];
    upload   = slots[i].uploadStaging ? slots[i].uploadStaging->_priv : NULL;
    if (!slots[i].command || !slots[i].fence || !upload ||
        !upload->buffer || !upload->memory || !upload->mapped ||
        slots[i].uploadCapacity < VULKAN_TRANSFER_TEST_BYTES) {
      (void)wait_queue(queue, fence);
      GPUDestroyBuffer(buffer);
      return 0;
    }
  }

  readback         = native->readbackStaging;
  readbackNative   = readback ? readback->_priv : NULL;
  readbackBuffer   = readbackNative ? readbackNative->buffer : VK_NULL_HANDLE;
  readbackMemory   = readbackNative ? readbackNative->memory : VK_NULL_HANDLE;
  readbackMapped   = readbackNative ? readbackNative->mapped : NULL;
  readbackCapacity = native->readbackCapacity;
  if (!readbackNative || !readbackBuffer || !readbackMemory ||
      !readbackMapped || readbackCapacity < sizeof(value)) {
    (void)wait_queue(queue, fence);
    GPUDestroyBuffer(buffer);
    return 0;
  }

  for (uint32_t i = 0u; i < 16u; i++) {
    value  = UINT32_C(0xabc00000) + i;
    copied = 0u;
    if (GPUQueueWriteBuffer(queue,
                            buffer,
                            0u,
                            &value,
                            sizeof(value)) != GPU_OK ||
        GPUQueueReadBuffer(queue,
                           buffer,
                           0u,
                           &copied,
                           sizeof(copied)) != GPU_OK ||
        copied != value ||
        native->readbackStaging != readback ||
        native->readbackStaging->_priv != readbackNative ||
        readbackNative->buffer != readbackBuffer ||
        readbackNative->memory != readbackMemory ||
        readbackNative->mapped != readbackMapped ||
        native->readbackCapacity != readbackCapacity ||
        native->transferOpen) {
      ok = 0;
      break;
    }
  }

  for (uint32_t i = 0u; ok && i < GPU_VK_TRANSFER_SLOT_COUNT; i++) {
    GPUTransferSlotVk *slot;

    slot = &native->transferSlots[i];
    ok = slot->command == slots[i].command &&
         slot->fence == slots[i].fence &&
         slot->uploadStaging == slots[i].uploadStaging &&
         slot->uploadCapacity == slots[i].uploadCapacity;
  }
  ok = wait_queue(queue, fence) && ok;
  GPUDestroyBuffer(buffer);
  return ok;
}

static int
texture_uploads_reuse(GPUDevice       *device,
                      GPUQueue        *queue,
                      GPUFence        *fence) {
  GPUQueueVk            *native;
  GPUTextureCreateInfo   textureInfo = {0};
  GPUTextureWriteRegion writeRegion = {0};
  GPUTransferSlotVk      slots[GPU_VK_TRANSFER_SLOT_COUNT];
  GPUTexture            *texture;
  static uint8_t         pixels[VULKAN_TRANSFER_TEST_BYTES];
  int                    ok;

  native  = queue ? queue->_priv : NULL;
  texture = NULL;
  if (!native || !device) {
    return 0;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "vulkan-sync-texture-transfer";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 128u;
  textureInfo.height           = 128u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK ||
      !texture) {
    return 0;
  }

  writeRegion.width        = 128u;
  writeRegion.height       = 128u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = 128u * 4u;
  writeRegion.rowsPerImage = 128u;
  memset(pixels, 0, sizeof(pixels));
  pixels[3] = 255u;
  ok = 1;
  for (uint32_t i = 0u;
       ok && i < GPU_VK_TRANSFER_SLOT_COUNT *
                   VULKAN_TEXTURE_UPLOADS_PER_SLOT;
       i++) {
    pixels[0] = (uint8_t)i;
    ok = GPUQueueWriteTexture(queue,
                              texture,
                              &writeRegion,
                              pixels,
                              sizeof(pixels)) == GPU_OK;
  }
  ok = ok && wait_queue(queue, fence);
  for (uint32_t i = 0u; ok && i < GPU_VK_TRANSFER_SLOT_COUNT; i++) {
    GPUBufferVk *upload;

    slots[i] = native->transferSlots[i];
    upload   = slots[i].uploadStaging ? slots[i].uploadStaging->_priv : NULL;
    ok = slots[i].command && slots[i].fence && upload && upload->buffer &&
         upload->memory && upload->mapped &&
         slots[i].uploadCapacity >= sizeof(pixels);
  }
  for (uint32_t i = 0u; ok && i < 16u; i++) {
    pixels[0] = (uint8_t)i;
    pixels[1] = (uint8_t)(i + 1u);
    pixels[2] = (uint8_t)(i + 2u);
    if (GPUQueueWriteTexture(queue,
                             texture,
                             &writeRegion,
                             pixels,
                             sizeof(pixels)) != GPU_OK ||
        !native->transferOpen) {
      ok = 0;
      break;
    }
  }

  for (uint32_t i = 0u; ok && i < GPU_VK_TRANSFER_SLOT_COUNT; i++) {
    GPUTransferSlotVk *slot;

    slot = &native->transferSlots[i];
    ok = slot->command == slots[i].command &&
         slot->fence == slots[i].fence &&
         slot->uploadStaging == slots[i].uploadStaging &&
         slot->uploadCapacity == slots[i].uploadCapacity;
  }
  ok = wait_queue(queue, fence) && ok;
  GPUDestroyTexture(texture);
  return ok;
}

static int
submit_empty_batch(GPUQueue *queue,
                   GPUFence        *fence,
                   CompletionProbe *probe) {
  GPUCommandBuffer *buffers[VULKAN_COMMAND_BATCH_SIZE];
  GPUQueueSubmitInfo submitInfo = {0};

  memset(buffers, 0, sizeof(buffers));
  for (uint32_t i = 0; i < VULKAN_COMMAND_BATCH_SIZE; i++) {
    if (GPUAcquireCommandBuffer(queue,
                                "vulkan-empty",
                                &buffers[i]) != GPU_OK ||
        !buffers[i]) {
      return 0;
    }
  }
  if (probe) {
    GPUSetCommandBufferCompletionHandler(
      buffers[VULKAN_COMMAND_BATCH_SIZE - 1u],
      probe,
      on_complete
    );
  }

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = VULKAN_COMMAND_BATCH_SIZE;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = fence;
  return GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
         GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
}

static int
submit_empty_compute(GPUQueue *queue, GPUFence *fence) {
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  GPUQueueSubmitInfo     submitInfo = {0};

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(queue, "vulkan-empty-compute", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "vulkan-empty-compute"))) {
    return 0;
  }
  GPUEndComputePass(pass);

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  return GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
         GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
}

static int
timestamp_roundtrip(GPUDevice       *device,
                    GPUQueue        *queue,
                    GPUFence        *fence) {
  GPUQuerySetCreateInfo queryInfo = {0};
  GPUBufferCreateInfo   bufferInfo = {0};
  GPUQueueSubmitInfo    submitInfo = {0};
  GPUCommandBuffer     *cmdb;
  GPUQuerySet          *set;
  GPUBuffer            *buffer;
  uint64_t              timestamps[2] = {UINT64_MAX, UINT64_MAX};
  double                timestampPeriod;
  int                   ok;

  set    = NULL;
  buffer = NULL;
  cmdb   = NULL;
  ok     = 0;

  timestampPeriod = 0.0;
  if (GPUGetTimestampPeriod(queue, &timestampPeriod) != GPU_OK ||
      !(timestampPeriod > 0.0)) {
    goto cleanup;
  }

  queryInfo.chain.sType      = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  queryInfo.chain.structSize = sizeof(queryInfo);
  queryInfo.label            = "vulkan-timestamps";
  queryInfo.type             = GPU_QUERY_TIMESTAMP;
  queryInfo.count            = 2u;
  if (GPUCreateQuerySet(device, &queryInfo, &set) != GPU_OK || !set) {
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "vulkan-timestamp-results";
  bufferInfo.sizeBytes        = sizeof(timestamps);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    goto cleanup;
  }
  if (GPUQueueWriteBuffer(queue,
                          buffer,
                          0u,
                          timestamps,
                          sizeof(timestamps)) != GPU_OK) {
    goto cleanup;
  }
  if (GPUAcquireCommandBuffer(queue,
                              "vulkan-timestamp",
                              &cmdb) != GPU_OK || !cmdb) {
    goto cleanup;
  }

  GPUWriteTimestamp(cmdb, set, 0u);
  GPUWriteTimestamp(cmdb, set, 1u);
  GPUResolveQuerySet(cmdb, set, 0u, 2u, buffer, 0u);

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;
  if (GPUWaitFence(fence, UINT64_MAX) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffer,
                         0u,
                         timestamps,
                         sizeof(timestamps)) != GPU_OK ||
      timestamps[0] == UINT64_MAX || timestamps[1] == UINT64_MAX ||
      timestamps[1] < timestamps[0]) {
    goto cleanup;
  }

  ok = 1;

cleanup:
  GPUDestroyBuffer(buffer);
  GPUDestroyQuerySet(set);
  return ok;
}

static int
pipeline_statistics_roundtrip(GPUDevice       *device,
                              GPUQueue        *queue,
                              GPUFence        *fence) {
  GPUQuerySetCreateInfo  queryInfo  = {0};
  GPUBufferCreateInfo    bufferInfo = {0};
  GPUQueueSubmitInfo     submitInfo = {0};
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  GPUQuerySet           *set;
  GPUBuffer             *buffer;
  GPUCommandBuffer      *buffers[1];
  GPUPipelineStatisticsResult result;
  int ok;

  set    = NULL;
  buffer = NULL;
  cmdb   = NULL;
  ok     = 0;
  memset(&result, 0, sizeof(result));

  queryInfo.chain.sType       = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  queryInfo.chain.structSize  = sizeof(queryInfo);
  queryInfo.label             = "vulkan-pipeline-statistics";
  queryInfo.type              = GPU_QUERY_PIPELINE_STATISTICS;
  queryInfo.count             = 1u;
  queryInfo.pipelineStatsMask = GPU_PIPESTAT_ALL;
  if (GPUCreateQuerySet(device, &queryInfo, &set) != GPU_OK || !set) {
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "vulkan-pipeline-statistics-results";
  bufferInfo.sizeBytes        = sizeof(result);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer ||
      GPUAcquireCommandBuffer(queue,
                              "vulkan-pipeline-statistics",
                              &cmdb) != GPU_OK || !cmdb) {
    goto cleanup;
  }

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = fence;

  GPUBeginPipelineStatisticsQuery(cmdb, set, 0u);
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT ||
      !(pass = GPUBeginComputePass(cmdb, "vulkan-pipeline-statistics"))) {
    goto cleanup;
  }
  GPUEndComputePass(pass);
  GPUEndPipelineStatisticsQuery(cmdb, set);
  GPUResolveQuerySet(cmdb, set, 0u, 1u, buffer, 0u);

  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;
  if (GPUWaitFence(fence, UINT64_MAX) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffer,
                         0u,
                         &result,
                         sizeof(result)) != GPU_OK) {
    goto cleanup;
  }

  ok = 1;

cleanup:
  GPUDestroyBuffer(buffer);
  GPUDestroyQuerySet(set);
  return ok;
}

static int
occlusion_roundtrip(GPUDevice       *device,
                    GPUQueue        *queue,
                    GPUFence        *fence) {
  GPUTextureCreateInfo           textureInfo = {0};
  GPUTextureCreateInfo           resolveTextureInfo = {0};
  GPUTextureCreateInfo           depthTextureInfo = {0};
  GPUTextureViewCreateInfo       viewInfo = {0};
  GPUTextureViewCreateInfo       depthViewInfo = {0};
  GPUQuerySetCreateInfo          queryInfo = {0};
  GPUBufferCreateInfo            bufferInfo = {0};
  GPURenderPassColorAttachment   colors[2] = {{0}};
  GPURenderPassDepthStencilAttachment depthStencil = {0};
  GPURenderPassCreateInfo        passInfo = {0};
  GPUQueueSubmitInfo             submitInfo = {0};
  GPUTexture                    *texture;
  GPUTexture                    *texture2;
  GPUTexture                    *resolveTexture;
  GPUTexture                    *resolveTexture2;
  GPUTexture                    *depthTexture;
  GPUTextureView                *view;
  GPUTextureView                *view2;
  GPUTextureView                *resolveView;
  GPUTextureView                *resolveView2;
  GPUTextureView                *depthView;
  GPUQuerySet                   *querySet;
  GPUBuffer                     *resultBuffer;
  GPUCommandBuffer              *cmdb;
  GPURenderPassEncoder          *pass;
  uint64_t                       resultValue;
  int                            ok;

  texture         = NULL;
  texture2        = NULL;
  resolveTexture  = NULL;
  resolveTexture2 = NULL;
  depthTexture    = NULL;
  view            = NULL;
  view2           = NULL;
  resolveView     = NULL;
  resolveView2    = NULL;
  depthView       = NULL;
  querySet        = NULL;
  resultBuffer    = NULL;
  cmdb            = NULL;
  pass            = NULL;
  resultValue     = UINT64_MAX;
  ok              = 0;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "vulkan-occlusion-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_BGRA8_UNORM;
  textureInfo.width            = 4u;
  textureInfo.height           = 4u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 4u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET;
  viewInfo.chain.sType         = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize    = sizeof(viewInfo);
  viewInfo.label               = "vulkan-occlusion-target-view";
  viewInfo.viewType            = GPU_TEXTURE_VIEW_2D;
  viewInfo.format              = GPU_FORMAT_BGRA8_UNORM;
  viewInfo.mipLevelCount       = 1u;
  viewInfo.arrayLayerCount     = 1u;
  resolveTextureInfo             = textureInfo;
  resolveTextureInfo.label       = "vulkan-resolve-target";
  resolveTextureInfo.sampleCount = 1u;
  depthTextureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  depthTextureInfo.chain.structSize = sizeof(depthTextureInfo);
  depthTextureInfo.label            = "vulkan-depth-stencil-target";
  depthTextureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  depthTextureInfo.format           = GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
  depthTextureInfo.width            = 4u;
  depthTextureInfo.height           = 4u;
  depthTextureInfo.depthOrLayers    = 1u;
  depthTextureInfo.mipLevelCount    = 1u;
  depthTextureInfo.sampleCount      = 4u;
  depthTextureInfo.usage            = GPU_TEXTURE_USAGE_DEPTH_STENCIL;
  depthViewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  depthViewInfo.chain.structSize = sizeof(depthViewInfo);
  depthViewInfo.label            = "vulkan-depth-stencil-view";
  depthViewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  depthViewInfo.format           = GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
  depthViewInfo.mipLevelCount    = 1u;
  depthViewInfo.arrayLayerCount  = 1u;
  queryInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  queryInfo.chain.structSize   = sizeof(queryInfo);
  queryInfo.label              = "vulkan-occlusion";
  queryInfo.type               = GPU_QUERY_OCCLUSION;
  queryInfo.count              = 1u;
  bufferInfo.chain.sType       = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize  = sizeof(bufferInfo);
  bufferInfo.label             = "vulkan-occlusion-result";
  bufferInfo.sizeBytes         = sizeof(resultValue);
  bufferInfo.usage             = GPU_BUFFER_USAGE_COPY_DST |
                                 GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture ||
      GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK || !view ||
      GPUCreateTexture(device, &textureInfo, &texture2) != GPU_OK || !texture2 ||
      GPUCreateTextureView(texture2, &viewInfo, &view2) != GPU_OK || !view2 ||
      GPUCreateTexture(device, &resolveTextureInfo, &resolveTexture) != GPU_OK ||
      !resolveTexture ||
      GPUCreateTextureView(resolveTexture, &viewInfo, &resolveView) != GPU_OK ||
      !resolveView ||
      GPUCreateTexture(device,
                       &resolveTextureInfo,
                       &resolveTexture2) != GPU_OK ||
      !resolveTexture2 ||
      GPUCreateTextureView(resolveTexture2,
                           &viewInfo,
                           &resolveView2) != GPU_OK ||
      !resolveView2 ||
      GPUCreateTexture(device, &depthTextureInfo, &depthTexture) != GPU_OK ||
      !depthTexture ||
      GPUCreateTextureView(depthTexture, &depthViewInfo, &depthView) != GPU_OK ||
      !depthView ||
      GPUCreateQuerySet(device, &queryInfo, &querySet) != GPU_OK || !querySet ||
      GPUCreateBuffer(device, &bufferInfo, &resultBuffer) != GPU_OK ||
      !resultBuffer ||
      GPUAcquireCommandBuffer(queue, "vulkan-occlusion", &cmdb) != GPU_OK ||
      !cmdb) {
    goto cleanup;
  }

  colors[0].view                = view;
  colors[0].resolveView         = resolveView;
  colors[0].loadOp              = GPU_LOAD_OP_CLEAR;
  colors[0].storeOp             = GPU_STORE_OP_STORE;
  colors[1]                     = colors[0];
  colors[1].view                = view2;
  colors[1].resolveView         = resolveView2;
  depthStencil.view             = depthView;
  depthStencil.depthLoadOp      = GPU_LOAD_OP_CLEAR;
  depthStencil.depthStoreOp     = GPU_STORE_OP_STORE;
  depthStencil.stencilLoadOp    = GPU_LOAD_OP_CLEAR;
  depthStencil.stencilStoreOp   = GPU_STORE_OP_STORE;
  depthStencil.clearDepth       = 1.0f;
  depthStencil.clearStencil     = 7u;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "vulkan-occlusion";
  passInfo.occlusionQuerySet    = querySet;
  passInfo.colorAttachmentCount = 2u;
  passInfo.pColorAttachments    = colors;
  passInfo.pDepthStencilAttachment = &depthStencil;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    goto cleanup;
  }

  GPUBeginOcclusionQuery(pass, querySet, 0u);
  if (!pass->_occlusionQueryActive) {
    goto cleanup;
  }
  GPUEndOcclusionQuery(pass);
  GPUEndRenderPass(pass);
  pass = NULL;
  GPUResolveQuerySet(cmdb, querySet, 0u, 1u, resultBuffer, 0u);

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         resultBuffer,
                         0u,
                         &resultValue,
                         sizeof(resultValue)) != GPU_OK ||
      resultValue != 0u) {
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;

  passInfo.occlusionQuerySet = NULL;
  GPUResetStats(device);
  for (uint32_t i = 0u; i < VULKAN_WARM_ITERATIONS; i++) {
    colors[0].loadOp  = (GPULoadOp)(i % 3u);
    colors[0].storeOp = (GPUStoreOp)((i / 3u) % 2u);
    colors[1].loadOp  = (GPULoadOp)((i / 2u) % 3u);
    colors[1].storeOp = (GPUStoreOp)((i / 5u) % 2u);
    depthStencil.depthLoadOp    = (GPULoadOp)((i / 2u) % 3u);
    depthStencil.depthStoreOp   = (GPUStoreOp)((i / 5u) % 2u);
    depthStencil.stencilLoadOp  = (GPULoadOp)((i / 3u) % 3u);
    depthStencil.stencilStoreOp = (GPUStoreOp)((i / 7u) % 2u);
    passInfo.colorAttachmentCount = i % 3u;
    if (GPUAcquireCommandBuffer(queue,
                                "vulkan-offscreen-warm",
                                &cmdb) != GPU_OK ||
        !cmdb || !(pass = GPUBeginRenderPass(cmdb, &passInfo))) {
      goto cleanup;
    }
    GPUEndRenderPass(pass);
    pass = NULL;

    submitInfo.ppCommandBuffers = &cmdb;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
        GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
      cmdb = NULL;
      goto cleanup;
    }
    cmdb = NULL;
  }
  passInfo.colorAttachmentCount = 2u;
  if (device->currentFrameStats.hotPathAllocCount != 0u ||
      device->currentFrameStats.hotPathFreeCount != 0u) {
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (pass) {
    GPUEndOcclusionQuery(pass);
    GPUEndRenderPass(pass);
  }
  GPUDestroyBuffer(resultBuffer);
  GPUDestroyQuerySet(querySet);
  GPUDestroyTextureView(depthView);
  GPUDestroyTexture(depthTexture);
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);
  GPUDestroyTextureView(view2);
  GPUDestroyTexture(texture2);
  GPUDestroyTextureView(resolveView);
  GPUDestroyTexture(resolveTexture);
  GPUDestroyTextureView(resolveView2);
  GPUDestroyTexture(resolveTexture2);
  return ok;
}

int
main(void) {
  GPUInstanceCreateInfo  instanceInfo  = {0};
  GPUDeviceCreateInfo    deviceInfo    = {0};
  GPURuntimeConfig       runtimeConfig = {0};
  GPUAdapterCapabilities adapterCaps   = {0};
  GPUDeviceCapabilities  deviceCaps    = {0};
  CompletionProbe        probe         = {0};
  GPUFrameStats          stats;
  GPUInstance           *instance;
  GPUAdapter            *adapter;
  GPUDevice             *device;
  GPUQueue              *graphics;
  GPUQueue              *compute;
  GPUFence              *fence;
  GPUResult              result;
  GPUFeature             requiredFeatures[5];
  uint32_t               adapterCount;
  uint32_t               requiredFeatureCount;
  bool                   pipelineStatsSupported;
  int                    ok;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  instanceInfo.enableValidation = true;
  instance = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "vulkan instance failed\n");
    return 1;
  }

  adapter      = NULL;
  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    fprintf(stderr, "vulkan adapter failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }
  pipelineStatsSupported =
    GPUIsFeatureSupported(adapter, GPU_FEATURE_PIPELINE_STATISTICS);
  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_COMPUTE) ||
      !GPUIsFeatureSupported(adapter, GPU_FEATURE_TIMESTAMPS) ||
      !GPUIsFeatureSupported(adapter, GPU_FEATURE_INDIRECT_DRAW) ||
      !GPUIsFeatureSupported(adapter, GPU_FEATURE_MULTI_DRAW) ||
      GPUGetAdapterCapabilities(adapter, &adapterCaps) != GPU_OK ||
      feature_set_contains(&adapterCaps.supported,
                           GPU_FEATURE_PIPELINE_STATISTICS) !=
        pipelineStatsSupported) {
    fprintf(stderr, "vulkan feature reporting failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }

  requiredFeatures[0]                = GPU_FEATURE_COMPUTE;
  requiredFeatures[1]                = GPU_FEATURE_TIMESTAMPS;
  requiredFeatures[2]                = GPU_FEATURE_INDIRECT_DRAW;
  requiredFeatures[3]                = GPU_FEATURE_MULTI_DRAW;
  requiredFeatureCount               = 4u;
  if (pipelineStatsSupported) {
    requiredFeatures[requiredFeatureCount++] =
      GPU_FEATURE_PIPELINE_STATISTICS;
  }
  deviceInfo.chain.sType             = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize        = sizeof(deviceInfo);
  deviceInfo.required.featureCount   = requiredFeatureCount;
  deviceInfo.required.pFeatures      = requiredFeatures;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "vulkan device failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_COMPUTE) ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_TIMESTAMPS) ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_INDIRECT_DRAW) ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_MULTI_DRAW) ||
      GPUIsFeatureEnabled(device, GPU_FEATURE_PIPELINE_STATISTICS) !=
        pipelineStatsSupported ||
      GPUGetDeviceCapabilities(device, &deviceCaps) != GPU_OK ||
      feature_set_contains(&deviceCaps.enabled,
                           GPU_FEATURE_PIPELINE_STATISTICS) !=
        pipelineStatsSupported) {
    fprintf(stderr, "vulkan enabled feature reporting failed\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  runtimeConfig.chain.sType       = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize  = sizeof(runtimeConfig);
  runtimeConfig.validationMode    = GPU_VALIDATION_FULL;
  runtimeConfig.enableVerboseLogs = true;
  runtimeConfig.enableStats       = true;
  GPUConfigureRuntime(device, &runtimeConfig);

  graphics = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  compute  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!graphics || !compute ||
      !(GPUGetAvailableQueueBits(device) & GPU_QUEUE_GRAPHICS) ||
      !(GPUGetAvailableQueueBits(device) & GPU_QUEUE_COMPUTE)) {
    fprintf(stderr, "vulkan queues failed\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  fence = NULL;
  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "vulkan fence failed\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  ok = 1;
  VULKAN_CHECK("descriptor binding path",
               descriptor_binding_path(device, compute, fence));
  VULKAN_CHECK("buffer transfer reuse",
               buffer_transfers_reuse(device, graphics, fence));
  VULKAN_CHECK("texture upload reuse",
               texture_uploads_reuse(device, graphics, fence));
  VULKAN_CHECK("timestamp roundtrip",
               timestamp_roundtrip(device, graphics, fence));
  VULKAN_CHECK("frame time roundtrip",
               frame_time_roundtrip(device, graphics, fence));
  VULKAN_CHECK("occlusion roundtrip",
               occlusion_roundtrip(device, graphics, fence));
  if (pipelineStatsSupported) {
    VULKAN_CHECK("pipeline statistics roundtrip",
                 pipeline_statistics_roundtrip(device, graphics, fence));
  }
  VULKAN_CHECK("graphics batch submit",
               submit_empty_batch(graphics, fence, &probe));
  VULKAN_CHECK("compute submit", submit_empty_compute(compute, fence));
  VULKAN_CHECK("completion callback",
               probe.count == 1u && probe.cmdb != NULL);
  GPUResetStats(device);
  for (uint32_t i = 0; ok && i < VULKAN_WARM_ITERATIONS; i++) {
    ok = submit_empty_batch(graphics, fence, NULL) &&
         submit_empty_compute(compute, fence);
  }

  device->lastFrameStats = device->currentFrameStats;
  memset(&stats, 0, sizeof(stats));
  ok = ok && GPUGetLastFrameStats(device, &stats) == GPU_OK &&
       stats.hotPathAllocCount == 0u &&
       stats.hotPathFreeCount == 0u;

  GPUDestroyFence(fence);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);

  if (!ok) {
    fprintf(stderr,
            "vulkan warm path failed: %llu allocs, %llu frees\n",
            (unsigned long long)stats.hotPathAllocCount,
            (unsigned long long)stats.hotPathFreeCount);
    return 1;
  }

  puts("Vulkan queue validation passed");
  return 0;
}
