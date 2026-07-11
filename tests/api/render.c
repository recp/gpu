#include "test.h"
#include "../../src/api/buffer_internal.h"
#include "../../src/api/cmdqueue_internal.h"
#include "../../src/api/render/pipeline_internal.h"
#include "../../src/api/texture_internal.h"

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

static uint32_t gRenderDrawCalls;
static uint32_t gRenderDrawIndexedCalls;
static uint32_t gRenderDrawIndirectCalls;
static uint32_t gRenderDrawIndexedIndirectCalls;
static uint32_t gRenderViewportCalls;
static uint32_t gRenderScissorCalls;
static uint32_t gRenderBlendCalls;
static uint32_t gRenderStencilCalls;

static void
count_draw_primitives(GPURenderCommandEncoder *rce,
                      GPUPrimitiveType type,
                      size_t start,
                      size_t count,
                      uint32_t instanceCount,
                      uint32_t firstInstance) {
  (void)rce;
  (void)type;
  (void)start;
  (void)count;
  (void)instanceCount;
  (void)firstInstance;
  gRenderDrawCalls++;
}

static void
count_draw_indexed(GPURenderCommandEncoder *rce,
                   uint32_t indexCount,
                   uint32_t instanceCount,
                   uint32_t firstIndex,
                   int32_t vertexOffset,
                   uint32_t firstInstance) {
  (void)rce;
  (void)indexCount;
  (void)instanceCount;
  (void)firstIndex;
  (void)vertexOffset;
  (void)firstInstance;
  gRenderDrawIndexedCalls++;
}

static void
count_draw_indirect(GPURenderCommandEncoder *rce,
                    GPUPrimitiveType type,
                    GPUBuffer *argsBuffer,
                    uint64_t argsOffset) {
  (void)rce;
  (void)type;
  (void)argsBuffer;
  (void)argsOffset;
  gRenderDrawIndirectCalls++;
}

static void
count_draw_indexed_indirect(GPURenderCommandEncoder *rce,
                            GPUBuffer *argsBuffer,
                            uint64_t argsOffset) {
  (void)rce;
  (void)argsBuffer;
  (void)argsOffset;
  gRenderDrawIndexedIndirectCalls++;
}

static void
count_viewport(GPURenderCommandEncoder *rce, const GPUViewport *viewport) {
  (void)rce;
  (void)viewport;
  gRenderViewportCalls++;
}

static void
count_scissor(GPURenderCommandEncoder *rce, const GPUScissorRect *scissor) {
  (void)rce;
  (void)scissor;
  gRenderScissorCalls++;
}

static void
count_blend_constant(GPURenderCommandEncoder *rce, const float rgba[4]) {
  (void)rce;
  (void)rgba;
  gRenderBlendCalls++;
}

static void
count_stencil_reference(GPURenderCommandEncoder *rce, uint32_t reference) {
  (void)rce;
  (void)reference;
  gRenderStencilCalls++;
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
  GPUPipelineLayoutCreateInfo layoutInfo = {0};
  GPUPipelineLayout *pipelineLayout = NULL;
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
  if (!expect_render_pipeline_error(device,
                                    NULL,
                                    "render pipeline create accepted null info")) {
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
  info.vertexEntry = NULL;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted null vertex entry")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.vertexEntry = "api_vs";
  info.fragmentEntry = NULL;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted null fragment entry")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.fragmentEntry = "api_fs";
  info.vertexEntry = "missing_vs";
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted missing vertex entry")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.vertexEntry = "api_vs";
  info.fragmentEntry = "missing_fs";
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted missing fragment entry")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.fragmentEntry = "api_fs";
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted null layout")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label = "api-render-empty-layout";
  if (GPUCreatePipelineLayout(device, &layoutInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "failed to create render validation pipeline layout\n");
    GPUDestroyShaderLibrary(library);
    return 0;
  }
  info.layout = pipelineLayout;

  info.colorTargetCount = 0u;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted no color targets")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.colorTargetCount = (uint32_t)GPU_ARRAY_LEN(colorTargets);
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted too many color targets")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.colorTargetCount = 1u;
  info.pColorTargets = NULL;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted null color targets")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.pColorTargets = colorTargets;
  colorTargets[0].format = GPU_FORMAT_UNDEFINED;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted undefined color format")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  colorTargets[0].format = GPU_FORMAT_BGRA8_UNORM;
  info.vertex.pBufferLayouts = NULL;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted null vertex layouts")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.vertex.pBufferLayouts = &vertexLayout;
  vertexLayout.pAttributes = NULL;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted null vertex attributes")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  vertexLayout.pAttributes = &attr;
  attr.format = GPUUnknown;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid vertex format")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  attr.format = GPUFloat2;
  vertexLayout.stepMode = (GPUVertexStepMode)99;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid vertex step mode")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  vertexLayout.stepMode = GPU_VERTEX_STEP_MODE_VERTEX;
  info.primitiveTopology = (GPUPrimitiveTopology)99;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid topology")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted unsupported topology")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode = (GPUCullMode)99;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid cull mode")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.cullMode = GPU_CULL_MODE_NONE;
  info.frontFace = (GPUFrontFace)99;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid front face")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.frontFace = GPU_FRONT_FACE_CCW;
  colorTargets[0].blend.enabled = true;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted unsupported blend state")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  memset(&colorTargets[0].blend, 0, sizeof(colorTargets[0].blend));
  depthStencil.depthTestEnable = true;
  info.pDepthStencilState = &depthStencil;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted unsupported depth state")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.pDepthStencilState = NULL;
  info.multisample.alphaToCoverageEnable = true;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted alpha-to-coverage")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.multisample.alphaToCoverageEnable = false;
  info.multisample.sampleMask = 0x7fffffffu;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted unsupported sample mask")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.multisample.sampleMask = 0u;
  if (!check_pipeline_cache_validation(device, &info)) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyShaderLibrary(library);
  return 1;
}

typedef enum RenderReadbackDrawMode {
  RENDER_READBACK_DRAW,
  RENDER_READBACK_DRAW_INDEXED,
  RENDER_READBACK_DRAW_INDIRECT,
  RENDER_READBACK_DRAW_INDEXED_INDIRECT,
  RENDER_READBACK_DRAW_MULTI_INDIRECT,
  RENDER_READBACK_DRAW_INDEXED_MULTI_INDIRECT
} RenderReadbackDrawMode;

