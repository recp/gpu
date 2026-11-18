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

#include "test.h"
#include "../../src/api/cmdqueue_internal.h"

_Static_assert(sizeof(GPUPipelineStatisticsResult) == 11u * sizeof(uint64_t),
               "pipeline statistics result layout changed");

static int
check_query_set_create_validation(GPUDevice *device) {
  GPUQuerySetCreateInfo info = {0};
  GPUQueue             *queue;
  GPUQuerySet          *set;
  double                timestampPeriod;
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.type             = GPU_QUERY_TIMESTAMP;
  info.count            = 2u;

  set = (GPUQuerySet *)(uintptr_t)1u;

  if (GPUCreateQuerySet(NULL, &info, &set) != GPU_ERROR_INVALID_ARGUMENT
      || set != NULL) {
    fprintf(stderr, "query set create accepted null device\n");
    return 0;
  }

  if (GPUCreateQuerySet(device, &info, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "query set create accepted null output\n");
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  set              = (GPUQuerySet *)(uintptr_t)1u;

  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT
      || set != NULL) {
    fprintf(stderr, "query set create accepted wrong sType\n");
    return 0;
  }

  info.chain.sType      = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  info.chain.structSize = (uint32_t)(sizeof(info) - 1u);
  set                   = (GPUQuerySet *)(uintptr_t)1u;

  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT
      || set != NULL) {
    fprintf(stderr, "query set create accepted short structSize\n");
    return 0;
  }

  info.chain.structSize = sizeof(info);
  info.count            = 0u;
  set                   = (GPUQuerySet *)(uintptr_t)1u;

  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT
      || set != NULL) {
    fprintf(stderr, "query set create accepted zero count\n");
    return 0;
  }

  info.count = 2u;
  info.type  = (GPUQueryType)99;
  set        = (GPUQuerySet *)(uintptr_t)1u;

  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT
      || set != NULL) {
    fprintf(stderr, "query set create accepted invalid query type\n");
    return 0;
  }

  info.type              = GPU_QUERY_TIMESTAMP;
  info.pipelineStatsMask = GPU_PIPESTAT_VERTEX_SHADER_INVOCATIONS;
  set                    = (GPUQuerySet *)(uintptr_t)1u;

  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT
      || set != NULL) {
    fprintf(stderr, "timestamp query accepted pipeline stats mask\n");
    return 0;
  }

  info.pipelineStatsMask = 0u;
  set                    = (GPUQuerySet *)(uintptr_t)1u;

  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_UNSUPPORTED
      || set != NULL) {
    fprintf(stderr, "timestamp query did not report unsupported\n");
    return 0;
  }

  queue           = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  timestampPeriod = 1.0;

  if (!queue
      || GPUGetTimestampPeriod(queue, &timestampPeriod) != GPU_ERROR_UNSUPPORTED
      || timestampPeriod != 0.0) {
    fprintf(stderr, "disabled timestamp period did not report unsupported\n");
    return 0;
  }

  info.type = GPU_QUERY_OCCLUSION;
  set       = NULL;
  result    = GPUCreateQuerySet(device, &info, &set);

  if ((result != GPU_OK && result != GPU_ERROR_UNSUPPORTED)
      || (result == GPU_OK) != (set != NULL)) {
    fprintf(stderr, "occlusion query set create failed\n");
    return 0;
  }

  if (set) {
    GPUDestroyQuerySet(set);
  }

  set = NULL;

  info.type              = GPU_QUERY_PIPELINE_STATISTICS;
  info.pipelineStatsMask = 0u;
  set                    = (GPUQuerySet *)(uintptr_t)1u;

  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT
      || set != NULL) {
    fprintf(stderr, "pipeline statistics query accepted zero mask\n");
    return 0;
  }

  info.pipelineStatsMask = 1u << 31;
  set                    = (GPUQuerySet *)(uintptr_t)1u;

  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT
      || set != NULL) {
    fprintf(stderr, "pipeline statistics query accepted unknown mask\n");
    return 0;
  }

  info.pipelineStatsMask = GPU_PIPESTAT_VERTEX_SHADER_INVOCATIONS |
                           GPU_PIPESTAT_FRAGMENT_SHADER_INVOCATIONS;
  set                    = (GPUQuerySet *)(uintptr_t)1u;

  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_UNSUPPORTED
      || set != NULL) {
    fprintf(stderr, "pipeline statistics query did not report unsupported\n");
    return 0;
  }

  GPUDestroyQuerySet(NULL);

  return 1;
}

