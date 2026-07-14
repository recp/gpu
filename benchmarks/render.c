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
#include "api/cmdqueue_internal.h"
#include "bench.h"
#include "render.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  BENCH_DEFAULT_DRAWS   = 1000,
  BENCH_DEFAULT_WARMUP  = 300,
  BENCH_DEFAULT_FRAMES  = 3000,
  BENCH_DEFAULT_REPEATS = 5
};

static const float benchVertices[] = {
  -1.0f, -1.0f,
   3.0f, -1.0f,
  -1.0f,  3.0f
};

bool
bench_renderConfig(int argc, char *argv[], BenchRenderConfig *config) {
  if (!config || argc < 2 || argc > 7) {
    if (argv && argv[0]) {
      fprintf(stderr,
              "usage: %s <shader.us> [default|metal|vulkan|dx12] "
              "[draws] [warmup] [frames] [repeats]\n",
              argv[0]);
    }
    return false;
  }

  memset(config, 0, sizeof(*config));
  config->artifactPath   = argv[1];
  config->backend        = GPU_BACKEND_DEFAULT;
  config->drawCount      = BENCH_DEFAULT_DRAWS;
  config->warmupFrames   = BENCH_DEFAULT_WARMUP;
  config->measuredFrames = BENCH_DEFAULT_FRAMES;
  config->repeats        = BENCH_DEFAULT_REPEATS;
  if ((argc > 2 && !bench_parseBackend(argv[2], &config->backend)) ||
      (argc > 3 && !bench_parseU32(argv[3], 1u, &config->drawCount)) ||
      (argc > 4 && !bench_parseU32(argv[4], 0u, &config->warmupFrames)) ||
      (argc > 5 && !bench_parseU32(argv[5], 1u, &config->measuredFrames)) ||
      (argc > 6 && !bench_parseU32(argv[6], 1u, &config->repeats))) {
    fprintf(stderr, "invalid render benchmark arguments\n");
    return false;
  }
  return true;
}

