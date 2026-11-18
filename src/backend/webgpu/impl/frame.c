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

#include "../common.h"
#include "../impl.h"
#include "../../../api/pacer_internal.h"

/* the surface's texture as the frame's target; frame->drawable is left as it is */
static bool
webgpu__acquireTexture(GPUSwapchain *swapchain, GPUSwapchainWebGPU *native) {
  WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;

  wgpuSurfaceGetCurrentTexture(native->surface, &surfaceTexture);

  if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal
      && surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
    if (surfaceTexture.texture) {
      wgpuTextureRelease(surfaceTexture.texture);
    }

    switch (surfaceTexture.status) {
      case WGPUSurfaceGetCurrentTextureStatus_Outdated:
        swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_OUT_OF_DATE);
        break;
      case WGPUSurfaceGetCurrentTextureStatus_Lost:
        swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_SURFACE_LOST);
        break;
      default:
        swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_UNAVAILABLE);
        break;
    }

    return false;
  }

  native->currentTexture = surfaceTexture.texture;

  if (!(native->currentView = wgpuTextureCreateView(native->currentTexture, NULL))) {
    wgpuTextureRelease(native->currentTexture);
    native->currentTexture = NULL;
    return false;
  }

  memset(&native->texture, 0, sizeof(native->texture));
  memset(&native->view, 0, sizeof(native->view));

  native->texture._priv         = native->currentTexture;
  native->texture.format        = swapchain->format;
  native->texture.dimension     = GPU_TEXTURE_DIMENSION_2D;
  native->texture.width         = swapchain->width;
  native->texture.height        = swapchain->height;
  native->texture.depthOrLayers = 1u;
  native->texture.mipLevelCount = 1u;
  native->texture.sampleCount   = 1u;
  native->texture.usage         = GPU_TEXTURE_USAGE_COLOR_TARGET;

  native->view._priv           = native->currentView;
  native->view._texture        = &native->texture;
  native->view.format          = swapchain->format;
  native->view.viewType        = GPU_TEXTURE_VIEW_2D;
  native->view.mipLevelCount   = 1u;
  native->view.arrayLayerCount = 1u;

  native->frame.target     = &native->texture;
  native->frame.targetView = &native->view;

  if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
    swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_SUBOPTIMAL);
  }

  return true;
}

static GPUFrame*
webgpu_beginFrame(GPUApi *api, GPUSwapchain *swapchain) {
  GPUSwapchainWebGPU *native;

  GPU__UNUSED(api);
  native = webgpuSwapchain(swapchain);

  if (!native || native->acquired) {
    return NULL;
  }

  memset(&native->frame, 0, sizeof(native->frame));
  if (!webgpu__acquireTexture(swapchain, native)) {
    return NULL;
  }

  native->frame._priv    = native;
  native->frame.drawable = native->currentTexture;
  native->acquired       = true;

  return &native->frame;
}

/* a pacer's tick: the texture is taken at the frame's first use. A browser
 * presents the canvas's texture once it has been asked for (cleared, if
 * nothing was drawn), and a native surface's would wait for a present: a tick
 * that draws nothing asks for none */
static GPUFrame*
webgpu_beginDrawable(GPUApi *api, GPUSwapchain *swapchain, void *pacer) {
  GPUSwapchainWebGPU *native;

  GPU__UNUSED(api);
  GPU__UNUSED(pacer);
  native = webgpuSwapchain(swapchain);

  if (!native || native->acquired) {
    return NULL;
  }

  memset(&native->frame, 0, sizeof(native->frame));
  native->frame._priv       = native;
  native->frame.drawable    = native;
  native->frame.targetState = GPU_FRAME_TARGET_PENDING;
  native->acquired          = true;

  return &native->frame;
}

static bool
webgpu_acquireDrawable(GPUApi *api, GPUFrame *frame) {
  GPUSwapchainWebGPU *native;

  GPU__UNUSED(api);
  native = frame->_priv;

  return native && native->acquired && !native->currentTexture
         && webgpu__acquireTexture(frame->swapchain, native);
}

static void
webgpu_endFrame(GPUApi *api, GPUFrame *frame) {
  GPUSwapchainWebGPU *native;

  GPU__UNUSED(api);
  native = frame ? frame->_priv : NULL;

  if (!native) {
    return;
  }

  if (native->currentView) {
    wgpuTextureViewRelease(native->currentView);
  }

  if (native->currentTexture) {
    wgpuTextureRelease(native->currentTexture);
  }

  native->currentView    = NULL;
  native->currentTexture = NULL;
  native->acquired       = false;
}

void
webgpu_initFrame(GPUFrameApi *api) {
  api->beginFrame      = webgpu_beginFrame;
  api->endFrame        = webgpu_endFrame;
  api->beginDrawable   = webgpu_beginDrawable;
  api->acquireDrawable = webgpu_acquireDrawable;
  framePacerUseClock(api);
}
