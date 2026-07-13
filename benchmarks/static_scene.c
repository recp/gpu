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

#include <gpu/gpu.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  STATIC_DEFAULT_DRAWS   = 1000,
  STATIC_DEFAULT_WARMUP  = 300,
  STATIC_DEFAULT_FRAMES  = 3000,
  STATIC_DEFAULT_REPEATS = 5
};

typedef struct StaticConfig {
  const char *artifactPath;
  GPUBackend  backend;
  uint32_t    drawCount;
  uint32_t    warmupFrames;
  uint32_t    measuredFrames;
  uint32_t    repeats;
} StaticConfig;

typedef struct StaticBench {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUCommandQueue   *queue;
  GPUShaderLibrary  *library;
  GPUPipelineLayout *pipelineLayout;
  GPURenderPipeline *pipeline;
  GPUBuffer         *vertexBuffer;
  GPUTexture        *target;
  GPUTextureView    *targetView;
  GPUFence          *fence;
  GPUAdapterProperties adapterProperties;
} StaticBench;

typedef struct StaticMetrics {
  double   *samples;
  double   *repeatMedians;
  uint64_t  requestedStateCalls;
  uint64_t  emittedStateCalls;
  uint64_t  requestedBindCalls;
  uint64_t  emittedBindCalls;
  uint64_t  drawCalls;
  uint64_t  maxAllocCount;
  uint64_t  maxAllocBytes;
  uint64_t  maxFreeCount;
  uint64_t  maxFreeBytes;
  size_t    sampleCount;
} StaticMetrics;

static const float staticVertices[] = {
  -1.0f, -1.0f,
   3.0f, -1.0f,
  -1.0f,  3.0f
};

static const char *
static_backendName(GPUBackend backend) {
  switch (backend) {
    case GPU_BACKEND_METAL:
      return "metal";
    case GPU_BACKEND_VULKAN:
      return "vulkan";
    case GPU_BACKEND_DX12:
      return "dx12";
    case GPU_BACKEND_OPENGL:
      return "opengl";
    default:
      return "default";
  }
}

static int
static_parseBackend(const char *value, GPUBackend *outBackend) {
  if (!value || !outBackend) {
    return 0;
  }
  if (strcmp(value, "default") == 0) {
    *outBackend = GPU_BACKEND_DEFAULT;
  } else if (strcmp(value, "metal") == 0) {
    *outBackend = GPU_BACKEND_METAL;
  } else if (strcmp(value, "vulkan") == 0) {
    *outBackend = GPU_BACKEND_VULKAN;
  } else if (strcmp(value, "dx12") == 0) {
    *outBackend = GPU_BACKEND_DX12;
  } else {
    return 0;
  }
  return 1;
}

static int
static_parseU32(const char *value, uint32_t minimum, uint32_t *outValue) {
  unsigned long parsed;
  char         *end;

  if (!value || !outValue) {
    return 0;
  }
  errno  = 0;
  parsed = strtoul(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' ||
      parsed < minimum || parsed > UINT32_MAX) {
    return 0;
  }
  *outValue = (uint32_t)parsed;
  return 1;
}