static GPUAdapter *
bench_selectAdapter(GPUInstance *instance) {
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
bench_createLibrary(BenchRender *bench, const char *path) {
  void     *artifact;
  uint64_t  artifactSize;
  GPUResult result;

  artifact = bench_read(path, &artifactSize);
  if (!artifact) {
    fprintf(stderr, "failed to read USL artifact: %s\n", path);
    return false;
  }
  result = GPUCreateShaderLibraryFromUSL(bench->device,
                                         artifact,
                                         artifactSize,
                                         &bench->library);
  free(artifact);
  if (result != GPU_OK || !bench->library) {
    fprintf(stderr, "failed to create USL shader library\n");
    return false;
  }
  return true;
}

static bool
bench_createResources(BenchRender *bench, uint32_t width, uint32_t height) {
  GPUPipelineLayoutCreateInfo pipelineLayoutInfo;
  GPUBufferCreateInfo         bufferInfo;
  GPUTextureCreateInfo        textureInfo;
  GPUTextureViewCreateInfo    viewInfo;

  memset(&pipelineLayoutInfo, 0, sizeof(pipelineLayoutInfo));
  memset(&bufferInfo, 0, sizeof(bufferInfo));
  memset(&textureInfo, 0, sizeof(textureInfo));
  memset(&viewInfo, 0, sizeof(viewInfo));

  pipelineLayoutInfo.chain.sType =
    GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.chain.structSize = sizeof(pipelineLayoutInfo);
  pipelineLayoutInfo.label            = "benchmark-layout";
  if (GPUCreatePipelineLayout(bench->device,
                              &pipelineLayoutInfo,
                              &bench->pipelineLayout) != GPU_OK ||
      !bench->pipelineLayout) {
    return false;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "benchmark-vertices";
  bufferInfo.sizeBytes        = sizeof(benchVertices);
  bufferInfo.usage = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(bench->device,
                      &bufferInfo,
                      &bench->vertexBuffer) != GPU_OK ||
      !bench->vertexBuffer ||
      GPUQueueWriteBuffer(bench->queue,
                          bench->vertexBuffer,
                          0u,
                          benchVertices,
                          sizeof(benchVertices)) != GPU_OK) {
    return false;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "benchmark-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_BGRA8_UNORM;
  textureInfo.width            = width;
  textureInfo.height           = height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET;
  if (GPUCreateTexture(bench->device,
                       &textureInfo,
                       &bench->target) != GPU_OK ||
      !bench->target) {
    return false;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "benchmark-target-view";
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
    return false;
  }
  return true;
}

bool
bench_renderInit(BenchRender             *bench,
                 const BenchRenderConfig *config,
                 uint32_t                 width,
                 uint32_t                 height) {
  GPUInstanceCreateInfo instanceInfo;
  GPUDeviceCreateInfo   deviceInfo;
  GPURuntimeConfig      runtimeConfig;

  if (!bench || !config || width == 0u || height == 0u) {
    return false;
  }
  memset(bench, 0, sizeof(*bench));
  (void)bench_processMemory(&bench->baselineMemory);
  memset(&instanceInfo, 0, sizeof(instanceInfo));
  memset(&deviceInfo, 0, sizeof(deviceInfo));
  memset(&runtimeConfig, 0, sizeof(runtimeConfig));

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = config->backend;
  if (GPUCreateInstance(&instanceInfo, &bench->instance) != GPU_OK ||
      !bench->instance) {
    fprintf(stderr, "failed to create GPU instance\n");
    return false;
  }

  bench->adapter = bench_selectAdapter(bench->instance);
  if (!bench->adapter) {
    fprintf(stderr, "failed to select GPU adapter\n");
    return false;
  }
  if (config->required.featureCount > 0u) {
    if (!config->required.pFeatures) {
      fprintf(stderr, "required feature list is missing\n");
      return false;
    }
    for (uint32_t i = 0u; i < config->required.featureCount; i++) {
      if (!GPUIsFeatureSupported(bench->adapter,
                                 config->required.pFeatures[i])) {
        bench->requiredUnsupported = true;
        return false;
      }
    }
    deviceInfo.chain.sType      = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.chain.structSize = sizeof(deviceInfo);
    deviceInfo.required         = config->required;
    if (GPUCreateDevice(bench->adapter,
                        &deviceInfo,
                        &bench->device) != GPU_OK) {
      bench->device = NULL;
    }
  } else {
    bench->device = GPUCreateDeviceWithDefaultQueues(bench->adapter);
  }
  if (!bench->device) {
    fprintf(stderr, "failed to create GPU device\n");
    return false;
  }
  bench->queue = GPUGetQueue(bench->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!bench->queue) {
    fprintf(stderr, "failed to get graphics queue\n");
    return false;
  }

  runtimeConfig.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize = sizeof(runtimeConfig);
  runtimeConfig.validationMode   = GPU_VALIDATION_OFF;
  runtimeConfig.enableStats      = true;
  if (GPUConfigureRuntime(bench->device, &runtimeConfig) != GPU_OK ||
      GPUGetAdapterProperties(bench->adapter,
                              &bench->adapterProperties) != GPU_OK ||
      !bench_createLibrary(bench, config->artifactPath) ||
      !bench_createResources(bench, width, height)) {
    fprintf(stderr, "failed to configure render benchmark\n");
    return false;
  }
  return true;
}

bool
bench_renderPipeline(BenchRender             *bench,
                     const BenchPipelineInfo *info,
                     GPURenderPipeline       **outPipeline) {
  GPURenderPipelineCreateInfo pipelineInfo;
  GPUVertexAttribute          attribute;
  GPUVertexBufferLayout       vertexLayout;
  GPUColorTargetState         colorTarget;

  if (!bench || !info || !outPipeline) {
    return false;
  }
  *outPipeline = NULL;
  memset(&pipelineInfo, 0, sizeof(pipelineInfo));
  memset(&attribute, 0, sizeof(attribute));
  memset(&vertexLayout, 0, sizeof(vertexLayout));
  memset(&colorTarget, 0, sizeof(colorTarget));

  if (info->vertexInput) {
    attribute.shaderLocation    = 0u;
    attribute.format            = GPU_VERTEX_FORMAT_FLOAT32X2;
    vertexLayout.strideBytes    = 2u * sizeof(float);
    vertexLayout.stepMode       = GPU_VERTEX_STEP_MODE_VERTEX;
    vertexLayout.attributeCount = 1u;
    vertexLayout.pAttributes    = &attribute;
  }
  colorTarget.format          = GPU_FORMAT_BGRA8_UNORM;
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;
  if (info->blendEnabled) {
    colorTarget.blend.enabled         = true;
    colorTarget.blend.color.srcFactor = GPU_BLEND_FACTOR_SRC_ALPHA;
    colorTarget.blend.color.dstFactor =
      GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorTarget.blend.color.op         = GPU_BLEND_OP_ADD;
    colorTarget.blend.alpha.srcFactor = GPU_BLEND_FACTOR_ONE;
    colorTarget.blend.alpha.dstFactor = GPU_BLEND_FACTOR_ZERO;
    colorTarget.blend.alpha.op         = GPU_BLEND_OP_ADD;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = info->label;
  pipelineInfo.layout           = bench->pipelineLayout;
  pipelineInfo.library          = bench->library;
  pipelineInfo.vertexEntry   = info->vertexEntry ? info->vertexEntry : "api_vs";
  pipelineInfo.fragmentEntry = info->fragmentEntry
                                 ? info->fragmentEntry
                                 : "api_fs";
  pipelineInfo.vertex.bufferLayoutCount = info->vertexInput ? 1u : 0u;
  pipelineInfo.vertex.pBufferLayouts    = info->vertexInput
                                            ? &vertexLayout
                                            : NULL;
  pipelineInfo.colorTargetCount         = 1u;
  pipelineInfo.pColorTargets            = &colorTarget;
  pipelineInfo.depthStencilFormat       = GPU_FORMAT_UNDEFINED;
  pipelineInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode          = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace         = info->frontFace;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = UINT32_MAX;
  return GPUCreateRenderPipeline(bench->device,
                                 &pipelineInfo,
                                 outPipeline) == GPU_OK &&
         *outPipeline != NULL;
}

static bool
bench_renderFrame(BenchRender         *bench,
                  uint32_t             drawCount,
                  BenchRenderEncodeFn  encode,
                  void                *userData,
                  double              *outEncodeNs,
                  GPUFrameStats       *outStats) {
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
                              "benchmark-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    return false;
  }

  color.view                  = bench->targetView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType        = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize   = sizeof(passInfo);
  passInfo.label              = "benchmark-pass";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    return false;
  }

  vertexBinding.buffer = bench->vertexBuffer;
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBinding);
  if (!encode(pass, drawCount, userData)) {
    return false;
  }
  GPUEndRenderPass(pass);
  cmdb->_recordsGPUFrameTime = bench->device->runtimeConfig.enableStats;

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = bench->fence;
  result = GPUQueueSubmit(bench->queue, &submitInfo);
  end    = bench_now();
  if (result != GPU_OK || GPUWaitFence(bench->fence, UINT64_MAX) != GPU_OK) {
    return false;
  }

  *outStats = bench->device->currentFrameStats;
  {
    GPUFrameStats completedStats;

    if (GPUGetLastFrameStats(bench->device, &completedStats) == GPU_OK) {
      outStats->gpuFrameMs = completedStats.gpuFrameMs;
    }
  }
  *outEncodeNs = (end - begin) * 1e9;
  return true;
}

