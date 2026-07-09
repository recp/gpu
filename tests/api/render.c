#include "test.h"
#include "../../src/api/cmdqueue_internal.h"

static const char *kRenderPipelineMSL =
  "#include <metal_stdlib>\n"
  "using namespace metal;\n"
  "struct VSIn { float2 position [[attribute(0)]]; };\n"
  "vertex float4 api_vs(VSIn in [[stage_in]]) {\n"
  "  return float4(in.position, 0.0, 1.0);\n"
  "}\n"
  "fragment float4 api_fs() {\n"
  "  return float4(1.0, 0.0, 0.0, 1.0);\n"
  "}\n";

static int
create_render_test_library(GPUDevice *device, GPUShaderLibrary **outLibrary) {
  GPUShaderLibraryCreateInfo info = {0};

  info.chain.sType = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label = "api-render-pipeline.metal";
  info.sourceKind = GPU_SHADER_SOURCE_MSL_TEXT;
  info.sourceData = kRenderPipelineMSL;
  info.sourceSize = (uint64_t)strlen(kRenderPipelineMSL);

  return GPUCreateShaderLibrary(device, &info, outLibrary) == GPU_OK &&
         *outLibrary;
}

static int
expect_render_pipeline_error(GPUDevice *device,
                             const GPURenderPipelineCreateInfo *info,
                             const char *message) {
  GPURenderPipeline *pipeline = (GPURenderPipeline *)(uintptr_t)1u;

  if (GPUCreateRenderPipeline(device, info, &pipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      pipeline != NULL) {
    fprintf(stderr, "%s\n", message);
    GPUDestroyRenderPipeline(pipeline);
    return 0;
  }

  return 1;
}

static int
check_pipeline_cache_validation(GPUDevice *device,
                                GPURenderPipelineCreateInfo *info) {
  GPUPipelineCacheCreateInfo cacheInfo = {0};
  GPUPipelineCache *cache = NULL;
  GPURenderPipeline *pipeline = NULL;
  GPURenderPipeline *asyncPipeline = (GPURenderPipeline *)(uintptr_t)1u;
  GPUPipelineCompileHandle handle = {0};
  GPUPipelineCompileStatus status = GPU_PIPELINE_COMPILE_PENDING;
  GPUCacheStats stats;

  if (GPUGetCacheStats(NULL, &stats) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetCacheStats(device, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "cache stats accepted null input\n");
    return 0;
  }

  GPUResetStats(device);
  cacheInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  cacheInfo.chain.structSize = sizeof(cacheInfo);
  cacheInfo.label = "api-render-cache";

  if (GPUCreatePipelineCache(NULL, &cacheInfo, &cache) != GPU_ERROR_INVALID_ARGUMENT ||
      cache != NULL) {
    fprintf(stderr, "pipeline cache create accepted null device\n");
    return 0;
  }
  if (GPUCreatePipelineCache(device, &cacheInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "pipeline cache create accepted null output\n");
    return 0;
  }

  cacheInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  if (GPUCreatePipelineCache(device, &cacheInfo, &cache) != GPU_ERROR_INVALID_ARGUMENT ||
      cache != NULL) {
    fprintf(stderr, "pipeline cache create accepted wrong sType\n");
    return 0;
  }

  cacheInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  cacheInfo.chain.structSize = (uint32_t)(sizeof(cacheInfo) - 1u);
  if (GPUCreatePipelineCache(device, &cacheInfo, &cache) != GPU_ERROR_INVALID_ARGUMENT ||
      cache != NULL) {
    fprintf(stderr, "pipeline cache create accepted short structSize\n");
    return 0;
  }

  cacheInfo.chain.structSize = sizeof(cacheInfo);
  if (GPUCreatePipelineCache(device, &cacheInfo, &cache) != GPU_OK || !cache) {
    fprintf(stderr, "pipeline cache create rejected valid cache\n");
    return 0;
  }

  info->cache = cache;
  if (GPUCreateRenderPipeline(device, info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "render pipeline create rejected valid cached pipeline\n");
    goto fail;
  }
  GPUDestroyRenderPipeline(pipeline);
  pipeline = NULL;
  info->cache = NULL;

  if (GPUGetCacheStats(device, &stats) != GPU_OK ||
      stats.pipelineCompiles != 1u ||
      stats.pipelineMisses != 1u ||
      stats.pipelineHits != 0u) {
    fprintf(stderr, "pipeline cache stats did not record compile miss\n");
    goto fail;
  }

  GPUResetStats(device);
  if (GPUGetCacheStats(device, &stats) != GPU_OK ||
      stats.pipelineCompiles != 0u ||
      stats.pipelineMisses != 0u) {
    fprintf(stderr, "pipeline cache stats reset failed\n");
    goto fail;
  }

  if (GPUCompileRenderPipelineAsync(device, cache, info, NULL) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "async pipeline compile accepted null handle\n");
    goto fail;
  }
  if (GPUCompileRenderPipelineAsync(device, cache, info, &handle) !=
      GPU_ERROR_UNSUPPORTED ||
      handle.id != 0u) {
    fprintf(stderr, "async pipeline compile did not report unsupported\n");
    goto fail;
  }
  if (GPUPollRenderPipelineCompile(device, handle, &status, &asyncPipeline) !=
      GPU_ERROR_UNSUPPORTED ||
      status != GPU_PIPELINE_COMPILE_FAILED ||
      asyncPipeline != NULL) {
    fprintf(stderr, "async pipeline poll did not report unsupported\n");
    goto fail;
  }

  GPUDestroyPipelineCache(cache);
  return 1;

fail:
  info->cache = NULL;
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyPipelineCache(cache);
  return 0;
}