static GPUAdapter *
static_selectAdapter(GPUInstance *instance) {
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

static int
static_createLibrary(StaticBench *bench, const char *path) {
  void     *artifact;
  uint64_t  artifactSize;
  GPUResult result;

  artifact = bench_read(path, &artifactSize);
  if (!artifact) {
    fprintf(stderr, "failed to read USL artifact: %s\n", path);
    return 0;
  }
  result = GPUCreateShaderLibraryFromUSL(bench->device,
                                         artifact,
                                         artifactSize,
                                         &bench->library);
  free(artifact);
  if (result != GPU_OK || !bench->library) {
    fprintf(stderr, "failed to create USL shader library\n");
    return 0;
  }
  return 1;
}

static int
static_createResources(StaticBench *bench) {
  GPUPipelineLayoutCreateInfo pipelineLayoutInfo;
  GPURenderPipelineCreateInfo pipelineInfo;
  GPUVertexAttribute          attribute;
  GPUVertexBufferLayout       vertexLayout;
  GPUColorTargetState         colorTarget;
  GPUBufferCreateInfo         bufferInfo;
  GPUTextureCreateInfo        textureInfo;
  GPUTextureViewCreateInfo    viewInfo;

  memset(&pipelineLayoutInfo, 0, sizeof(pipelineLayoutInfo));
  memset(&pipelineInfo, 0, sizeof(pipelineInfo));
  memset(&attribute, 0, sizeof(attribute));
  memset(&vertexLayout, 0, sizeof(vertexLayout));
  memset(&colorTarget, 0, sizeof(colorTarget));
  memset(&bufferInfo, 0, sizeof(bufferInfo));
  memset(&textureInfo, 0, sizeof(textureInfo));
  memset(&viewInfo, 0, sizeof(viewInfo));

  pipelineLayoutInfo.chain.sType =
    GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.chain.structSize = sizeof(pipelineLayoutInfo);
  pipelineLayoutInfo.label            = "static-scene-layout";
  if (GPUCreatePipelineLayout(bench->device,
                              &pipelineLayoutInfo,
                              &bench->pipelineLayout) != GPU_OK ||
      !bench->pipelineLayout) {
    return 0;
  }

  attribute.shaderLocation      = 0u;
  attribute.format              = GPU_VERTEX_FORMAT_FLOAT2;
  vertexLayout.strideBytes      = 2u * sizeof(float);
  vertexLayout.stepMode         = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount   = 1u;
  vertexLayout.pAttributes      = &attribute;
  colorTarget.format            = GPU_FORMAT_BGRA8_UNORM;
  colorTarget.blend.writeMask   = GPU_COLOR_WRITE_ALL;
  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "static-scene-pipeline";
  pipelineInfo.layout           = bench->pipelineLayout;
  pipelineInfo.library          = bench->library;
  pipelineInfo.vertexEntry      = "api_vs";
  pipelineInfo.fragmentEntry    = "api_fs";
  pipelineInfo.vertex.bufferLayoutCount = 1u;
  pipelineInfo.vertex.pBufferLayouts    = &vertexLayout;
  pipelineInfo.colorTargetCount         = 1u;
  pipelineInfo.pColorTargets            = &colorTarget;
  pipelineInfo.depthStencilFormat       = GPU_FORMAT_UNDEFINED;
  pipelineInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode          = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace         = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(bench->device,
                              &pipelineInfo,
                              &bench->pipeline) != GPU_OK ||
      !bench->pipeline) {
    return 0;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "static-scene-vertices";
  bufferInfo.sizeBytes        = sizeof(staticVertices);
  bufferInfo.usage = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(bench->device,
                      &bufferInfo,
                      &bench->vertexBuffer) != GPU_OK ||
      !bench->vertexBuffer ||
      GPUQueueWriteBuffer(bench->queue,
                          bench->vertexBuffer,
                          0u,
                          staticVertices,
                          sizeof(staticVertices)) != GPU_OK) {
    return 0;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "static-scene-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_BGRA8_UNORM;
  textureInfo.width            = 1u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET;
  if (GPUCreateTexture(bench->device,
                       &textureInfo,
                       &bench->target) != GPU_OK ||
      !bench->target) {
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "static-scene-target-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_BGRA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(bench->target,
                           &viewInfo,
                           &bench->targetView) != GPU_OK ||
      !bench->targetView ||
      GPUCreateFence(bench->device, NULL, &bench->fence) != GPU_OK ||
      !bench->fence) {
    return 0;
  }
  return 1;
}

static int
static_init(StaticBench *bench, const StaticConfig *config) {
  GPUInstanceCreateInfo instanceInfo;
  GPURuntimeConfig      runtimeConfig;

  memset(bench, 0, sizeof(*bench));
  memset(&instanceInfo, 0, sizeof(instanceInfo));
  memset(&runtimeConfig, 0, sizeof(runtimeConfig));

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = config->backend;
  if (GPUCreateInstance(&instanceInfo, &bench->instance) != GPU_OK ||
      !bench->instance) {
    fprintf(stderr, "failed to create GPU instance\n");
    return 0;
  }

  bench->adapter = static_selectAdapter(bench->instance);
  if (!bench->adapter) {
    fprintf(stderr, "failed to select GPU adapter\n");
    return 0;
  }
  bench->device = GPUCreateDeviceWithDefaultQueues(bench->adapter);
  if (!bench->device) {
    fprintf(stderr, "failed to create GPU device\n");
    return 0;
  }
  bench->queue = GPUGetQueue(bench->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!bench->queue) {
    fprintf(stderr, "failed to get graphics queue\n");
    return 0;
  }

  runtimeConfig.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize = sizeof(runtimeConfig);
  runtimeConfig.validationMode   = GPU_VALIDATION_OFF;
  runtimeConfig.enableStats      = true;
  if (GPUConfigureRuntime(bench->device, &runtimeConfig) != GPU_OK ||
      GPUGetAdapterProperties(bench->adapter,
                              &bench->adapterProperties) != GPU_OK ||
      !static_createLibrary(bench, config->artifactPath) ||
      !static_createResources(bench)) {
    fprintf(stderr, "failed to configure static scene\n");
    return 0;
  }
  return 1;
}