static int
check_query_commands_are_safe_noops(void) {
  GPURenderPassEncoder endedPass     = {0};
  GPUCommandBuffer     submittedCmdb = {0};
  double               timestampPeriod;

  endedPass._ended         = true;
  submittedCmdb._submitted = true;

  GPUBeginOcclusionQuery(NULL, NULL, 0u);
  GPUBeginOcclusionQuery(&endedPass, NULL, 0u);
  GPUEndOcclusionQuery(NULL);
  GPUEndOcclusionQuery(&endedPass);
  GPUBeginPipelineStatisticsQuery(NULL, NULL, 0u);
  GPUBeginPipelineStatisticsQuery(&submittedCmdb, NULL, 0u);
  GPUEndPipelineStatisticsQuery(NULL, NULL);
  GPUEndPipelineStatisticsQuery(&submittedCmdb, NULL);
  GPUResolveQuerySet(NULL, NULL, 0u, 0u, NULL, 0u);
  GPUResolveQuerySet(&submittedCmdb, NULL, 0u, 0u, NULL, 0u);

  timestampPeriod = 1.0;

  if (GPUGetTimestampPeriod(NULL, &timestampPeriod) != GPU_ERROR_INVALID_ARGUMENT
      || timestampPeriod != 0.0
      || GPUGetTimestampPeriod(NULL, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "timestamp period accepted invalid arguments\n");
    return 0;
  }

  return 1;
}