static int
check_render_pipeline_validation(GPUDevice *device) {
  GPUShaderLibrary *library = NULL;
  GPUColorTargetState colorTargets[9] = {0};
  GPUVertexAttribute attr = {0};
  GPUVertexBufferLayout vertexLayout = {0};
  GPURenderPipelineCreateInfo info = {0};
  GPUDepthStencilState depthStencil = {0};
  GPURenderPipeline *pipeline;

  if (!create_render_test_library(device, &library)) {
    fprintf(stderr, "failed to create render pipeline test library\n");
    return 0;
  }

  colorTargets[0].format = GPU_FORMAT_BGRA8_UNORM;
  attr.shaderLocation = 0u;
  attr.format = GPUFloat2;
  attr.offset = 0u;
  vertexLayout.strideBytes = 8u;
  vertexLayout.stepMode = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount = 1u;
  vertexLayout.pAttributes = &attr;

  info.chain.sType = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.library = library;
  info.vertexEntry = "api_vs";
  info.fragmentEntry = "api_fs";
  info.vertex.bufferLayoutCount = 1u;
  info.vertex.pBufferLayouts = &vertexLayout;
  info.colorTargetCount = 1u;
  info.pColorTargets = colorTargets;
  info.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode = GPU_CULL_MODE_NONE;
  info.frontFace = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;

  pipeline = (GPURenderPipeline *)(uintptr_t)1u;
  if (GPUCreateRenderPipeline(NULL, &info, &pipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      pipeline != NULL) {
    fprintf(stderr, "render pipeline create accepted null device\n");
    GPUDestroyRenderPipeline(pipeline);
    GPUDestroyShaderLibrary(library);
    return 0;
  }
  if (GPUCreateRenderPipeline(device, &info, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "render pipeline create accepted null output\n");
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted wrong sType")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize = (uint32_t)(sizeof(info) - 1u);
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted short structSize")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.chain.structSize = sizeof(info);
  info.library = NULL;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted null library")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.library = library;
  info.colorTargetCount = 0u;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted no color targets")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.colorTargetCount = (uint32_t)GPU_ARRAY_LEN(colorTargets);
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted too many color targets")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.colorTargetCount = 1u;
  colorTargets[0].format = GPU_FORMAT_UNDEFINED;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted undefined color format")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  colorTargets[0].format = GPU_FORMAT_BGRA8_UNORM;
  attr.format = GPUUnknown;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid vertex format")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  attr.format = GPUFloat2;
  vertexLayout.stepMode = (GPUVertexStepMode)99;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid vertex step mode")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  vertexLayout.stepMode = GPU_VERTEX_STEP_MODE_VERTEX;
  info.primitiveTopology = (GPUPrimitiveTopology)99;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid topology")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted unsupported topology")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode = (GPUCullMode)99;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid cull mode")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.cullMode = GPU_CULL_MODE_NONE;
  info.frontFace = (GPUFrontFace)99;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid front face")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.frontFace = GPU_FRONT_FACE_CCW;
  colorTargets[0].blend.enabled = true;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted unsupported blend state")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  memset(&colorTargets[0].blend, 0, sizeof(colorTargets[0].blend));
  depthStencil.depthTestEnable = true;
  info.pDepthStencilState = &depthStencil;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted unsupported depth state")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.pDepthStencilState = NULL;
  info.multisample.alphaToCoverageEnable = true;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted alpha-to-coverage")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.multisample.alphaToCoverageEnable = false;
  info.multisample.sampleMask = 0x7fffffffu;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted unsupported sample mask")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.multisample.sampleMask = 0u;
  if (!check_pipeline_cache_validation(device, &info)) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  GPUDestroyShaderLibrary(library);
  return 1;
}

static int
check_render_pass_validation(void) {
  GPUCommandBuffer fakeCmdb = {0};
  GPUTextureView *fakeView = (GPUTextureView *)(uintptr_t)1u;
  GPURenderPassColorAttachment colors[9] = {0};
  GPURenderPassDepthStencilAttachment depthStencil = {0};
  GPURenderPassCreateInfo rp = {0};

  if (GPUBeginRenderPass(NULL, &rp) ||
      GPUBeginRenderPass(&fakeCmdb, NULL)) {
    fprintf(stderr, "render pass accepted null input\n");
    return 0;
  }

  rp.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  rp.chain.structSize = sizeof(rp);
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted wrong sType\n");
    return 0;
  }

  rp.chain.sType = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rp.chain.structSize = (uint32_t)(sizeof(rp) - 1u);
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted short structSize\n");
    return 0;
  }

  rp.chain.structSize = sizeof(rp);
  rp.colorAttachmentCount = 1u;
  rp.pColorAttachments = NULL;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted missing color attachments\n");
    return 0;
  }

  rp.pColorAttachments = colors;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted null color view\n");
    return 0;
  }

  colors[0].view = fakeView;
  colors[0].loadOp = (GPULoadOp)99;
  colors[0].storeOp = GPU_STORE_OP_STORE;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid color load op\n");
    return 0;
  }

  colors[0].loadOp = GPU_LOAD_OP_CLEAR;
  colors[0].storeOp = (GPUStoreOp)99;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid color store op\n");
    return 0;
  }

  colors[0].storeOp = GPU_STORE_OP_STORE;
  rp.colorAttachmentCount = (uint32_t)GPU_ARRAY_LEN(colors);
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted too many color attachments\n");
    return 0;
  }

  rp.colorAttachmentCount = 0u;
  rp.pColorAttachments = NULL;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted no attachments\n");
    return 0;
  }

  rp.pDepthStencilAttachment = &depthStencil;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted null depth-stencil view\n");
    return 0;
  }

  depthStencil.view = fakeView;
  depthStencil.depthLoadOp = (GPULoadOp)99;
  depthStencil.depthStoreOp = GPU_STORE_OP_STORE;
  depthStencil.stencilLoadOp = GPU_LOAD_OP_DONT_CARE;
  depthStencil.stencilStoreOp = GPU_STORE_OP_DONT_CARE;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid depth load op\n");
    return 0;
  }

  depthStencil.depthLoadOp = GPU_LOAD_OP_CLEAR;
  fakeCmdb._activeEncoder = true;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted command buffer with active encoder\n");
    return 0;
  }

  fakeCmdb._activeEncoder = false;
  fakeCmdb._submitted = true;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted submitted command buffer\n");
    return 0;
  }

  return 1;
}