typedef struct RenderIndirectArgs {
  uint32_t vertexCount;
  uint32_t instanceCount;
  uint32_t firstVertex;
  uint32_t firstInstance;
} RenderIndirectArgs;

typedef struct RenderIndexedIndirectArgs {
  uint32_t indexCount;
  uint32_t instanceCount;
  uint32_t firstIndex;
  int32_t  vertexOffset;
  uint32_t firstInstance;
} RenderIndexedIndirectArgs;

static const char *
render_readback_label(RenderReadbackDrawMode mode, int clipped) {
  switch (mode) {
    case RENDER_READBACK_DRAW_INDEXED:
      return clipped ? "api-render-readback-indexed-dynamic"
                     : "api-render-readback-indexed";
    case RENDER_READBACK_DRAW_INDIRECT:
      return "api-render-readback-indirect";
    case RENDER_READBACK_DRAW_INDEXED_INDIRECT:
      return "api-render-readback-indexed-indirect";
    case RENDER_READBACK_DRAW_MULTI_INDIRECT:
      return "api-render-readback-multi-indirect";
    case RENDER_READBACK_DRAW_INDEXED_MULTI_INDIRECT:
      return "api-render-readback-indexed-multi-indirect";
    case RENDER_READBACK_DRAW:
    default:
      return "api-render-readback";
  }
}

static int
render_readback_pixel_is_red(const uint8_t *pixels,
                             uint32_t width,
                             uint32_t x,
                             uint32_t y) {
  size_t offset = ((size_t)y * width + x) * 4u;

  return pixels[offset + 0u] <= 2u &&
         pixels[offset + 1u] <= 2u &&
         pixels[offset + 2u] >= 250u &&
         pixels[offset + 3u] >= 250u;
}

static int
render_readback_has_red_in_x_range(const uint8_t *pixels,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t minX,
                                   uint32_t maxX) {
  for (uint32_t y = 0u; y < height; y++) {
    for (uint32_t x = minX; x < maxX; x++) {
      if (render_readback_pixel_is_red(pixels, width, x, y)) {
        return 1;
      }
    }
  }

  return 0;
}