static int
check_timestamp_query_roundtrip(GPUAdapter *adapter) {
  GPUDevice                   *device      = NULL;
  GPUQueue                    *queue       = NULL;
  GPUCommandBuffer            *cmdb        = NULL;
  GPUQuerySet                 *set         = NULL;
  GPUBuffer                   *buffer      = NULL;
  GPUTexture                  *target      = NULL;
  GPUTextureView              *targetView  = NULL;
  GPUFence                    *fence       = NULL;
  GPUComputePassEncoder       *computePass = NULL;
  GPURenderPassEncoder        *renderPass  = NULL;
  GPUCommandBuffer            *buffers[1];
  const char                  *metalMode;
  GPUDeviceCreateInfo          deviceInfo      = {0};
  GPUQuerySetCreateInfo        queryInfo       = {0};
  GPUBufferCreateInfo          bufferInfo      = {0};
  GPUTextureCreateInfo         textureInfo     = {0};
  GPUTextureViewCreateInfo     viewInfo        = {0};
  GPUFenceCreateInfo           fenceInfo       = {0};
  GPUQueueSubmitInfo           submitInfo      = {0};
  GPUComputePassCreateInfo     computeInfo     = {0};
  GPURenderPassColorAttachment color           = {0};
  GPURenderPassCreateInfo      renderInfo      = {0};
  GPUPassTimestampWrites       timestampWrites = {0};
  GPUDeviceCapabilities        caps            = {0};
  GPUFeature                   timestampFeature = GPU_FEATURE_TIMESTAMPS;
  GPUResult                    periodResult;
  uint64_t                     queryResults[5] = {
    UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX
  };
  double                       timestampPeriod;
  int                          ok = 0;

  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_TIMESTAMPS)) {
    return 1;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.featureCount = 1;
  deviceInfo.required.pFeatures    = &timestampFeature;

  if (gpu_test_create_device(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "timestamp feature device create failed\n");
    return 0;
  }

  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_TIMESTAMPS)
      || GPUGetDeviceCapabilities(device, &caps) != GPU_OK
      || caps.enabled.featureCount == 0) {
    fprintf(stderr, "timestamp feature was not enabled\n");
    goto cleanup;
  }

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0))) {
    fprintf(stderr, "timestamp query device missing graphics queue\n");
    goto cleanup;
  }

  metalMode       = getenv("GPU_METAL_MODE");
  timestampPeriod = 0.0;
  periodResult    = GPUGetTimestampPeriod(queue, &timestampPeriod);

  if (periodResult != GPU_OK && periodResult != GPU_ERROR_UNSUPPORTED) {
    fprintf(stderr, "timestamp period query failed\n");
    goto cleanup;
  }

  if (periodResult == GPU_OK && !(timestampPeriod > 0.0)) {
    fprintf(stderr, "timestamp period was invalid\n");
    goto cleanup;
  }

  if (metalMode && strcmp(metalMode, "metal4") == 0
      && periodResult != GPU_OK) {
    fprintf(stderr, "Metal 4 timestamp period was unavailable\n");
    goto cleanup;
  }

  if (metalMode && strcmp(metalMode, "classic") == 0
      && (periodResult != GPU_ERROR_UNSUPPORTED || timestampPeriod != 0.0)) {
    fprintf(stderr, "classic Metal exposed an uncalibrated timestamp period\n");
    goto cleanup;
  }

  queryInfo.chain.sType      = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  queryInfo.chain.structSize = sizeof(queryInfo);
  queryInfo.label            = "api-test-timestamps";
  queryInfo.type             = GPU_QUERY_TIMESTAMP;
  queryInfo.count            = 4u;

  if (GPUCreateQuerySet(device, &queryInfo, &set) != GPU_OK || !set) {
    fprintf(stderr, "timestamp query set create failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(queryResults);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;

  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    fprintf(stderr, "timestamp resolve buffer create failed\n");
    goto cleanup;
  }

  if (GPUQueueWriteBuffer(queue,
                          buffer,
                          0u,
                          queryResults,
                          sizeof(queryResults)) != GPU_OK) {
    fprintf(stderr, "timestamp resolve buffer initialize failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "timestamp-query-render-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 4u;
  textureInfo.height           = 4u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET;

  if (GPUCreateTexture(device, &textureInfo, &target) != GPU_OK || !target) {
    fprintf(stderr, "timestamp render target create failed\n");
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "timestamp-query-render-target-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;

  if (GPUCreateTextureView(target, &viewInfo, &targetView) != GPU_OK
      || !targetView) {
    fprintf(stderr, "timestamp render target view create failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue,
                              "timestamp-query-test",
                              &cmdb) != GPU_OK
      || !cmdb) {
    fprintf(stderr, "timestamp query command buffer acquire failed\n");
    goto cleanup;
  }

  timestampWrites.querySet     = set;
  timestampWrites.beginIndex   = 0u;
  timestampWrites.endIndex     = 0u;
  computeInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PASS_CREATE_INFO;
  computeInfo.chain.structSize = sizeof(computeInfo);
  computeInfo.label            = "timestamp-query-compute-pass";
  computeInfo.timestampWrites  = &timestampWrites;
  computePass                  = GPUBeginComputePassWithInfo(cmdb, &computeInfo);

  if (computePass) {
    GPUEndComputePass(computePass);
    computePass = NULL;
    fprintf(stderr, "timestamp query accepted identical pass indices\n");
    goto cleanup;
  }

  timestampWrites.endIndex = 1u;

  if (!(computePass = GPUBeginComputePassWithInfo(cmdb, &computeInfo))) {
    fprintf(stderr, "timestamp query compute pass begin failed\n");
    goto cleanup;
  }

  GPUEndComputePass(computePass);
  computePass = NULL;

  timestampWrites.beginIndex = 2u;
  timestampWrites.endIndex   = 3u;

  color.view                  = targetView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[3] = 1.0f;

  renderInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderInfo.chain.structSize     = sizeof(renderInfo);
  renderInfo.label                = "timestamp-query-render-pass";
  renderInfo.timestampWrites      = &timestampWrites;
  renderInfo.pColorAttachments    = &color;
  renderInfo.colorAttachmentCount = 1u;

  if (!(renderPass = GPUBeginRenderPass(cmdb, &renderInfo))) {
    fprintf(stderr, "timestamp query render pass begin failed\n");
    goto cleanup;
  }

  GPUEndRenderPass(renderPass);
  renderPass = NULL;
  GPUResolveQuerySet(cmdb, set, 0u, 4u, buffer, sizeof(uint64_t));

  fenceInfo.chain.sType      = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);

  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "timestamp query fence create failed\n");
    goto cleanup;
  }

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = fence;

  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
    fprintf(stderr, "timestamp query submit failed\n");
    cmdb = NULL;
    goto cleanup;
  }

  cmdb = NULL;

  if (GPUWaitFence(fence, 5000000000ull) != GPU_OK) {
    fprintf(stderr, "timestamp query fence wait failed\n");
    goto cleanup;
  }

  if (GPUQueueReadBuffer(queue,
                         buffer,
                         0u,
                         queryResults,
                         sizeof(queryResults)) != GPU_OK) {
    fprintf(stderr, "timestamp query readback failed\n");
    goto cleanup;
  }

  if (queryResults[0] != UINT64_MAX
      || queryResults[1] == UINT64_MAX || queryResults[2] == UINT64_MAX
      || queryResults[3] == UINT64_MAX || queryResults[4] == UINT64_MAX
      || queryResults[2] < queryResults[1]
      || queryResults[3] < queryResults[2]
      || queryResults[4] < queryResults[3]) {
    fprintf(stderr,
            "timestamp query resolved error values: "
            "%llu, %llu, %llu, %llu, %llu\n",
            (unsigned long long)queryResults[0],
            (unsigned long long)queryResults[1],
            (unsigned long long)queryResults[2],
            (unsigned long long)queryResults[3],
            (unsigned long long)queryResults[4]);
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (computePass) {
    GPUEndComputePass(computePass);
  }
  if (renderPass) {
    GPUEndRenderPass(renderPass);
  }
  if (cmdb) {
    (void)GPUDiscardCommandBuffer(cmdb);
  }
  GPUDestroyFence(fence);
  GPUDestroyTextureView(targetView);
  GPUDestroyTexture(target);
  GPUDestroyBuffer(buffer);
  GPUDestroyQuerySet(set);
  GPUDestroyDevice(device);
  return ok;
}

