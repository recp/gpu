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

#include "api/device_internal.h"
#include "bench.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  BINDLESS_DISPATCH_ARRAY_SIZE         = 16,
  BINDLESS_DISPATCH_DEFAULT_DISPATCHES = 1024,
  BINDLESS_DISPATCH_DEFAULT_REPEATS    = 7,
  BINDLESS_DISPATCH_MAX_DISPATCHES     = 8192,
  BINDLESS_DISPATCH_MAX_REPEATS        = 31,
  BINDLESS_DISPATCH_WARMUP_RUNS        = 3
};

typedef enum BindlessDispatchPath {
  BINDLESS_DISPATCH_STABLE = 0,
  BINDLESS_DISPATCH_CHURN,
  BINDLESS_DISPATCH_PATH_COUNT
} BindlessDispatchPath;

typedef enum BindlessDispatchInitResult {
  BINDLESS_DISPATCH_INIT_FAILED = 0,
  BINDLESS_DISPATCH_INIT_READY,
  BINDLESS_DISPATCH_INIT_UNSUPPORTED
} BindlessDispatchInitResult;

typedef struct BindlessDispatchConfig {
  const char *artifactPath;
  GPUBackend  backend;
  uint32_t    dispatchCount;
  uint32_t    repeats;
} BindlessDispatchConfig;

typedef struct BindlessDispatchBench {
  GPUInstance        *instance;
  GPUAdapter         *adapter;
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *shaderLayout;
  GPUBindGroupLayout *bindlessLayout;
  GPUPipelineLayout  *pipelineLayout;
  GPUComputePipeline *pipeline;
  GPUBindGroup       *groups[2];
  GPUTexture         *textures[2];
  GPUTextureView     *views[2];
  GPUSampler         *samplers[2];
  GPUBuffer          *selectionBuffer;
  GPUBuffer          *outputBuffer;
  GPUFence           *fence;
  void               *artifact;
} BindlessDispatchBench;

static bool
bindless_parseConfig(int argc,
                     char *argv[],
                     BindlessDispatchConfig *config) {
  if (!config || argc < 2 || argc > 5) {
    if (argv && argv[0]) {
      fprintf(stderr,
              "usage: %s <artifact.us> [default|metal|vulkan|dx12] "
              "[dispatches 1..8192] [repeats 1..31]\n",
              argv[0]);
    }
    return false;
  }

  memset(config, 0, sizeof(*config));
  config->artifactPath  = argv[1];
  config->backend       = GPU_BACKEND_DEFAULT;
  config->dispatchCount = BINDLESS_DISPATCH_DEFAULT_DISPATCHES;
  config->repeats       = BINDLESS_DISPATCH_DEFAULT_REPEATS;
  if ((argc > 2 && !bench_parseBackend(argv[2], &config->backend)) ||
      (argc > 3 && !bench_parseU32(argv[3], 1u, &config->dispatchCount)) ||
      (argc > 4 && !bench_parseU32(argv[4], 1u, &config->repeats)) ||
      config->dispatchCount > BINDLESS_DISPATCH_MAX_DISPATCHES ||
      config->repeats > BINDLESS_DISPATCH_MAX_REPEATS) {
    fprintf(stderr, "invalid bindless-dispatch benchmark arguments\n");
    return false;
  }
  return true;
}

static GPUAdapter *
bindless_selectAdapter(GPUInstance *instance) {
  GPUAdapter *adapter;
  GPUResult   result;
  uint32_t    count;

  adapter = NULL;
  count   = 1u;
  result  = GPUEnumerateAdapters(instance, &count, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    return NULL;
  }
  return adapter;
}

static bool
bindless_createTexture(BindlessDispatchBench *bench,
                       uint32_t               index,
                       const uint8_t          color[4]) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTextureWriteRegion    writeRegion = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "bindless-dispatch-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 1u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(bench->device,
                       &textureInfo,
                       &bench->textures[index]) != GPU_OK ||
      !bench->textures[index]) {
    return false;
  }

  writeRegion.width        = 1u;
  writeRegion.height       = 1u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = 4u;
  writeRegion.rowsPerImage = 1u;
  if (GPUQueueWriteTexture(bench->queue,
                           bench->textures[index],
                           &writeRegion,
                           color,
                           4u) != GPU_OK) {
    return false;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "bindless-dispatch-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  return GPUCreateTextureView(bench->textures[index],
                              &viewInfo,
                              &bench->views[index]) == GPU_OK &&
         bench->views[index];
}