static int
check_render_encoder_validation(void) {
  GPURenderPassEncoder pass = {0};
  GPURenderPassEncoder endedPass = {0};
  GPUBuffer *fakeBuffer = (GPUBuffer *)(uintptr_t)1u;
  GPUBufferBinding binding = {0};
  GPUDynamicStateApplyInfo dynamicState = {0};
  uint32_t pushValue = 0xaabbccddu;
  uint8_t pushBefore[16];

  GPUBindRenderPipeline(NULL, NULL);
  GPUBindVertexBuffers(NULL, 0u, 0u, NULL);
  GPUBindIndexBuffer(NULL, fakeBuffer, 0u, GPUIndexTypeUInt16);
  GPUDraw(NULL, 1u, 1u, 0u, 0u);
  GPUDrawIndexed(NULL, 1u, 1u, 0u, 0, 0u);
  GPUDrawIndirect(NULL, fakeBuffer, 0u);
  GPUDrawIndexedIndirect(NULL, fakeBuffer, 0u);
  GPUApplyDynamicState(NULL, &dynamicState);
  GPUApplyDynamicState(&pass, NULL);
  GPUSetRenderPushConstants(NULL, 0u, sizeof(pushValue), &pushValue);
  GPUDraw(&pass, 1u, 1u, 0u, 0u);
  GPUDrawIndirect(&pass, fakeBuffer, 0u);

  pass._hasPipeline = true;
  GPUDrawIndirect(&pass, NULL, 0u);
  GPUDrawIndexedIndirect(&pass, fakeBuffer, 0u);
  pass._pushConstantSizeBytes = sizeof(pushBefore);
  pass._pushConstantStages = GPU_SHADER_STAGE_VERTEX_BIT |
                             GPU_SHADER_STAGE_FRAGMENT_BIT;
  GPUSetRenderPushConstants(&pass, 4u, sizeof(pushValue), &pushValue);
  if (memcmp(pass._pushConstants + 4u, &pushValue, sizeof(pushValue)) != 0) {
    fprintf(stderr, "render push constants did not update expected range\n");
    return 0;
  }

  memcpy(pushBefore, pass._pushConstants, sizeof(pushBefore));
  GPUSetRenderPushConstants(&pass, 14u, sizeof(pushValue), &pushValue);
  if (memcmp(pass._pushConstants, pushBefore, sizeof(pushBefore)) != 0) {
    fprintf(stderr, "render push constants accepted out-of-range update\n");
    return 0;
  }

  binding.buffer = fakeBuffer;
  GPUBindVertexBuffers(&pass, UINT32_MAX, 2u, &binding);
  GPUBindIndexBuffer(&pass, fakeBuffer, 0u, (GPUIndexType)99);
  if (pass._hasIndexBuffer) {
    fprintf(stderr, "render encoder accepted invalid index type\n");
    return 0;
  }

  GPUBindIndexBuffer(&pass, fakeBuffer, 0u, GPUIndexTypeUInt16);
  if (!pass._hasIndexBuffer ||
      pass._indexBuffer != fakeBuffer ||
      pass._indexType != GPUIndexTypeUInt16) {
    fprintf(stderr, "render encoder rejected valid index binding\n");
    return 0;
  }
  GPUDrawIndirect(&pass, fakeBuffer, 0u);
  GPUDrawIndexedIndirect(&pass, fakeBuffer, 0u);

  dynamicState.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  dynamicState.chain.structSize = sizeof(dynamicState);
  GPUApplyDynamicState(&pass, &dynamicState);

  dynamicState.chain.sType = GPU_STRUCTURE_TYPE_DYNAMIC_STATE_APPLY_INFO;
  dynamicState.chain.structSize = (uint32_t)(sizeof(dynamicState) - 1u);
  GPUApplyDynamicState(&pass, &dynamicState);

  endedPass._ended = true;
  GPUBindIndexBuffer(&endedPass, fakeBuffer, 0u, GPUIndexTypeUInt16);
  if (endedPass._hasIndexBuffer) {
    fprintf(stderr, "render encoder accepted index binding after end\n");
    return 0;
  }
  GPUBindVertexBuffers(&endedPass, 0u, 1u, &binding);
  GPUSetViewport(&endedPass, &dynamicState.viewport);
  GPUSetRenderPushConstants(&endedPass, 0u, sizeof(pushValue), &pushValue);
  GPUDraw(&endedPass, 1u, 1u, 0u, 0u);
  GPUDrawIndexed(&endedPass, 1u, 1u, 0u, 0, 0u);
  GPUDrawIndirect(&endedPass, fakeBuffer, 0u);
  GPUDrawIndexedIndirect(&endedPass, fakeBuffer, 0u);
  GPUApplyDynamicState(&endedPass, &dynamicState);
  GPUEndRenderPass(&endedPass);

  return 1;
}

int
gpu_test_render(GPUDevice *device) {
  return check_render_pass_validation() &&
         check_render_encoder_validation() &&
         check_render_pipeline_validation(device);
}