/* HOST_READ buffers: bytes copied and timestamps resolved into them (where
 * the adapter has timestamps) are read through the mapping once the fence
 * signals, with no GPUQueueReadBuffer, the same buffers reused across
 * submits (WebGPU, whose mapping is asynchronous, reads them with
 * GPUQueueReadBuffer) */
static int
check_host_read_timestamps(GPUAdapter *adapter) {
  enum { RUNS = 2, QUERIES = 4, WORDS = 1 + QUERIES, COPY_BYTES = 64 };
  GPUDevice                   *device      = NULL;
  GPUQueue                    *queue       = NULL;
  GPUCommandBuffer            *cmdb        = NULL;
  GPUQuerySet                 *set         = NULL;
  GPUBuffer                   *results     = NULL;
  GPUBuffer                   *copied      = NULL;
  GPUBuffer                   *source      = NULL;
  GPUBuffer                   *rejected    = NULL;
  GPUTexture                  *target      = NULL;
  GPUTextureView              *targetView  = NULL;
  GPUFence                    *fence       = NULL;
  GPUComputePassEncoder       *computePass = NULL;
  GPURenderPassEncoder        *renderPass  = NULL;
  GPUTransferPassEncoder      *copyPass    = NULL;
  GPUCommandBuffer            *buffers[1];
  GPUDeviceCreateInfo          deviceInfo      = {0};
  GPUQuerySetCreateInfo        queryInfo       = {0};
  GPUBufferCreateInfo          bufferInfo      = {0};
  GPUTextureCreateInfo         textureInfo     = {0};
  GPUTextureViewCreateInfo     viewInfo        = {0};
  GPUQueueSubmitInfo           submitInfo      = {0};
  GPUComputePassCreateInfo     computeInfo     = {0};
  GPURenderPassColorAttachment color           = {0};
  GPURenderPassCreateInfo      renderInfo      = {0};
  GPUPassTimestampWrites       timestampWrites = {0};
  GPUBufferCopyRegion          region          = {0};
  GPUAdapterProperties         properties      = {0};
  GPUFeature                   timestampFeature = GPU_FEATURE_TIMESTAMPS;
  uint64_t                     values[WORDS];
  uint64_t                     previousEnd = 0u;
  uint8_t                      pattern[COPY_BYTES];
  uint8_t                      readBytes[COPY_BYTES];
  void                        *resultsHost, *copiedHost;
  double                       period, elapsedNs;
  uint32_t                     run, i, first;
  bool                         asyncMapping, timestamps;
  int                          ok = 0;

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.optional.featureCount = 1;
  deviceInfo.optional.pFeatures    = &timestampFeature;

  if (gpu_test_create_device(adapter, &deviceInfo, &device) != GPU_OK || !device
      || !(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0))
      || GPUGetAdapterProperties(adapter, &properties) != GPU_OK) {
    fprintf(stderr, "host-read device create failed\n");
    goto cleanup;
  }

  asyncMapping = properties.backend == GPU_BACKEND_WEBGPU;
  timestamps   = GPUIsFeatureEnabled(device, GPU_FEATURE_TIMESTAMPS);

  if (!timestamps || GPUGetTimestampPeriod(queue, &period) != GPU_OK) {
    period = 0.0;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "host-read-rejected";
  bufferInfo.sizeBytes        = sizeof(values);
  bufferInfo.usage            = GPU_BUFFER_USAGE_HOST_READ | GPU_BUFFER_USAGE_STORAGE;

  if (GPUCreateBuffer(device, &bufferInfo, &rejected) != GPU_ERROR_INVALID_ARGUMENT || rejected) {
    fprintf(stderr, "host-read buffer accepted storage usage\n");
    goto cleanup;
  }

  bufferInfo.usage = GPU_BUFFER_USAGE_HOST_READ | GPU_BUFFER_USAGE_COPY_SRC;

  if (GPUCreateBuffer(device, &bufferInfo, &rejected) != GPU_ERROR_INVALID_ARGUMENT || rejected) {
    fprintf(stderr, "host-read buffer accepted copy-source usage\n");
    goto cleanup;
  }

  bufferInfo.label = "host-read-timestamps";
  bufferInfo.usage = GPU_BUFFER_USAGE_HOST_READ | GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &bufferInfo, &results) != GPU_OK || !results) {
    fprintf(stderr, "host-read timestamp buffer create failed\n");
    goto cleanup;
  }

  bufferInfo.label     = "host-read-copies";
  bufferInfo.sizeBytes = COPY_BYTES;

  if (GPUCreateBuffer(device, &bufferInfo, &copied) != GPU_OK || !copied) {
    fprintf(stderr, "host-read copy buffer create failed\n");
    goto cleanup;
  }

  bufferInfo.label = "host-read-source";
  bufferInfo.usage = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &bufferInfo, &source) != GPU_OK || !source) {
    fprintf(stderr, "host-read source buffer create failed\n");
    goto cleanup;
  }

  resultsHost = GPUGetBufferHostAddress(results);
  copiedHost  = GPUGetBufferHostAddress(copied);

  if (!asyncMapping && (!resultsHost || !copiedHost)) {
    fprintf(stderr, "host-read buffer has no host address\n");
    goto cleanup;
  }

  queryInfo.chain.sType      = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  queryInfo.chain.structSize = sizeof(queryInfo);
  queryInfo.label            = "host-read-timestamps";
  queryInfo.type             = GPU_QUERY_TIMESTAMP;
  queryInfo.count            = QUERIES;

  if (timestamps && (GPUCreateQuerySet(device, &queryInfo, &set) != GPU_OK || !set)) {
    fprintf(stderr, "host-read timestamp query set create failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "host-read-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 256u;
  textureInfo.height           = 256u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET;

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;

  if (GPUCreateTexture(device, &textureInfo, &target) != GPU_OK || !target
      || GPUCreateTextureView(target, &viewInfo, &targetView) != GPU_OK || !targetView
      || GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "host-read render target or fence create failed\n");
    goto cleanup;
  }

  for (run = 0u; run < RUNS; run++) {
    memset(values, 0xff, sizeof(values));

    for (i = 0u; i < COPY_BYTES; i++) {
      pattern[i] = (uint8_t)(i * 7u + run * 13u + 1u);
    }

    /* the host writes its mapping too: guards around the resolved range */
    if (resultsHost) {
      memcpy(resultsHost, values, sizeof(values));
    } else if (GPUQueueWriteBuffer(queue, results, 0u, values, sizeof(values)) != GPU_OK) {
      fprintf(stderr, "host-read timestamp buffer initialize failed\n");
      goto cleanup;
    }

    if (GPUQueueWriteBuffer(queue, source, 0u, pattern, sizeof(pattern)) != GPU_OK
        || GPUAcquireCommandBuffer(queue, "host-read-timestamps", &cmdb) != GPU_OK || !cmdb) {
      fprintf(stderr, "host-read command buffer setup failed\n");
      goto cleanup;
    }

    timestampWrites.querySet     = set;
    timestampWrites.beginIndex   = 0u;
    timestampWrites.endIndex     = 1u;
    computeInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PASS_CREATE_INFO;
    computeInfo.chain.structSize = sizeof(computeInfo);
    computeInfo.label            = "host-read-compute";
    computeInfo.timestampWrites  = timestamps ? &timestampWrites : NULL;

    if (!(computePass = GPUBeginComputePassWithInfo(cmdb, &computeInfo))) {
      fprintf(stderr, "host-read compute pass begin failed\n");
      goto cleanup;
    }

    GPUEndComputePass(computePass);
    computePass = NULL;

    timestampWrites.beginIndex      = 2u;
    timestampWrites.endIndex        = 3u;
    color.view                      = targetView;
    color.loadOp                    = GPU_LOAD_OP_CLEAR;
    color.storeOp                   = GPU_STORE_OP_STORE;
    color.clearColor.float32[0]     = 0.25f;
    color.clearColor.float32[3]     = 1.0f;
    renderInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderInfo.chain.structSize     = sizeof(renderInfo);
    renderInfo.label                = "host-read-render";
    renderInfo.timestampWrites      = timestamps ? &timestampWrites : NULL;
    renderInfo.pColorAttachments    = &color;
    renderInfo.colorAttachmentCount = 1u;

    if (!(renderPass = GPUBeginRenderPass(cmdb, &renderInfo))) {
      fprintf(stderr, "host-read render pass begin failed\n");
      goto cleanup;
    }

    GPUEndRenderPass(renderPass);
    renderPass = NULL;

    if (!(copyPass = GPUBeginTransferPass(cmdb, "host-read-copy"))) {
      fprintf(stderr, "host-read copy pass begin failed\n");
      goto cleanup;
    }

    region.sizeBytes = COPY_BYTES;
    GPUCopyBufferToBuffer(copyPass, source, copied, &region);
    GPUEndTransferPass(copyPass);
    copyPass = NULL;
    if (timestamps) {
      GPUResolveQuerySet(cmdb, set, 0u, QUERIES, results, sizeof(uint64_t));
    }

    buffers[0]                    = cmdb;
    submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize   = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers   = buffers;
    submitInfo.fence              = fence;

    if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
      fprintf(stderr, "host-read submit failed\n");
      cmdb = NULL;
      goto cleanup;
    }

    cmdb = NULL;

    if (GPUWaitFence(fence, 5000000000ull) != GPU_OK) {
      fprintf(stderr, "host-read fence wait failed\n");
      goto cleanup;
    }

    GPUResetFence(fence);

    if (resultsHost) {
      memcpy(values, resultsHost, sizeof(values));
      memcpy(readBytes, copiedHost, sizeof(readBytes));
    } else if (GPUQueueReadBuffer(queue, results, 0u, values, sizeof(values)) != GPU_OK
               || GPUQueueReadBuffer(queue, copied, 0u, readBytes, sizeof(readBytes)) != GPU_OK) {
      fprintf(stderr, "host-read readback failed\n");
      goto cleanup;
    }

    if (memcmp(readBytes, pattern, sizeof(pattern)) != 0) {
      fprintf(stderr, "host-read copy mismatch in run %u\n", run);
      goto cleanup;
    }

    /* and read through the queue as well, where it lives */
    memset(readBytes, 0, sizeof(readBytes));

    if (GPUQueueReadBuffer(queue, copied, 0u, readBytes, sizeof(readBytes)) != GPU_OK
        || memcmp(readBytes, pattern, sizeof(pattern)) != 0) {
      fprintf(stderr, "host-read queue read mismatch in run %u\n", run);
      goto cleanup;
    }

    if (!timestamps) {
      if (values[0] != UINT64_MAX || values[4] != UINT64_MAX) {
        fprintf(stderr, "host-read guards overwritten in run %u\n", run);
        goto cleanup;
      }
      continue;
    }

    /* in order, under a second, after the previous run's; the empty pass's
     * pair real (what a caller timing work between empty passes reads) and
     * the span more than a tick, but WebGPU quantizes timestamps and may give
     * an empty pass 0 */
    first     = asyncMapping && values[1] == 0u && values[2] == 0u ? 3u : 1u;
    elapsedNs = period > 0.0 ? (double)(values[4] - values[first]) * period : 0.0;

    if (values[0] != UINT64_MAX || values[first] == 0u
        || values[1] == UINT64_MAX || values[2] == UINT64_MAX
        || values[3] == UINT64_MAX || values[4] == UINT64_MAX
        || values[2] < values[1] || values[3] < values[2] || values[4] < values[3]
        || (!asyncMapping && values[4] == values[1])
        || values[first] < previousEnd || elapsedNs > 1e9) {
      fprintf(stderr,
              "host-read timestamps implausible in run %u: %llu, %llu, %llu, %llu, %llu "
              "(previous end %llu, %.0f ns)\n",
              run,
              (unsigned long long)values[0], (unsigned long long)values[1],
              (unsigned long long)values[2], (unsigned long long)values[3],
              (unsigned long long)values[4], (unsigned long long)previousEnd, elapsedNs);
      goto cleanup;
    }

    previousEnd = values[4];
  }

  ok = 1;

