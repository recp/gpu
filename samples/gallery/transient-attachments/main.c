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

typedef struct RetiredDepth {
  GPUTexture          *texture;
  GPUTextureView      *view;
  GPUFence            *fence;
  struct RetiredDepth *next;
} RetiredDepth;

typedef struct TransientSample {
  GPUInstance    *instance;
  GPUDevice      *device;
  GPUQueue       *queue;
  GPUSurface     *surface;
  GPUSwapchain   *swapchain;
  GPUTexture     *depth;
  GPUTextureView *depthView;
  RetiredDepth   *retired;
  WebGPURequest  request;
  uint32_t       width;
  uint32_t       height;
  uint32_t       depthWidth;
  uint32_t       depthHeight;
} TransientSample;

static TransientSample app;

static void
collect_depth(TransientSample *state) {
  RetiredDepth **link;
  RetiredDepth  *item;

  link = &state->retired;
  while ((item = *link)) {
    if (!GPUIsFenceSignaled(item->fence)) {
      link = &item->next;
      continue;
    }

    *link = item->next;
    GPUDestroyFence(item->fence);
    GPUDestroyTextureView(item->view);
    GPUDestroyTexture(item->texture);
    free(item);
  }
}

static bool
retire_depth(TransientSample *state) {
  GPUQueueSubmitInfo info = {0};
  GPUCommandBuffer  *command;
  RetiredDepth      *item;

  if (!state->depth)
    return true;

  if (!(item = calloc(1u, sizeof(*item))))
    return false;

  if (GPUCreateFence(state->device, NULL, &item->fence) != GPU_OK
      || GPUAcquireCommandBuffer(state->queue, "retire resized depth", &command) != GPU_OK) {
    GPUDestroyFence(item->fence);
    free(item);
    return false;
  }

  info.ppCommandBuffers   = &command;
  info.commandBufferCount = 1u;
  info.fence              = item->fence;
  if (GPUQueueSubmit(state->queue, &info) != GPU_OK) {
    GPUDestroyFence(item->fence);
    free(item);
    return false;
  }

  /* only resizing allocates a retirement record; ordinary frames never wait. */
  item->texture    = state->depth;
  item->view       = state->depthView;
  item->next       = state->retired;
  state->retired   = item;
  state->depth     = NULL;
  state->depthView = NULL;
  return true;
}

static void
tick(void *user) {
  GPURenderPassColorAttachment        color = {0};
  GPURenderPassDepthStencilAttachment depth = {0};
  GPURenderPassCreateInfo             info  = {0};

  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTextureInfo           targetInfo;
  TransientSample        *state;
  GPUFrame               *frame;
  GPUCommandBuffer       *command;
  GPURenderPassEncoder   *pass;

  state = user;
  collect_depth(state);
  if (!resize_webgpu_canvas(state->swapchain, &state->width, &state->height)
      || !(frame = GPUBeginFrame(state->swapchain)))
    return;

  if (GPUGetTextureInfo(GPUFrameGetTarget(frame), &targetInfo) != GPU_OK) {
    GPUEndFrame(frame);
    return;
  }

  if (state->depthWidth != targetInfo.width || state->depthHeight != targetInfo.height) {
    if (!retire_depth(state)) {
      GPUEndFrame(frame);
      set_status("GPU: transient attachment retirement failed", 1);
      return;
    }
    textureInfo.dimension     = GPU_TEXTURE_DIMENSION_2D;
    textureInfo.format        = GPU_FORMAT_DEPTH32_FLOAT;
    textureInfo.width         = targetInfo.width;
    textureInfo.height        = targetInfo.height;
    textureInfo.depthOrLayers = textureInfo.mipLevelCount = textureInfo.sampleCount = 1u;
    textureInfo.usage         = GPU_TEXTURE_USAGE_DEPTH_STENCIL | GPU_TEXTURE_USAGE_TRANSIENT_ATTACHMENT;
    viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
    viewInfo.format           = textureInfo.format;
    viewInfo.mipLevelCount    = viewInfo.arrayLayerCount = 1u;
    if (GPUCreateTexture(state->device, &textureInfo, &state->depth) != GPU_OK
        || GPUCreateTextureView(state->depth, &viewInfo, &state->depthView) != GPU_OK) {
      GPUEndFrame(frame);
      set_status("GPU: transient depth allocation failed", 1);
      return;
    }
    state->depthWidth  = targetInfo.width;
    state->depthHeight = targetInfo.height;
  }

  if (GPUAcquireCommandBuffer(state->queue, "transient attachment", &command) != GPU_OK) {
    GPUEndFrame(frame);
    return;
  }

  color.view                   = GPUFrameGetTargetView(frame);
  color.loadOp                 = GPU_LOAD_OP_CLEAR;
  color.storeOp                = GPU_STORE_OP_STORE;
  color.clearColor.float32[0]  = 0.12f;
  color.clearColor.float32[1]  = 0.35f;
  color.clearColor.float32[2]  = 0.65f;
  color.clearColor.float32[3]  = 1.0f;
  depth.view                   = state->depthView;
  depth.depthLoadOp            = GPU_LOAD_OP_CLEAR;
  depth.depthStoreOp           = GPU_STORE_OP_DONT_CARE;
  depth.stencilLoadOp          = GPU_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp         = GPU_STORE_OP_DONT_CARE;
  depth.clearDepth             = 1.0f;
  info.pColorAttachments       = &color;
  info.colorAttachmentCount    = 1u;
  info.pDepthStencilAttachment = &depth;
  if (!(pass = GPUBeginRenderPass(command, &info))) {
    GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    set_status("GPU: transient attachment pass failed", 1);
    return;
  }
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, command, frame) != GPU_OK)
    set_status("GPU: transient attachment presentation failed", 1);
}

static void
ready(GPUResult result, GPUAdapter *adapter, GPUDevice *device, void *user) {
  TransientSample *state;

  state = user;
  if (result != GPU_OK) {
    set_status("GPU: device unavailable", 1);
    return;
  }
  state->device = device;
  state->queue  = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  state->surface = GPUCreateSurfaceFromNative(state->instance, adapter, (void *)"#canvas",
                                             GPU_SURFACE_WEB_CANVAS, 1.0f);
  if (!state->surface || !resize_webgpu_canvas(NULL, &state->width, &state->height)
      || !(state->swapchain = GPUCreateSwapchainDefault(device, state->surface,
                                                        state->width, state->height))) {
    set_status("GPU: transient sample surface unavailable", 1);
    return;
  }
  set_status(GPUIsFeatureEnabled(device, GPU_FEATURE_MEMORYLESS_ATTACHMENTS)
               ? "GPU: native memoryless depth; clear at pass begin, discard at end"
               : "GPU: transient depth with ordinary backing on this backend", 0);
  emscripten_set_main_loop_arg(tick, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  const GPUFeature feature = GPU_FEATURE_MEMORYLESS_ATTACHMENTS;

  info.preferredBackend = GPU_BACKEND_WEBGPU;
  if (GPUCreateInstance(&info, &app.instance) != GPU_OK)
    return 1;

  return request_webgpu_device_features(app.instance, &app.request, ready, &app,
                                        &feature, 1u) == GPU_OK ? 0 : 1;
}