static bool
bindless_createShader(BindlessDispatchBench        *bench,
                      const BindlessDispatchConfig *config) {
  const GPUBindGroupLayoutEntry *entries;
  GPUBindlessLayoutEXT           bindlessInfo       = {0};
  GPUBindGroupLayoutCreateInfo   layoutInfo         = {0};
  GPUPipelineLayoutCreateInfo    pipelineLayoutInfo = {0};
  GPUComputePipelineCreateInfo   pipelineInfo       = {0};
  uint64_t                       artifactSize;
  uint32_t                       entryCount;

  bench->artifact = bench_read(config->artifactPath, &artifactSize);
  if (!bench->artifact || artifactSize == 0u ||
      GPUCreateShaderLibraryFromUSL(bench->device,
                                    bench->artifact,
                                    artifactSize,
                                    &bench->library) != GPU_OK ||
      !bench->library ||
      GPUCreateShaderLayout(bench->device,
                            bench->library,
                            &bench->shaderLayout) != GPU_OK ||
      !bench->shaderLayout ||
      bench->shaderLayout->bindGroupLayoutCount != 1u ||
      !bench->shaderLayout->bindGroupLayouts ||
      !bench->shaderLayout->bindGroupLayouts[0]) {
    return false;
  }

  entries = GPUGetBindGroupLayoutEntries(
    bench->shaderLayout->bindGroupLayouts[0],
    &entryCount
  );
  if (!entries || entryCount != 4u ||
      entries[0].binding != 0u ||
      entries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      entries[0].arrayCount != BINDLESS_DISPATCH_ARRAY_SIZE ||
      entries[1].binding != 1u ||
      entries[1].bindingType != GPU_BINDING_SAMPLER ||
      entries[1].arrayCount != BINDLESS_DISPATCH_ARRAY_SIZE ||
      entries[2].binding != 2u ||
      entries[2].bindingType != GPU_BINDING_UNIFORM_BUFFER ||
      entries[3].binding != 3u ||
      entries[3].bindingType != GPU_BINDING_STORAGE_BUFFER) {
    return false;
  }

  bindlessInfo.chain.sType      = GPU_STRUCTURE_TYPE_BINDLESS_LAYOUT_EXT;
  bindlessInfo.chain.structSize = sizeof(bindlessInfo);
  bindlessInfo.sourceLayout     = bench->shaderLayout->bindGroupLayouts[0];
  layoutInfo.chain.sType        = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize   = sizeof(layoutInfo);
  layoutInfo.chain.pNext        = &bindlessInfo;
  layoutInfo.label              = "bindless-dispatch-layout";
  if (GPUCreateBindGroupLayout(bench->device,
                               &layoutInfo,
                               &bench->bindlessLayout) != GPU_OK ||
      !bench->bindlessLayout) {
    return false;
  }

  pipelineLayoutInfo.chain.sType =
    GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.chain.structSize     = sizeof(pipelineLayoutInfo);
  pipelineLayoutInfo.label                = "bindless-dispatch-layout";
  pipelineLayoutInfo.bindGroupLayoutCount = 1u;
  pipelineLayoutInfo.ppBindGroupLayouts   = &bench->bindlessLayout;
  if (GPUCreatePipelineLayout(bench->device,
                              &pipelineLayoutInfo,
                              &bench->pipelineLayout) != GPU_OK ||
      !bench->pipelineLayout) {
    return false;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "bindless-dispatch";
  pipelineInfo.layout           = bench->pipelineLayout;
  pipelineInfo.library          = bench->library;
  pipelineInfo.entryPoint       = "bindless_dispatch";
  return GPUCreateComputePipeline(bench->device,
                                  &pipelineInfo,
                                  &bench->pipeline) == GPU_OK &&
         bench->pipeline;
}

static bool
bindless_createResources(BindlessDispatchBench *bench) {
  static const uint8_t colors[2][4] = {
    {255u, 0u, 0u, 255u},
    {0u, 255u, 0u, 255u}
  };
  uint32_t             selection[64] = {0u};
  float                output[4]     = {0.0f};
  GPUSamplerCreateInfo samplerInfo   = {0};
  GPUBufferCreateInfo  bufferInfo    = {0};

  for (uint32_t i = 0u; i < 2u; i++) {
    if (!bindless_createTexture(bench, i, colors[i])) {
      return false;
    }
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "bindless-dispatch-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  for (uint32_t i = 0u; i < 2u; i++) {
    if (GPUCreateSampler(bench->device,
                         &samplerInfo,
                         false,
                         &bench->samplers[i]) != GPU_OK ||
        !bench->samplers[i]) {
      return false;
    }
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "bindless-dispatch-selection";
  bufferInfo.sizeBytes        = sizeof(selection);
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(bench->device,
                      &bufferInfo,
                      &bench->selectionBuffer) != GPU_OK ||
      !bench->selectionBuffer ||
      GPUQueueWriteBuffer(bench->queue,
                          bench->selectionBuffer,
                          0u,
                          selection,
                          sizeof(selection)) != GPU_OK) {
    return false;
  }

  bufferInfo.label     = "bindless-dispatch-output";
  bufferInfo.sizeBytes = sizeof(output);
  bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                         GPU_BUFFER_USAGE_COPY_SRC |
                         GPU_BUFFER_USAGE_COPY_DST;
  return GPUCreateBuffer(bench->device,
                         &bufferInfo,
                         &bench->outputBuffer) == GPU_OK &&
         bench->outputBuffer &&
         GPUQueueWriteBuffer(bench->queue,
                             bench->outputBuffer,
                             0u,
                             output,
                             sizeof(output)) == GPU_OK;
}

static bool
bindless_createGroups(BindlessDispatchBench *bench) {
  GPUBindGroupCreateInfo groupInfo = {0};

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "bindless-dispatch-group";
  groupInfo.layout           = bench->bindlessLayout;
  for (uint32_t i = 0u; i < 2u; i++) {
    GPUBindGroupEntry entries[4] = {{0}};

    if (GPUCreateBindGroup(bench->device,
                           &groupInfo,
                           &bench->groups[i]) != GPU_OK ||
        !bench->groups[i]) {
      return false;
    }

    entries[0].binding     = 0u;
    entries[0].arrayIndex  = 0u;
    entries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
    entries[0].textureView = bench->views[i];
    entries[1].binding     = 1u;
    entries[1].arrayIndex  = 0u;
    entries[1].bindingType = GPU_BINDING_SAMPLER;
    entries[1].sampler     = bench->samplers[i];
    entries[2].binding       = 2u;
    entries[2].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
    entries[2].buffer.buffer = bench->selectionBuffer;
    entries[2].buffer.size   = 256u;
    entries[3].binding       = 3u;
    entries[3].bindingType   = GPU_BINDING_STORAGE_BUFFER;
    entries[3].buffer.buffer = bench->outputBuffer;
    entries[3].buffer.size   = 16u;
    if (GPUUpdateBindGroupEXT(bench->groups[i],
                              (uint32_t)GPU_ARRAY_LEN(entries),
                              entries) != GPU_OK) {
      return false;
    }
  }
  return true;
}

static BindlessDispatchInitResult
bindless_init(BindlessDispatchBench        *bench,
              const BindlessDispatchConfig *config,
              GPUAdapterProperties         *properties) {
  GPUFeature            feature      = GPU_FEATURE_BINDLESS;
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUDeviceCreateInfo   deviceInfo   = {0};
  GPURuntimeConfig      runtimeInfo  = {0};

  memset(bench, 0, sizeof(*bench));
  memset(properties, 0, sizeof(*properties));
  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = config->backend;
  if (GPUCreateInstance(&instanceInfo, &bench->instance) != GPU_OK ||
      !bench->instance) {
    return BINDLESS_DISPATCH_INIT_FAILED;
  }

  bench->adapter = bindless_selectAdapter(bench->instance);
  if (!bench->adapter) {
    return BINDLESS_DISPATCH_INIT_FAILED;
  }
  if (!GPUIsFeatureSupported(bench->adapter, GPU_FEATURE_BINDLESS)) {
    return BINDLESS_DISPATCH_INIT_UNSUPPORTED;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.featureCount = 1u;
  deviceInfo.required.pFeatures    = &feature;
  if (GPUCreateDevice(bench->adapter, &deviceInfo, &bench->device) != GPU_OK ||
      !bench->device ||
      !GPUIsFeatureEnabled(bench->device, GPU_FEATURE_BINDLESS) ||
      !GPUGetProcAddr(bench->device, "GPUUpdateBindGroupEXT")) {
    return BINDLESS_DISPATCH_INIT_FAILED;
  }

  bench->queue = GPUGetQueue(bench->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!bench->queue) {
    return BINDLESS_DISPATCH_INIT_FAILED;
  }

  runtimeInfo.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeInfo.chain.structSize = sizeof(runtimeInfo);
  runtimeInfo.validationMode   = GPU_VALIDATION_OFF;
  runtimeInfo.enableStats      = true;
  if (GPUConfigureRuntime(bench->device, &runtimeInfo) != GPU_OK ||
      GPUGetAdapterProperties(bench->adapter, properties) != GPU_OK ||
      !bindless_createShader(bench, config) ||
      !bindless_createResources(bench) ||
      !bindless_createGroups(bench) ||
      GPUCreateFence(bench->device, NULL, &bench->fence) != GPU_OK ||
      !bench->fence) {
    return BINDLESS_DISPATCH_INIT_FAILED;
  }
  return BINDLESS_DISPATCH_INIT_READY;
}

static void
bindless_cleanup(BindlessDispatchBench *bench) {
  if (!bench) {
    return;
  }

  GPUDestroyFence(bench->fence);
  GPUDestroyBindGroup(bench->groups[1]);
  GPUDestroyBindGroup(bench->groups[0]);
  GPUDestroyBuffer(bench->outputBuffer);
  GPUDestroyBuffer(bench->selectionBuffer);
  GPUDestroySampler(bench->samplers[1]);
  GPUDestroySampler(bench->samplers[0]);
  GPUDestroyTextureView(bench->views[1]);
  GPUDestroyTextureView(bench->views[0]);
  GPUDestroyTexture(bench->textures[1]);
  GPUDestroyTexture(bench->textures[0]);
  GPUDestroyComputePipeline(bench->pipeline);
  GPUDestroyPipelineLayout(bench->pipelineLayout);
  GPUDestroyBindGroupLayout(bench->bindlessLayout);
  GPUDestroyShaderLayout(bench->shaderLayout);
  GPUDestroyShaderLibrary(bench->library);
  free(bench->artifact);
  GPUDestroyDevice(bench->device);
  GPUDestroyInstance(bench->instance);
}

static bool
bindless_run(BindlessDispatchBench *bench,
             BindlessDispatchPath   path,
             uint32_t               dispatchCount,
             double                *outNsPerDispatch) {
  GPUCommandBuffer      *cmdb;
  GPUCommandBuffer      *buffers[1];
  GPUComputePassEncoder *pass;
  GPUQueueSubmitInfo     submitInfo = {0};
  double                 begin;
  double                 elapsed;

  cmdb = NULL;
  pass = NULL;
  if (GPUAcquireCommandBuffer(bench->queue,
                              "bindless-dispatch",
                              &cmdb) != GPU_OK ||
      !cmdb ||
      !(pass = GPUBeginComputePass(cmdb, "bindless-dispatch"))) {
    return false;
  }

  GPUBindComputePipeline(pass, bench->pipeline);
  begin = bench_now();
  for (uint32_t i = 0u; i < dispatchCount; i++) {
    GPUBindGroup *group;

    group = path == BINDLESS_DISPATCH_STABLE
              ? bench->groups[0]
              : bench->groups[i & 1u];
    GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
    GPUDispatch(pass, 1u, 1u, 1u);
  }
  elapsed = bench_now() - begin;
  GPUEndComputePass(pass);

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = bench->fence;
  GPUResetFence(bench->fence);
  if (GPUQueueSubmit(bench->queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(bench->fence, UINT64_MAX) != GPU_OK) {
    return false;
  }

  if (outNsPerDispatch) {
    *outNsPerDispatch = elapsed * 1e9 / dispatchCount;
  }
  return true;
}

static bool
bindless_metricsMatch(const BindlessDispatchConfig *config,
                      const GPUFrameStats           *stats) {
  uint64_t expectedRequests;
  uint64_t expectedEmissions;

  expectedRequests = (uint64_t)config->repeats * 2u *
                     (config->dispatchCount + 1u);
  expectedEmissions = (uint64_t)config->repeats *
                      (config->dispatchCount + 3u);
  return stats->requestedBindCalls == expectedRequests &&
         stats->emittedBindCalls == expectedEmissions &&
         stats->hotPathAllocCount == 0u &&
         stats->hotPathAllocBytes == 0u &&
         stats->hotPathFreeCount == 0u &&
         stats->hotPathFreeBytes == 0u;
}

int
main(int argc, char *argv[]) {
  BindlessDispatchConfig     config;
  BindlessDispatchBench      bench;
  BindlessDispatchInitResult initResult;
  GPUAdapterProperties       properties;
  GPUFrameStats              stats;
  double                     samples[BINDLESS_DISPATCH_PATH_COUNT]
                                    [BINDLESS_DISPATCH_MAX_REPEATS];
  double                     median[BINDLESS_DISPATCH_PATH_COUNT];
  double                     p95[BINDLESS_DISPATCH_PATH_COUNT];
  double                     p99[BINDLESS_DISPATCH_PATH_COUNT];
  double                     churnCost;
  bool                       ok;

  memset(&bench, 0, sizeof(bench));
  memset(&properties, 0, sizeof(properties));
  memset(&stats, 0, sizeof(stats));
  memset(samples, 0, sizeof(samples));
  if (!bindless_parseConfig(argc, argv, &config)) {
    return EXIT_FAILURE;
  }

  initResult = bindless_init(&bench, &config, &properties);
  if (initResult == BINDLESS_DISPATCH_INIT_UNSUPPORTED) {
    fprintf(stderr,
            "bindless-dispatch benchmark skipped: feature unsupported\n");
    bindless_cleanup(&bench);
    return EXIT_SUCCESS;
  }
  if (initResult != BINDLESS_DISPATCH_INIT_READY) {
    fprintf(stderr, "failed to initialize bindless-dispatch benchmark\n");
    bindless_cleanup(&bench);
    return EXIT_FAILURE;
  }

  ok = true;
  for (uint32_t i = 0u; ok && i < BINDLESS_DISPATCH_WARMUP_RUNS; i++) {
    ok = bindless_run(&bench,
                      BINDLESS_DISPATCH_STABLE,
                      config.dispatchCount,
                      NULL) &&
         bindless_run(&bench,
                      BINDLESS_DISPATCH_CHURN,
                      config.dispatchCount,
                      NULL);
  }

  GPUResetStats(bench.device);
  for (uint32_t repeat = 0u; ok && repeat < config.repeats; repeat++) {
    if ((repeat & 1u) == 0u) {
      for (uint32_t path = 0u; path < BINDLESS_DISPATCH_PATH_COUNT; path++) {
        ok = bindless_run(&bench,
                          (BindlessDispatchPath)path,
                          config.dispatchCount,
                          &samples[path][repeat]);
        if (!ok) {
          break;
        }
      }
    } else {
      for (uint32_t path = BINDLESS_DISPATCH_PATH_COUNT; path-- > 0u;) {
        ok = bindless_run(&bench,
                          (BindlessDispatchPath)path,
                          config.dispatchCount,
                          &samples[path][repeat]);
        if (!ok) {
          break;
        }
      }
    }
  }

  stats = bench.device->currentFrameStats;
  ok    = ok && bindless_metricsMatch(&config, &stats);
  for (uint32_t path = 0u; path < BINDLESS_DISPATCH_PATH_COUNT; path++) {
    median[path] = bench_percentile(samples[path], config.repeats, 0.50);
    p95[path]    = bench_percentile(samples[path], config.repeats, 0.95);
    p99[path]    = bench_percentile(samples[path], config.repeats, 0.99);
  }
  churnCost = median[BINDLESS_DISPATCH_CHURN] -
              median[BINDLESS_DISPATCH_STABLE];

  if (ok) {
    printf("GPU bindless-dispatch benchmark\n");
    printf("adapter: %s, backend: %s\n",
           properties.name ? properties.name : "unknown",
           bench_backendName(properties.backend));
    printf("dispatches: %u, repeats: %u\n",
           config.dispatchCount,
           config.repeats);
    printf("stable: median %8.2f ns/dispatch, p95 %8.2f, p99 %8.2f\n",
           median[BINDLESS_DISPATCH_STABLE],
           p95[BINDLESS_DISPATCH_STABLE],
           p99[BINDLESS_DISPATCH_STABLE]);
    printf("churn : median %8.2f ns/dispatch, p95 %8.2f, p99 %8.2f\n",
           median[BINDLESS_DISPATCH_CHURN],
           p95[BINDLESS_DISPATCH_CHURN],
           p99[BINDLESS_DISPATCH_CHURN]);
    printf("median bind churn cost: %.2f ns/dispatch\n", churnCost);
    printf("bind requests: %u, emissions: %u\n",
           stats.requestedBindCalls,
           stats.emittedBindCalls);
    printf("warm hot-path allocations: %" PRIu64 ", frees: %" PRIu64 "\n",
           stats.hotPathAllocCount,
           stats.hotPathFreeCount);
  }

  bindless_cleanup(&bench);
  if (!ok) {
    fprintf(stderr,
            "bindless-dispatch benchmark failed: %u requests, %u emissions, "
            "%" PRIu64 " allocations, %" PRIu64 " frees\n",
            stats.requestedBindCalls,
            stats.emittedBindCalls,
            stats.hotPathAllocCount,
            stats.hotPathFreeCount);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
