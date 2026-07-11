#include "test.h"
#include "../../src/api/cmdqueue_internal.h"

_Static_assert(sizeof(GPUPipelineStatisticsResult) == 11u * sizeof(uint64_t),
               "pipeline statistics result layout changed");

static int
check_query_set_create_validation(GPUDevice *device) {
  GPUQuerySetCreateInfo info = {0};
  GPUCommandQueue      *queue;
  GPUQuerySet          *set;
  double                timestampPeriod;

  info.chain.sType = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.type = GPU_QUERY_TIMESTAMP;
  info.count = 2u;

  set = (GPUQuerySet *)(uintptr_t)1u;
  if (GPUCreateQuerySet(NULL, &info, &set) != GPU_ERROR_INVALID_ARGUMENT ||
      set != NULL) {
    fprintf(stderr, "query set create accepted null device\n");
    return 0;
  }
  if (GPUCreateQuerySet(device, &info, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "query set create accepted null output\n");
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  set = (GPUQuerySet *)(uintptr_t)1u;
  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT ||
      set != NULL) {
    fprintf(stderr, "query set create accepted wrong sType\n");
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  info.chain.structSize = (uint32_t)(sizeof(info) - 1u);
  set = (GPUQuerySet *)(uintptr_t)1u;
  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT ||
      set != NULL) {
    fprintf(stderr, "query set create accepted short structSize\n");
    return 0;
  }

  info.chain.structSize = sizeof(info);
  info.count = 0u;
  set = (GPUQuerySet *)(uintptr_t)1u;
  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT ||
      set != NULL) {
    fprintf(stderr, "query set create accepted zero count\n");
    return 0;
  }

  info.count = 2u;
  info.type = (GPUQueryType)99;
  set = (GPUQuerySet *)(uintptr_t)1u;
  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT ||
      set != NULL) {
    fprintf(stderr, "query set create accepted invalid query type\n");
    return 0;
  }

  info.type = GPU_QUERY_TIMESTAMP;
  info.pipelineStatsMask = GPU_PIPESTAT_VERTEX_SHADER_INVOCATIONS;
  set = (GPUQuerySet *)(uintptr_t)1u;
  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT ||
      set != NULL) {
    fprintf(stderr, "timestamp query accepted pipeline stats mask\n");
    return 0;
  }

  info.pipelineStatsMask = 0u;
  set = (GPUQuerySet *)(uintptr_t)1u;
  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_UNSUPPORTED ||
      set != NULL) {
    fprintf(stderr, "timestamp query did not report unsupported\n");
    return 0;
  }
  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  timestampPeriod = 1.0;
  if (!queue ||
      GPUGetTimestampPeriod(queue, &timestampPeriod) != GPU_ERROR_UNSUPPORTED ||
      timestampPeriod != 0.0) {
    fprintf(stderr, "disabled timestamp period did not report unsupported\n");
    return 0;
  }

  info.type = GPU_QUERY_OCCLUSION;
  set = NULL;
  if (GPUCreateQuerySet(device, &info, &set) != GPU_OK || !set) {
    fprintf(stderr, "occlusion query set create failed\n");
    return 0;
  }
  GPUDestroyQuerySet(set);
  set = NULL;

  info.type = GPU_QUERY_PIPELINE_STATISTICS;
  info.pipelineStatsMask = 0u;
  set = (GPUQuerySet *)(uintptr_t)1u;
  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT ||
      set != NULL) {
    fprintf(stderr, "pipeline statistics query accepted zero mask\n");
    return 0;
  }

  info.pipelineStatsMask = 1u << 31;
  set = (GPUQuerySet *)(uintptr_t)1u;
  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_INVALID_ARGUMENT ||
      set != NULL) {
    fprintf(stderr, "pipeline statistics query accepted unknown mask\n");
    return 0;
  }

  info.pipelineStatsMask = GPU_PIPESTAT_VERTEX_SHADER_INVOCATIONS |
                           GPU_PIPESTAT_FRAGMENT_SHADER_INVOCATIONS;
  set = (GPUQuerySet *)(uintptr_t)1u;
  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_UNSUPPORTED ||
      set != NULL) {
    fprintf(stderr, "pipeline statistics query did not report unsupported\n");
    return 0;
  }

  GPUDestroyQuerySet(NULL);
  return 1;
}

