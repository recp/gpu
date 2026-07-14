#include "test.h"
#include "../../src/api/buffer_internal.h"
#include "../../src/api/cmdqueue_internal.h"
#include "../../src/api/device_internal.h"
#include "../../src/api/render/pipeline_internal.h"
#include "../../src/api/texture_internal.h"

static int
create_render_usl_library(GPUDevice *device,
                          const char *bytecodePath,
                          GPUShaderLibrary **outLibrary) {
  uint64_t bytecodeSize;
  void *bytecode;
  GPUResult result;

  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  if (!bytecode) {
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(device,
                                         bytecode,
                                         bytecodeSize,
                                         outLibrary);
  free(bytecode);
  return result == GPU_OK && *outLibrary;
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
static uint32_t gRenderMultiDrawIndirectCalls;
static uint32_t gRenderMultiDrawIndexedIndirectCalls;

static int
check_pipeline_disk_cache(GPUDevice                  *device,
                          GPURenderPipelineCreateInfo *info) {
  GPUPipelineCacheCreateInfo cacheInfo = {0};
  GPUPipelineCache          *cache;
  GPURenderPipeline         *pipeline;
  GPUApi                    *api;
  GPUResult                  result;
  char                       path[160];
  char                       temporaryPath[168];
  FILE                      *file;
  long                       fileSize;
  int                        ok;

  api = gpuDeviceApi(device);
  if (!api) {
    return 0;
  }

  cacheInfo.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  cacheInfo.chain.structSize = sizeof(cacheInfo);
  cacheInfo.label            = "api-render-disk-cache";
  cacheInfo.enableDiskCache  = true;
  if (GPUCreatePipelineCache(device, &cacheInfo, &cache) !=
        GPU_ERROR_INVALID_ARGUMENT || cache != NULL) {
    fprintf(stderr, "pipeline disk cache accepted null path\n");
    return 0;
  }

  snprintf(path,
           sizeof(path),
           ".gpu-api-metal-cache-%p.bin",
           (void *)device);
  snprintf(temporaryPath, sizeof(temporaryPath), "%s.tmp", path);
  remove(path);
  remove(temporaryPath);
  cacheInfo.cachePath = path;
  cache               = NULL;
  result              = GPUCreatePipelineCache(device, &cacheInfo, &cache);
  if (api->backend != GPU_BACKEND_METAL) {
    if (result != GPU_ERROR_UNSUPPORTED || cache != NULL) {
      fprintf(stderr, "pipeline disk cache accepted unsupported backend\n");
      GPUDestroyPipelineCache(cache);
      return 0;
    }
    return 1;
  }
  if (result != GPU_OK || !cache) {
    fprintf(stderr, "Metal pipeline disk cache create failed\n");
    return 0;
  }

  ok          = 0;
  pipeline    = NULL;
  info->cache = cache;
  if (GPUCreateRenderPipeline(device, info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "Metal cached render pipeline create failed\n");
    goto cleanup;
  }
  GPUDestroyRenderPipeline(pipeline);
  pipeline = NULL;
  GPUDestroyPipelineCache(cache);
  cache       = NULL;
  info->cache = NULL;

  file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "Metal pipeline cache file was not written\n");
    goto cleanup;
  }
  fseek(file, 0, SEEK_END);
  fileSize = ftell(file);
  fclose(file);
  if (fileSize <= 0) {
    fprintf(stderr, "Metal pipeline cache file is empty\n");
    goto cleanup;
  }

  if (GPUCreatePipelineCache(device, &cacheInfo, &cache) != GPU_OK || !cache) {
    fprintf(stderr, "Metal pipeline disk cache reopen failed\n");
    goto cleanup;
  }
  info->cache = cache;
  if (GPUCreateRenderPipeline(device, info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "Metal pipeline create from reopened cache failed\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  GPUDestroyRenderPipeline(pipeline);
  info->cache = NULL;
  GPUDestroyPipelineCache(cache);
  remove(path);
  remove(temporaryPath);
  return ok;
}
static uint32_t gRenderVertexBufferCalls;
static uint32_t gRenderPushConstantCalls;
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

static bool
count_multi_draw_indirect(GPURenderCommandEncoder *rce,
                          GPUPrimitiveType type,
                          GPUBuffer *argsBuffer,
                          uint64_t argsOffset,
                          uint32_t drawCount,
                          uint32_t strideBytes) {
  (void)rce;
  (void)type;
  (void)argsBuffer;
  (void)argsOffset;
  (void)drawCount;
  (void)strideBytes;
  gRenderMultiDrawIndirectCalls++;
  return true;
}

static bool
count_multi_draw_indexed_indirect(GPURenderCommandEncoder *rce,
                                  GPUBuffer *argsBuffer,
                                  uint64_t argsOffset,
                                  uint32_t drawCount,
                                  uint32_t strideBytes) {
  (void)rce;
  (void)argsBuffer;
  (void)argsOffset;
  (void)drawCount;
  (void)strideBytes;
  gRenderMultiDrawIndexedIndirectCalls++;
  return true;
}

static void
count_vertex_buffer(GPURenderCommandEncoder *rce,
                    GPUBuffer               *buffer,
                    uint64_t                 offset,
                    uint32_t                 index) {
  (void)rce;
  (void)buffer;
  (void)offset;
  (void)index;
  gRenderVertexBufferCalls++;
}

static void
count_render_push_constants(GPURenderCommandEncoder *rce,
                            GPUShaderStageFlags       stages,
                            const void               *data,
                            uint32_t                  sizeBytes) {
  (void)rce;
  (void)stages;
  (void)data;
  (void)sizeBytes;
  gRenderPushConstantCalls++;
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
check_wide_pipeline_cache_key(GPUDevice                   *device,
                              GPURenderPipelineCreateInfo *info) {
  const GPUColorTargetState *originalTargets;
  GPURenderPipeline         *pipeline;
  GPUColorTargetState        targets[4];
  GPUCacheStats              stats;
  uint32_t                   originalTargetCount;
  int                        ok;

  originalTargets     = info->pColorTargets;
  originalTargetCount = info->colorTargetCount;
  pipeline            = NULL;
  ok                  = 0;
  for (uint32_t i = 0u; i < (uint32_t)GPU_ARRAY_LEN(targets); i++) {
    targets[i] = originalTargets[0];
  }

  info->colorTargetCount = (uint32_t)GPU_ARRAY_LEN(targets);
  info->pColorTargets    = targets;
  GPUResetStats(device);
  if (GPUCreateRenderPipeline(device, info, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "wide render pipeline cache miss failed\n");
    goto cleanup;
  }
  GPUDestroyRenderPipeline(pipeline);
  pipeline = NULL;
  if (GPUCreateRenderPipeline(device, info, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "wide render pipeline cache hit failed\n");
    goto cleanup;
  }
  if (GPUGetCacheStats(device, &stats) != GPU_OK ||
      stats.pipelineCompiles != 1u ||
      stats.pipelineMisses != 1u ||
      stats.pipelineHits != 1u) {
    fprintf(stderr, "wide render pipeline cache stats mismatch\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  GPUDestroyRenderPipeline(pipeline);
  info->colorTargetCount = originalTargetCount;
  info->pColorTargets    = originalTargets;
  return ok;
}

static int
check_pipeline_cache_validation(GPUDevice *device,
                                GPURenderPipelineCreateInfo *info) {
  GPUPipelineCacheCreateInfo cacheInfo = {0};
  GPUPipelineCache *cache = NULL;
  GPURenderPipeline *pipeline = NULL;
  GPURenderPipeline *asyncPipeline = NULL;
  GPUPipelineCompileHandle handle = {0};
  GPUPipelineCompileStatus status = GPU_PIPELINE_COMPILE_PENDING;
  GPUCacheStats stats;
  GPUCullMode originalCull;
  GPUCullMode alternateCull;

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
  if (!check_pipeline_disk_cache(device, info)) {
    return 0;
  }
  GPUResetStats(device);
  cacheInfo.enableDiskCache = false;
  cacheInfo.maxEntries      = 1u;
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

  if (GPUCreateRenderPipeline(device, info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "render pipeline cache hit failed\n");
    goto fail;
  }
  GPUDestroyRenderPipeline(pipeline);
  pipeline = NULL;

  if (GPUPrewarmRenderPipelines(device, cache, 1u, info) != GPU_OK) {
    fprintf(stderr, "render pipeline prewarm failed\n");
    goto fail;
  }

  originalCull  = info->cullMode;
  alternateCull = originalCull == GPU_CULL_MODE_BACK
                    ? GPU_CULL_MODE_FRONT
                    : GPU_CULL_MODE_BACK;
  info->cullMode = alternateCull;
  if (GPUCreateRenderPipeline(device, info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "render pipeline cache key change failed\n");
    goto fail;
  }
  GPUDestroyRenderPipeline(pipeline);
  pipeline = NULL;

  info->cullMode = originalCull;
  if (GPUCreateRenderPipeline(device, info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "render pipeline cache eviction failed\n");
    goto fail;
  }
  GPUDestroyRenderPipeline(pipeline);
  pipeline = NULL;

  if (GPUGetCacheStats(device, &stats) != GPU_OK ||
      stats.pipelineCompiles != 3u ||
      stats.pipelineMisses != 3u ||
      stats.pipelineHits != 2u) {
    fprintf(stderr, "pipeline cache stats did not record hits and eviction\n");
    goto fail;
  }
  if (!check_wide_pipeline_cache_key(device, info)) {
    goto fail;
  }

  info->cache = NULL;
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
  if (GPUCompileRenderPipelineAsync(device, cache, info, &handle) != GPU_OK ||
      handle.id == 0u) {
    fprintf(stderr, "async pipeline compile enqueue failed\n");
    goto fail;
  }
  for (uint32_t i = 0u; i < 1000000u; i++) {
    if (GPUPollRenderPipelineCompile(device,
                                     handle,
                                     &status,
                                     &asyncPipeline) != GPU_OK) {
      fprintf(stderr, "async pipeline poll failed\n");
      goto fail;
    }
    if (status != GPU_PIPELINE_COMPILE_PENDING) {
      break;
    }
  }
  if (status != GPU_PIPELINE_COMPILE_READY || !asyncPipeline) {
    fprintf(stderr, "async pipeline compile did not become ready\n");
    goto fail;
  }
  GPUDestroyRenderPipeline(asyncPipeline);
  asyncPipeline = NULL;
  if (GPUPollRenderPipelineCompile(device,
                                   handle,
                                   &status,
                                   &asyncPipeline) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "async pipeline handle was not consumed\n");
    goto fail;
  }

  GPUDestroyPipelineCache(cache);
  return 1;

fail:
  info->cache = NULL;
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyRenderPipeline(asyncPipeline);
  GPUDestroyPipelineCache(cache);
  return 0;
}

static int
check_render_pipeline_validation(GPUDevice *device,
                                 const char *bytecodePath) {
  static const GPUPrimitiveTopology topologies[] = {
    GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    GPU_PRIMITIVE_TOPOLOGY_LINE_LIST
  };
  GPUShaderLibrary *library = NULL;
  GPUPipelineLayoutCreateInfo layoutInfo = {0};
  GPUPipelineLayout *pipelineLayout = NULL;
  GPUColorTargetState colorTargets[9] = {0};
  GPUVertexAttribute attr = {0};
  GPUVertexBufferLayout vertexLayout = {0};
  GPURenderPipelineCreateInfo info = {0};
  GPUDepthStencilState depthStencil = {0};
  GPURenderPipeline *pipeline;

  if (!create_render_usl_library(device, bytecodePath, &library)) {
    fprintf(stderr, "failed to create render pipeline test library\n");
    return 0;
  }

  colorTargets[0].format = GPU_FORMAT_BGRA8_UNORM;
  attr.shaderLocation = 0u;
  attr.format = GPU_VERTEX_FORMAT_FLOAT32X2;
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
  info.pColorTargets     = NULL;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted no attachments")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  depthStencil.depthTestEnable  = true;
  depthStencil.depthWriteEnable = true;
  depthStencil.depthCompare     = GPU_COMPARE_LESS;
  info.depthStencilFormat       = GPU_FORMAT_DEPTH32_FLOAT;
  info.pDepthStencilState       = &depthStencil;
  pipeline                      = NULL;
  if (GPUCreateRenderPipeline(device, &info, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "render pipeline create rejected depth-only target\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }
  GPUDestroyRenderPipeline(pipeline);

  memset(&depthStencil, 0, sizeof(depthStencil));
  info.depthStencilFormat = GPU_FORMAT_UNDEFINED;
  info.pDepthStencilState = NULL;
  info.pColorTargets      = colorTargets;
  info.colorTargetCount   = (uint32_t)GPU_ARRAY_LEN(colorTargets);
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
  attr.format = GPU_VERTEX_FORMAT_UNDEFINED;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid vertex format")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  attr.format = GPU_VERTEX_FORMAT_FLOAT32X2;
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

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(topologies); i++) {
    info.primitiveTopology = topologies[i];
    pipeline               = NULL;
    if (GPUCreateRenderPipeline(device, &info, &pipeline) != GPU_OK ||
        !pipeline) {
      fprintf(stderr,
              "render pipeline create rejected valid topology %u\n",
              (unsigned)topologies[i]);
      GPUDestroyPipelineLayout(pipelineLayout);
      GPUDestroyShaderLibrary(library);
      return 0;
    }
    GPUDestroyRenderPipeline(pipeline);
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
  colorTargets[0].blend.enabled         = true;
  colorTargets[0].blend.color.srcFactor = GPU_BLEND_FACTOR_SRC_ALPHA;
  colorTargets[0].blend.color.dstFactor =
    GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorTargets[0].blend.color.op         = GPU_BLEND_OP_ADD;
  colorTargets[0].blend.alpha.srcFactor = GPU_BLEND_FACTOR_ONE;
  colorTargets[0].blend.alpha.dstFactor = GPU_BLEND_FACTOR_ZERO;
  colorTargets[0].blend.alpha.op         = GPU_BLEND_OP_ADD;
  colorTargets[0].blend.writeMask        = GPU_COLOR_WRITE_ALL;
  pipeline = NULL;
  if (GPUCreateRenderPipeline(device, &info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "render pipeline create rejected valid blend state\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }
  GPUDestroyRenderPipeline(pipeline);

  colorTargets[0].blend.color.srcFactor = (GPUBlendFactor)99;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid blend factor")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }
  colorTargets[0].blend.color.srcFactor = GPU_BLEND_FACTOR_SRC_ALPHA;
  colorTargets[0].blend.alpha.op = (GPUBlendOp)99;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid blend op")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }
  colorTargets[0].blend.alpha.op  = GPU_BLEND_OP_ADD;
  colorTargets[0].blend.writeMask = GPU_COLOR_WRITE_ALL | (1u << 8);
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted invalid color write mask")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }
  colorTargets[0].blend.writeMask =
    GPU_COLOR_WRITE_NONE | GPU_COLOR_WRITE_R;
  if (!expect_render_pipeline_error(
        device,
        &info,
        "render pipeline create accepted mixed color write none mask")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  memset(&colorTargets[0].blend, 0, sizeof(colorTargets[0].blend));
  depthStencil.depthTestEnable = true;
  info.pDepthStencilState = &depthStencil;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline create accepted depth state without format")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.depthStencilFormat = GPU_FORMAT_DEPTH32_FLOAT;
  depthStencil.depthWriteEnable = true;
  depthStencil.depthCompare     = GPU_COMPARE_LESS;
  pipeline = NULL;
  if (GPUCreateRenderPipeline(device, &info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "render pipeline create rejected valid depth state\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }
  GPUDestroyRenderPipeline(pipeline);

  depthStencil.stencilTestEnable = true;
  if (!expect_render_pipeline_error(device,
                                    &info,
                                    "render pipeline accepted stencil state without stencil format")) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.depthStencilFormat          = GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
  depthStencil.front.compare       = GPU_COMPARE_ALWAYS;
  depthStencil.front.failOp        = GPU_STENCIL_OP_REPLACE;
  depthStencil.front.depthFailOp   = GPU_STENCIL_OP_INCREMENT_WRAP;
  depthStencil.front.passOp        = GPU_STENCIL_OP_KEEP;
  depthStencil.back.compare        = GPU_COMPARE_ALWAYS;
  depthStencil.back.failOp         = GPU_STENCIL_OP_ZERO;
  depthStencil.back.depthFailOp    = GPU_STENCIL_OP_DECREMENT_WRAP;
  depthStencil.back.passOp         = GPU_STENCIL_OP_INVERT;
  depthStencil.stencilReadMask     = UINT8_MAX;
  depthStencil.stencilWriteMask    = UINT8_MAX;
  pipeline = NULL;
  if (GPUCreateRenderPipeline(device, &info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "render pipeline create rejected valid stencil state\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }
  GPUDestroyRenderPipeline(pipeline);

  info.pDepthStencilState = NULL;
  info.depthStencilFormat = GPU_FORMAT_UNDEFINED;
  info.multisample.sampleCount           = 4u;
  info.multisample.alphaToCoverageEnable = true;
  pipeline = NULL;
  if (GPUCreateRenderPipeline(device, &info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "render pipeline create rejected alpha-to-coverage\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }
  GPUDestroyRenderPipeline(pipeline);

  info.multisample.sampleCount           = 1u;
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
  RENDER_READBACK_DRAW_INDEXED_MULTI_INDIRECT,
  RENDER_READBACK_DRAW_OCCLUSION,
  RENDER_READBACK_DRAW_MSAA,
  RENDER_READBACK_DRAW_MRT,
  RENDER_READBACK_DRAW_COLOR_WRITE_NONE
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
    case RENDER_READBACK_DRAW_OCCLUSION:
      return "api-render-readback-occlusion";
    case RENDER_READBACK_DRAW_MSAA:
      return "api-render-readback-msaa";
    case RENDER_READBACK_DRAW_MRT:
      return "api-render-readback-mrt";
    case RENDER_READBACK_DRAW_COLOR_WRITE_NONE:
      return "api-render-readback-color-write-none";
    case RENDER_READBACK_DRAW:
    default:
      return "api-render-readback";
  }
}

static int
render_readback_pixel_is_red(const uint8_t *pixels,
                             uint32_t rowPitch,
                             uint32_t x,
                             uint32_t y) {
  size_t offset = (size_t)y * rowPitch + (size_t)x * 4u;

  return pixels[offset + 0u] <= 2u &&
         pixels[offset + 1u] <= 2u &&
         pixels[offset + 2u] >= 250u &&
         pixels[offset + 3u] >= 250u;
}

static int
render_readback_pixel_is_half_red(const uint8_t *pixels,
                                  uint32_t       rowPitch,
                                  uint32_t       x,
                                  uint32_t       y) {
  size_t offset = (size_t)y * rowPitch + (size_t)x * 4u;

  return pixels[offset + 0u] <= 2u &&
         pixels[offset + 1u] <= 2u &&
         pixels[offset + 2u] >= 96u &&
         pixels[offset + 2u] <= 160u;
}

static int
render_readback_has_red_in_x_range(const uint8_t *pixels,
                                   uint32_t height,
                                   uint32_t rowPitch,
                                   uint32_t minX,
                                   uint32_t maxX) {
  for (uint32_t y = 0u; y < height; y++) {
    for (uint32_t x = minX; x < maxX; x++) {
      if (render_readback_pixel_is_red(pixels, rowPitch, x, y)) {
        return 1;
      }
    }
  }

  return 0;
}

static int
check_render_readback_case(GPUDevice *device,
                           RenderReadbackDrawMode mode,
                           int clipped,
                           const char *mrtBytecodePath) {
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
  const uint32_t rowPitch = 256u;
  const uint64_t imageBytes = (uint64_t)rowPitch * height;
  const uint64_t readbackBytes = imageBytes *
                                 (mode == RENDER_READBACK_DRAW_MRT ? 2u : 1u);
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
  const int occlusion = mode == RENDER_READBACK_DRAW_OCCLUSION;
  const int msaa = mode == RENDER_READBACK_DRAW_MSAA;
  const int mrt = mode == RENDER_READBACK_DRAW_MRT;
  const int colorWriteNone =
    mode == RENDER_READBACK_DRAW_COLOR_WRITE_NONE;
  const void *vertexData = multi ? (const void *)kMultiTriangles :
                                  (const void *)kFullscreenTriangle;
  const uint64_t vertexDataSize = multi ? sizeof(kMultiTriangles) :
                                          sizeof(kFullscreenTriangle);
  const void *indexData = multi ? (const void *)kMultiTriangleIndices :
                                 (const void *)kTriangleIndices;
  const uint64_t indexDataSize = multi ? sizeof(kMultiTriangleIndices) :
                                         sizeof(kTriangleIndices);
  GPUQueue        *queue;
  GPUShaderLibrary *library = NULL;
  GPURenderPipeline *pipeline = NULL;
  GPUBuffer *vertexBuffer = NULL;
  GPUBuffer *indexBuffer = NULL;
  GPUBuffer *argsBuffer = NULL;
  GPUBuffer *readbackBuffer = NULL;
  GPUBuffer *queryBuffer = NULL;
  GPUTexture *target = NULL;
  GPUTexture *target2 = NULL;
  GPUTexture *resolveTarget = NULL;
  GPUTextureView *targetView = NULL;
  GPUTextureView *targetView2 = NULL;
  GPUTextureView *resolveView = NULL;
  GPUQuerySet *querySet = NULL;
  GPUPipelineLayout *pipelineLayout = NULL;
  GPUCommandBuffer *cmdb = NULL;
  GPUCommandBuffer *buffers[1];
  GPURenderPassEncoder *renderPass = NULL;
  GPUCopyPassEncoder *copyPass = NULL;
  GPUFence *fence = NULL;
  GPUColorTargetState colorTargets[2] = {{0}};
  GPUVertexAttribute attr = {0};
  GPUVertexBufferLayout vertexLayout = {0};
  GPUPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
  GPURenderPipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo bufferInfo = {0};
  GPUQuerySetCreateInfo queryInfo = {0};
  GPUResult queryResult;
  GPUTextureCreateInfo textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo = {0};
  GPURenderPassColorAttachment colors[2] = {{0}};
  GPURenderPassCreateInfo rp = {0};
  GPUBufferBinding vertexBinding = {0};
  GPUTextureBarrier textureBarriers[2] = {{0}};
  GPUBarrierBatch barrierBatch = {0};
  GPUBufferTextureCopyRegion copyRegion = {0};
  GPUQueueSubmitInfo submitInfo = {0};
  GPUDynamicStateApplyInfo dynamicState = {0};
  uint8_t pixels[2u * 256u * 4u] = {0};
  uint64_t occlusionResult = 0u;
  size_t centerOffset;
  int ok = 0;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for %s test\n", label);
    return 0;
  }
  if (occlusion) {
    queryInfo.chain.sType      = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
    queryInfo.chain.structSize = sizeof(queryInfo);
    queryInfo.label            = label;
    queryInfo.type             = GPU_QUERY_OCCLUSION;
    queryInfo.count            = 1u;
    queryResult = GPUCreateQuerySet(device, &queryInfo, &querySet);
    if (queryResult == GPU_ERROR_UNSUPPORTED && !querySet) {
      return 1;
    }
    if (queryResult != GPU_OK || !querySet) {
      fprintf(stderr, "failed to create %s query set\n", label);
      return 0;
    }
  }
  if (!create_render_usl_library(device, mrtBytecodePath, &library)) {
    fprintf(stderr, "failed to create %s library\n", label);
    goto cleanup;
  }

  colorTargets[0].format = GPU_FORMAT_BGRA8_UNORM;
  colorTargets[1].format = GPU_FORMAT_BGRA8_UNORM;
  if (colorWriteNone) {
    colorTargets[0].blend.writeMask = GPU_COLOR_WRITE_NONE;
  }
  if (mrt) {
    colorTargets[0].blend.enabled         = true;
    colorTargets[0].blend.color.srcFactor = GPU_BLEND_FACTOR_SRC_ALPHA;
    colorTargets[0].blend.color.dstFactor =
      GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorTargets[0].blend.color.op         = GPU_BLEND_OP_ADD;
    colorTargets[0].blend.alpha.srcFactor = GPU_BLEND_FACTOR_ONE;
    colorTargets[0].blend.alpha.dstFactor = GPU_BLEND_FACTOR_ZERO;
    colorTargets[0].blend.alpha.op         = GPU_BLEND_OP_ADD;
    colorTargets[0].blend.writeMask        = GPU_COLOR_WRITE_ALL;
    colorTargets[1].blend.writeMask        = GPU_COLOR_WRITE_G;
  }
  attr.shaderLocation = 0u;
  attr.format = GPU_VERTEX_FORMAT_FLOAT32X2;
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
  pipelineInfo.fragmentEntry = mrt ? "api_mrt_fs"
                                   : (msaa ? "api_alpha_fs" : "api_fs");
  pipelineInfo.vertex.bufferLayoutCount = 1u;
  pipelineInfo.vertex.pBufferLayouts = &vertexLayout;
  pipelineInfo.colorTargetCount = mrt ? 2u : 1u;
  pipelineInfo.pColorTargets = colorTargets;
  pipelineInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount           = msaa ? 4u : 1u;
  pipelineInfo.multisample.alphaToCoverageEnable = msaa;
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
  bufferInfo.sizeBytes = readbackBytes;
  bufferInfo.usage = GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readbackBuffer) != GPU_OK ||
      !readbackBuffer) {
    fprintf(stderr, "failed to create %s readback buffer\n", label);
    goto cleanup;
  }
  if (occlusion) {
    bufferInfo.sizeBytes = sizeof(occlusionResult);
    bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_DST |
                           GPU_BUFFER_USAGE_COPY_SRC;
    if (GPUCreateBuffer(device, &bufferInfo, &queryBuffer) != GPU_OK ||
        !queryBuffer) {
      fprintf(stderr, "failed to create %s query buffer\n", label);
      goto cleanup;
    }
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
  textureInfo.sampleCount = msaa ? 4u : 1u;
  textureInfo.usage = GPU_TEXTURE_USAGE_COLOR_TARGET |
                      (msaa ? 0u : GPU_TEXTURE_USAGE_COPY_SRC);
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
  if (mrt &&
      (GPUCreateTexture(device, &textureInfo, &target2) != GPU_OK ||
       !target2 ||
       GPUCreateTextureView(target2, &viewInfo, &targetView2) != GPU_OK ||
       !targetView2)) {
    fprintf(stderr, "failed to create %s second target\n", label);
    goto cleanup;
  }
  if (msaa) {
    textureInfo.sampleCount = 1u;
    textureInfo.usage       = GPU_TEXTURE_USAGE_COLOR_TARGET |
                              GPU_TEXTURE_USAGE_COPY_SRC;
    if (GPUCreateTexture(device, &textureInfo, &resolveTarget) != GPU_OK ||
        !resolveTarget ||
        GPUCreateTextureView(resolveTarget, &viewInfo, &resolveView) != GPU_OK ||
        !resolveView) {
      fprintf(stderr, "failed to create %s resolve target\n", label);
      goto cleanup;
    }
  }

  if (GPUAcquireCommandBuffer(queue, label, &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "failed to acquire %s command buffer\n", label);
    goto cleanup;
  }

  colors[0].view = targetView;
  colors[0].resolveView = resolveView;
  colors[0].loadOp = GPU_LOAD_OP_CLEAR;
  colors[0].storeOp = GPU_STORE_OP_STORE;
  colors[0].clearColor.float32[0] = 0.0f;
  colors[0].clearColor.float32[1] = 0.0f;
  colors[0].clearColor.float32[2] = mrt ? 1.0f : 0.0f;
  colors[0].clearColor.float32[3] = 1.0f;
  colors[1] = colors[0];
  colors[1].view = targetView2;
  colors[1].resolveView = NULL;
  rp.chain.sType = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rp.chain.structSize = sizeof(rp);
  rp.label = label;
  rp.occlusionQuerySet = querySet;
  rp.colorAttachmentCount = mrt ? 2u : 1u;
  rp.pColorAttachments = colors;

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
    dynamicState.viewport.x        = 0.0f;
    dynamicState.viewport.y        = 0.0f;
    dynamicState.viewport.width    = (float)width;
    dynamicState.viewport.height   = (float)height;
    dynamicState.viewport.minDepth = 0.0f;
    dynamicState.viewport.maxDepth = 1.0f;
    dynamicState.scissor.x         = 1;
    dynamicState.scissor.y         = 1;
    dynamicState.scissor.width     = 2u;
    dynamicState.scissor.height    = 2u;
    GPUApplyDynamicState(renderPass, &dynamicState);
  }
  if (occlusion) {
    GPUBeginOcclusionQuery(renderPass, querySet, 0u);
    if (!renderPass->_occlusionQueryActive) {
      fprintf(stderr, "failed to begin %s query\n", label);
      goto cleanup;
    }
    GPUEndRenderPass(renderPass);
    if (renderPass->_ended || !renderPass->_occlusionQueryActive) {
      fprintf(stderr, "%s pass ended with an active query\n", label);
      goto cleanup;
    }
  }
  if (mode == RENDER_READBACK_DRAW_INDEXED) {
    GPUBindIndexBuffer(renderPass, indexBuffer, 0u, GPU_INDEX_TYPE_UINT16);
    GPUDrawIndexed(renderPass, 3u, 1u, 0u, 0, 0u);
  } else if (mode == RENDER_READBACK_DRAW_INDIRECT) {
    GPUDrawIndirect(renderPass, argsBuffer, 0u);
  } else if (mode == RENDER_READBACK_DRAW_INDEXED_INDIRECT) {
    GPUBindIndexBuffer(renderPass, indexBuffer, 0u, GPU_INDEX_TYPE_UINT16);
    GPUDrawIndexedIndirect(renderPass, argsBuffer, 0u);
  } else if (mode == RENDER_READBACK_DRAW_MULTI_INDIRECT) {
    GPUMultiDrawIndirect(renderPass,
                         argsBuffer,
                         0u,
                         2u,
                         (uint32_t)sizeof(RenderIndirectArgs));
  } else if (mode == RENDER_READBACK_DRAW_INDEXED_MULTI_INDIRECT) {
    GPUBindIndexBuffer(renderPass, indexBuffer, 0u, GPU_INDEX_TYPE_UINT16);
    GPUMultiDrawIndexedIndirect(renderPass,
                                argsBuffer,
                                0u,
                                2u,
                                (uint32_t)sizeof(RenderIndexedIndirectArgs));
  } else {
    GPUDraw(renderPass, 3u, 1u, 0u, 0u);
  }
  if (occlusion) {
    GPUEndOcclusionQuery(renderPass);
    if (renderPass->_occlusionQueryActive) {
      fprintf(stderr, "failed to end %s query\n", label);
      goto cleanup;
    }
  }
  GPUEndRenderPass(renderPass);
  renderPass = NULL;
  if (occlusion) {
    GPUResolveQuerySet(cmdb, querySet, 0u, 1u, queryBuffer, 0u);
  }

  textureBarriers[0].texture = msaa ? resolveTarget : target;
  textureBarriers[0].srcAccess = GPU_ACCESS_COLOR_WRITE;
  textureBarriers[0].dstAccess = GPU_ACCESS_TRANSFER_READ;
  textureBarriers[0].mipCount = 1u;
  textureBarriers[0].layerCount = 1u;
  textureBarriers[1] = textureBarriers[0];
  textureBarriers[1].texture = target2;
  barrierBatch.srcStages = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = mrt ? 2u : 1u;
  barrierBatch.pTextureBarriers = textureBarriers;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, label);
  if (!copyPass) {
    fprintf(stderr, "failed to begin %s copy pass\n", label);
    goto cleanup;
  }
  copyRegion.bytesPerRow = rowPitch;
  copyRegion.rowsPerImage = height;
  copyRegion.texture.width = width;
  copyRegion.texture.height = height;
  copyRegion.texture.depth = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass,
                         msaa ? resolveTarget : target,
                         readbackBuffer,
                         &copyRegion);
  if (mrt) {
    copyRegion.bufferOffset = imageBytes;
    GPUCopyTextureToBuffer(copyPass,
                           target2,
                           readbackBuffer,
                           &copyRegion);
  }
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
                         readbackBytes) != GPU_OK) {
    fprintf(stderr, "%s buffer read failed\n", label);
    goto cleanup;
  }
  if (occlusion &&
      (GPUQueueReadBuffer(queue,
                          queryBuffer,
                          0u,
                          &occlusionResult,
                          sizeof(occlusionResult)) != GPU_OK ||
       occlusionResult == 0u)) {
    fprintf(stderr, "%s query result was not visible\n", label);
    goto cleanup;
  }

  if (colorWriteNone) {
    centerOffset = (size_t)2u * rowPitch + 2u * 4u;
    if (pixels[centerOffset + 0u] > 2u ||
        pixels[centerOffset + 1u] > 2u ||
        pixels[centerOffset + 2u] > 2u ||
        pixels[centerOffset + 3u] < 250u) {
      fprintf(stderr, "%s draw modified disabled target\n", label);
      goto cleanup;
    }
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

  if (mrt) {
    centerOffset = (size_t)2u * rowPitch + 2u * 4u;
    if (pixels[centerOffset + 0u] < 96u ||
        pixels[centerOffset + 0u] > 160u ||
        pixels[centerOffset + 1u] > 2u ||
        pixels[centerOffset + 2u] < 96u ||
        pixels[centerOffset + 2u] > 160u ||
        pixels[centerOffset + 3u] < 96u ||
        pixels[centerOffset + 3u] > 160u ||
        pixels[imageBytes + centerOffset + 0u] < 250u ||
        pixels[imageBytes + centerOffset + 1u] < 250u ||
        pixels[imageBytes + centerOffset + 2u] > 2u ||
        pixels[imageBytes + centerOffset + 3u] < 250u) {
      fprintf(stderr, "%s blend/write-mask readback mismatch\n", label);
      goto cleanup;
    }
  } else if (multi) {
    if (!render_readback_has_red_in_x_range(pixels,
                                            height,
                                            rowPitch,
                                            0u,
                                            2u) ||
        !render_readback_has_red_in_x_range(pixels,
                                            height,
                                            rowPitch,
                                            2u,
                                            4u)) {
      fprintf(stderr, "%s multi draw did not touch both target halves\n", label);
      goto cleanup;
    }
  } else if (!colorWriteNone &&
             (msaa
                ? !render_readback_pixel_is_half_red(pixels,
                                                     rowPitch,
                                                     2u,
                                                     2u)
                : !render_readback_pixel_is_red(pixels,
                                                rowPitch,
                                                2u,
                                                2u))) {
    centerOffset = (size_t)2u * rowPitch + 2u * 4u;
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
    GPUEndOcclusionQuery(renderPass);
    GPUEndRenderPass(renderPass);
  }
  GPUDestroyFence(fence);
  GPUDestroyTextureView(resolveView);
  GPUDestroyTexture(resolveTarget);
  GPUDestroyTextureView(targetView2);
  GPUDestroyTexture(target2);
  GPUDestroyTextureView(targetView);
  GPUDestroyTexture(target);
  GPUDestroyBuffer(queryBuffer);
  GPUDestroyBuffer(readbackBuffer);
  GPUDestroyBuffer(argsBuffer);
  GPUDestroyBuffer(indexBuffer);
  GPUDestroyBuffer(vertexBuffer);
  GPUDestroyQuerySet(querySet);
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyShaderLibrary(library);
  return ok;
}

static int
check_render_readback(GPUDevice *device, const char *mrtBytecodePath) {
  return check_render_readback_case(device,
                                    RENDER_READBACK_DRAW,
                                    0,
                                    mrtBytecodePath) &&
         check_render_readback_case(device,
                                    RENDER_READBACK_DRAW_INDEXED,
                                    1,
                                    mrtBytecodePath) &&
         check_render_readback_case(device,
                                    RENDER_READBACK_DRAW_INDIRECT,
                                    0,
                                    mrtBytecodePath) &&
         check_render_readback_case(device,
                                    RENDER_READBACK_DRAW_INDEXED_INDIRECT,
                                    0,
                                    mrtBytecodePath) &&
         check_render_readback_case(device,
                                    RENDER_READBACK_DRAW_MULTI_INDIRECT,
                                    0,
                                    mrtBytecodePath) &&
         check_render_readback_case(device,
                                    RENDER_READBACK_DRAW_INDEXED_MULTI_INDIRECT,
                                    0,
                                    mrtBytecodePath) &&
         check_render_readback_case(device,
                                    RENDER_READBACK_DRAW_OCCLUSION,
                                    0,
                                    mrtBytecodePath) &&
         check_render_readback_case(device,
                                    RENDER_READBACK_DRAW_MSAA,
                                    0,
                                    mrtBytecodePath) &&
         check_render_readback_case(device,
                                    RENDER_READBACK_DRAW_MRT,
                                    0,
                                    mrtBytecodePath) &&
         check_render_readback_case(device,
                                    RENDER_READBACK_DRAW_COLOR_WRITE_NONE,
                                    0,
                                    mrtBytecodePath);
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
  bool (*oldMultiDrawIndirect)(GPURenderCommandEncoder *,
                               GPUPrimitiveType,
                               GPUBuffer *,
                               uint64_t,
                               uint32_t,
                               uint32_t);
  bool (*oldMultiDrawIndexedIndirect)(GPURenderCommandEncoder *,
                                      GPUBuffer *,
                                      uint64_t,
                                      uint32_t,
                                      uint32_t);
  GPUBindGroupLayout          *layout         = NULL;
  GPUBindGroupLayout          *layouts[1];
  GPUPipelineLayout           *pipelineLayout = NULL;
  GPUBindGroupLayoutEntry      entry           = {0};
  GPUBindGroupLayoutCreateInfo layoutInfo      = {0};
  GPUPipelineLayoutCreateInfo  pipelineInfo    = {0};
  GPUQueue                     fakeQueue       = {0};
  GPUCommandBuffer             fakeCmdb        = {0};
  GPURenderPassEncoder         pass            = {0};
  GPUBuffer                    indirectBuffer  = {0};
  GPUBuffer                    wrongUsageBuffer = {0};
  GPUFrameStats                savedFrameStats;
  GPUValidationMode            savedValidationMode;
  uint64_t                     oldEnabledFeatureMask;
  uint64_t                     drawCallsBeforeMissing;
  bool                         savedStatsEnabled;
  int                          ok = 0;

  api = gpuDeviceApi(device);
  if (!api) {
    fprintf(stderr, "render draw validation has no device api\n");
    return 0;
  }
  oldEnabledFeatureMask = device->enabledFeatureMask;
  fakeQueue._device     = device;
  fakeCmdb._queue       = &fakeQueue;
  pass._cmdb            = &fakeCmdb;

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
  oldMultiDrawIndirect = api->rce.multiDrawPrimitivesIndirect;
  oldMultiDrawIndexedIndirect = api->rce.multiDrawIndexedPrimsIndirect;
  api->rce.drawPrimitives = count_draw_primitives;
  api->rce.drawIndexedPrims = count_draw_indexed;
  api->rce.drawPrimitivesIndirect = count_draw_indirect;
  api->rce.drawIndexedPrimsIndirect = count_draw_indexed_indirect;
  api->rce.multiDrawPrimitivesIndirect = count_multi_draw_indirect;
  api->rce.multiDrawIndexedPrimsIndirect = count_multi_draw_indexed_indirect;

  savedFrameStats                   = device->currentFrameStats;
  savedStatsEnabled                 = device->runtimeConfig.enableStats;
  device->runtimeConfig.enableStats = true;
  device->currentFrameStats.drawCalls = 0u;

  gRenderDrawCalls = 0u;
  gRenderDrawIndexedCalls = 0u;
  gRenderDrawIndirectCalls = 0u;
  gRenderDrawIndexedIndirectCalls = 0u;
  gRenderMultiDrawIndirectCalls = 0u;
  gRenderMultiDrawIndexedIndirectCalls = 0u;
  device->enabledFeatureMask &= ~(1ull << GPU_FEATURE_MULTI_DRAW);

  indirectBuffer.sizeBytes = 128u;
  indirectBuffer.usage = GPU_BUFFER_USAGE_INDIRECT | GPU_BUFFER_USAGE_INDEX;
  wrongUsageBuffer.sizeBytes = 128u;
  wrongUsageBuffer.usage = GPU_BUFFER_USAGE_VERTEX;

  pass._hasPipeline = true;
  pass._pipelineLayout = pipelineLayout;
  pass._primitiveType = GPUPrimitiveTypeTriangle;
  savedValidationMode = device->runtimeConfig.validationMode;

  device->runtimeConfig.validationMode = GPU_VALIDATION_OFF;
  pass._requiredBindGroupMask = 1u;
  GPUDraw(&pass, 3u, 1u, 0u, 0u);
  if (gRenderDrawCalls != 1u) {
    fprintf(stderr, "render draw runtime-off fast path failed\n");
    goto cleanup;
  }
  gRenderDrawCalls = 0u;
  device->currentFrameStats.drawCalls = 0u;
  device->runtimeConfig.validationMode = GPU_VALIDATION_BASIC;

  pass._requiredBindGroupMask = 0u;
  GPUDraw(&pass, 3u, 1u, 0u, 0u);
  GPUDrawIndirect(&pass, &indirectBuffer, 0u);
  if (gRenderDrawCalls != 1u || gRenderDrawIndirectCalls != 1u) {
    fprintf(stderr, "render draw validation rejected no-bind pipeline\n");
    goto cleanup;
  }
  gRenderDrawCalls = 0u;
  gRenderDrawIndirectCalls = 0u;

  drawCallsBeforeMissing = device->currentFrameStats.drawCalls;
  pass._requiredBindGroupMask = 1u;
  GPUDraw(&pass, 3u, 1u, 0u, 0u);
  GPUDrawIndexed(&pass, 3u, 1u, 0u, 0, 0u);
  GPUDrawIndirect(&pass, &indirectBuffer, 0u);
  GPUDrawIndexedIndirect(&pass, &indirectBuffer, 0u);
  GPUMultiDrawIndirect(&pass, &indirectBuffer, 0u, 2u, 16u);
  GPUMultiDrawIndexedIndirect(&pass, &indirectBuffer, 0u, 2u, 20u);
#if GPU_BUILD_WITH_VALIDATION
  if (gRenderDrawCalls != 0u ||
      gRenderDrawIndexedCalls != 0u ||
      gRenderDrawIndirectCalls != 0u ||
      gRenderDrawIndexedIndirectCalls != 0u ||
      gRenderMultiDrawIndirectCalls != 0u ||
      gRenderMultiDrawIndexedIndirectCalls != 0u) {
    fprintf(stderr, "render draw validation called backend without required bind group\n");
    goto cleanup;
  }
#else
  if (gRenderDrawCalls != 1u ||
      gRenderDrawIndexedCalls != 0u ||
      gRenderDrawIndirectCalls != 3u ||
      gRenderDrawIndexedIndirectCalls != 0u ||
      gRenderMultiDrawIndirectCalls != 0u ||
      gRenderMultiDrawIndexedIndirectCalls != 0u) {
    fprintf(stderr, "compiled-out render validation did not reach backend\n");
    goto cleanup;
  }
#endif
  gRenderDrawCalls = 0u;
  gRenderDrawIndexedCalls = 0u;
  gRenderDrawIndirectCalls = 0u;
  gRenderDrawIndexedIndirectCalls = 0u;
  gRenderMultiDrawIndirectCalls = 0u;
  gRenderMultiDrawIndexedIndirectCalls = 0u;
  device->currentFrameStats.drawCalls = drawCallsBeforeMissing;

  pass._boundGroupLayouts[0] = layout;
  GPUDraw(&pass, 0u, 1u, 0u, 0u);
  GPUDraw(&pass, 3u, 0u, 0u, 0u);
  GPUDrawIndirect(&pass, &wrongUsageBuffer, 0u);
  GPUDrawIndirect(&pass, &indirectBuffer, 2u);
  GPUDrawIndirect(&pass, &indirectBuffer, 120u);
  GPUMultiDrawIndirect(&pass, &indirectBuffer, 2u, 2u, 16u);
  GPUMultiDrawIndirect(&pass, &indirectBuffer, 0u, 2u, 12u);
  GPUMultiDrawIndirect(&pass, &indirectBuffer, 0u, 2u, 18u);
  GPUMultiDrawIndirect(&pass, &indirectBuffer, 96u, 3u, 16u);
  if (gRenderDrawCalls != 0u ||
      gRenderDrawIndirectCalls != 0u ||
      gRenderMultiDrawIndirectCalls != 0u) {
    fprintf(stderr, "render draw validation called backend for invalid non-indexed draw\n");
    goto cleanup;
  }

  GPUDraw(&pass, 3u, 1u, 0u, 0u);
  GPUDrawIndirect(&pass, &indirectBuffer, 0u);
  GPUMultiDrawIndirect(&pass, &indirectBuffer, 0u, 2u, 16u);
  if (gRenderDrawCalls != 1u ||
      gRenderDrawIndirectCalls != 3u ||
      gRenderMultiDrawIndirectCalls != 0u) {
    fprintf(stderr, "render draw validation rejected multi-draw fallback\n");
    goto cleanup;
  }

  gRenderDrawIndirectCalls = 0u;
  device->enabledFeatureMask |= 1ull << GPU_FEATURE_MULTI_DRAW;
  GPUMultiDrawIndirect(&pass, &indirectBuffer, 0u, 2u, 16u);
  if (gRenderDrawIndirectCalls != 0u ||
      gRenderMultiDrawIndirectCalls != 1u) {
    fprintf(stderr, "render draw validation rejected native multi-draw path\n");
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

  GPUBindIndexBuffer(&pass, &indirectBuffer, 0u, GPU_INDEX_TYPE_UINT16);
  GPUDrawIndexedIndirect(&pass, &indirectBuffer, 2u);
  GPUMultiDrawIndexedIndirect(&pass, &indirectBuffer, 2u, 2u, 20u);
  GPUMultiDrawIndexedIndirect(&pass, &indirectBuffer, 0u, 2u, 16u);
  GPUMultiDrawIndexedIndirect(&pass, &indirectBuffer, 0u, 2u, 22u);
  GPUMultiDrawIndexedIndirect(&pass, &indirectBuffer, 92u, 2u, 20u);
  if (gRenderDrawIndexedIndirectCalls != 0u ||
      gRenderMultiDrawIndexedIndirectCalls != 0u) {
    fprintf(stderr, "render draw validation accepted invalid indexed indirect layout\n");
    goto cleanup;
  }
  GPUDrawIndexed(&pass, 3u, 1u, 0u, 0, 0u);
  GPUDrawIndexedIndirect(&pass, &indirectBuffer, 0u);
  GPUMultiDrawIndexedIndirect(&pass, &indirectBuffer, 0u, 2u, 20u);
  if (gRenderDrawIndexedCalls != 1u ||
      gRenderDrawIndexedIndirectCalls != 1u ||
      gRenderMultiDrawIndexedIndirectCalls != 1u ||
      device->currentFrameStats.drawCalls != 12u) {
    fprintf(stderr, "render draw validation rejected valid indexed draw\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  device->currentFrameStats                    = savedFrameStats;
  device->runtimeConfig.enableStats           = savedStatsEnabled;
  device->runtimeConfig.validationMode         = savedValidationMode;
  device->enabledFeatureMask                  = oldEnabledFeatureMask;
  api->rce.drawPrimitives                     = oldDraw;
  api->rce.drawIndexedPrims                   = oldDrawIndexed;
  api->rce.drawPrimitivesIndirect             = oldDrawIndirect;
  api->rce.drawIndexedPrimsIndirect           = oldDrawIndexedIndirect;
  api->rce.multiDrawPrimitivesIndirect        = oldMultiDrawIndirect;
  api->rce.multiDrawIndexedPrimsIndirect = oldMultiDrawIndexedIndirect;
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(layout);
  return ok;
}

static int
check_vertex_buffer_shadowing_calls(GPUDevice *activeDevice) {
  GPUApi *api;
  void (*oldVertexBuffer)(GPURenderCommandEncoder *,
                          GPUBuffer *,
                          uint64_t,
                          uint32_t);
  GPUDevice            device  = {0};
  GPUQueue             queue   = {0};
  GPUCommandBuffer     cmdb    = {0};
  GPUBuffer            buffer  = {0};
  GPUBufferBinding     binding = {0};
  GPURenderPassEncoder pass    = {0};
  int                  ok;

  api = gpuDeviceApi(activeDevice);
  if (!api) {
    fprintf(stderr, "vertex buffer shadowing has no device api\n");
    return 0;
  }

  oldVertexBuffer                     = api->rce.vertexInputBuffer;
  api->rce.vertexInputBuffer          = count_vertex_buffer;
  device._api                         = api;
  device.runtimeConfig.enableStats    = true;
  queue._device                       = &device;
  cmdb._queue                         = &queue;
  pass._cmdb                          = &cmdb;
  buffer.sizeBytes                    = 256u;
  buffer.usage                        = GPU_BUFFER_USAGE_VERTEX;
  binding.buffer                      = &buffer;
  binding.offset                      = 0u;
  gRenderVertexBufferCalls            = 0u;
  ok                                  = 0;

  GPUBindVertexBuffers(&pass, 0u, 1u, &binding);
  GPUResetStats(&device);
  gRenderVertexBufferCalls = 0u;
  for (uint32_t i = 1u; i <= 16u; i++) {
    binding.offset = i * 4u;
    GPUBindVertexBuffers(&pass, 0u, 1u, &binding);
    GPUBindVertexBuffers(&pass, 0u, 1u, &binding);
  }
  if (gRenderVertexBufferCalls != 16u ||
      device.currentFrameStats.requestedBindCalls != 32u ||
      device.currentFrameStats.emittedBindCalls != 16u ||
      device.currentFrameStats.hotPathAllocCount != 0u ||
      device.currentFrameStats.hotPathFreeCount != 0u) {
    fprintf(stderr, "vertex buffer shadowing warm path mismatch\n");
    goto cleanup;
  }

  GPUBindVertexBuffers(&pass,
                       GPU__RENDER_VERTEX_SHADOW_SLOT_COUNT,
                       1u,
                       &binding);
  GPUBindVertexBuffers(&pass,
                       GPU__RENDER_VERTEX_SHADOW_SLOT_COUNT,
                       1u,
                       &binding);
  if (gRenderVertexBufferCalls != 18u ||
      device.currentFrameStats.requestedBindCalls != 34u ||
      device.currentFrameStats.emittedBindCalls != 18u) {
    fprintf(stderr, "vertex buffer shadowing constrained high slots\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  api->rce.vertexInputBuffer = oldVertexBuffer;
  return ok;
}

static int
check_render_push_constant_shadowing_calls(GPUDevice *activeDevice) {
  GPUApi *api;
  void (*oldPushConstants)(GPURenderCommandEncoder *,
                           GPUShaderStageFlags,
                           const void *,
                           uint32_t);
  GPUDevice            device = {0};
  GPUQueue             queue  = {0};
  GPUCommandBuffer     cmdb   = {0};
  GPURenderPassEncoder pass   = {0};
  uint32_t             value;
  int                  ok;

  api = gpuDeviceApi(activeDevice);
  if (!api) {
    fprintf(stderr, "render push constant shadowing has no device api\n");
    return 0;
  }

  oldPushConstants                  = api->rce.pushConstants;
  api->rce.pushConstants            = count_render_push_constants;
  device._api                       = api;
  device.runtimeConfig.enableStats = true;
  queue._device                     = &device;
  cmdb._queue                       = &queue;
  pass._cmdb                        = &cmdb;
  pass._hasPipeline                 = true;
  pass._pushConstantSizeBytes       = 16u;
  pass._pushConstantStages          = GPU_SHADER_STAGE_VERTEX_BIT |
                                      GPU_SHADER_STAGE_FRAGMENT_BIT;
  value                             = 0u;
  gRenderPushConstantCalls          = 0u;
  ok                                = 0;

  GPUSetRenderPushConstants(&pass, 0u, sizeof(value), &value);
  GPUSetRenderPushConstants(&pass, 0u, sizeof(value), &value);
  if (gRenderPushConstantCalls != 1u ||
      device.currentFrameStats.requestedStateCalls != 2u ||
      device.currentFrameStats.emittedStateCalls != 1u) {
    fprintf(stderr, "render push constant first-zero shadowing failed\n");
    goto cleanup;
  }

  GPUResetStats(&device);
  gRenderPushConstantCalls = 0u;
  for (uint32_t i = 1u; i <= 16u; i++) {
    value = i;
    GPUSetRenderPushConstants(&pass, (i & 1u) * 4u, sizeof(value), &value);
    GPUSetRenderPushConstants(&pass, (i & 1u) * 4u, sizeof(value), &value);
  }
  if (gRenderPushConstantCalls != 16u ||
      device.currentFrameStats.requestedStateCalls != 32u ||
      device.currentFrameStats.emittedStateCalls != 16u ||
      device.currentFrameStats.hotPathAllocCount != 0u ||
      device.currentFrameStats.hotPathFreeCount != 0u) {
    fprintf(stderr, "render push constant shadowing warm path mismatch\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  api->rce.pushConstants = oldPushConstants;
  return ok;
}

static int
check_dynamic_state_validation_calls(GPUDevice *activeDevice) {
  GPUApi             *api;
  GPUApi              scopedApi;
  GPUDevice           device = {0};
  GPUQueue            queue  = {0};
  GPUCommandBuffer    cmdb   = {0};
  GPURenderPassEncoder pass = {0};
  GPURenderPassEncoder endedPass = {0};
  GPUDynamicStateApplyInfo info = {0};
  int ok = 0;

  api = gpuDeviceApi(activeDevice);
  if (!api) {
    fprintf(stderr, "dynamic state validation has no device api\n");
    return 0;
  }

  scopedApi                      = *api;
  scopedApi.rce.viewport         = count_viewport;
  scopedApi.rce.scissor          = count_scissor;
  scopedApi.rce.blendConstant    = count_blend_constant;
  scopedApi.rce.stencilReference = count_stencil_reference;

  device._api                      = &scopedApi;
  device.runtimeConfig.enableStats = true;
  queue._device                    = &device;
  cmdb._queue                      = &queue;
  pass._cmdb                       = &cmdb;

  gRenderViewportCalls = 0u;
  gRenderScissorCalls = 0u;
  gRenderBlendCalls = 0u;
  gRenderStencilCalls = 0u;

  GPUApplyDynamicState(NULL, &info);
  GPUApplyDynamicState(&pass, NULL);
  GPUSetViewport(NULL, &info.viewport);
  GPUSetViewport(&pass, NULL);
  GPUSetScissor(NULL, &info.scissor);
  GPUSetScissor(&pass, NULL);
  GPUSetBlendConstant(NULL, info.blendConstant);
  GPUSetBlendConstant(&pass, NULL);
  GPUSetStencilReference(NULL, 0u);

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
  GPUSetViewport(&endedPass, &info.viewport);
  GPUSetScissor(&endedPass, &info.scissor);
  GPUSetBlendConstant(&endedPass, info.blendConstant);
  GPUSetStencilReference(&endedPass, 0u);

  info.chain.sType      = GPU_STRUCTURE_TYPE_DYNAMIC_STATE_APPLY_INFO;
  info.mask            |= 1ull << 63;
  GPUApplyDynamicState(&pass, &info);

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
      gRenderStencilCalls != 1u ||
      device.currentFrameStats.requestedStateCalls != 4u ||
      device.currentFrameStats.emittedStateCalls != 4u) {
    fprintf(stderr, "dynamic state validation did not apply selected state\n");
    goto cleanup;
  }

  GPUApplyDynamicState(&pass, &info);
  if (gRenderViewportCalls != 1u ||
      gRenderScissorCalls != 1u ||
      gRenderBlendCalls != 1u ||
      gRenderStencilCalls != 1u ||
      device.currentFrameStats.requestedStateCalls != 8u ||
      device.currentFrameStats.emittedStateCalls != 4u) {
    fprintf(stderr, "dynamic state shadowing emitted redundant state\n");
    goto cleanup;
  }

  info.mask = GPU_DYNAMIC_STATE_STENCIL_REFERENCE_BIT;
  info.stencilReference = 7u;
  GPUApplyDynamicState(&pass, &info);
  if (gRenderStencilCalls != 2u ||
      device.currentFrameStats.requestedStateCalls != 9u ||
      device.currentFrameStats.emittedStateCalls != 5u) {
    fprintf(stderr, "dynamic state shadowing suppressed changed state\n");
    goto cleanup;
  }

  GPUSetViewport(&pass, &info.viewport);
  GPUSetScissor(&pass, &info.scissor);
  GPUSetBlendConstant(&pass, info.blendConstant);
  GPUSetStencilReference(&pass, info.stencilReference);
  if (gRenderViewportCalls != 1u ||
      gRenderScissorCalls != 1u ||
      gRenderBlendCalls != 1u ||
      gRenderStencilCalls != 2u ||
      device.currentFrameStats.requestedStateCalls != 13u ||
      device.currentFrameStats.emittedStateCalls != 5u) {
    fprintf(stderr, "direct dynamic setters emitted redundant state\n");
    goto cleanup;
  }

  info.viewport.x         = 1.0f;
  info.scissor.width      = 1u;
  info.blendConstant[0]   = 1.0f;
  info.stencilReference   = 9u;
  GPUSetViewport(&pass, &info.viewport);
  GPUSetScissor(&pass, &info.scissor);
  GPUSetBlendConstant(&pass, info.blendConstant);
  GPUSetStencilReference(&pass, info.stencilReference);
  if (gRenderViewportCalls != 2u ||
      gRenderScissorCalls != 2u ||
      gRenderBlendCalls != 2u ||
      gRenderStencilCalls != 3u ||
      device.currentFrameStats.requestedStateCalls != 17u ||
      device.currentFrameStats.emittedStateCalls != 9u ||
      device.currentFrameStats.hotPathAllocCount != 0u ||
      device.currentFrameStats.hotPathFreeCount != 0u) {
    fprintf(stderr, "direct dynamic setters suppressed changed state\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  return ok;
}

static int
check_render_encoder_validation(GPUDevice *device) {
  GPUQueue        fakeQueue = {0};
  GPUCommandBuffer fakeCmdb = {0};
  GPURenderPassEncoder pass = {0};
  GPURenderPassEncoder endedPass = {0};
  GPURenderPipeline fakePipeline = {0};
  GPUBuffer fakeBufferStorage = {0};
  GPUBuffer *fakeBuffer = &fakeBufferStorage;
  GPUBufferBinding binding = {0};
  GPUDynamicStateApplyInfo dynamicState = {0};
  uint32_t pushValue = 0xaabbccddu;
  uint8_t pushBefore[16];

  fakeQueue._device = device;
  fakeCmdb._queue   = &fakeQueue;
  pass._cmdb        = &fakeCmdb;

  fakeBufferStorage.sizeBytes = 128u;
  fakeBufferStorage.usage = GPU_BUFFER_USAGE_VERTEX |
                            GPU_BUFFER_USAGE_INDEX |
                            GPU_BUFFER_USAGE_INDIRECT;

  GPUBindRenderPipeline(NULL, NULL);
  GPUBindVertexBuffers(NULL, 0u, 0u, NULL);
  GPUBindIndexBuffer(NULL, fakeBuffer, 0u, GPU_INDEX_TYPE_UINT16);
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

  GPUBindIndexBuffer(&pass, fakeBuffer, 0u, GPU_INDEX_TYPE_UINT16);
  if (!pass._hasIndexBuffer ||
      pass._indexBuffer != fakeBuffer ||
      pass._indexType != GPU_INDEX_TYPE_UINT16) {
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
  GPUBindIndexBuffer(&endedPass, fakeBuffer, 0u, GPU_INDEX_TYPE_UINT16);
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
gpu_test_render(GPUDevice *device, const char *mrtBytecodePath) {
  return check_render_pass_validation() &&
         check_render_encoder_validation(device) &&
         check_render_pipeline_validation(device, mrtBytecodePath) &&
         gpu_test_metal_vertex_slots(device, mrtBytecodePath) &&
         check_render_draw_validation_calls(device) &&
         check_vertex_buffer_shadowing_calls(device) &&
         check_render_push_constant_shadowing_calls(device) &&
         check_dynamic_state_validation_calls(device) &&
         check_render_readback(device, mrtBytecodePath) &&
         gpu_test_texture_view_render(device) &&
         gpu_test_texture_integer_clear(device) &&
         gpu_test_texture_view_depth(device) &&
         gpu_test_texture_view_depth_stencil(device);
}