cleanup:
  if (computePass) {
    GPUEndComputePass(computePass);
  }
  if (renderPass) {
    GPUEndRenderPass(renderPass);
  }
  if (copyPass) {
    GPUEndTransferPass(copyPass);
  }
  if (cmdb) {
    (void)GPUDiscardCommandBuffer(cmdb);
  }
  GPUDestroyFence(fence);
  GPUDestroyTextureView(targetView);
  GPUDestroyTexture(target);
  GPUDestroyBuffer(source);
  GPUDestroyBuffer(copied);
  GPUDestroyBuffer(results);
  GPUDestroyQuerySet(set);
  GPUDestroyDevice(device);
  return ok;
}

static int
check_pipeline_statistics_roundtrip(GPUAdapter *adapter,
                                    const char *bytecodePath) {
  GPUFeature                   feature      = GPU_FEATURE_PIPELINE_STATISTICS;
  GPUDeviceCreateInfo          deviceInfo   = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUQuerySetCreateInfo        queryInfo    = {0};
  GPUBufferCreateInfo          bufferInfo   = {0};
  GPUQueueSubmitInfo           submitInfo   = {0};
  GPUPipelineStatisticsResult  statistics   = {0};
  GPUCommandBuffer            *submitBuffers[1];
  GPUDevice                   *device       = NULL;
  GPUQueue                    *queue        = NULL;
  GPUShaderLibrary            *library      = NULL;
  GPUShaderLayout             *shaderLayout = NULL;
  GPUComputePipeline          *pipeline     = NULL;
  GPUQuerySet                 *set          = NULL;
  GPUBuffer                   *buffer       = NULL;
  GPUCommandBuffer            *cmdb         = NULL;
  GPUComputePassEncoder       *pass         = NULL;
  GPUFence                    *fence        = NULL;
  void                        *bytecode     = NULL;
  uint64_t                     bytecodeSize = 0u;
  int                          ok           = 0;

  if (!GPUIsFeatureSupported(adapter, feature)) {
    return 1;
  }

  if (!bytecodePath) {
    return 0;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.featureCount = 1u;
  deviceInfo.required.pFeatures    = &feature;

  if (gpu_test_create_device(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "pipeline statistics feature device create failed\n");
    goto cleanup;
  }

  queue    = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);

  if (!queue || !bytecode
      || GPUCreateShaderLibraryFromUSL(device,
                                       bytecode,
                                       bytecodeSize,
                                       &library) != GPU_OK
      || !library
      || GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK
      || !shaderLayout || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "pipeline statistics shader setup failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "api-pipeline-statistics";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "api_cs";

  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK
      || !pipeline) {
    fprintf(stderr, "pipeline statistics pipeline create failed\n");
    goto cleanup;
  }

  queryInfo.chain.sType       = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  queryInfo.chain.structSize  = sizeof(queryInfo);
  queryInfo.label             = "api-pipeline-statistics";
  queryInfo.type              = GPU_QUERY_PIPELINE_STATISTICS;
  queryInfo.count             = 1u;
  queryInfo.pipelineStatsMask = GPU_PIPESTAT_COMPUTE_SHADER_INVOCATIONS;

  if (GPUCreateQuerySet(device, &queryInfo, &set) != GPU_OK || !set) {
    fprintf(stderr, "pipeline statistics query set create failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-pipeline-statistics";
  bufferInfo.sizeBytes        = sizeof(statistics);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    fprintf(stderr, "pipeline statistics resolve buffer create failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "api-pipeline-statistics", &cmdb) != GPU_OK || !cmdb) {
    fprintf(stderr, "pipeline statistics command buffer acquire failed\n");
    goto cleanup;
  }

  GPUBeginPipelineStatisticsQuery(cmdb, set, 0u);

  if (!(pass = GPUBeginComputePass(cmdb, "api-pipeline-statistics"))) {
    fprintf(stderr, "pipeline statistics compute pass begin failed\n");
    goto cleanup;
  }

  GPUBindComputePipeline(pass, pipeline);
  GPUDispatch(pass, 4u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;
  GPUEndPipelineStatisticsQuery(cmdb, set);
  GPUResolveQuerySet(cmdb, set, 0u, 1u, buffer, 0u);

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "pipeline statistics fence create failed\n");
    goto cleanup;
  }

  submitBuffers[0]              = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = submitBuffers;
  submitInfo.fence              = fence;

  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK
      || GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "pipeline statistics submit failed\n");
    cmdb = NULL;
    goto cleanup;
  }

  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         buffer,
                         0u,
                         &statistics,
                         sizeof(statistics)) != GPU_OK
      || statistics.computeShaderInvocations < 4u) {
    fprintf(stderr,
            "pipeline statistics readback mismatch: %llu compute invocations\n",
            (unsigned long long)statistics.computeShaderInvocations);
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  free(bytecode);
  GPUDestroyFence(fence);
  GPUDestroyBuffer(buffer);
  GPUDestroyQuerySet(set);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  return ok;
}

int
gpu_test_query(GPUAdapter *adapter,
               GPUDevice  *device,
               const char *computeBytecodePath) {
  return check_query_set_create_validation(device)
         && check_query_commands_are_safe_noops()
         && check_pipeline_statistics_roundtrip(adapter, computeBytecodePath)
         && check_timestamp_query_roundtrip(adapter)
         && check_host_read_timestamps(adapter);
}