static int
check_query_commands_are_safe_noops(void) {
  GPURenderPassEncoder endedPass = {0};
  GPUCommandBuffer submittedCmdb = {0};
  double timestampPeriod;

  endedPass._ended = true;
  submittedCmdb._submitted = true;

  GPUWriteTimestamp(NULL, NULL, 0u);
  GPUWriteTimestamp(&submittedCmdb, NULL, 0u);
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
  if (GPUGetTimestampPeriod(NULL, &timestampPeriod) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      timestampPeriod != 0.0 ||
      GPUGetTimestampPeriod(NULL, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "timestamp period accepted invalid arguments\n");
    return 0;
  }

  return 1;
}

static int
check_timestamp_query_roundtrip(GPUAdapter *adapter) {
  GPUFeature timestampFeature = GPU_FEATURE_TIMESTAMPS;
  GPUDeviceCreateInfo deviceInfo = {0};
  GPUQuerySetCreateInfo queryInfo = {0};
  GPUBufferCreateInfo bufferInfo = {0};
  GPUFenceCreateInfo fenceInfo = {0};
  GPUQueueSubmitInfo submitInfo = {0};
  GPUDeviceCapabilities caps = {0};
  GPUDevice *device = NULL;
  GPUCommandQueue *queue = NULL;
  GPUCommandBuffer *cmdb = NULL;
  GPUQuerySet *set = NULL;
  GPUBuffer *buffer = NULL;
  GPUFence *fence = NULL;
  const char *metalMode;
  GPUCommandBuffer *buffers[1];
  uint64_t timestamps[2] = {0, 0};
  double timestampPeriod;
  GPUResult periodResult;
  int ok = 0;

  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_TIMESTAMPS)) {
    return 1;
  }

  deviceInfo.chain.sType = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize = sizeof(deviceInfo);
  deviceInfo.required.featureCount = 1;
  deviceInfo.required.pFeatures = &timestampFeature;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "timestamp feature device create failed\n");
    return 0;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_TIMESTAMPS) ||
      GPUGetDeviceCapabilities(device, &caps) != GPU_OK ||
      caps.enabled.featureCount == 0) {
    fprintf(stderr, "timestamp feature was not enabled\n");
    goto cleanup;
  }

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  if (!queue) {
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
  if (metalMode && strcmp(metalMode, "metal4") == 0 &&
      periodResult != GPU_OK) {
    fprintf(stderr, "Metal 4 timestamp period was unavailable\n");
    goto cleanup;
  }
  if (metalMode && strcmp(metalMode, "classic") == 0 &&
      (periodResult != GPU_ERROR_UNSUPPORTED || timestampPeriod != 0.0)) {
    fprintf(stderr, "classic Metal exposed an uncalibrated timestamp period\n");
    goto cleanup;
  }

  queryInfo.chain.sType = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  queryInfo.chain.structSize = sizeof(queryInfo);
  queryInfo.label = "api-test-timestamps";
  queryInfo.type = GPU_QUERY_TIMESTAMP;
  queryInfo.count = 2u;
  if (GPUCreateQuerySet(device, &queryInfo, &set) != GPU_OK || !set) {
    fprintf(stderr, "timestamp query set create failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes = sizeof(timestamps);
  bufferInfo.usage = GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    fprintf(stderr, "timestamp resolve buffer create failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "timestamp-query-test", &cmdb) != GPU_OK || !cmdb) {
    fprintf(stderr, "timestamp query command buffer acquire failed\n");
    goto cleanup;
  }

  GPUWriteTimestamp(cmdb, set, 0u);
  GPUWriteTimestamp(cmdb, set, 1u);
  GPUResolveQuerySet(cmdb, set, 0u, 2u, buffer, 0u);

  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "timestamp query fence create failed\n");
    goto cleanup;
  }

  buffers[0] = cmdb;
  submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers = buffers;
  submitInfo.fence = fence;
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
  if (GPUQueueReadBuffer(queue, buffer, 0u, timestamps, sizeof(timestamps)) != GPU_OK) {
    fprintf(stderr, "timestamp query readback failed\n");
    goto cleanup;
  }
  if (timestamps[0] == UINT64_MAX || timestamps[1] == UINT64_MAX) {
    fprintf(stderr, "timestamp query resolved error values\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  GPUDestroyFence(fence);
  GPUDestroyBuffer(buffer);
  GPUDestroyQuerySet(set);
  GPUDestroyDevice(device);
  return ok;
}

int
gpu_test_query(GPUAdapter *adapter, GPUDevice *device) {
  return check_query_set_create_validation(device) &&
         check_query_commands_are_safe_noops() &&
         check_timestamp_query_roundtrip(adapter);
}
