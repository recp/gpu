#include "test.h"
#include "../../src/api/cmdqueue_internal.h"
#include "../../src/api/compute_internal.h"

static const char *kComputePipelineMSL =
  "#include <metal_stdlib>\n"
  "using namespace metal;\n"
  "kernel void api_cs(uint3 gid [[thread_position_in_grid]]) {\n"
  "  (void)gid;\n"
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
  GPUBuffer *fakeBuffer = (GPUBuffer *)(uintptr_t)1u;
  uint32_t dynamicOffset = 0u;

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
  GPUDispatch(NULL, 1u, 1u, 1u);
  GPUDispatch(&fakePass, 1u, 1u, 1u);
  GPUDispatch(&fakePass, 0u, 1u, 1u);
  GPUDispatch(&fakePass, 1u, 0u, 1u);
  GPUDispatch(&fakePass, 1u, 1u, 0u);
  GPUDispatchIndirect(NULL, fakeBuffer, 0u);
  GPUDispatchIndirect(&fakePass, NULL, 0u);
  GPUEndComputePass(NULL);

  fakePass._ended = true;
  GPUBindComputePipeline(&fakePass, &fakePipeline);
  GPUBindComputeGroup(&fakePass, 0u, NULL, 0u, NULL);
  GPUDispatch(&fakePass, 1u, 1u, 1u);
  GPUDispatchIndirect(&fakePass, fakeBuffer, 0u);
  GPUEndComputePass(&fakePass);

  return 1;
}

int
gpu_test_compute(GPUDevice *device) {
  return check_compute_pass_validation() &&
         check_compute_pipeline_validation(device);
}
