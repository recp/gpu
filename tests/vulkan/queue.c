#include <gpu/gpu.h>

#include "api/device_internal.h"
#include "api/query_internal.h"
#include "backend/vk/common.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  VULKAN_COMMAND_BATCH_SIZE = 4u,
  VULKAN_WARM_ITERATIONS    = 256u
};

typedef struct CompletionProbe {
  GPUCommandBuffer *cmdb;
  uint32_t          count;
} CompletionProbe;

typedef struct VulkanTestQuerySet {
  VkDevice    device;
  VkQueryPool pool;
} VulkanTestQuerySet;

_Static_assert(sizeof(GPUPipelineStatisticsResult) == 11u * sizeof(uint64_t),
               "pipeline statistics result layout changed");

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
submit_empty_batch(GPUCommandQueue *queue,
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
submit_empty_compute(GPUCommandQueue *queue, GPUFence *fence) {
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
                    GPUCommandQueue *queue,
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
                              GPUCommandQueue *queue,
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
                    GPUCommandQueue *queue,
                    GPUFence        *fence) {
  GPUDeviceVk                   *deviceVk;
  GPUCommandBufferVk            *command;
  GPUTextureVk                  *textureVk;
  GPUTextureViewVk              *viewVk;
  VulkanTestQuerySet            *queryVk;
  GPUTextureCreateInfo           textureInfo = {0};
  GPUTextureViewCreateInfo       viewInfo = {0};
  GPUQuerySetCreateInfo          queryInfo = {0};
  GPUBufferCreateInfo            bufferInfo = {0};
  GPUQueueSubmitInfo             submitInfo = {0};
  GPURenderPassEncoder           pass = {0};
  GPURenderEncoderVk             encoder = {0};
  VkAttachmentDescription       attachment = {0};
  VkAttachmentReference         colorReference = {0};
  VkSubpassDescription          subpass = {0};
  VkRenderPassCreateInfo        renderPassInfo = {0};
  VkFramebufferCreateInfo       framebufferInfo = {0};
  VkImageMemoryBarrier          imageBarrier = {0};
  VkRenderPassBeginInfo         beginInfo = {0};
  VkClearValue                  clearValue = {0};
  GPUTexture                   *texture;
  GPUTextureView               *view;
  GPUQuerySet                  *querySet;
  GPUBuffer                    *resultBuffer;
  GPUCommandBuffer             *cmdb;
  GPUCommandBuffer             *buffers[1];
  VkRenderPass                  renderPass;
  VkFramebuffer                 framebuffer;
  uint64_t                      resultValue;
  bool                          nativePassActive;
  int                           ok;

  deviceVk         = device ? device->_priv : NULL;
  texture          = NULL;
  view             = NULL;
  querySet         = NULL;
  resultBuffer     = NULL;
  cmdb             = NULL;
  renderPass       = VK_NULL_HANDLE;
  framebuffer      = VK_NULL_HANDLE;
  resultValue      = UINT64_MAX;
  nativePassActive = false;
  ok               = 0;
  if (!deviceVk || !deviceVk->device) {
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "vulkan-occlusion-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_BGRA8_UNORM;
  textureInfo.width            = 4u;
  textureInfo.height           = 4u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET;
  viewInfo.chain.sType         = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize    = sizeof(viewInfo);
  viewInfo.label               = "vulkan-occlusion-target-view";
  viewInfo.viewType            = GPU_TEXTURE_VIEW_2D;
  viewInfo.format              = GPU_FORMAT_BGRA8_UNORM;
  viewInfo.mipLevelCount       = 1u;
  viewInfo.arrayLayerCount     = 1u;
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
      GPUCreateQuerySet(device, &queryInfo, &querySet) != GPU_OK || !querySet ||
      GPUCreateBuffer(device, &bufferInfo, &resultBuffer) != GPU_OK ||
      !resultBuffer ||
      GPUAcquireCommandBuffer(queue, "vulkan-occlusion", &cmdb) != GPU_OK ||
      !cmdb) {
    goto cleanup;
  }

  textureVk = texture->_priv;
  viewVk    = view->_priv;
  queryVk   = querySet->_priv;
  command   = cmdb->_priv;
  if (!textureVk || !textureVk->image || !viewVk || !viewVk->view ||
      !queryVk || !queryVk->pool || !command || !command->command) {
    goto cleanup;
  }

  attachment.format         = VK_FORMAT_B8G8R8A8_UNORM;
  attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorReference.attachment = 0u;
  colorReference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  subpass.pipelineBindPoint      = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount  = 1u;
  subpass.pColorAttachments      = &colorReference;
  renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = 1u;
  renderPassInfo.pAttachments    = &attachment;
  renderPassInfo.subpassCount    = 1u;
  renderPassInfo.pSubpasses      = &subpass;
  if (vkCreateRenderPass(deviceVk->device,
                         &renderPassInfo,
                         NULL,
                         &renderPass) != VK_SUCCESS) {
    goto cleanup;
  }

  framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass      = renderPass;
  framebufferInfo.attachmentCount = 1u;
  framebufferInfo.pAttachments    = &viewVk->view;
  framebufferInfo.width           = 4u;
  framebufferInfo.height          = 4u;
  framebufferInfo.layers          = 1u;
  if (vkCreateFramebuffer(deviceVk->device,
                          &framebufferInfo,
                          NULL,
                          &framebuffer) != VK_SUCCESS) {
    goto cleanup;
  }

  imageBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  imageBarrier.srcAccessMask       = 0u;
  imageBarrier.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  imageBarrier.oldLayout           = textureVk->layout;
  imageBarrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  imageBarrier.image               = textureVk->image;
  imageBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  imageBarrier.subresourceRange.baseMipLevel   = 0u;
  imageBarrier.subresourceRange.levelCount     = 1u;
  imageBarrier.subresourceRange.baseArrayLayer = 0u;
  imageBarrier.subresourceRange.layerCount     = 1u;
  vkCmdPipelineBarrier(command->command,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       0u,
                       0u,
                       NULL,
                       0u,
                       NULL,
                       1u,
                       &imageBarrier);
  textureVk->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  vkCmdResetQueryPool(command->command, queryVk->pool, 0u, querySet->count);
  beginInfo.sType                   = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  beginInfo.renderPass              = renderPass;
  beginInfo.framebuffer             = framebuffer;
  beginInfo.renderArea.extent.width  = 4u;
  beginInfo.renderArea.extent.height = 4u;
  beginInfo.clearValueCount         = 1u;
  beginInfo.pClearValues            = &clearValue;
  vkCmdBeginRenderPass(command->command,
                       &beginInfo,
                       VK_SUBPASS_CONTENTS_INLINE);
  nativePassActive = true;

  encoder.command         = command->command;
  pass._priv              = &encoder;
  pass._cmdb              = cmdb;
  pass._occlusionQuerySet = querySet;
  cmdb->_activeEncoder    = true;
  GPUBeginOcclusionQuery(&pass, querySet, 0u);
  if (!pass._occlusionQueryActive) {
    goto cleanup;
  }
  GPUEndOcclusionQuery(&pass);
  vkCmdEndRenderPass(command->command);
  nativePassActive     = false;
  pass._ended          = true;
  cmdb->_activeEncoder = false;
  GPUResolveQuerySet(cmdb, querySet, 0u, 1u, resultBuffer, 0u);

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
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
  ok   = 1;

cleanup:
  if (nativePassActive) {
    GPUEndOcclusionQuery(&pass);
    vkCmdEndRenderPass(command->command);
    pass._ended          = true;
    cmdb->_activeEncoder = false;
  }
  if (framebuffer) {
    vkDestroyFramebuffer(deviceVk->device, framebuffer, NULL);
  }
  if (renderPass) {
    vkDestroyRenderPass(deviceVk->device, renderPass, NULL);
  }
  GPUDestroyBuffer(resultBuffer);
  GPUDestroyQuerySet(querySet);
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);
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
  GPUCommandQueue       *graphics;
  GPUCommandQueue       *compute;
  GPUFence              *fence;
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
  if (GPUEnumerateAdapters(instance, &adapterCount, &adapter) != GPU_OK ||
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

  runtimeConfig.chain.sType         = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize    = sizeof(runtimeConfig);
  runtimeConfig.validationMode      = GPU_VALIDATION_FULL;
  runtimeConfig.enableVerboseLogs = true;
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

  ok = timestamp_roundtrip(device, graphics, fence) &&
       occlusion_roundtrip(device, graphics, fence) &&
       (!pipelineStatsSupported ||
        pipeline_statistics_roundtrip(device, graphics, fence)) &&
       submit_empty_batch(graphics, fence, &probe) &&
       submit_empty_compute(compute, fence) &&
       probe.count == 1u && probe.cmdb != NULL;
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