static void
bench_accumulate(BenchSceneMetrics *metrics, const GPUFrameStats *stats) {
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

bool
bench_renderRun(BenchRender             *bench,
                const BenchRenderConfig *config,
                BenchRenderEncodeFn      encode,
                void                    *userData,
                BenchSceneMetrics       *metrics) {
  GPUFrameStats stats;
  size_t        sampleCount;

  if (!bench || !config || !encode || !metrics ||
      (size_t)config->measuredFrames >
        SIZE_MAX / (size_t)config->repeats) {
    return false;
  }
  sampleCount = (size_t)config->measuredFrames * config->repeats;
  memset(metrics, 0, sizeof(*metrics));
  metrics->samples       = calloc(sampleCount, sizeof(*metrics->samples));
  metrics->repeatMedians = calloc(config->repeats,
                                  sizeof(*metrics->repeatMedians));
  metrics->gpuSamples       = calloc(sampleCount,
                                     sizeof(*metrics->gpuSamples));
  metrics->gpuRepeatMedians = calloc(config->repeats,
                                     sizeof(*metrics->gpuRepeatMedians));
  metrics->sampleCount   = sampleCount;
  if (!metrics->samples || !metrics->repeatMedians ||
      !metrics->gpuSamples || !metrics->gpuRepeatMedians) {
    return false;
  }

  for (uint32_t repeat = 0u; repeat < config->repeats; repeat++) {
    size_t gpuBase;
    size_t base;

    for (uint32_t frame = 0u; frame < config->warmupFrames; frame++) {
      double ignored;

      if (!bench_renderFrame(bench,
                             config->drawCount,
                             encode,
                             userData,
                             &ignored,
                             &stats)) {
        return false;
      }
    }

    base    = (size_t)repeat * config->measuredFrames;
    gpuBase = metrics->gpuSampleCount;
    for (uint32_t frame = 0u; frame < config->measuredFrames; frame++) {
      double *sample;

      sample = &metrics->samples[base + frame];
      if (!bench_renderFrame(bench,
                             config->drawCount,
                             encode,
                             userData,
                             sample,
                             &stats) ||
          stats.drawCalls != config->drawCount) {
        return false;
      }
      bench_accumulate(metrics, &stats);
      if (stats.gpuFrameMs > 0.0) {
        metrics->gpuSamples[metrics->gpuSampleCount++] =
          stats.gpuFrameMs * 1e6;
      }
    }
    metrics->repeatMedians[repeat] =
      bench_percentile(&metrics->samples[base],
                       config->measuredFrames,
                       0.5);
    if (metrics->gpuSampleCount > gpuBase) {
      metrics->gpuRepeatMedians[metrics->gpuRepeatCount++] =
        bench_percentile(&metrics->gpuSamples[gpuBase],
                         metrics->gpuSampleCount - gpuBase,
                         0.5);
    }
  }
  return true;
}

void
bench_renderPrint(const char              *title,
                  const BenchRender       *bench,
                  const BenchRenderConfig *config,
                  BenchSceneMetrics       *metrics) {
  double median;
  double p95;
  double p99;
  double gpuMedian;
  double gpuP95;
  double gpuP99;
  double frames;
  BenchProcessMemory processMemory;

  median = bench_percentile(metrics->repeatMedians, config->repeats, 0.5);
  p95    = bench_percentile(metrics->samples, metrics->sampleCount, 0.95);
  p99    = bench_percentile(metrics->samples, metrics->sampleCount, 0.99);
  frames = (double)metrics->sampleCount;

  printf("GPU %s benchmark\n", title);
  printf("adapter: %s, backend: %s, validation: %s\n",
         bench->adapterProperties.name ? bench->adapterProperties.name : "unknown",
         bench_backendName(bench->adapterProperties.backend),
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
  if (metrics->gpuSampleCount > 0u) {
    gpuMedian = bench_percentile(metrics->gpuRepeatMedians,
                                 metrics->gpuRepeatCount,
                                 0.5);
    gpuP95    = bench_percentile(metrics->gpuSamples,
                                 metrics->gpuSampleCount,
                                 0.95);
    gpuP99    = bench_percentile(metrics->gpuSamples,
                                 metrics->gpuSampleCount,
                                 0.99);
    printf("GPU frame: median %.3f us, p95 %.3f us, p99 %.3f us\n",
           gpuMedian / 1e3,
           gpuP95 / 1e3,
           gpuP99 / 1e3);
  } else {
    printf("GPU frame: unavailable\n");
  }
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
  if (bench_processMemory(&processMemory) &&
      bench->baselineMemory.residentBytes > 0u) {
    double residentDelta;
    double peakDelta;

    residentDelta = processMemory.residentBytes >
                      bench->baselineMemory.residentBytes
                      ? (double)(processMemory.residentBytes -
                                 bench->baselineMemory.residentBytes)
                      : 0.0;
    peakDelta = processMemory.peakResidentBytes >
                  bench->baselineMemory.peakResidentBytes
                  ? (double)(processMemory.peakResidentBytes -
                             bench->baselineMemory.peakResidentBytes)
                  : 0.0;
    printf("process memory: resident %.2f MiB (+%.2f), peak %.2f MiB "
           "(+%.2f)\n",
           (double)processMemory.residentBytes / (1024.0 * 1024.0),
           residentDelta / (1024.0 * 1024.0),
           (double)processMemory.peakResidentBytes / (1024.0 * 1024.0),
           peakDelta / (1024.0 * 1024.0));
  } else {
    printf("process memory: unavailable\n");
  }
}

bool
bench_renderMetricsPass(const BenchSceneMetrics *metrics) {
  return metrics &&
         metrics->maxAllocCount == 0u &&
         metrics->maxAllocBytes == 0u &&
         metrics->maxFreeCount == 0u &&
         metrics->maxFreeBytes == 0u;
}

void
bench_renderFreeMetrics(BenchSceneMetrics *metrics) {
  if (!metrics) {
    return;
  }
  free(metrics->repeatMedians);
  free(metrics->samples);
  free(metrics->gpuRepeatMedians);
  free(metrics->gpuSamples);
}

void
bench_renderCleanup(BenchRender *bench) {
  GPUApi *api;

  if (!bench) {
    return;
  }
  api = gpuDeviceApi(bench->device);
  if (api && api->device.waitIdle) {
    (void)api->device.waitIdle(bench->device);
  }
  GPUDestroyFence(bench->fence);
  GPUDestroyTextureView(bench->targetView);
  GPUDestroyTexture(bench->target);
  GPUDestroyBuffer(bench->vertexBuffer);
  GPUDestroyPipelineLayout(bench->pipelineLayout);
  GPUDestroyShaderLibrary(bench->library);
  GPUDestroyDevice(bench->device);
  GPUDestroyInstance(bench->instance);
}
