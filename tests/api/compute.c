#include "test.h"
#include "../../src/api/buffer_internal.h"
#include "../../src/api/cmdqueue_internal.h"
#include "../../src/api/compute_internal.h"

static const char *kComputePipelineMSL =
  "#include <metal_stdlib>\n"
  "using namespace metal;\n"
  "kernel void api_cs(uint3 gid [[thread_position_in_grid]]) {\n"
  "  (void)gid;\n"
  "}\n";

static const char *kComputeIndirectMSL =
  "#include <metal_stdlib>\n"
  "using namespace metal;\n"
  "kernel void api_indirect_cs(device uint *out [[buffer(0)]],\n"
  "                            uint3 gid [[thread_position_in_grid]]) {\n"
  "  out[gid.x] = gid.x + 1u;\n"
  "}\n";

static int
create_compute_test_library(GPUDevice *device, GPUShaderLibrary **outLibrary) {
  GPUShaderLibraryCreateInfo info = {0};

  info.chain.sType = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label = "api-compute-pipeline.metal";
  info.sourceKind = GPU_SHADER_SOURCE_MSL_TEXT;
  info.sourceData = kComputePipelineMSL;
  info.sourceSize = (uint64_t)strlen(kComputePipelineMSL);

  return GPUCreateShaderLibrary(device, &info, outLibrary) == GPU_OK &&
         *outLibrary;
}

static int
create_compute_indirect_library(GPUDevice *device,
                                GPUShaderLibrary **outLibrary) {
  GPUShaderLibraryCreateInfo info = {0};

  info.chain.sType = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label = "api-compute-indirect.metal";
  info.sourceKind = GPU_SHADER_SOURCE_MSL_TEXT;
  info.sourceData = kComputeIndirectMSL;
  info.sourceSize = (uint64_t)strlen(kComputeIndirectMSL);

  return GPUCreateShaderLibrary(device, &info, outLibrary) == GPU_OK &&
         *outLibrary;
}

