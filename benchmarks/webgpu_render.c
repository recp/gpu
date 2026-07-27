/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../samples/common/webgpu.h"
#include "../src/api/cmdqueue_internal.h"
#include "../src/api/device_internal.h"

#include <emscripten/emscripten.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  WEBGPU_RENDER_BATCH_SIZE    = 8u,
  WEBGPU_RENDER_WARMUP_FRAMES = 60u,
  WEBGPU_RENDER_SAMPLE_COUNT  = 300u,
  WEBGPU_RENDER_MODE_COUNT    = 2u
};

typedef enum WebGPURenderMode {
  WEBGPU_RENDER_SERIAL_SUBMIT,
  WEBGPU_RENDER_BATCH_SUBMIT
} WebGPURenderMode;

typedef struct WebGPURenderSamples {
  uint64_t maxAllocCount;
  uint64_t maxAllocBytes;
  uint64_t maxFreeCount;
  uint64_t maxFreeBytes;
  uint32_t frameCount;
  uint32_t sampleCount;
  uint32_t droppedBatches;
  double   total[WEBGPU_RENDER_SAMPLE_COUNT];
  double   acquire[WEBGPU_RENDER_SAMPLE_COUNT];
  double   encode[WEBGPU_RENDER_SAMPLE_COUNT];
  double   submit[WEBGPU_RENDER_SAMPLE_COUNT];
} WebGPURenderSamples;

typedef struct WebGPURenderTimes {
  double total;
  double acquire;
  double encode;
  double submit;
} WebGPURenderTimes;

typedef struct WebGPURenderBench {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  GPUTexture        *target;
  GPUTextureView    *targetView;
  WebGPURequest      request;
  WebGPURenderSamples modes[WEBGPU_RENDER_MODE_COUNT];
  WebGPURenderMode    mode;
  bool               draining;
} WebGPURenderBench;

static WebGPURenderBench bench;

EM_JS(void, publish_results, (const char *result), {
  const output = document.getElementById("results");
  const text = UTF8ToString(result);

  output.textContent = text;
  console.log(text);
});

static int
compare_samples(const void *left, const void *right) {
  double a, b;

  a = *(const double *)left;
  b = *(const double *)right;
  return (a > b) - (a < b);
}

static double
percentile(const double *samples, uint32_t count, double value) {
  uint32_t index;

  index = (uint32_t)(value * (double)(count - 1u) + 0.5);
  return samples[index];
}

static int
create_target(WebGPURenderBench *state) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "webgpu-render-bench-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 64u;
  textureInfo.height           = 64u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->target) != GPU_OK ||
      !state->target) {
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "webgpu-render-bench-target-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = textureInfo.format;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  return GPUCreateTextureView(state->target,
                              &viewInfo,
                              &state->targetView) == GPU_OK &&
         state->targetView;
}

static int
create_pipeline(WebGPURenderBench *state) {
  GPUColorTargetState         color = {0};
  GPURenderPipelineCreateInfo info  = {0};
  void                       *artifact;
  uint64_t                    artifactSize;
  GPUResult                   result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/triangle.us", &artifact, &artifactSize)) {
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library ||
      GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout) {
    return 0;
  }

  color.format          = GPU_FORMAT_RGBA8_UNORM;
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.label                   = "webgpu-render-bench-pipeline";
  info.layout                  = state->shaderLayout->pipelineLayout;
  info.library                 = state->library;
  info.vertexEntry             = "tri_vs";
  info.fragmentEntry           = "tri_fs";
  info.pColorTargets           = &color;
  info.colorTargetCount        = 1u;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  return GPUCreateRenderPipeline(state->device,
                                 &info,
                                 &state->pipeline) == GPU_OK &&
         state->pipeline;
}

static int
acquire_commands(WebGPURenderBench *state, GPUCommandBuffer **buffers) {
  for (uint32_t i = 0u; i < WEBGPU_RENDER_BATCH_SIZE; i++) {
    buffers[i] = NULL;
    if (GPUAcquireCommandBuffer(state->queue,
                                "webgpu-render-bench",
                                &buffers[i]) != GPU_OK ||
        !buffers[i]) {
      return 0;
    }
  }
  return 1;
}

static int
encode_commands(WebGPURenderBench *state, GPUCommandBuffer **buffers) {
  GPURenderPassColorAttachment color    = {0};
  GPURenderPassCreateInfo      passInfo = {0};

  color.view                  = state->targetView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[3] = 1.0f;

  passInfo.chain.sType        = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize   = sizeof(passInfo);
  passInfo.label              = "webgpu-render-bench-pass";
  passInfo.pColorAttachments  = &color;
  passInfo.colorAttachmentCount = 1u;
  for (uint32_t i = 0u; i < WEBGPU_RENDER_BATCH_SIZE; i++) {
    GPURenderPassEncoder *pass;

    pass = GPUBeginRenderPass(buffers[i], &passInfo);
    if (!pass) {
      return 0;
    }
    GPUBindRenderPipeline(pass, state->pipeline);
    GPUDraw(pass, 3u, 1u, 0u, 0u);
    GPUEndRenderPass(pass);
  }
  return 1;
}

