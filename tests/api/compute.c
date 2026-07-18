#include "test.h"
#include "../../src/api/buffer_internal.h"
#include "../../src/api/cmdqueue_internal.h"
#include "../../src/api/compute_internal.h"
#include "../../src/api/device_internal.h"

typedef enum ComputeReadbackMode {
  COMPUTE_READBACK_DIRECT = 0,
  COMPUTE_READBACK_INDIRECT,
  COMPUTE_READBACK_MULTI_INDIRECT
} ComputeReadbackMode;

static int
create_compute_usl_library(GPUDevice        *device,
                           const char       *bytecodePath,
                           GPUShaderLibrary **outLibrary) {
  uint64_t bytecodeSize;
  void    *bytecode;
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
check_compute_disk_cache(GPUDevice                   *device,
                         GPUComputePipelineCreateInfo *info) {
  GPUPipelineCacheCreateInfo cacheInfo = {0};
  GPUComputePipeline       *pipeline;
  GPUPipelineCache         *cache;
  GPUApi                   *api;
  char                      path[160];
  char                      metadataPath[168];
  char                      metadataTemporaryPath[176];
  char                      temporaryPath[168];
  FILE                     *file;
  long                      fileSize;
  int                       ok;

  api = gpuDeviceApi(device);
  if (!api) {
    return 0;
  }
  if (api->backend != GPU_BACKEND_METAL &&
      api->backend != GPU_BACKEND_VULKAN &&
      api->backend != GPU_BACKEND_DX12) {
    return 1;
  }

  snprintf(path,
           sizeof(path),
           ".gpu-api-compute-cache-%u-%p.bin",
           (uint32_t)api->backend,
           (void *)device);
  snprintf(temporaryPath, sizeof(temporaryPath), "%s.tmp", path);
  snprintf(metadataPath, sizeof(metadataPath), "%s.meta", path);
  snprintf(metadataTemporaryPath,
           sizeof(metadataTemporaryPath),
           "%s.meta.tmp",
           path);
  remove(path);
  remove(temporaryPath);
  remove(metadataPath);
  remove(metadataTemporaryPath);
  cacheInfo.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  cacheInfo.chain.structSize = sizeof(cacheInfo);
  cacheInfo.label            = "api-compute-disk-cache";
  cacheInfo.enableDiskCache  = true;
  cacheInfo.cachePath        = path;
  cache                      = NULL;
  if (GPUCreatePipelineCache(device, &cacheInfo, &cache) != GPU_OK || !cache) {
    fprintf(stderr, "failed to create native compute disk cache\n");
    return 0;
  }

  ok          = 0;
  pipeline    = NULL;
  info->cache = cache;
  if (GPUCreateComputePipeline(device, info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "native cached compute pipeline create failed\n");
    goto cleanup;
  }
  GPUDestroyComputePipeline(pipeline);
  pipeline    = NULL;
  info->cache = NULL;
  GPUDestroyPipelineCache(cache);
  cache = NULL;

  file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "native compute pipeline cache file was not written\n");
    goto cleanup;
  }
  fseek(file, 0, SEEK_END);
  fileSize = ftell(file);
  fclose(file);
  if (fileSize <= 0) {
    fprintf(stderr, "native compute pipeline cache file is empty\n");
    goto cleanup;
  }

  if (GPUCreatePipelineCache(device, &cacheInfo, &cache) != GPU_OK || !cache) {
    fprintf(stderr, "native compute pipeline disk cache reopen failed\n");
    goto cleanup;
  }
  info->cache = cache;
  if (GPUCreateComputePipeline(device, info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "native compute pipeline create from reopened cache failed\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  GPUDestroyComputePipeline(pipeline);
  info->cache = NULL;
  GPUDestroyPipelineCache(cache);
  remove(path);
  remove(temporaryPath);
  remove(metadataPath);
  remove(metadataTemporaryPath);
  return ok;
}

static uint32_t gComputeDispatchCalls;
static uint32_t gComputeDispatchIndirectCalls;
static uint32_t gComputeMultiDispatchIndirectCalls;
static uint32_t gComputePushConstantCalls;

static void
count_dispatch(GPUComputePassEncoder *enc,
               uint32_t x,
               uint32_t y,
               uint32_t z) {
  (void)enc;
  (void)x;
  (void)y;
  (void)z;
  gComputeDispatchCalls++;
}

static void
count_dispatch_indirect(GPUComputePassEncoder *enc,
                        GPUBuffer *argsBuffer,
                        uint64_t argsOffset) {
  (void)enc;
  (void)argsBuffer;
  (void)argsOffset;
  gComputeDispatchIndirectCalls++;
}

static bool
count_multi_dispatch_indirect(GPUComputePassEncoder *enc,
                              GPUBuffer *argsBuffer,
                              uint64_t argsOffset,
                              uint32_t dispatchCount,
                              uint32_t strideBytes) {
  (void)enc;
  (void)argsBuffer;
  (void)argsOffset;
  (void)dispatchCount;
  (void)strideBytes;
  gComputeMultiDispatchIndirectCalls++;
  return true;
}

static void
count_compute_push_constants(GPUComputePassEncoder *enc,
                             const void            *data,
                             uint32_t               sizeBytes) {
  (void)enc;
  (void)data;
  (void)sizeBytes;
  gComputePushConstantCalls++;
}

static int
check_compute_push_constant_shadowing_calls(GPUDevice *activeDevice) {
  GPUApi *api;
  void (*oldPushConstants)(GPUComputePassEncoder *,
                           const void *,
                           uint32_t);
  GPUDevice             device = {0};
  GPUQueue              queue  = {0};
  GPUCommandBuffer      cmdb   = {0};
  GPUComputePassEncoder pass   = {0};
  uint32_t              value;
  int                   ok;

  api = gpuDeviceApi(activeDevice);
  if (!api) {
    fprintf(stderr, "compute push constant shadowing has no device api\n");
    return 0;
  }

  oldPushConstants                  = api->compute.pushConstants;
  api->compute.pushConstants        = count_compute_push_constants;
  device._api                       = api;
  device.runtimeConfig.enableStats = true;
  queue._device                     = &device;
  cmdb._queue                       = &queue;
  pass._cmdb                        = &cmdb;
  pass._stats                       = &device.currentFrameStats;
  pass._hasPipeline                 = true;
  pass._pushConstantSizeBytes       = 16u;
  pass._pushConstantStages          = GPU_SHADER_STAGE_COMPUTE_BIT;
  value                             = 0u;
  gComputePushConstantCalls         = 0u;
  ok                                = 0;

  GPUSetComputePushConstants(&pass, 0u, sizeof(value), &value);
  GPUSetComputePushConstants(&pass, 0u, sizeof(value), &value);
  if (gComputePushConstantCalls != 1u ||
      device.currentFrameStats.requestedStateCalls != 2u ||
      device.currentFrameStats.emittedStateCalls != 1u) {
    fprintf(stderr, "compute push constant first-zero shadowing failed\n");
    goto cleanup;
  }

  GPUResetStats(&device);
  gComputePushConstantCalls = 0u;
  for (uint32_t i = 1u; i <= 16u; i++) {
    value = i;
    GPUSetComputePushConstants(&pass, (i & 1u) * 4u, sizeof(value), &value);
    GPUSetComputePushConstants(&pass, (i & 1u) * 4u, sizeof(value), &value);
  }
  if (gComputePushConstantCalls != 16u ||
      device.currentFrameStats.requestedStateCalls != 32u ||
      device.currentFrameStats.emittedStateCalls != 16u ||
      device.currentFrameStats.hotPathAllocCount != 0u ||
      device.currentFrameStats.hotPathFreeCount != 0u) {
    fprintf(stderr, "compute push constant shadowing warm path mismatch\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  api->compute.pushConstants = oldPushConstants;
  return ok;
}

static int
check_compute_pipeline_validation(GPUDevice *device,
                                  const char *bytecodePath) {
  GPUShaderLibrary *library = NULL;
  GPUPipelineLayoutCreateInfo layoutInfo = {0};
  GPUPipelineLayout *pipelineLayout = NULL;
  GPUPipelineCacheCreateInfo cacheInfo = {0};
  GPUPipelineCache *cache = NULL;
  GPUComputePipelineCreateInfo info = {0};
  GPUComputePipeline *pipeline;
  GPUCacheStats stats;

  if (!create_compute_usl_library(device, bytecodePath, &library)) {
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
  if (!expect_compute_pipeline_error(device,
                                     &info,
                                     "compute pipeline create accepted null layout")) {
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label = "api-compute-empty-layout";
  if (GPUCreatePipelineLayout(device, &layoutInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "failed to create compute validation pipeline layout\n");
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.layout = pipelineLayout;
  pipeline = NULL;
  if (GPUCreateComputePipeline(device, &info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "compute pipeline create rejected valid pipeline\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  GPUDestroyComputePipeline(pipeline);
  pipeline = NULL;

  if (!check_compute_disk_cache(device, &info)) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  cacheInfo.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  cacheInfo.chain.structSize = sizeof(cacheInfo);
  cacheInfo.label            = "api-compute-cache";
  cacheInfo.maxEntries       = 2u;
  GPUResetStats(device);
  if (GPUCreatePipelineCache(device, &cacheInfo, &cache) != GPU_OK || !cache) {
    fprintf(stderr, "failed to create compute pipeline cache\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  info.cache = cache;
  if (GPUCreateComputePipeline(device, &info, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "compute pipeline cache miss create failed\n");
    goto cache_fail;
  }
  GPUDestroyComputePipeline(pipeline);
  pipeline = NULL;
  if (GPUCreateComputePipeline(device, &info, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "compute pipeline cache hit failed\n");
    goto cache_fail;
  }
  GPUDestroyComputePipeline(pipeline);
  pipeline = NULL;
  if (GPUGetCacheStats(device, &stats) != GPU_OK ||
      stats.pipelineCompiles != 1u ||
      stats.pipelineMisses != 1u ||
      stats.pipelineHits != 1u) {
    fprintf(stderr, "compute pipeline cache stats mismatch\n");
    goto cache_fail;
  }

  info.cache = NULL;
  GPUDestroyPipelineCache(cache);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyShaderLibrary(library);
  return 1;

cache_fail:
  info.cache = NULL;
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyPipelineCache(cache);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyShaderLibrary(library);
  return 0;
}

static int
check_compute_dispatch_validation_calls(GPUDevice *device) {
  GPUApi *api;
  void (*oldDispatch)(GPUComputePassEncoder *, uint32_t, uint32_t, uint32_t);
  void (*oldDispatchIndirect)(GPUComputePassEncoder *, GPUBuffer *, uint64_t);
  bool (*oldMultiDispatchIndirect)(GPUComputePassEncoder *,
                                   GPUBuffer *,
                                   uint64_t,
                                   uint32_t,
                                   uint32_t);
  GPUBindGroupLayoutEntry entry = {0};
  GPUBindGroupLayoutCreateInfo layoutInfo = {0};
  GPUBindGroupLayout *layout = NULL;
  GPUBindGroupLayout *layouts[1];
  GPUPipelineLayoutCreateInfo pipelineInfo = {0};
  GPUPipelineLayout *pipelineLayout = NULL;
  GPUComputePassEncoder pass = {0};
  GPUBuffer indirectBuffer = {0};
  GPUBuffer wrongUsageBuffer = {0};
  GPUValidationMode savedValidationMode;
  int ok = 0;

  api = gpuDeviceApi(device);
  if (!api) {
    fprintf(stderr, "compute dispatch validation has no device api\n");
    return 0;
  }

  entry.binding = 0u;
  entry.bindingType = GPU_BINDING_STORAGE_BUFFER;
  entry.visibility = GPU_SHADER_STAGE_COMPUTE_BIT;
  entry.arrayCount = 1u;
  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label = "dispatch-validation-layout";
  layoutInfo.entryCount = 1u;
  layoutInfo.pEntries = &entry;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK || !layout) {
    fprintf(stderr, "compute dispatch validation layout setup failed\n");
    return 0;
  }

  layouts[0] = layout;
  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.bindGroupLayoutCount = 1u;
  pipelineInfo.ppBindGroupLayouts = layouts;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "compute dispatch validation pipeline layout setup failed\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  oldDispatch = api->compute.dispatch;
  oldDispatchIndirect = api->compute.dispatchIndirect;
  oldMultiDispatchIndirect = api->compute.multiDispatchIndirect;
  api->compute.dispatch = count_dispatch;
  api->compute.dispatchIndirect = count_dispatch_indirect;
  api->compute.multiDispatchIndirect = count_multi_dispatch_indirect;
  gComputeDispatchCalls = 0u;
  gComputeDispatchIndirectCalls = 0u;
  gComputeMultiDispatchIndirectCalls = 0u;

  indirectBuffer.sizeBytes = 64u;
  indirectBuffer.usage = GPU_BUFFER_USAGE_INDIRECT;
  wrongUsageBuffer.sizeBytes = 64u;
  wrongUsageBuffer.usage = GPU_BUFFER_USAGE_STORAGE;

  pass._hasPipeline = true;
  pass._pipelineLayout = pipelineLayout;
  savedValidationMode = device->runtimeConfig.validationMode;

  device->runtimeConfig.validationMode = GPU_VALIDATION_OFF;
  pass._requiredBindGroupMask = 1u;
  GPUDispatch(&pass, 1u, 1u, 1u);
  if (gComputeDispatchCalls != 1u) {
    fprintf(stderr, "compute dispatch runtime-off fast path failed\n");
    goto cleanup;
  }
  gComputeDispatchCalls = 0u;
  device->runtimeConfig.validationMode = GPU_VALIDATION_BASIC;

  pass._requiredBindGroupMask = 0u;
  GPUDispatch(&pass, 1u, 1u, 1u);
  GPUDispatchIndirect(&pass, &indirectBuffer, 0u);
  if (gComputeDispatchCalls != 1u ||
      gComputeDispatchIndirectCalls != 1u) {
    fprintf(stderr, "compute dispatch validation rejected no-bind pipeline\n");
    goto cleanup;
  }
  gComputeDispatchCalls = 0u;
  gComputeDispatchIndirectCalls = 0u;

  pass._requiredBindGroupMask = 1u;
  GPUDispatch(&pass, 1u, 1u, 1u);
  GPUDispatchIndirect(&pass, &indirectBuffer, 0u);
  GPUMultiDispatchIndirect(&pass, &indirectBuffer, 0u, 2u, 12u);
#if GPU_BUILD_WITH_VALIDATION
  if (gComputeDispatchCalls != 0u ||
      gComputeDispatchIndirectCalls != 0u ||
      gComputeMultiDispatchIndirectCalls != 0u) {
    fprintf(stderr, "compute dispatch validation called backend without required bind group\n");
    goto cleanup;
  }
#else
  if (gComputeDispatchCalls != 1u ||
      gComputeDispatchIndirectCalls != 1u ||
      gComputeMultiDispatchIndirectCalls != 1u) {
    fprintf(stderr, "compiled-out compute validation did not reach backend\n");
    goto cleanup;
  }
#endif
  gComputeDispatchCalls = 0u;
  gComputeDispatchIndirectCalls = 0u;
  gComputeMultiDispatchIndirectCalls = 0u;

  pass._boundGroupLayouts[0] = layout;
  GPUDispatch(&pass, 0u, 1u, 1u);
  GPUDispatch(&pass, 1u, 0u, 1u);
  GPUDispatch(&pass, 1u, 1u, 0u);
  GPUDispatchIndirect(&pass, &wrongUsageBuffer, 0u);
  GPUDispatchIndirect(&pass, &indirectBuffer, 2u);
  GPUDispatchIndirect(&pass, &indirectBuffer, 60u);
  GPUMultiDispatchIndirect(&pass, &indirectBuffer, UINT64_MAX, 2u, 12u);
  GPUMultiDispatchIndirect(&pass, &indirectBuffer, 0u, 0u, 12u);
  GPUMultiDispatchIndirect(&pass, &indirectBuffer, 2u, 2u, 12u);
  GPUMultiDispatchIndirect(&pass, &indirectBuffer, 0u, 2u, 8u);
  GPUMultiDispatchIndirect(&pass, &indirectBuffer, 0u, 2u, 14u);
  GPUMultiDispatchIndirect(&pass, &indirectBuffer, 48u, 2u, 12u);
  if (gComputeDispatchCalls != 0u ||
      gComputeDispatchIndirectCalls != 0u ||
      gComputeMultiDispatchIndirectCalls != 0u) {
    fprintf(stderr, "compute dispatch validation called backend for invalid dispatch\n");
    goto cleanup;
  }

  GPUDispatch(&pass, 1u, 1u, 1u);
  GPUDispatchIndirect(&pass, &indirectBuffer, 0u);
  GPUMultiDispatchIndirect(&pass, &indirectBuffer, 0u, 2u, 12u);
  if (gComputeDispatchCalls != 1u ||
      gComputeDispatchIndirectCalls != 1u ||
      gComputeMultiDispatchIndirectCalls != 1u) {
    fprintf(stderr, "compute dispatch validation rejected valid dispatch\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  device->runtimeConfig.validationMode = savedValidationMode;
  api->compute.dispatch = oldDispatch;
  api->compute.dispatchIndirect = oldDispatchIndirect;
  api->compute.multiDispatchIndirect = oldMultiDispatchIndirect;
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(layout);
  return ok;
}

static int
check_compute_pass_validation(GPUDevice *device) {
  GPUQueue        fakeQueue = {0};
  GPUCommandBuffer fakeCmdb = {0};
  GPUComputePassEncoder fakePass = {0};
  GPUComputePipeline fakePipeline = {0};
  GPUBuffer fakeBufferStorage = {0};
  GPUBuffer *fakeBuffer = &fakeBufferStorage;
  uint32_t dynamicOffset = 0u;
  uint32_t pushValue = 0x11223344u;
  uint8_t pushBefore[16];

  fakeQueue._device = device;
  fakeCmdb._queue = &fakeQueue;
  fakePass._cmdb = &fakeCmdb;
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
check_compute_readback_case(GPUDevice          *device,
                            ComputeReadbackMode  mode,
                            const char          *bytecodePath) {
  typedef struct DispatchArgs {
    uint32_t x, y, z;
  } DispatchArgs;

  static const DispatchArgs kDispatchArgs = {4u, 1u, 1u};
  static const DispatchArgs kMultiDispatchArgs[2] = {
    {2u, 1u, 1u},
    {4u, 1u, 1u}
  };
  static const uint32_t kZeroWords[4] = {0u, 0u, 0u, 0u};
  const uint32_t *expectedWords;
  const uint32_t expectedSingle[4] = {1u, 2u, 3u, 4u};
  const uint32_t expectedMulti[4] = {1u, 2u, 3u, 4u};
  const int indirect = mode != COMPUTE_READBACK_DIRECT;
  const int multi = mode == COMPUTE_READBACK_MULTI_INDIRECT;
  const char *label = multi ? "api-compute-multi-indirect"
                            : (indirect ? "api-compute-indirect"
                                        : "api-compute-direct");
  GPUQueue        *queue;
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

  expectedWords = multi ? expectedMulti : expectedSingle;
  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for %s test\n", label);
    return 0;
  }
  if (!create_compute_usl_library(device, bytecodePath, &library)) {
    fprintf(stderr, "failed to create %s library\n", label);
    goto cleanup;
  }

  layoutEntry.binding = 0u;
  layoutEntry.bindingType = GPU_BINDING_STORAGE_BUFFER;
  layoutEntry.visibility = GPU_SHADER_STAGE_COMPUTE_BIT;
  layoutEntry.arrayCount = 1u;
  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label = label;
  layoutInfo.entryCount = 1u;
  layoutInfo.pEntries = &layoutEntry;
  if (GPUCreateBindGroupLayout(device,
                               &layoutInfo,
                               &bindGroupLayout) != GPU_OK ||
      !bindGroupLayout) {
    fprintf(stderr, "failed to create %s bind group layout\n", label);
    goto cleanup;
  }

  layouts[0] = bindGroupLayout;
  pipelineLayoutInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.chain.structSize = sizeof(pipelineLayoutInfo);
  pipelineLayoutInfo.label = label;
  pipelineLayoutInfo.bindGroupLayoutCount = 1u;
  pipelineLayoutInfo.ppBindGroupLayouts = layouts;
  if (GPUCreatePipelineLayout(device,
                              &pipelineLayoutInfo,
                              &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "failed to create %s pipeline layout\n", label);
    goto cleanup;
  }

  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label = label;
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.library = library;
  pipelineInfo.entryPoint = "api_indirect_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "failed to create %s pipeline\n", label);
    goto cleanup;
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label = label;
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
    fprintf(stderr, "failed to create %s output buffer\n", label);
    goto cleanup;
  }

  if (indirect) {
    bufferInfo.label = label;
    bufferInfo.sizeBytes = multi ? sizeof(kMultiDispatchArgs) :
                                   sizeof(kDispatchArgs);
    bufferInfo.usage = GPU_BUFFER_USAGE_INDIRECT | GPU_BUFFER_USAGE_COPY_DST;
    if (GPUCreateBuffer(device, &bufferInfo, &argsBuffer) != GPU_OK ||
        !argsBuffer ||
        GPUQueueWriteBuffer(queue,
                            argsBuffer,
                            0u,
                            multi ? (const void *)kMultiDispatchArgs :
                                    (const void *)&kDispatchArgs,
                            bufferInfo.sizeBytes) != GPU_OK) {
      fprintf(stderr, "failed to create %s args buffer\n", label);
      goto cleanup;
    }
  }

  groupEntry.binding = 0u;
  groupEntry.bindingType = GPU_BINDING_STORAGE_BUFFER;
  groupEntry.buffer.buffer = outputBuffer;
  groupEntry.buffer.offset = 0u;
  groupEntry.buffer.size = sizeof(readWords);
  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label = label;
  groupInfo.layout = bindGroupLayout;
  groupInfo.entryCount = 1u;
  groupInfo.pEntries = &groupEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &bindGroup) != GPU_OK ||
      !bindGroup) {
    fprintf(stderr, "failed to create %s bind group\n", label);
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, label, &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "failed to acquire %s command buffer\n", label);
    goto cleanup;
  }

  computePass = GPUBeginComputePass(cmdb, label);
  if (!computePass) {
    fprintf(stderr, "failed to begin %s pass\n", label);
    goto cleanup;
  }
  if (!indirect) {
    GPUResetStats(device);
  }
  GPUBindComputePipeline(computePass, pipeline);
  GPUBindComputePipeline(computePass, pipeline);
  GPUBindComputeGroup(computePass, 0u, bindGroup, 0u, NULL);
  GPUBindComputeGroup(computePass, 0u, bindGroup, 0u, NULL);
  if (!indirect &&
      (device->currentFrameStats.requestedBindCalls != 4u ||
       device->currentFrameStats.emittedBindCalls != 2u)) {
    fprintf(stderr,
            "compute bind shadowing mismatch: %u requested, %u emitted\n",
            device->currentFrameStats.requestedBindCalls,
            device->currentFrameStats.emittedBindCalls);
    goto cleanup;
  }
  if (!indirect) {
    GPUDispatch(computePass, 4u, 1u, 1u);
  } else if (multi) {
    GPUMultiDispatchIndirect(computePass,
                             argsBuffer,
                             0u,
                             2u,
                             (uint32_t)sizeof(DispatchArgs));
  } else {
    GPUDispatchIndirect(computePass, argsBuffer, 0u);
  }
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
                         outputBuffer,
                         0u,
                         readWords,
                         sizeof(readWords)) != GPU_OK ||
      memcmp(readWords, expectedWords, sizeof(readWords)) != 0) {
    fprintf(stderr,
            "%s readback mismatch: %u %u %u %u\n",
            label,
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

static int
check_compute_readback(GPUDevice *device, const char *bytecodePath) {
  return check_compute_readback_case(device,
                                     COMPUTE_READBACK_DIRECT,
                                     bytecodePath) &&
         check_compute_readback_case(device,
                                     COMPUTE_READBACK_INDIRECT,
                                     bytecodePath) &&
         check_compute_readback_case(device,
                                     COMPUTE_READBACK_MULTI_INDIRECT,
                                     bytecodePath);
}

int
gpu_test_compute(GPUDevice *device, const char *bytecodePath) {
  GPURuntimeConfig config;
  GPURuntimeConfig savedConfig;
  int              ok;

  if (!device) {
    return 0;
  }

  savedConfig            = device->runtimeConfig;
  config                 = savedConfig;
  config.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  config.chain.structSize = sizeof(config);
  config.enableStats      = true;
  if (GPUConfigureRuntime(device, &config) != GPU_OK) {
    return 0;
  }

  ok = check_compute_pass_validation(device) &&
       check_compute_pipeline_validation(device, bytecodePath) &&
       check_compute_dispatch_validation_calls(device) &&
       check_compute_push_constant_shadowing_calls(device) &&
       check_compute_readback(device, bytecodePath);
  device->runtimeConfig = savedConfig;
  return ok;
}
