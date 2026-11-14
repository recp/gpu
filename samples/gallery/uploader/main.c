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

#include "../../common/sample_platform.h"

#if defined(GPU_SAMPLE_GALLERY_APPLE)
#  include <pthread.h>
#  include <time.h>
#endif

typedef struct UploaderSample {
  GPUInstance       *instance;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUTexture        *texture;
  GPUTextureView    *view;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *layout;
  GPURenderPipeline *pipeline;
  GPUBindGroup      *group;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
} UploaderSample;

static UploaderSample app;

#if defined(GPU_SAMPLE_GALLERY_APPLE)
typedef struct Worker {
  GPUQueue   *queue;
  GPUTexture *texture;
  pthread_t   thread;
  uint32_t    index;
} Worker;

static Worker workers[4];
static uint32_t workerCount;
static int stopWorkers, workerFailed;

static void*
upload(void *user) {
  GPUUploaderCreateInfo info = {0};
  GPUTextureWriteRegion write = {0};
  struct timespec delay = {0, 16000000};
  uint8_t pixels[32u * 32u * 4u];
  Worker *worker;
  GPUUploader *uploader;
  uint32_t x, y, epoch = 0u;

  worker = user;
  info.chain.sType      = GPU_STRUCTURE_TYPE_UPLOADER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.stagingSizeBytes = 32u * 256u;
  info.stagingBufferCount = 3u;
  if (GPUCreateUploader(worker->queue, &info, &uploader) != GPU_OK) {
    __atomic_store_n(&workerFailed, 1, __ATOMIC_RELEASE);
    return NULL;
  }

  write.x = (worker->index & 1u) * 32u;
  write.y = (worker->index >> 1u) * 32u;
  write.width = write.height = 32u;
  write.depth = write.layerCount = 1u;
  write.bytesPerRow = 128u;
  while (!__atomic_load_n(&stopWorkers, __ATOMIC_ACQUIRE)) {
    for (y = 0u; y < 32u; y++) {
      for (x = 0u; x < 32u; x++) {
        pixels[(y * 32u + x) * 4u + 0u] = (uint8_t)(worker->index * 64u);
        pixels[(y * 32u + x) * 4u + 1u] = (uint8_t)(y * 8u);
        pixels[(y * 32u + x) * 4u + 2u] = (uint8_t)(x * 8u + epoch);
        pixels[(y * 32u + x) * 4u + 3u] = 255u;
      }
    }

    if (GPUUploadTexture(uploader, worker->texture, &write, pixels, sizeof(pixels)) != GPU_OK
        || GPUFlushUploads(uploader, NULL) != GPU_OK) {
      __atomic_store_n(&workerFailed, 1, __ATOMIC_RELEASE);
      break;
    }

    epoch++;
    nanosleep(&delay, NULL);
  }

  GPUDestroyUploader(uploader);
  return NULL;
}

static void
stop_workers(void) {
  uint32_t i;

  __atomic_store_n(&stopWorkers, 1, __ATOMIC_RELEASE);
  for (i = 0u; i < workerCount; i++) {
    pthread_join(workers[i].thread, NULL);
  }
  workerCount = 0u;
}
#endif

static int
prepare(UploaderSample *state, GPUDevice *device) {
  GPURenderPipelineCreateInfo pipelineInfo = {0};
  GPUTextureCreateInfo        textureInfo  = {0};
  GPUTextureViewCreateInfo    viewInfo     = {0};
  GPUTextureWriteRegion       write        = {0};
  GPUColorTargetState         color        = {0};
  GPUBindGroupCreateInfo      groupInfo    = {0};
  GPUBindGroupEntry           entry        = {0};
  uint8_t                    pixels[64u * 64u * 4u] = {0};
  void                      *artifact;
  uint64_t                   size;
  GPUResult                  result;

  textureInfo.dimension     = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format        = GPU_FORMAT_BGRA8_UNORM;
  textureInfo.width         = 64u;
  textureInfo.height        = 64u;
  textureInfo.depthOrLayers = 1u;
  textureInfo.mipLevelCount = 1u;
  textureInfo.sampleCount   = 1u;
  textureInfo.usage         = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;
  viewInfo.format          = textureInfo.format;
  viewInfo.viewType        = GPU_TEXTURE_VIEW_2D;
  viewInfo.mipLevelCount   = 1u;
  viewInfo.arrayLayerCount = 1u;
  write.width = write.height = 64u;
  write.depth = write.layerCount = 1u;
  write.bytesPerRow = 256u;
  if (GPUCreateTexture(device, &textureInfo, &state->texture) != GPU_OK
      || GPUQueueWriteTexture(state->queue, state->texture, &write, pixels, sizeof(pixels)) != GPU_OK
      || GPUCreateTextureView(state->texture, &viewInfo, &state->view) != GPU_OK
      || !read_file("/uploader.us", &artifact, &size))
    return 0;

  result = GPUCreateShaderLibraryFromUSL(device, artifact, size, &state->library);
  free(artifact);
  if (result != GPU_OK || GPUCreateShaderLayout(device, state->library, &state->layout) != GPU_OK)
    return 0;

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  pipelineInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize        = sizeof(pipelineInfo);
  pipelineInfo.layout                  = state->layout->pipelineLayout;
  pipelineInfo.library                 = state->library;
  pipelineInfo.vertexEntry             = "buffer_vs";
  pipelineInfo.fragmentEntry           = "buffer_fs";
  pipelineInfo.pColorTargets           = &color;
  pipelineInfo.colorTargetCount        = 1u;
  pipelineInfo.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &state->pipeline) != GPU_OK)
    return 0;

  entry.textureView = state->view;
  entry.bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = state->layout->bindGroupLayouts[0];
  groupInfo.pEntries         = &entry;
  groupInfo.entryCount       = 1u;
  return GPUCreateBindGroup(device, &groupInfo, &state->group) == GPU_OK;
}