static int
check_render_readback_case(GPUDevice *device,
                           RenderReadbackDrawMode mode,
                           int clipped) {
  static const float kFullscreenTriangle[] = {
    -1.0f, -1.0f,
     3.0f, -1.0f,
    -1.0f,  3.0f
  };
  static const float kMultiTriangles[] = {
    -1.0f, -1.0f,
     0.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f,
     0.0f,  1.0f,
     1.0f, -1.0f
  };
  static const uint16_t kTriangleIndices[] = {0u, 1u, 2u};
  static const uint16_t kMultiTriangleIndices[] = {0u, 1u, 2u, 3u, 4u, 5u};
  static const RenderIndirectArgs kIndirectArgs = {3u, 1u, 0u, 0u};
  static const RenderIndirectArgs kMultiIndirectArgs[2] = {
    {3u, 1u, 0u, 0u},
    {3u, 1u, 3u, 0u}
  };
  static const RenderIndexedIndirectArgs kIndexedIndirectArgs = {
    3u, 1u, 0u, 0, 0u
  };
  static const RenderIndexedIndirectArgs kMultiIndexedIndirectArgs[2] = {
    {3u, 1u, 0u, 0, 0u},
    {3u, 1u, 3u, 0, 0u}
  };
  const uint32_t width = 4u;
  const uint32_t height = 4u;
  const uint64_t pixelBytes = (uint64_t)width * height * 4u;
  const char *label = render_readback_label(mode, clipped);
  const int indexed = mode == RENDER_READBACK_DRAW_INDEXED ||
                      mode == RENDER_READBACK_DRAW_INDEXED_INDIRECT ||
                      mode == RENDER_READBACK_DRAW_INDEXED_MULTI_INDIRECT;
  const int indirect = mode == RENDER_READBACK_DRAW_INDIRECT ||
                       mode == RENDER_READBACK_DRAW_INDEXED_INDIRECT ||
                       mode == RENDER_READBACK_DRAW_MULTI_INDIRECT ||
                       mode == RENDER_READBACK_DRAW_INDEXED_MULTI_INDIRECT;
  const int multi = mode == RENDER_READBACK_DRAW_MULTI_INDIRECT ||
                    mode == RENDER_READBACK_DRAW_INDEXED_MULTI_INDIRECT;
  const void *vertexData = multi ? (const void *)kMultiTriangles :
                                  (const void *)kFullscreenTriangle;
  const uint64_t vertexDataSize = multi ? sizeof(kMultiTriangles) :
                                          sizeof(kFullscreenTriangle);
  const void *indexData = multi ? (const void *)kMultiTriangleIndices :
                                 (const void *)kTriangleIndices;
  const uint64_t indexDataSize = multi ? sizeof(kMultiTriangleIndices) :
                                         sizeof(kTriangleIndices);
  GPUCommandQueue *queue;
  GPUShaderLibrary *library = NULL;
  GPURenderPipeline *pipeline = NULL;
  GPUBuffer *vertexBuffer = NULL;
  GPUBuffer *indexBuffer = NULL;
  GPUBuffer *argsBuffer = NULL;
  GPUBuffer *readbackBuffer = NULL;
  GPUTexture *target = NULL;
  GPUTextureView *targetView = NULL;
  GPUPipelineLayout *pipelineLayout = NULL;
  GPUCommandBuffer *cmdb = NULL;
  GPUCommandBuffer *buffers[1];
  GPURenderPassEncoder *renderPass = NULL;
  GPUCopyPassEncoder *copyPass = NULL;
  GPUFence *fence = NULL;
  GPUColorTargetState colorTarget = {0};
  GPUVertexAttribute attr = {0};
  GPUVertexBufferLayout vertexLayout = {0};
  GPUPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
  GPURenderPipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo bufferInfo = {0};
  GPUTextureCreateInfo textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo = {0};
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo rp = {0};
  GPUBufferBinding vertexBinding = {0};
  GPUTextureBarrier textureBarrier = {0};
  GPUBarrierBatch barrierBatch = {0};
  GPUBufferTextureCopyRegion copyRegion = {0};
  GPUQueueSubmitInfo submitInfo = {0};
  GPUDynamicStateApplyInfo dynamicState = {0};
  uint8_t pixels[4u * 4u * 4u] = {0};
  size_t centerOffset;
  int ok = 0;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for %s test\n", label);
    return 0;
  }
  if (!create_render_test_library(device, &library)) {
    fprintf(stderr, "failed to create %s library\n", label);
    goto cleanup;
  }

  colorTarget.format = GPU_FORMAT_BGRA8_UNORM;
  attr.shaderLocation = 0u;
  attr.format = GPUFloat2;
  attr.offset = 0u;
  vertexLayout.strideBytes = 8u;
  vertexLayout.stepMode = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount = 1u;
  vertexLayout.pAttributes = &attr;

  pipelineLayoutInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.chain.structSize = sizeof(pipelineLayoutInfo);
  pipelineLayoutInfo.label = label;
  if (GPUCreatePipelineLayout(device, &pipelineLayoutInfo, &pipelineLayout) !=
        GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "failed to create %s pipeline layout\n", label);
    goto cleanup;
  }

  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label = label;
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.library = library;
  pipelineInfo.vertexEntry = "api_vs";
  pipelineInfo.fragmentEntry = "api_fs";
  pipelineInfo.vertex.bufferLayoutCount = 1u;
  pipelineInfo.vertex.pBufferLayouts = &vertexLayout;
  pipelineInfo.colorTargetCount = 1u;
  pipelineInfo.pColorTargets = &colorTarget;
  pipelineInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "failed to create %s pipeline\n", label);
    goto cleanup;
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label = label;
  bufferInfo.sizeBytes = vertexDataSize;
  bufferInfo.usage = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &vertexBuffer) != GPU_OK ||
      !vertexBuffer ||
      GPUQueueWriteBuffer(queue,
                          vertexBuffer,
                          0u,
                          vertexData,
                          vertexDataSize) != GPU_OK) {
    fprintf(stderr, "failed to create %s vertex buffer\n", label);
    goto cleanup;
  }

  if (indexed) {
    bufferInfo.sizeBytes = indexDataSize;
    bufferInfo.usage = GPU_BUFFER_USAGE_INDEX | GPU_BUFFER_USAGE_COPY_DST;
    if (GPUCreateBuffer(device, &bufferInfo, &indexBuffer) != GPU_OK ||
        !indexBuffer ||
        GPUQueueWriteBuffer(queue,
                            indexBuffer,
                            0u,
                            indexData,
                            indexDataSize) != GPU_OK) {
      fprintf(stderr, "failed to create %s index buffer\n", label);
      goto cleanup;
    }
  }

  if (indirect) {
    const void *argsData;

    if (mode == RENDER_READBACK_DRAW_INDIRECT) {
      argsData = &kIndirectArgs;
      bufferInfo.sizeBytes = sizeof(kIndirectArgs);
    } else if (mode == RENDER_READBACK_DRAW_MULTI_INDIRECT) {
      argsData = kMultiIndirectArgs;
      bufferInfo.sizeBytes = sizeof(kMultiIndirectArgs);
    } else if (mode == RENDER_READBACK_DRAW_INDEXED_MULTI_INDIRECT) {
      argsData = kMultiIndexedIndirectArgs;
      bufferInfo.sizeBytes = sizeof(kMultiIndexedIndirectArgs);
    } else {
      argsData = &kIndexedIndirectArgs;
      bufferInfo.sizeBytes = sizeof(kIndexedIndirectArgs);
    }

    bufferInfo.usage = GPU_BUFFER_USAGE_INDIRECT | GPU_BUFFER_USAGE_COPY_DST;
    if (GPUCreateBuffer(device, &bufferInfo, &argsBuffer) != GPU_OK ||
        !argsBuffer ||
        GPUQueueWriteBuffer(queue,
                            argsBuffer,
                            0u,
                            argsData,
                            bufferInfo.sizeBytes) != GPU_OK) {
      fprintf(stderr, "failed to create %s args buffer\n", label);
      goto cleanup;
    }
  }

  bufferInfo.label = label;
  bufferInfo.sizeBytes = pixelBytes;
  bufferInfo.usage = GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readbackBuffer) != GPU_OK ||
      !readbackBuffer) {
    fprintf(stderr, "failed to create %s readback buffer\n", label);
    goto cleanup;
  }

  textureInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label = label;
  textureInfo.dimension = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format = GPU_FORMAT_BGRA8_UNORM;
  textureInfo.width = width;
  textureInfo.height = height;
  textureInfo.depthOrLayers = 1u;
  textureInfo.mipLevelCount = 1u;
  textureInfo.sampleCount = 1u;
  textureInfo.usage = GPU_TEXTURE_USAGE_COLOR_TARGET |
                      GPU_TEXTURE_USAGE_COPY_SRC;
  if (GPUCreateTexture(device, &textureInfo, &target) != GPU_OK || !target) {
    fprintf(stderr, "failed to create %s target\n", label);
    goto cleanup;
  }

  viewInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label = label;
  viewInfo.viewType = GPU_TEXTURE_VIEW_2D;
  viewInfo.format = GPU_FORMAT_BGRA8_UNORM;
  viewInfo.mipLevelCount = 1u;
  viewInfo.arrayLayerCount = 1u;
  if (GPUCreateTextureView(target, &viewInfo, &targetView) != GPU_OK ||
      !targetView) {
    fprintf(stderr, "failed to create %s target view\n", label);
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, label, &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "failed to acquire %s command buffer\n", label);
    goto cleanup;
  }

  color.view = targetView;
  color.loadOp = GPU_LOAD_OP_CLEAR;
  color.storeOp = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.0f;
  color.clearColor.float32[1] = 0.0f;
  color.clearColor.float32[2] = 0.0f;
  color.clearColor.float32[3] = 1.0f;
  rp.chain.sType = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rp.chain.structSize = sizeof(rp);
  rp.label = label;
  rp.colorAttachmentCount = 1u;
  rp.pColorAttachments = &color;

  renderPass = GPUBeginRenderPass(cmdb, &rp);
  if (!renderPass) {
    fprintf(stderr, "failed to begin %s pass\n", label);
    goto cleanup;
  }
  vertexBinding.buffer = vertexBuffer;
  GPUBindRenderPipeline(renderPass, pipeline);
  GPUBindVertexBuffers(renderPass, 0u, 1u, &vertexBinding);
  if (clipped) {
    dynamicState.chain.sType = GPU_STRUCTURE_TYPE_DYNAMIC_STATE_APPLY_INFO;
    dynamicState.chain.structSize = sizeof(dynamicState);
    dynamicState.mask = GPU_DYNAMIC_STATE_VIEWPORT_BIT |
                        GPU_DYNAMIC_STATE_SCISSOR_BIT;
    dynamicState.viewport.originX = 0.0;
    dynamicState.viewport.originY = 0.0;
    dynamicState.viewport.width = (double)width;
    dynamicState.viewport.height = (double)height;
    dynamicState.viewport.znear = 0.0;
    dynamicState.viewport.zfar = 1.0;
    dynamicState.scissor.x = 1u;
    dynamicState.scissor.y = 1u;
    dynamicState.scissor.width = 2u;
    dynamicState.scissor.height = 2u;
    GPUApplyDynamicState(renderPass, &dynamicState);
  }
  if (mode == RENDER_READBACK_DRAW_INDEXED) {
    GPUBindIndexBuffer(renderPass, indexBuffer, 0u, GPUIndexTypeUInt16);
    GPUDrawIndexed(renderPass, 3u, 1u, 0u, 0, 0u);
  } else if (mode == RENDER_READBACK_DRAW_INDIRECT) {
    GPUDrawIndirect(renderPass, argsBuffer, 0u);
  } else if (mode == RENDER_READBACK_DRAW_INDEXED_INDIRECT) {
    GPUBindIndexBuffer(renderPass, indexBuffer, 0u, GPUIndexTypeUInt16);
    GPUDrawIndexedIndirect(renderPass, argsBuffer, 0u);
  } else if (mode == RENDER_READBACK_DRAW_MULTI_INDIRECT) {
    GPUMultiDrawIndirect(renderPass,
                         argsBuffer,
                         0u,
                         2u,
                         (uint32_t)sizeof(RenderIndirectArgs));
  } else if (mode == RENDER_READBACK_DRAW_INDEXED_MULTI_INDIRECT) {
    GPUBindIndexBuffer(renderPass, indexBuffer, 0u, GPUIndexTypeUInt16);
    GPUMultiDrawIndexedIndirect(renderPass,
                                argsBuffer,
                                0u,
                                2u,
                                (uint32_t)sizeof(RenderIndexedIndirectArgs));
  } else {
    GPUDraw(renderPass, 3u, 1u, 0u, 0u);
  }
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  textureBarrier.texture = target;
  textureBarrier.srcAccess = GPU_ACCESS_COLOR_WRITE;
  textureBarrier.dstAccess = GPU_ACCESS_TRANSFER_READ;
  textureBarrier.mipCount = 1u;
  textureBarrier.layerCount = 1u;
  barrierBatch.srcStages = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 1u;
  barrierBatch.pTextureBarriers = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, label);
  if (!copyPass) {
    fprintf(stderr, "failed to begin %s copy pass\n", label);
    goto cleanup;
  }
  copyRegion.bytesPerRow = width * 4u;
  copyRegion.rowsPerImage = height;
  copyRegion.texture.width = width;
  copyRegion.texture.height = height;
  copyRegion.texture.depth = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass, target, readbackBuffer, &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "failed to create %s fence\n", label);
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
    fprintf(stderr, "%s submit failed\n", label);
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         readbackBuffer,
                         0u,
                         pixels,
                         sizeof(pixels)) != GPU_OK) {
    fprintf(stderr, "%s buffer read failed\n", label);
    goto cleanup;
  }

  if (clipped &&
      (pixels[0] > 2u || pixels[1] > 2u || pixels[2] > 2u ||
       pixels[3] < 250u)) {
    fprintf(stderr,
            "%s scissor pixel mismatch: %u %u %u %u\n",
            label,
            (unsigned)pixels[0],
            (unsigned)pixels[1],
            (unsigned)pixels[2],
            (unsigned)pixels[3]);
    goto cleanup;
  }

  if (multi) {
    if (!render_readback_has_red_in_x_range(pixels, width, height, 0u, 2u) ||
        !render_readback_has_red_in_x_range(pixels, width, height, 2u, 4u)) {
      fprintf(stderr, "%s multi draw did not touch both target halves\n", label);
      goto cleanup;
    }
  } else if (!render_readback_pixel_is_red(pixels, width, 2u, 2u)) {
    centerOffset = ((size_t)2u * width + 2u) * 4u;
    fprintf(stderr,
            "%s draw pixel mismatch: %u %u %u %u\n",
            label,
            (unsigned)pixels[centerOffset + 0u],
            (unsigned)pixels[centerOffset + 1u],
            (unsigned)pixels[centerOffset + 2u],
            (unsigned)pixels[centerOffset + 3u]);
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  if (renderPass) {
    GPUEndRenderPass(renderPass);
  }
  GPUDestroyFence(fence);
  GPUDestroyTextureView(targetView);
  GPUDestroyTexture(target);
  GPUDestroyBuffer(readbackBuffer);
  GPUDestroyBuffer(argsBuffer);
  GPUDestroyBuffer(indexBuffer);
  GPUDestroyBuffer(vertexBuffer);
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyShaderLibrary(library);
  return ok;
}