static void
static_cleanup(StaticBench *bench) {
  if (!bench) {
    return;
  }
  GPUDestroyFence(bench->fence);
  GPUDestroyTextureView(bench->targetView);
  GPUDestroyTexture(bench->target);
  GPUDestroyBuffer(bench->vertexBuffer);
  GPUDestroyRenderPipeline(bench->pipeline);
  GPUDestroyPipelineLayout(bench->pipelineLayout);
  GPUDestroyShaderLibrary(bench->library);
  GPUDestroyDevice(bench->device);
  GPUDestroyInstance(bench->instance);
}

static int
static_frame(StaticBench   *bench,
             uint32_t       drawCount,
             double        *outEncodeNs,
             GPUFrameStats *outStats) {
  GPUCommandBuffer              *cmdb;
  GPUCommandBuffer              *buffers[1];
  GPURenderPassEncoder          *pass;
  GPURenderPassColorAttachment   color;
  GPURenderPassCreateInfo        passInfo;
  GPUBufferBinding               vertexBinding;
  GPUQueueSubmitInfo             submitInfo;
  GPUResult                      result;
  double                         begin;
  double                         end;

  memset(&color, 0, sizeof(color));
  memset(&passInfo, 0, sizeof(passInfo));
  memset(&vertexBinding, 0, sizeof(vertexBinding));
  memset(&submitInfo, 0, sizeof(submitInfo));

  GPUResetStats(bench->device);
  begin = bench_now();

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(bench->queue,
                              "static-scene-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    return 0;
  }

  color.view                  = bench->targetView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType        = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize   = sizeof(passInfo);
  passInfo.label              = "static-scene-pass";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    return 0;
  }

  vertexBinding.buffer = bench->vertexBuffer;
  GPUBindRenderPipeline(pass, bench->pipeline);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBinding);
  for (uint32_t draw = 0u; draw < drawCount; draw++) {
    GPUDraw(pass, 3u, 1u, 0u, 0u);
  }
  GPUEndRenderPass(pass);

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = bench->fence;
  result = GPUQueueSubmit(bench->queue, &submitInfo);
  end    = bench_now();
  if (result != GPU_OK || GPUWaitFence(bench->fence, UINT64_MAX) != GPU_OK) {
    return 0;
  }

  *outStats    = bench->device->currentFrameStats;
  *outEncodeNs = (end - begin) * 1e9;
  return 1;
}

static void
static_accumulate(StaticMetrics *metrics, const GPUFrameStats *stats) {
  metrics->requestedStateCalls += stats->requestedStateCalls;
  metrics->emittedStateCalls   += stats->emittedStateCalls;
  metrics->requestedBindCalls  += stats->requestedBindCalls;
  metrics->emittedBindCalls    += stats->emittedBindCalls;
  metrics->drawCalls           += stats->drawCalls;
  if (stats->hotPathAllocCount > metrics->maxAllocCount) {
    metrics->maxAllocCount = stats->hotPathAllocCount;
  }
  if (stats->hotPathAllocBytes > metrics->maxAllocBytes) {
    metrics->maxAllocBytes = stats->hotPathAllocBytes;
  }
  if (stats->hotPathFreeCount > metrics->maxFreeCount) {
    metrics->maxFreeCount = stats->hotPathFreeCount;
  }
  if (stats->hotPathFreeBytes > metrics->maxFreeBytes) {
    metrics->maxFreeBytes = stats->hotPathFreeBytes;
  }
}

static int
static_run(StaticBench        *bench,
           const StaticConfig *config,
           StaticMetrics      *metrics) {
  GPUFrameStats stats;
  size_t        sampleCount;

  if ((size_t)config->measuredFrames >
      SIZE_MAX / (size_t)config->repeats) {
    return 0;
  }
  sampleCount = (size_t)config->measuredFrames * config->repeats;
  memset(metrics, 0, sizeof(*metrics));
  metrics->samples       = calloc(sampleCount, sizeof(*metrics->samples));
  metrics->repeatMedians = calloc(config->repeats,
                                  sizeof(*metrics->repeatMedians));
  metrics->sampleCount   = sampleCount;
  if (!metrics->samples || !metrics->repeatMedians) {
    return 0;
  }

  for (uint32_t repeat = 0u; repeat < config->repeats; repeat++) {
    size_t base;

    for (uint32_t frame = 0u; frame < config->warmupFrames; frame++) {
      double ignored;

      if (!static_frame(bench, config->drawCount, &ignored, &stats)) {
        return 0;
      }
    }

    base = (size_t)repeat * config->measuredFrames;
    for (uint32_t frame = 0u; frame < config->measuredFrames; frame++) {
      double *sample;

      sample = &metrics->samples[base + frame];
      if (!static_frame(bench, config->drawCount, sample, &stats) ||
          stats.drawCalls != config->drawCount) {
        return 0;
      }
      static_accumulate(metrics, &stats);
    }
    metrics->repeatMedians[repeat] =
      bench_percentile(&metrics->samples[base],
                       config->measuredFrames,
                       0.5);
  }
  return 1;
}