static int
submit_commands(WebGPURenderBench *state,
                GPUCommandBuffer **buffers,
                WebGPURenderMode   mode) {
  GPUQueueSubmitInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  info.chain.structSize = sizeof(info);
  if (mode == WEBGPU_RENDER_BATCH_SUBMIT) {
    info.ppCommandBuffers   = buffers;
    info.commandBufferCount = WEBGPU_RENDER_BATCH_SIZE;
    return GPUQueueSubmit(state->queue, &info) == GPU_OK;
  }

  info.commandBufferCount = 1u;
  for (uint32_t i = 0u; i < WEBGPU_RENDER_BATCH_SIZE; i++) {
    info.ppCommandBuffers = &buffers[i];
    if (GPUQueueSubmit(state->queue, &info) != GPU_OK) {
      return 0;
    }
  }
  return 1;
}

static void
discard_commands(GPUCommandBuffer **buffers) {
  for (uint32_t i = 0u; i < WEBGPU_RENDER_BATCH_SIZE; i++) {
    if (buffers[i] && !buffers[i]->_submitted) {
      (void)GPUDiscardCommandBuffer(buffers[i]);
    }
  }
}

static int
run_commands(WebGPURenderBench *state, WebGPURenderTimes *times) {
  GPUCommandBuffer *buffers[WEBGPU_RENDER_BATCH_SIZE];
  double            acquireBegin;
  double            acquireEnd;
  double            encodeEnd;
  double            submitEnd;

  memset(buffers, 0, sizeof(buffers));
  acquireBegin = emscripten_get_now();
  if (!acquire_commands(state, buffers)) {
    discard_commands(buffers);
    return 0;
  }
  acquireEnd = emscripten_get_now();
  if (!encode_commands(state, buffers)) {
    discard_commands(buffers);
    return 0;
  }
  encodeEnd = emscripten_get_now();
  if (!submit_commands(state, buffers, state->mode)) {
    discard_commands(buffers);
    return 0;
  }
  submitEnd = emscripten_get_now();

  times->total   = submitEnd - acquireBegin;
  times->acquire = acquireEnd - acquireBegin;
  times->encode  = encodeEnd - acquireEnd;
  times->submit  = submitEnd - encodeEnd;
  return 1;
}

static void
record_stats(WebGPURenderBench   *state,
             WebGPURenderSamples *samples) {
  GPUFrameStats stats;

  if (GPUGetLastFrameStats(state->device, &stats) != GPU_OK) {
    return;
  }
  if (stats.hotPathAllocCount > samples->maxAllocCount) {
    samples->maxAllocCount = stats.hotPathAllocCount;
  }
  if (stats.hotPathAllocBytes > samples->maxAllocBytes) {
    samples->maxAllocBytes = stats.hotPathAllocBytes;
  }
  if (stats.hotPathFreeCount > samples->maxFreeCount) {
    samples->maxFreeCount = stats.hotPathFreeCount;
  }
  if (stats.hotPathFreeBytes > samples->maxFreeBytes) {
    samples->maxFreeBytes = stats.hotPathFreeBytes;
  }
}

static void
snapshot_stats(WebGPURenderBench   *state,
               WebGPURenderSamples *samples) {
  /* Offscreen work has no frame end to publish current runtime counters. */
  gpuDeviceEndFrame(state->device);
  record_stats(state, samples);
}

static void
sort_samples(WebGPURenderSamples *samples) {
  qsort(samples->total,
        samples->sampleCount,
        sizeof(*samples->total),
        compare_samples);
  qsort(samples->acquire,
        samples->sampleCount,
        sizeof(*samples->acquire),
        compare_samples);
  qsort(samples->encode,
        samples->sampleCount,
        sizeof(*samples->encode),
        compare_samples);
  qsort(samples->submit,
        samples->sampleCount,
        sizeof(*samples->submit),
        compare_samples);
}

