/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../samples/common/webgpu.h"
#include "../src/api/device_internal.h"

#include <emscripten/emscripten.h>

#include <stdio.h>
#include <stdlib.h>

enum {
  WEBGPU_RENDER_BATCH_SIZE    = 8u,
  WEBGPU_RENDER_WARMUP_FRAMES = 60u,
  WEBGPU_RENDER_SAMPLE_COUNT  = 300u
};

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
  uint64_t           maxAllocCount;
  uint64_t           maxAllocBytes;
  uint64_t           maxFreeCount;
  uint64_t           maxFreeBytes;
  uint32_t           frameCount;
  uint32_t           sampleCount;
  uint32_t           droppedBatches;
  bool               draining;
  double             samples[WEBGPU_RENDER_SAMPLE_COUNT];
} WebGPURenderBench;

static WebGPURenderBench bench;

EM_JS(void,
      publish_results,
      (double median,
       double p95,
       double p99,
       uint64_t allocCount,
       uint64_t allocBytes,
       uint64_t freeCount,
       uint64_t freeBytes,
       uint32_t droppedBatches), {
  const output = document.getElementById("results");
  const text = [
    "WebGPU warm offscreen render",
    `median: ${median.toFixed(3)} us`,
    `p95:    ${p95.toFixed(3)} us`,
    `p99:    ${p99.toFixed(3)} us`,
    `alloc:  ${allocCount} calls, ${allocBytes} bytes`,
    `free:   ${freeCount} calls, ${freeBytes} bytes`,
    `dropped batches: ${droppedBatches}`
  ].join("\n");

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
encode_command(WebGPURenderBench *state) {
  GPUCommandBuffer              *buffers[1];
  GPUCommandBuffer              *cmdb;
  GPURenderPassEncoder          *pass;
  GPURenderPassColorAttachment   color = {0};
  GPURenderPassCreateInfo        passInfo = {0};
  GPUQueueSubmitInfo             submitInfo = {0};

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(state->queue,
                              "webgpu-render-bench",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    return 0;
  }

  color.view                  = state->targetView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[3] = 1.0f;

  passInfo.chain.sType        = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize   = sizeof(passInfo);
  passInfo.label              = "webgpu-render-bench-pass";
  passInfo.pColorAttachments  = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    return 0;
  }

  GPUBindRenderPipeline(pass, state->pipeline);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.commandBufferCount = 1u;
  return GPUQueueSubmit(state->queue, &submitInfo) == GPU_OK;
}

static void
record_stats(WebGPURenderBench *state) {
  GPUFrameStats stats;

  if (GPUGetLastFrameStats(state->device, &stats) != GPU_OK) {
    return;
  }
  if (stats.hotPathAllocCount > state->maxAllocCount) {
    state->maxAllocCount = stats.hotPathAllocCount;
  }
  if (stats.hotPathAllocBytes > state->maxAllocBytes) {
    state->maxAllocBytes = stats.hotPathAllocBytes;
  }
  if (stats.hotPathFreeCount > state->maxFreeCount) {
    state->maxFreeCount = stats.hotPathFreeCount;
  }
  if (stats.hotPathFreeBytes > state->maxFreeBytes) {
    state->maxFreeBytes = stats.hotPathFreeBytes;
  }
}

static void
snapshot_stats(WebGPURenderBench *state) {
  /* Offscreen work has no frame end to publish current runtime counters. */
  gpuDeviceEndFrame(state->device);
  record_stats(state);
}

static void
run_frame(void *userData) {
  WebGPURenderBench *state;
  double             begin, end;

  state = userData;
  if (state->draining) {
    snapshot_stats(state);
    qsort(state->samples,
          state->sampleCount,
          sizeof(*state->samples),
          compare_samples);
    publish_results(percentile(state->samples,
                               state->sampleCount,
                               0.50),
                    percentile(state->samples,
                               state->sampleCount,
                               0.95),
                    percentile(state->samples,
                               state->sampleCount,
                               0.99),
                    state->maxAllocCount,
                    state->maxAllocBytes,
                    state->maxFreeCount,
                    state->maxFreeBytes,
                    state->droppedBatches);
    emscripten_cancel_main_loop();
    return;
  }

  if (state->frameCount > WEBGPU_RENDER_WARMUP_FRAMES) {
    snapshot_stats(state);
  }
  GPUResetStats(state->device);
  begin = emscripten_get_now();
  for (uint32_t i = 0u; i < WEBGPU_RENDER_BATCH_SIZE; i++) {
    if (!encode_command(state)) {
      state->droppedBatches++;
      return;
    }
  }
  end = emscripten_get_now();

  state->frameCount++;
  if (state->frameCount <= WEBGPU_RENDER_WARMUP_FRAMES) {
    return;
  }

  snapshot_stats(state);
  state->samples[state->sampleCount++] =
    (end - begin) * 1000.0 / WEBGPU_RENDER_BATCH_SIZE;
  if (state->sampleCount == WEBGPU_RENDER_SAMPLE_COUNT) {
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