static void
static_print(const StaticBench   *bench,
             const StaticConfig  *config,
             StaticMetrics       *metrics) {
  double median;
  double p95;
  double p99;
  double frames;

  median = bench_percentile(metrics->repeatMedians, config->repeats, 0.5);
  p95    = bench_percentile(metrics->samples, metrics->sampleCount, 0.95);
  p99    = bench_percentile(metrics->samples, metrics->sampleCount, 0.99);
  frames = (double)metrics->sampleCount;

  printf("GPU static scene benchmark\n");
  printf("adapter: %s, backend: %s, validation: %s\n",
         bench->adapterProperties.name ? bench->adapterProperties.name : "unknown",
         static_backendName(bench->adapterProperties.backend),
         GPU_BUILD_WITH_VALIDATION ? "compiled" : "removed");
  printf("draws/frame: %u, warmup: %u, frames: %u, repeats: %u\n",
         config->drawCount,
         config->warmupFrames,
         config->measuredFrames,
         config->repeats);
  printf("encode+submit: median %.3f us, p95 %.3f us, p99 %.3f us\n",
         median / 1e3,
         p95 / 1e3,
         p99 / 1e3);
  printf("median per draw: %.3f ns\n", median / config->drawCount);
  printf("bind calls/frame: requested %.2f, emitted %.2f\n",
         metrics->requestedBindCalls / frames,
         metrics->emittedBindCalls / frames);
  printf("state calls/frame: requested %.2f, emitted %.2f\n",
         metrics->requestedStateCalls / frames,
         metrics->emittedStateCalls / frames);
  printf("hot-path max alloc: %" PRIu64 " calls, %" PRIu64 " bytes\n",
         metrics->maxAllocCount,
         metrics->maxAllocBytes);
  printf("hot-path max free : %" PRIu64 " calls, %" PRIu64 " bytes\n",
         metrics->maxFreeCount,
         metrics->maxFreeBytes);
}

static void
static_metricsFree(StaticMetrics *metrics) {
  if (!metrics) {
    return;
  }
  free(metrics->repeatMedians);
  free(metrics->samples);
}

int
main(int argc, char *argv[]) {
  StaticConfig  config;
  StaticBench   bench;
  StaticMetrics metrics;
  int           ok;

  memset(&config, 0, sizeof(config));
  config.backend        = GPU_BACKEND_DEFAULT;
  config.drawCount      = STATIC_DEFAULT_DRAWS;
  config.warmupFrames   = STATIC_DEFAULT_WARMUP;
  config.measuredFrames = STATIC_DEFAULT_FRAMES;
  config.repeats        = STATIC_DEFAULT_REPEATS;

  if (argc < 2 || argc > 7) {
    fprintf(stderr,
            "usage: %s <render_mrt.us> [default|metal|vulkan|dx12] "
            "[draws] [warmup] [frames] [repeats]\n",
            argv[0]);
    return EXIT_FAILURE;
  }
  config.artifactPath = argv[1];
  if ((argc > 2 && !static_parseBackend(argv[2], &config.backend)) ||
      (argc > 3 && !static_parseU32(argv[3], 1u, &config.drawCount)) ||
      (argc > 4 && !static_parseU32(argv[4], 0u, &config.warmupFrames)) ||
      (argc > 5 && !static_parseU32(argv[5], 1u, &config.measuredFrames)) ||
      (argc > 6 && !static_parseU32(argv[6], 1u, &config.repeats))) {
    fprintf(stderr, "invalid static scene benchmark arguments\n");
    return EXIT_FAILURE;
  }

  memset(&bench, 0, sizeof(bench));
  memset(&metrics, 0, sizeof(metrics));
  if (!static_init(&bench, &config)) {
    static_cleanup(&bench);
    return EXIT_FAILURE;
  }

  ok = static_run(&bench, &config, &metrics);
  if (ok) {
    static_print(&bench, &config, &metrics);
    ok = metrics.maxAllocCount == 0u &&
         metrics.maxAllocBytes == 0u &&
         metrics.maxFreeCount == 0u &&
         metrics.maxFreeBytes == 0u;
  }
  static_metricsFree(&metrics);
  static_cleanup(&bench);
  if (!ok) {
    fprintf(stderr, "static scene benchmark failed\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