static void
render(void *user) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  UploaderSample               *state;
  GPUFrame                   *frame;
  GPUCommandBuffer           *command;
  GPURenderPassEncoder       *pass;

  state = user;
#if defined(GPU_SAMPLE_GALLERY_APPLE)
  if (__atomic_load_n(&workerFailed, __ATOMIC_ACQUIRE)) {
    set_status("GPU: worker upload failed", 1);
    stop_workers();
    return;
  }
#endif
  if (!resize_webgpu_canvas(state->swapchain, &state->width, &state->height)
      || !(frame = GPUBeginFrame(state->swapchain)))
    return;

  if (GPUAcquireCommandBuffer(state->queue, "sample uploaded bytes", &command) != GPU_OK) {
    GPUEndFrame(frame);
    return;
  }

  color.view     = GPUFrameGetTargetView(frame);
  color.loadOp   = GPU_LOAD_OP_CLEAR;
  color.storeOp  = GPU_STORE_OP_STORE;
  info.pColorAttachments     = &color;
  info.colorAttachmentCount = 1u;
  if (!(pass = GPUBeginRenderPass(command, &info))) {
    GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    return;
  }

  GPUBindRenderPipeline(pass, state->pipeline);
  GPUBindRenderGroup(pass, 0u, state->group, 0u, NULL);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, command, frame) != GPU_OK)
    set_status("GPU: uploader presentation failed", 1);
}

static void
ready(GPUResult result, GPUAdapter *adapter, GPUDevice *device, void *user) {
  UploaderSample *state;

  state = user;
  if (result != GPU_OK || !GPUIsFeatureEnabled(device, GPU_FEATURE_THREADED_UPLOADS)) {
    set_status_notice("GPU: threaded uploaders unavailable on this backend");
    return;
  }

  state->queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  state->surface = GPUCreateSurfaceFromNative(state->instance, adapter, (void *)"#canvas",
                                             GPU_SURFACE_WEB_CANVAS, 1.0f);
  if (!state->surface || !resize_webgpu_canvas(NULL, &state->width, &state->height)
      || !(state->swapchain = GPUCreateSwapchainDefault(device, state->surface, state->width, state->height))
      || !prepare(state, device)) {
    set_status("GPU: failed to prepare uploader sample", 1);
    return;
  }

#if defined(GPU_SAMPLE_GALLERY_APPLE)
  atexit(stop_workers);
  for (workerCount = 0u; workerCount < 4u; workerCount++) {
    workers[workerCount].queue   = state->queue;
    workers[workerCount].texture = state->texture;
    workers[workerCount].index   = workerCount;
    if (pthread_create(&workers[workerCount].thread, NULL, upload, &workers[workerCount]) != 0) {
      set_status("GPU: failed to start upload worker", 1);
      stop_workers();
      return;
    }
  }

  set_status("GPU: four independent staging rings upload while the main thread renders", 0);
#else
  set_status_notice("GPU: this backend has no qualified threaded-upload implementation");
  return;
#endif
  emscripten_set_main_loop_arg(render, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  const GPUFeature features[] = {GPU_FEATURE_THREADED_UPLOADS};

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  if (GPUCreateInstance(&info, &app.instance) != GPU_OK)
    return 1;

  return request_webgpu_device_features(app.instance, &app.request, ready, &app,
                                        features, 1u) == GPU_OK ? 0 : 1;
}
