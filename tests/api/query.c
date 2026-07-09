#include "test.h"
#include "../../src/api/cmdqueue_internal.h"

static int
check_query_set_create_validation(GPUDevice *device) {
  GPUQuerySetCreateInfo info = {0};
  GPUQuerySet *set;

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

  info.type = GPU_QUERY_OCCLUSION;
  set = (GPUQuerySet *)(uintptr_t)1u;
  if (GPUCreateQuerySet(device, &info, &set) != GPU_ERROR_UNSUPPORTED ||
      set != NULL) {
    fprintf(stderr, "occlusion query did not report unsupported\n");
    return 0;
  }

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

  return 1;
}

int
gpu_test_query(GPUDevice *device) {
  return check_query_set_create_validation(device) &&
         check_query_commands_are_safe_noops();
}