static int
expect_compute_pipeline_error(GPUDevice *device,
                              const GPUComputePipelineCreateInfo *info,
                              const char *message) {
  GPUComputePipeline *pipeline = (GPUComputePipeline *)(uintptr_t)1u;

  if (GPUCreateComputePipeline(device, info, &pipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      pipeline != NULL) {
    fprintf(stderr, "%s\n", message);
    GPUDestroyComputePipeline(pipeline);
    return 0;
  }

  return 1;
}

static int
check_compute_pipeline_validation(GPUDevice *device) {
  GPUShaderLibrary *library = NULL;
  GPUComputePipelineCreateInfo info = {0};
  GPUComputePipeline *pipeline;

  if (!create_compute_test_library(device, &library)) {
    fprintf(stderr, "failed to create compute pipeline test library\n");
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.library = library;
  info.entryPoint = "api_cs";

  pipeline = (GPUComputePipeline *)(uintptr_t)1u;
  if (GPUCreateComputePipeline(NULL, &info, &pipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      pipeline != NULL) {
    fprintf(stderr, "compute pipeline create accepted null device\n");
    GPUDestroyComputePipeline(pipeline);
    GPUDestroyShaderLibrary(library);
    return 0;
  }
  if (GPUCreateComputePipeline(device, &info, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "compute pipeline create accepted null output\n");
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  if (!expect_compute_pipeline_error(device,
                                     &info,
                                     "compute pipeline create accepted wrong sType")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = (uint32_t)(sizeof(info) - 1u);
  if (!expect_compute_pipeline_error(device,
                                     &info,
                                     "compute pipeline create accepted short structSize")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.chain.structSize = sizeof(info);
  info.library = NULL;
  if (!expect_compute_pipeline_error(device,
                                     &info,
                                     "compute pipeline create accepted null library")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.library = library;
  info.entryPoint = NULL;
  if (!expect_compute_pipeline_error(device,
                                     &info,
                                     "compute pipeline create accepted null entry point")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.entryPoint = "missing_cs";
  if (!expect_compute_pipeline_error(device,
                                     &info,
                                     "compute pipeline create accepted missing entry point")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.entryPoint = "api_cs";
  pipeline = NULL;
  if (GPUCreateComputePipeline(device, &info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "compute pipeline create rejected valid pipeline\n");
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLibrary(library);
  return 1;
}

static int
check_compute_pass_validation(void) {
  GPUCommandBuffer fakeCmdb = {0};
  GPUComputePassEncoder fakePass = {0};
  GPUComputePipeline fakePipeline = {0};
  GPUBuffer fakeBufferStorage = {0};
  GPUBuffer *fakeBuffer = &fakeBufferStorage;
  uint32_t dynamicOffset = 0u;
  uint32_t pushValue = 0x11223344u;
  uint8_t pushBefore[16];

  fakeBufferStorage.sizeBytes = 128u;
  fakeBufferStorage.usage = GPU_BUFFER_USAGE_INDIRECT;

  if (GPUBeginComputePass(NULL, "null")) {
    fprintf(stderr, "compute pass accepted null command buffer\n");
    return 0;
  }

  fakeCmdb._submitted = true;
  if (GPUBeginComputePass(&fakeCmdb, "submitted")) {
    fprintf(stderr, "compute pass accepted submitted command buffer\n");
    return 0;
  }

  fakeCmdb._submitted = false;
  fakeCmdb._activeEncoder = true;
  if (GPUBeginComputePass(&fakeCmdb, "active")) {
    fprintf(stderr, "compute pass accepted command buffer with active encoder\n");
    return 0;
  }

  GPUBindComputePipeline(NULL, NULL);
  GPUBindComputePipeline(&fakePass, &fakePipeline);
  GPUBindComputeGroup(NULL, 0u, NULL, 0u, NULL);
  GPUBindComputeGroup(&fakePass, 1u, NULL, 0u, NULL);
  GPUBindComputeGroup(&fakePass, 0u, NULL, 1u, &dynamicOffset);
  GPUSetComputePushConstants(NULL, 0u, sizeof(pushValue), &pushValue);
  GPUSetComputePushConstants(&fakePass, 0u, sizeof(pushValue), &pushValue);
  GPUDispatch(NULL, 1u, 1u, 1u);
  GPUDispatch(&fakePass, 1u, 1u, 1u);
  GPUDispatch(&fakePass, 0u, 1u, 1u);
  GPUDispatch(&fakePass, 1u, 0u, 1u);
  GPUDispatch(&fakePass, 1u, 1u, 0u);
  GPUDispatchIndirect(NULL, fakeBuffer, 0u);
  GPUDispatchIndirect(&fakePass, NULL, 0u);
  GPUMultiDispatchIndirect(NULL, fakeBuffer, 0u, 1u, 12u);
  GPUMultiDispatchIndirect(&fakePass, NULL, 0u, 1u, 12u);
  GPUMultiDispatchIndirect(&fakePass, fakeBuffer, 0u, 0u, 12u);
  GPUMultiDispatchIndirect(&fakePass, fakeBuffer, 0u, 2u, 0u);
  GPUEndComputePass(NULL);

  fakePass._hasPipeline = true;
  GPUMultiDispatchIndirect(&fakePass, fakeBuffer, UINT64_MAX, 2u, 12u);
  GPUMultiDispatchIndirect(&fakePass, fakeBuffer, 0u, 2u, 12u);
  fakePass._pushConstantSizeBytes = sizeof(pushBefore);
  fakePass._pushConstantStages = GPU_SHADER_STAGE_COMPUTE_BIT;
  GPUSetComputePushConstants(&fakePass, 4u, sizeof(pushValue), &pushValue);
  if (memcmp(fakePass._pushConstants + 4u, &pushValue, sizeof(pushValue)) != 0) {
    fprintf(stderr, "compute push constants did not update expected range\n");
    return 0;
  }

  memcpy(pushBefore, fakePass._pushConstants, sizeof(pushBefore));
  GPUSetComputePushConstants(&fakePass, 14u, sizeof(pushValue), &pushValue);
  if (memcmp(fakePass._pushConstants, pushBefore, sizeof(pushBefore)) != 0) {
    fprintf(stderr, "compute push constants accepted out-of-range update\n");
    return 0;
  }

  fakePass._ended = true;
  GPUBindComputePipeline(&fakePass, &fakePipeline);
  GPUBindComputeGroup(&fakePass, 0u, NULL, 0u, NULL);
  GPUSetComputePushConstants(&fakePass, 0u, sizeof(pushValue), &pushValue);
  GPUDispatch(&fakePass, 1u, 1u, 1u);
  GPUDispatchIndirect(&fakePass, fakeBuffer, 0u);
  GPUMultiDispatchIndirect(&fakePass, fakeBuffer, 0u, 2u, 12u);
  GPUEndComputePass(&fakePass);

  return 1;
}

static int
check_compute_indirect_readback(GPUDevice *device) {
  typedef struct DispatchArgs {
    uint32_t x, y, z;
  } DispatchArgs;

  static const DispatchArgs kDispatchArgs = {4u, 1u, 1u};
  static const uint32_t kZeroWords[4] = {0u, 0u, 0u, 0u};
  const uint32_t expectedWords[4] = {1u, 2u, 3u, 4u};
  GPUCommandQueue *queue;
  GPUShaderLibrary *library = NULL;
  GPUBindGroupLayout *bindGroupLayout = NULL;
  GPUPipelineLayout *pipelineLayout = NULL;
  GPUComputePipeline *pipeline = NULL;
  GPUBuffer *outputBuffer = NULL;
  GPUBuffer *argsBuffer = NULL;
  GPUBindGroup *bindGroup = NULL;
  GPUCommandBuffer *cmdb = NULL;
  GPUCommandBuffer *buffers[1];
  GPUComputePassEncoder *computePass = NULL;
  GPUFence *fence = NULL;
  GPUBindGroupLayoutEntry layoutEntry = {0};
  GPUBindGroupLayoutCreateInfo layoutInfo = {0};
  GPUPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
  GPUBindGroupLayout *layouts[1];
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo bufferInfo = {0};
  GPUBindGroupEntry groupEntry = {0};
  GPUBindGroupCreateInfo groupInfo = {0};
  GPUBufferBarrier bufferBarrier = {0};
  GPUBarrierBatch barrierBatch = {0};
  GPUQueueSubmitInfo submitInfo = {0};
  uint32_t readWords[4] = {0u, 0u, 0u, 0u};
  int ok = 0;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for compute indirect test\n");
    return 0;
  }
  if (!create_compute_indirect_library(device, &library)) {
    fprintf(stderr, "failed to create compute indirect library\n");
    goto cleanup;
  }

  layoutEntry.binding = 0u;
  layoutEntry.bindingType = GPU_BINDING_STORAGE_BUFFER;
  layoutEntry.visibility = GPU_SHADER_STAGE_COMPUTE_BIT;
  layoutEntry.arrayCount = 1u;
  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label = "api-compute-indirect-bgl";
  layoutInfo.entryCount = 1u;
  layoutInfo.pEntries = &layoutEntry;
  if (GPUCreateBindGroupLayout(device,
                               &layoutInfo,
                               &bindGroupLayout) != GPU_OK ||
      !bindGroupLayout) {
    fprintf(stderr, "failed to create compute indirect bind group layout\n");
    goto cleanup;
  }

  layouts[0] = bindGroupLayout;
  pipelineLayoutInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.chain.structSize = sizeof(pipelineLayoutInfo);
  pipelineLayoutInfo.label = "api-compute-indirect-layout";
  pipelineLayoutInfo.bindGroupLayoutCount = 1u;
  pipelineLayoutInfo.ppBindGroupLayouts = layouts;
  if (GPUCreatePipelineLayout(device,
                              &pipelineLayoutInfo,
                              &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "failed to create compute indirect pipeline layout\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label = "api-compute-indirect-pipeline";
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.library = library;
  pipelineInfo.entryPoint = "api_indirect_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "failed to create compute indirect pipeline\n");
    goto cleanup;
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label = "api-compute-indirect-output";
  bufferInfo.sizeBytes = sizeof(readWords);
  bufferInfo.usage = GPU_BUFFER_USAGE_STORAGE |
                     GPU_BUFFER_USAGE_COPY_SRC |
                     GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &outputBuffer) != GPU_OK ||
      !outputBuffer ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          kZeroWords,
                          sizeof(kZeroWords)) != GPU_OK) {
    fprintf(stderr, "failed to create compute indirect output buffer\n");
    goto cleanup;
  }

  bufferInfo.label = "api-compute-indirect-args";
  bufferInfo.sizeBytes = sizeof(kDispatchArgs);
  bufferInfo.usage = GPU_BUFFER_USAGE_INDIRECT | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &argsBuffer) != GPU_OK ||
      !argsBuffer ||
      GPUQueueWriteBuffer(queue,
                          argsBuffer,
                          0u,
                          &kDispatchArgs,
                          sizeof(kDispatchArgs)) != GPU_OK) {
    fprintf(stderr, "failed to create compute indirect args buffer\n");
    goto cleanup;
  }

  groupEntry.binding = 0u;
  groupEntry.bindingType = GPU_BINDING_STORAGE_BUFFER;
  groupEntry.buffer.buffer = outputBuffer;
  groupEntry.buffer.offset = 0u;
  groupEntry.buffer.size = sizeof(readWords);
  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label = "api-compute-indirect-group";
  groupInfo.layout = bindGroupLayout;
  groupInfo.entryCount = 1u;
  groupInfo.pEntries = &groupEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &bindGroup) != GPU_OK ||
      !bindGroup) {
    fprintf(stderr, "failed to create compute indirect bind group\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "api-compute-indirect", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "failed to acquire compute indirect command buffer\n");
    goto cleanup;
  }

  computePass = GPUBeginComputePass(cmdb, "api-compute-indirect-pass");
  if (!computePass) {
    fprintf(stderr, "failed to begin compute indirect pass\n");
    goto cleanup;
  }
  GPUBindComputePipeline(computePass, pipeline);
  GPUBindComputeGroup(computePass, 0u, bindGroup, 0u, NULL);
  GPUDispatchIndirect(computePass, argsBuffer, 0u);
  GPUEndComputePass(computePass);
  computePass = NULL;

  bufferBarrier.buffer = outputBuffer;
  bufferBarrier.srcAccess = GPU_ACCESS_SHADER_WRITE;
  bufferBarrier.dstAccess = GPU_ACCESS_TRANSFER_READ;
  bufferBarrier.sizeBytes = sizeof(readWords);
  barrierBatch.srcStages = GPU_STAGE_COMPUTE;
  barrierBatch.dstStages = GPU_STAGE_TRANSFER;
  barrierBatch.bufferBarrierCount = 1u;
  barrierBatch.pBufferBarriers = &bufferBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "failed to create compute indirect fence\n");
    goto cleanup;
  }

  buffers[0] = cmdb;
  submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers = buffers;
  submitInfo.fence = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "compute indirect submit failed\n");
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         readWords,
                         sizeof(readWords)) != GPU_OK ||
      memcmp(readWords, expectedWords, sizeof(readWords)) != 0) {
    fprintf(stderr,
            "compute indirect readback mismatch: %u %u %u %u\n",
            readWords[0],
            readWords[1],
            readWords[2],
            readWords[3]);
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (computePass) {
    GPUEndComputePass(computePass);
  }
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(bindGroup);
  GPUDestroyBuffer(argsBuffer);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(bindGroupLayout);
  GPUDestroyShaderLibrary(library);
  return ok;
}

int
gpu_test_compute(GPUDevice *device) {
  return check_compute_pass_validation() &&
         check_compute_pipeline_validation(device) &&
         check_compute_indirect_readback(device);
}