static int
check_render_readback(GPUDevice *device) {
  return check_render_readback_case(device, RENDER_READBACK_DRAW, 0) &&
         check_render_readback_case(device, RENDER_READBACK_DRAW_INDEXED, 1) &&
         check_render_readback_case(device, RENDER_READBACK_DRAW_INDIRECT, 0) &&
         check_render_readback_case(device,
                                    RENDER_READBACK_DRAW_INDEXED_INDIRECT,
                                    0) &&
         check_render_readback_case(device,
                                    RENDER_READBACK_DRAW_MULTI_INDIRECT,
                                    0) &&
         check_render_readback_case(device,
                                    RENDER_READBACK_DRAW_INDEXED_MULTI_INDIRECT,
                                    0);
}

static int
check_render_pass_validation(void) {
  GPUCommandBuffer fakeCmdb = {0};
  GPUTexture fakeColorTarget = {0};
  GPUTexture fakeColorTargetDepthFormat = {0};
  GPUTexture fakeColorTargetMSAA = {0};
  GPUTexture fakeDepthStencil = {0};
  GPUTexture fakeDepthStencilColorFormat = {0};
  GPUTexture fakeResolveTarget = {0};
  GPUTexture fakeResolveTargetRGBA = {0};
  GPUTexture fakeResolveTargetMSAA = {0};
  GPUTexture fakeSampledTexture = {0};
  GPUTextureView fakeColorView = {0};
  GPUTextureView fakeColorDepthFormatView = {0};
  GPUTextureView fakeColorMSAAView = {0};
  GPUTextureView fakeDepthStencilView = {0};
  GPUTextureView fakeDepthStencilColorFormatView = {0};
  GPUTextureView fakeResolveView = {0};
  GPUTextureView fakeResolveRGBAView = {0};
  GPUTextureView fakeResolveMSAAView = {0};
  GPUTextureView fakeSampledView = {0};
  GPUTextureView *fakeView;
  GPURenderPassColorAttachment colors[9] = {0};
  GPURenderPassDepthStencilAttachment depthStencil = {0};
  GPURenderPassCreateInfo rp = {0};

  fakeColorTarget.usage = GPU_TEXTURE_USAGE_COLOR_TARGET;
  fakeColorTarget.format = GPU_FORMAT_BGRA8_UNORM;
  fakeColorTarget.sampleCount = 1u;
  fakeColorTargetDepthFormat.usage = GPU_TEXTURE_USAGE_COLOR_TARGET;
  fakeColorTargetDepthFormat.format = GPU_FORMAT_DEPTH32_FLOAT;
  fakeColorTargetDepthFormat.sampleCount = 1u;
  fakeColorTargetMSAA.usage = GPU_TEXTURE_USAGE_COLOR_TARGET;
  fakeColorTargetMSAA.format = GPU_FORMAT_BGRA8_UNORM;
  fakeColorTargetMSAA.sampleCount = 4u;
  fakeDepthStencil.usage = GPU_TEXTURE_USAGE_DEPTH_STENCIL;
  fakeDepthStencil.format = GPU_FORMAT_DEPTH32_FLOAT;
  fakeDepthStencil.sampleCount = 1u;
  fakeDepthStencilColorFormat.usage = GPU_TEXTURE_USAGE_DEPTH_STENCIL;
  fakeDepthStencilColorFormat.format = GPU_FORMAT_BGRA8_UNORM;
  fakeDepthStencilColorFormat.sampleCount = 1u;
  fakeResolveTarget.usage = GPU_TEXTURE_USAGE_COLOR_TARGET;
  fakeResolveTarget.format = GPU_FORMAT_BGRA8_UNORM;
  fakeResolveTarget.sampleCount = 1u;
  fakeResolveTargetRGBA.usage = GPU_TEXTURE_USAGE_COLOR_TARGET;
  fakeResolveTargetRGBA.format = GPU_FORMAT_RGBA8_UNORM;
  fakeResolveTargetRGBA.sampleCount = 1u;
  fakeResolveTargetMSAA.usage = GPU_TEXTURE_USAGE_COLOR_TARGET;
  fakeResolveTargetMSAA.format = GPU_FORMAT_BGRA8_UNORM;
  fakeResolveTargetMSAA.sampleCount = 4u;
  fakeSampledTexture.usage = GPU_TEXTURE_USAGE_SAMPLED;
  fakeSampledTexture.format = GPU_FORMAT_BGRA8_UNORM;
  fakeSampledTexture.sampleCount = 1u;
  fakeColorView._texture = &fakeColorTarget;
  fakeColorView.format = fakeColorTarget.format;
  fakeColorDepthFormatView._texture = &fakeColorTargetDepthFormat;
  fakeColorDepthFormatView.format = fakeColorTargetDepthFormat.format;
  fakeColorMSAAView._texture = &fakeColorTargetMSAA;
  fakeColorMSAAView.format = fakeColorTargetMSAA.format;
  fakeDepthStencilView._texture = &fakeDepthStencil;
  fakeDepthStencilView.format = fakeDepthStencil.format;
  fakeDepthStencilColorFormatView._texture = &fakeDepthStencilColorFormat;
  fakeDepthStencilColorFormatView.format = fakeDepthStencilColorFormat.format;
  fakeResolveView._texture = &fakeResolveTarget;
  fakeResolveView.format = fakeResolveTarget.format;
  fakeResolveRGBAView._texture = &fakeResolveTargetRGBA;
  fakeResolveRGBAView.format = fakeResolveTargetRGBA.format;
  fakeResolveMSAAView._texture = &fakeResolveTargetMSAA;
  fakeResolveMSAAView.format = fakeResolveTargetMSAA.format;
  fakeSampledView._texture = &fakeSampledTexture;
  fakeSampledView.format = fakeSampledTexture.format;
  fakeView = &fakeColorView;

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
  colors[0].view = &fakeSampledView;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted color view without color target usage\n");
    return 0;
  }

  colors[0].view = &fakeColorDepthFormatView;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted color view with depth format\n");
    return 0;
  }

  colors[0].view = fakeView;
  colors[0].resolveView = &fakeSampledView;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted resolve view without color target usage\n");
    return 0;
  }
  colors[0].resolveView = &fakeColorDepthFormatView;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted resolve view with depth format\n");
    return 0;
  }
  colors[0].view = &fakeColorMSAAView;
  colors[0].resolveView = &fakeResolveRGBAView;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted resolve view with format mismatch\n");
    return 0;
  }
  colors[0].view = fakeView;
  colors[0].resolveView = &fakeResolveView;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted resolve view for single-sample color\n");
    return 0;
  }
  colors[0].view = &fakeColorMSAAView;
  colors[0].resolveView = &fakeResolveMSAAView;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted multisampled resolve target\n");
    return 0;
  }
  colors[0].view = fakeView;
  colors[0].resolveView = NULL;

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

  depthStencil.view = &fakeDepthStencilView;
  depthStencil.depthLoadOp = (GPULoadOp)99;
  depthStencil.depthStoreOp = GPU_STORE_OP_STORE;
  depthStencil.stencilLoadOp = GPU_LOAD_OP_DONT_CARE;
  depthStencil.stencilStoreOp = GPU_STORE_OP_DONT_CARE;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid depth load op\n");
    return 0;
  }

  depthStencil.depthLoadOp = GPU_LOAD_OP_CLEAR;
  depthStencil.depthStoreOp = (GPUStoreOp)99;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid depth store op\n");
    return 0;
  }

  depthStencil.depthStoreOp = GPU_STORE_OP_STORE;
  depthStencil.stencilLoadOp = (GPULoadOp)99;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid stencil load op\n");
    return 0;
  }

  depthStencil.stencilLoadOp = GPU_LOAD_OP_DONT_CARE;
  depthStencil.stencilStoreOp = (GPUStoreOp)99;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid stencil store op\n");
    return 0;
  }

  depthStencil.stencilStoreOp = GPU_STORE_OP_DONT_CARE;
  depthStencil.view = &fakeSampledView;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted depth-stencil view without depth-stencil usage\n");
    return 0;
  }

  depthStencil.view = &fakeDepthStencilColorFormatView;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted depth-stencil view with color format\n");
    return 0;
  }

  depthStencil.view = &fakeDepthStencilView;
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
check_render_draw_validation_calls(GPUDevice *device) {
  GPUApi *api;
  void (*oldDraw)(GPURenderCommandEncoder *,
                  GPUPrimitiveType,
                  size_t,
                  size_t,
                  uint32_t,
                  uint32_t);
  void (*oldDrawIndexed)(GPURenderCommandEncoder *,
                         uint32_t,
                         uint32_t,
                         uint32_t,
                         int32_t,
                         uint32_t);
  void (*oldDrawIndirect)(GPURenderCommandEncoder *,
                          GPUPrimitiveType,
                          GPUBuffer *,
                          uint64_t);
  void (*oldDrawIndexedIndirect)(GPURenderCommandEncoder *,
                                 GPUBuffer *,
                                 uint64_t);
  GPUBindGroupLayoutEntry entry = {0};
  GPUBindGroupLayoutCreateInfo layoutInfo = {0};
  GPUBindGroupLayout *layout = NULL;
  GPUBindGroupLayout *layouts[1];
  GPUPipelineLayoutCreateInfo pipelineInfo = {0};
  GPUPipelineLayout *pipelineLayout = NULL;
  GPURenderPassEncoder pass = {0};
  GPUBuffer indirectBuffer = {0};
  GPUBuffer wrongUsageBuffer = {0};
  int ok = 0;

  api = gpuActiveGPUApi();
  if (!api) {
    fprintf(stderr, "render draw validation could not get active api\n");
    return 0;
  }

  entry.binding = 0u;
  entry.bindingType = GPU_BINDING_UNIFORM_BUFFER;
  entry.visibility = GPU_SHADER_STAGE_VERTEX_BIT | GPU_SHADER_STAGE_FRAGMENT_BIT;
  entry.arrayCount = 1u;
  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label = "draw-validation-layout";
  layoutInfo.entryCount = 1u;
  layoutInfo.pEntries = &entry;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK || !layout) {
    fprintf(stderr, "render draw validation layout setup failed\n");
    return 0;
  }

  layouts[0] = layout;
  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.bindGroupLayoutCount = 1u;
  pipelineInfo.ppBindGroupLayouts = layouts;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "render draw validation pipeline layout setup failed\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  oldDraw = api->rce.drawPrimitives;
  oldDrawIndexed = api->rce.drawIndexedPrims;
  oldDrawIndirect = api->rce.drawPrimitivesIndirect;
  oldDrawIndexedIndirect = api->rce.drawIndexedPrimsIndirect;
  api->rce.drawPrimitives = count_draw_primitives;
  api->rce.drawIndexedPrims = count_draw_indexed;
  api->rce.drawPrimitivesIndirect = count_draw_indirect;
  api->rce.drawIndexedPrimsIndirect = count_draw_indexed_indirect;

  gRenderDrawCalls = 0u;
  gRenderDrawIndexedCalls = 0u;
  gRenderDrawIndirectCalls = 0u;
  gRenderDrawIndexedIndirectCalls = 0u;

  indirectBuffer.sizeBytes = 128u;
  indirectBuffer.usage = GPU_BUFFER_USAGE_INDIRECT | GPU_BUFFER_USAGE_INDEX;
  wrongUsageBuffer.sizeBytes = 128u;
  wrongUsageBuffer.usage = GPU_BUFFER_USAGE_VERTEX;

  pass._hasPipeline = true;
  pass._pipelineLayout = pipelineLayout;
  pass._primitiveType = GPUPrimitiveTypeTriangle;

  pass._requiredBindGroupMask = 0u;
  GPUDraw(&pass, 3u, 1u, 0u, 0u);
  GPUDrawIndirect(&pass, &indirectBuffer, 0u);
  if (gRenderDrawCalls != 1u || gRenderDrawIndirectCalls != 1u) {
    fprintf(stderr, "render draw validation rejected no-bind pipeline\n");
    goto cleanup;
  }
  gRenderDrawCalls = 0u;
  gRenderDrawIndirectCalls = 0u;

  pass._requiredBindGroupMask = 1u;
  GPUDraw(&pass, 3u, 1u, 0u, 0u);
  GPUDrawIndexed(&pass, 3u, 1u, 0u, 0, 0u);
  GPUDrawIndirect(&pass, &indirectBuffer, 0u);
  GPUDrawIndexedIndirect(&pass, &indirectBuffer, 0u);
  if (gRenderDrawCalls != 0u ||
      gRenderDrawIndexedCalls != 0u ||
      gRenderDrawIndirectCalls != 0u ||
      gRenderDrawIndexedIndirectCalls != 0u) {
    fprintf(stderr, "render draw validation called backend without required bind group\n");
    goto cleanup;
  }

  pass._boundGroupLayouts[0] = layout;
  GPUDraw(&pass, 0u, 1u, 0u, 0u);
  GPUDraw(&pass, 3u, 0u, 0u, 0u);
  GPUDrawIndirect(&pass, &wrongUsageBuffer, 0u);
  GPUDrawIndirect(&pass, &indirectBuffer, 2u);
  GPUDrawIndirect(&pass, &indirectBuffer, 120u);
  GPUMultiDrawIndirect(&pass, &indirectBuffer, 2u, 2u, 16u);
  GPUMultiDrawIndirect(&pass, &indirectBuffer, 0u, 2u, 12u);
  GPUMultiDrawIndirect(&pass, &indirectBuffer, 0u, 2u, 18u);
  if (gRenderDrawCalls != 0u || gRenderDrawIndirectCalls != 0u) {
    fprintf(stderr, "render draw validation called backend for invalid non-indexed draw\n");
    goto cleanup;
  }

  GPUDraw(&pass, 3u, 1u, 0u, 0u);
  GPUDrawIndirect(&pass, &indirectBuffer, 0u);
  if (gRenderDrawCalls != 1u || gRenderDrawIndirectCalls != 1u) {
    fprintf(stderr, "render draw validation rejected valid non-indexed draw\n");
    goto cleanup;
  }

  GPUDrawIndexed(&pass, 0u, 1u, 0u, 0, 0u);
  GPUDrawIndexed(&pass, 3u, 0u, 0u, 0, 0u);
  GPUDrawIndexed(&pass, 128u, 1u, 0u, 0, 0u);
  GPUDrawIndexedIndirect(&pass, &wrongUsageBuffer, 0u);
  GPUDrawIndexedIndirect(&pass, &indirectBuffer, 120u);
  if (gRenderDrawIndexedCalls != 0u ||
      gRenderDrawIndexedIndirectCalls != 0u) {
    fprintf(stderr, "render draw validation called backend for invalid indexed draw\n");
    goto cleanup;
  }

  GPUBindIndexBuffer(&pass, &indirectBuffer, 0u, GPUIndexTypeUInt16);
  GPUDrawIndexedIndirect(&pass, &indirectBuffer, 2u);
  GPUMultiDrawIndexedIndirect(&pass, &indirectBuffer, 2u, 2u, 20u);
  GPUMultiDrawIndexedIndirect(&pass, &indirectBuffer, 0u, 2u, 16u);
  GPUMultiDrawIndexedIndirect(&pass, &indirectBuffer, 0u, 2u, 22u);
  if (gRenderDrawIndexedIndirectCalls != 0u) {
    fprintf(stderr, "render draw validation accepted invalid indexed indirect layout\n");
    goto cleanup;
  }
  GPUDrawIndexed(&pass, 3u, 1u, 0u, 0, 0u);
  GPUDrawIndexedIndirect(&pass, &indirectBuffer, 0u);
  if (gRenderDrawIndexedCalls != 1u ||
      gRenderDrawIndexedIndirectCalls != 1u) {
    fprintf(stderr, "render draw validation rejected valid indexed draw\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  api->rce.drawPrimitives = oldDraw;
  api->rce.drawIndexedPrims = oldDrawIndexed;
  api->rce.drawPrimitivesIndirect = oldDrawIndirect;
  api->rce.drawIndexedPrimsIndirect = oldDrawIndexedIndirect;
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(layout);
  return ok;
}

static int
check_dynamic_state_validation_calls(void) {
  GPUApi *api;
  void (*oldViewport)(GPURenderCommandEncoder *, const GPUViewport *);
  void (*oldScissor)(GPURenderCommandEncoder *, const GPUScissorRect *);
  void (*oldBlend)(GPURenderCommandEncoder *, const float[4]);
  void (*oldStencil)(GPURenderCommandEncoder *, uint32_t);
  GPURenderPassEncoder pass = {0};
  GPURenderPassEncoder endedPass = {0};
  GPUDynamicStateApplyInfo info = {0};
  int ok = 0;

  api = gpuActiveGPUApi();
  if (!api) {
    fprintf(stderr, "dynamic state validation could not get active api\n");
    return 0;
  }

  oldViewport = api->rce.viewport;
  oldScissor = api->rce.scissor;
  oldBlend = api->rce.blendConstant;
  oldStencil = api->rce.stencilReference;
  api->rce.viewport = count_viewport;
  api->rce.scissor = count_scissor;
  api->rce.blendConstant = count_blend_constant;
  api->rce.stencilReference = count_stencil_reference;

  gRenderViewportCalls = 0u;
  gRenderScissorCalls = 0u;
  gRenderBlendCalls = 0u;
  gRenderStencilCalls = 0u;

  GPUApplyDynamicState(NULL, &info);
  GPUApplyDynamicState(&pass, NULL);

  info.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  info.chain.structSize = sizeof(info);
  info.mask = GPU_DYNAMIC_STATE_VIEWPORT_BIT |
              GPU_DYNAMIC_STATE_SCISSOR_BIT |
              GPU_DYNAMIC_STATE_BLEND_CONSTANT_BIT |
              GPU_DYNAMIC_STATE_STENCIL_REFERENCE_BIT;
  GPUApplyDynamicState(&pass, &info);

  info.chain.sType = GPU_STRUCTURE_TYPE_DYNAMIC_STATE_APPLY_INFO;
  info.chain.structSize = (uint32_t)(sizeof(info) - 1u);
  GPUApplyDynamicState(&pass, &info);

  endedPass._ended = true;
  info.chain.structSize = sizeof(info);
  GPUApplyDynamicState(&endedPass, &info);

  if (gRenderViewportCalls != 0u ||
      gRenderScissorCalls != 0u ||
      gRenderBlendCalls != 0u ||
      gRenderStencilCalls != 0u) {
    fprintf(stderr, "dynamic state validation called backend for invalid input\n");
    goto cleanup;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_DYNAMIC_STATE_APPLY_INFO;
  info.chain.structSize = sizeof(info);
  info.mask = 0u;
  GPUApplyDynamicState(&pass, &info);
  if (gRenderViewportCalls != 0u ||
      gRenderScissorCalls != 0u ||
      gRenderBlendCalls != 0u ||
      gRenderStencilCalls != 0u) {
    fprintf(stderr, "dynamic state validation called backend for empty mask\n");
    goto cleanup;
  }

  info.mask = GPU_DYNAMIC_STATE_VIEWPORT_BIT |
              GPU_DYNAMIC_STATE_SCISSOR_BIT |
              GPU_DYNAMIC_STATE_BLEND_CONSTANT_BIT |
              GPU_DYNAMIC_STATE_STENCIL_REFERENCE_BIT;
  GPUApplyDynamicState(&pass, &info);
  if (gRenderViewportCalls != 1u ||
      gRenderScissorCalls != 1u ||
      gRenderBlendCalls != 1u ||
      gRenderStencilCalls != 1u) {
    fprintf(stderr, "dynamic state validation did not apply selected state\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  api->rce.viewport = oldViewport;
  api->rce.scissor = oldScissor;
  api->rce.blendConstant = oldBlend;
  api->rce.stencilReference = oldStencil;
  return ok;
}

static int
check_render_encoder_validation(void) {
  GPURenderPassEncoder pass = {0};
  GPURenderPassEncoder endedPass = {0};
  GPURenderPipeline fakePipeline = {0};
  GPUBuffer fakeBufferStorage = {0};
  GPUBuffer *fakeBuffer = &fakeBufferStorage;
  GPUBufferBinding binding = {0};
  GPUDynamicStateApplyInfo dynamicState = {0};
  uint32_t pushValue = 0xaabbccddu;
  uint8_t pushBefore[16];

  fakeBufferStorage.sizeBytes = 128u;
  fakeBufferStorage.usage = GPU_BUFFER_USAGE_VERTEX |
                            GPU_BUFFER_USAGE_INDEX |
                            GPU_BUFFER_USAGE_INDIRECT;

  GPUBindRenderPipeline(NULL, NULL);
  GPUBindVertexBuffers(NULL, 0u, 0u, NULL);
  GPUBindIndexBuffer(NULL, fakeBuffer, 0u, GPUIndexTypeUInt16);
  GPUDraw(NULL, 1u, 1u, 0u, 0u);
  GPUDrawIndexed(NULL, 1u, 1u, 0u, 0, 0u);
  GPUDrawIndirect(NULL, fakeBuffer, 0u);
  GPUDrawIndexedIndirect(NULL, fakeBuffer, 0u);
  GPUMultiDrawIndirect(NULL, fakeBuffer, 0u, 1u, 16u);
  GPUMultiDrawIndexedIndirect(NULL, fakeBuffer, 0u, 1u, 20u);
  GPUApplyDynamicState(NULL, &dynamicState);
  GPUApplyDynamicState(&pass, NULL);
  GPUSetRenderPushConstants(NULL, 0u, sizeof(pushValue), &pushValue);
  GPUDraw(&pass, 1u, 1u, 0u, 0u);
  GPUDrawIndirect(&pass, fakeBuffer, 0u);
  GPUMultiDrawIndirect(&pass, fakeBuffer, 0u, 2u, 16u);

  fakePipeline._state = &fakePipeline;
  fakePipeline._colorTargetCount = 1u;
  fakePipeline._colorTargetFormats[0] = GPU_FORMAT_BGRA8_UNORM;
  fakePipeline._sampleCount = 1u;
  fakePipeline._depthStencilFormat = GPU_FORMAT_UNDEFINED;

  pass._colorAttachmentCount = 1u;
  pass._colorAttachmentFormats[0] = GPU_FORMAT_RGBA8_UNORM;
  pass._colorAttachmentSampleCounts[0] = 1u;
  pass._depthStencilFormat = GPU_FORMAT_UNDEFINED;
  GPUBindRenderPipeline(&pass, &fakePipeline);
  if (pass._hasPipeline) {
    fprintf(stderr, "render encoder accepted pipeline with color format mismatch\n");
    return 0;
  }

  pass._colorAttachmentFormats[0] = GPU_FORMAT_BGRA8_UNORM;
  pass._colorAttachmentSampleCounts[0] = 4u;
  GPUBindRenderPipeline(&pass, &fakePipeline);
  if (pass._hasPipeline) {
    fprintf(stderr, "render encoder accepted pipeline with sample count mismatch\n");
    return 0;
  }

  pass._colorAttachmentSampleCounts[0] = 1u;
  pass._depthStencilFormat = GPU_FORMAT_DEPTH32_FLOAT;
  pass._depthStencilSampleCount = 1u;
  GPUBindRenderPipeline(&pass, &fakePipeline);
  if (pass._hasPipeline) {
    fprintf(stderr, "render encoder accepted pipeline with depth format mismatch\n");
    return 0;
  }

  pass._depthStencilFormat = GPU_FORMAT_UNDEFINED;
  pass._depthStencilSampleCount = 0u;
  pass._colorAttachmentHasResolve[0] = true;
  GPUBindRenderPipeline(&pass, &fakePipeline);
  if (pass._hasPipeline) {
    fprintf(stderr, "render encoder accepted single-sample pipeline with resolve target\n");
    return 0;
  }
  pass._colorAttachmentHasResolve[0] = false;

  pass._hasPipeline = true;
  GPUDrawIndirect(&pass, NULL, 0u);
  GPUDrawIndexedIndirect(&pass, fakeBuffer, 0u);
  GPUMultiDrawIndirect(&pass, NULL, 0u, 2u, 16u);
  GPUMultiDrawIndirect(&pass, fakeBuffer, 0u, 0u, 16u);
  GPUMultiDrawIndirect(&pass, fakeBuffer, 0u, 2u, 0u);
  GPUMultiDrawIndirect(&pass, fakeBuffer, UINT64_MAX, 2u, 16u);
  GPUMultiDrawIndexedIndirect(&pass, fakeBuffer, 0u, 2u, 20u);
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
  GPUMultiDrawIndirect(&pass, fakeBuffer, 0u, 2u, 16u);
  GPUMultiDrawIndexedIndirect(&pass, fakeBuffer, 0u, 2u, 20u);

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
  GPUMultiDrawIndirect(&endedPass, fakeBuffer, 0u, 2u, 16u);
  GPUMultiDrawIndexedIndirect(&endedPass, fakeBuffer, 0u, 2u, 20u);
  GPUApplyDynamicState(&endedPass, &dynamicState);
  GPUEndRenderPass(&endedPass);

  return 1;
}

int
gpu_test_render(GPUDevice *device) {
  return check_render_pass_validation() &&
         check_render_encoder_validation() &&
         check_render_pipeline_validation(device) &&
         check_render_draw_validation_calls(device) &&
         check_dynamic_state_validation_calls() &&
         check_render_readback(device);
}