static size_t
format_samples(char                      *output,
               size_t                     capacity,
               const char                *label,
               const WebGPURenderSamples *samples) {
  int count;

  count = snprintf(
    output,
    capacity,
    "%s\n"
    "  total:   %6.3f / %6.3f / %6.3f us\n"
    "  acquire: %6.3f / %6.3f / %6.3f us\n"
    "  encode:  %6.3f / %6.3f / %6.3f us\n"
    "  submit:  %6.3f / %6.3f / %6.3f us\n"
    "  alloc:   %" PRIu64 " calls, %" PRIu64 " bytes\n"
    "  free:    %" PRIu64 " calls, %" PRIu64 " bytes\n"
    "  dropped: %u batches\n",
    label,
    percentile(samples->total, samples->sampleCount, 0.50),
    percentile(samples->total, samples->sampleCount, 0.95),
    percentile(samples->total, samples->sampleCount, 0.99),
    percentile(samples->acquire, samples->sampleCount, 0.50),
    percentile(samples->acquire, samples->sampleCount, 0.95),
    percentile(samples->acquire, samples->sampleCount, 0.99),
    percentile(samples->encode, samples->sampleCount, 0.50),
    percentile(samples->encode, samples->sampleCount, 0.95),
    percentile(samples->encode, samples->sampleCount, 0.99),
    percentile(samples->submit, samples->sampleCount, 0.50),
    percentile(samples->submit, samples->sampleCount, 0.95),
    percentile(samples->submit, samples->sampleCount, 0.99),
    samples->maxAllocCount,
    samples->maxAllocBytes,
    samples->maxFreeCount,
    samples->maxFreeBytes,
    samples->droppedBatches
  );
  return count > 0 && (size_t)count < capacity ? (size_t)count : 0u;
}

static void
publish_report(WebGPURenderBench *state) {
  char   output[2048];
  size_t used;

  sort_samples(&state->modes[WEBGPU_RENDER_SERIAL_SUBMIT]);
  sort_samples(&state->modes[WEBGPU_RENDER_BATCH_SUBMIT]);
  used = (size_t)snprintf(output,
                          sizeof(output),
                          "WebGPU warm offscreen render\n"
                          "median / p95 / p99 per command\n\n");
  used += format_samples(output + used,
                         sizeof(output) - used,
                         "serial queue submits",
                         &state->modes[WEBGPU_RENDER_SERIAL_SUBMIT]);
  output[used++] = '\n';
  used += format_samples(output + used,
                         sizeof(output) - used,
                         "one batched queue submit",
                         &state->modes[WEBGPU_RENDER_BATCH_SUBMIT]);
  output[used] = '\0';
  publish_results(output);
}

static void
run_frame(void *userData) {
  WebGPURenderBench   *state;
  WebGPURenderSamples *samples;
  WebGPURenderTimes    times;
  double               scale;

  state   = userData;
  samples = &state->modes[state->mode];
  if (state->draining) {
    snapshot_stats(state, samples);
    if ((uint32_t)state->mode + 1u < WEBGPU_RENDER_MODE_COUNT) {
      state->mode     = (WebGPURenderMode)((uint32_t)state->mode + 1u);
      state->draining = false;
      GPUResetStats(state->device);
      set_status("GPU: warming batched WebGPU submit", 0);
    } else {
      publish_report(state);
      emscripten_cancel_main_loop();
    }
    return;
  }

  if (samples->frameCount > WEBGPU_RENDER_WARMUP_FRAMES) {
    snapshot_stats(state, samples);
  }
  GPUResetStats(state->device);
  if (!run_commands(state, &times)) {
    samples->droppedBatches++;
    return;
  }

  samples->frameCount++;
  if (samples->frameCount <= WEBGPU_RENDER_WARMUP_FRAMES) {
    return;
  }

  snapshot_stats(state, samples);
  scale = 1000.0 / WEBGPU_RENDER_BATCH_SIZE;
  samples->total[samples->sampleCount]   = times.total * scale;
  samples->acquire[samples->sampleCount] = times.acquire * scale;
  samples->encode[samples->sampleCount]  = times.encode * scale;
  samples->submit[samples->sampleCount]  = times.submit * scale;
  samples->sampleCount++;
  if (samples->sampleCount == WEBGPU_RENDER_SAMPLE_COUNT) {
    state->draining = true;
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPURenderBench *state;
  GPURuntimeConfig   runtime = {0};

  state = userData;
  if (result != GPU_OK || !adapter || !device) {
    set_status("GPU: failed to create WebGPU benchmark device", 1);
    return;
  }

  state->adapter = adapter;
  state->device  = device;
  state->queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);

  runtime.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtime.chain.structSize = sizeof(runtime);
  runtime.validationMode   = GPU_VALIDATION_OFF;
  runtime.enableStats      = true;
  if (!state->queue ||
      GPUConfigureRuntime(device, &runtime) != GPU_OK ||
      !create_target(state) ||
      !create_pipeline(state)) {
    set_status("GPU: failed to initialize WebGPU render benchmark", 1);
    return;
  }

  set_status("GPU: warming WebGPU render path", 0);
  emscripten_set_main_loop_arg(run_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "webgpu-render-bench";
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  info.enableValidation = false;
  result = GPUCreateInstance(&info, &bench.instance);
  if (result != GPU_OK || !bench.instance) {
    set_status("GPU: failed to create WebGPU benchmark instance", 1);
    return EXIT_FAILURE;
  }

  result = request_webgpu_device(bench.instance,
                                 &bench.request,
                                 webgpu_ready,
                                 &bench);
  return result == GPU_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
