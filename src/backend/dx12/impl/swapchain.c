/*
 * Copyright (C) 2020 Recep Aslantas
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

static void
dx12__logSwapchainError(const char *operation, HRESULT result) {
  fprintf(stderr,
          "GPU Direct3D 12 swapchain %s failed: 0x%08lx\n",
          operation,
          (unsigned long)result);
}

static void
dx12__releaseBackBuffers(GPUSwapchainDX12 *swapchain) {
  if (!swapchain || !swapchain->frames) {
    return;
  }

  for (UINT i = 0u; i < swapchain->imageCount; i++) {
    GPUFrameDX12 *frame;

    frame = &swapchain->frames[i];
    if (frame->renderTarget) {
      frame->renderTarget->lpVtbl->Release(frame->renderTarget);
    }
    memset(frame, 0, sizeof(*frame));
  }
}

static bool
dx12__createBackBuffers(GPUDevice          *device,
                        GPUSwapchainDX12   *swapchain,
                        GPUFormat           format,
                        uint32_t            width,
                        uint32_t            height) {
  GPUDeviceDX12              *deviceDX12;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv;
  HRESULT                     result;

  if (!device || !device->_priv || !swapchain || !swapchain->swapchain ||
      !swapchain->rtvHeap || !swapchain->frames || width == 0u || height == 0u) {
    return false;
  }

  deviceDX12 = device->_priv;
  (void)swapchain->rtvHeap->lpVtbl->GetCPUDescriptorHandleForHeapStart(
    swapchain->rtvHeap,
    &rtv
  );

  for (UINT i = 0u; i < swapchain->imageCount; i++) {
    GPUFrameDX12 *frame;

    frame = &swapchain->frames[i];
    result = swapchain->swapchain->lpVtbl->GetBuffer(
      swapchain->swapchain,
      i,
      &IID_ID3D12Resource,
      (void **)&frame->renderTarget
    );
    if (FAILED(result)) {
      dx12__logSwapchainError("GetBuffer", result);
      dx12__releaseBackBuffers(swapchain);
      return false;
    }

    deviceDX12->d3dDevice->lpVtbl->CreateRenderTargetView(
      deviceDX12->d3dDevice,
      frame->renderTarget,
      NULL,
      rtv
    );

    frame->swapchain           = swapchain;
    frame->state               = D3D12_RESOURCE_STATE_PRESENT;
    frame->nativeView.resource = frame->renderTarget;
    frame->nativeView.state    = &frame->state;
    frame->nativeView.rtv       = rtv;
    frame->nativeView.width     = width;
    frame->nativeView.height    = height;
    frame->nativeView.hasRtv    = true;
    frame->nativeView.swapchain = true;

    frame->target._priv          = &frame->nativeView;
    frame->target.device         = device;
    frame->target.format         = format;
    frame->target.dimension      = GPU_TEXTURE_DIMENSION_2D;
    frame->target.width          = width;
    frame->target.height         = height;
    frame->target.depthOrLayers  = 1u;
    frame->target.mipLevelCount  = 1u;
    frame->target.sampleCount    = 1u;
    frame->target.usage          = GPU_TEXTURE_USAGE_COLOR_TARGET;
    frame->target._ownsNative    = false;

    frame->targetView._priv           = &frame->nativeView;
    frame->targetView._texture        = &frame->target;
    frame->targetView.format          = format;
    frame->targetView.viewType        = GPU_TEXTURE_VIEW_2D;
    frame->targetView.baseMipLevel    = 0u;
    frame->targetView.mipLevelCount   = 1u;
    frame->targetView.baseArrayLayer  = 0u;
    frame->targetView.arrayLayerCount = 1u;
    frame->targetView._ownsNative     = false;

    frame->frame.device     = device;
    frame->frame._priv      = swapchain;
    frame->frame.target     = &frame->target;
    frame->frame.targetView = &frame->targetView;
    rtv.ptr                += swapchain->rtvDescriptorSize;
  }

  return true;
}

static void
dx12__destroySwapchainState(GPUSwapchainDX12 *swapchain) {
  if (!swapchain) {
    return;
  }

  dx12__releaseBackBuffers(swapchain);
  if (swapchain->rtvHeap) {
    swapchain->rtvHeap->lpVtbl->Release(swapchain->rtvHeap);
  }
  if (swapchain->swapchain) {
    swapchain->swapchain->lpVtbl->Release(swapchain->swapchain);
  }
  if (swapchain->frameEvent) {
    CloseHandle(swapchain->frameEvent);
  }
  free(swapchain->frames);
  free(swapchain);
}

GPU_HIDE
GPUSwapchain*
dx12_createSwapchain(GPUApi                    * __restrict api,
                     GPUDevice                 * __restrict device,
                     GPUCommandQueue           * __restrict queue,
                     const GPUSwapchainCreateInfo * __restrict info) {
  GPUInstanceDX12            *instanceDX12;
  GPUDeviceDX12              *deviceDX12;
  GPUCommandQueueDX12        *queueDX12;
  GPUSwapchainDX12           *native;
  GPUSwapchain               *swapchain;
  IDXGISwapChain1            *swapchain1;
  D3D12_DESCRIPTOR_HEAP_DESC  heapDesc = {0};
  DXGI_SWAP_CHAIN_DESC1       desc = {0};
  DXGI_FORMAT                 format;
  UINT                        imageCount;
  HRESULT                     result;

  GPU__UNUSED(api);

  if (!device || !device->_priv || !device->inst || !queue || !queue->_priv ||
      !info || !info->surface || info->width == 0u || info->height == 0u) {
    return NULL;
  }

  format = dx12_format(info->format);
  if (format == DXGI_FORMAT_UNKNOWN) {
    return NULL;
  }

  imageCount = info->imageCount ? info->imageCount : 2u;
  if (imageCount < 2u || imageCount > 3u) {
    return NULL;
  }

  instanceDX12 = device->inst->_priv;
  deviceDX12   = device->_priv;
  queueDX12    = queue->_priv;
  if (!instanceDX12 || !instanceDX12->dxgiFactory ||
      !deviceDX12->d3dDevice || !queueDX12->commandQueue) {
    return NULL;
  }

  native    = calloc(1, sizeof(*native));
  swapchain = calloc(1, sizeof(*swapchain));
  if (!native || !swapchain) {
    free(swapchain);
    free(native);
    return NULL;
  }

  native->frames = calloc(imageCount, sizeof(*native->frames));
  if (!native->frames) {
    free(swapchain);
    free(native);
    return NULL;
  }

  desc.Width              = info->width;
  desc.Height             = info->height;
  desc.Format             = format;
  desc.Stereo             = FALSE;
  desc.SampleDesc.Count   = 1u;
  desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount        = imageCount;
  desc.Scaling            = DXGI_SCALING_STRETCH;
  desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.AlphaMode          = DXGI_ALPHA_MODE_UNSPECIFIED;
  desc.Flags              = 0u;

  swapchain1 = NULL;
  switch (info->surface->type) {
    case GPU_SURFACE_WINDOWS_HWND:
      result = instanceDX12->dxgiFactory->lpVtbl->CreateSwapChainForHwnd(
        instanceDX12->dxgiFactory,
        (IUnknown *)queueDX12->commandQueue,
        (HWND)info->surface->_priv,
        &desc,
        NULL,
        NULL,
        &swapchain1
      );
      if (SUCCEEDED(result)) {
        (void)instanceDX12->dxgiFactory->lpVtbl->MakeWindowAssociation(
          instanceDX12->dxgiFactory,
          (HWND)info->surface->_priv,
          DXGI_MWA_NO_ALT_ENTER
        );
      }
      break;
    case GPU_SURFACE_WINDOWS_COREWINDOW:
      result = instanceDX12->dxgiFactory->lpVtbl->CreateSwapChainForCoreWindow(
        instanceDX12->dxgiFactory,
        (IUnknown *)queueDX12->commandQueue,
        (IUnknown *)info->surface->_priv,
        &desc,
        NULL,
        &swapchain1
      );
      break;
    default:
      result = E_INVALIDARG;
      break;
  }
  if (FAILED(result) || !swapchain1) {
    dx12__logSwapchainError("create", result);
    dx12__destroySwapchainState(native);
    free(swapchain);
    return NULL;
  }

  result = swapchain1->lpVtbl->QueryInterface(swapchain1,
                                              &IID_IDXGISwapChain3,
                                              (void **)&native->swapchain);
  swapchain1->lpVtbl->Release(swapchain1);
  if (FAILED(result) || !native->swapchain) {
    dx12__logSwapchainError("QueryInterface", result);
    dx12__destroySwapchainState(native);
    free(swapchain);
    return NULL;
  }

  native->frameEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
  if (!native->frameEvent) {
    dx12__destroySwapchainState(native);
    free(swapchain);
    return NULL;
  }

  heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heapDesc.NumDescriptors = imageCount;
  result = deviceDX12->d3dDevice->lpVtbl->CreateDescriptorHeap(
    deviceDX12->d3dDevice,
    &heapDesc,
    &IID_ID3D12DescriptorHeap,
    (void **)&native->rtvHeap
  );
  if (FAILED(result)) {
    dx12__logSwapchainError("RTV heap", result);
    dx12__destroySwapchainState(native);
    free(swapchain);
    return NULL;
  }

  native->queue             = queueDX12;
  native->format            = format;
  native->imageCount        = imageCount;
  native->rtvDescriptorSize = deviceDX12->d3dDevice->lpVtbl
    ->GetDescriptorHandleIncrementSize(deviceDX12->d3dDevice,
                                       D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  native->syncInterval = info->presentMode == GPU_PRESENT_MODE_FIFO ? 1u : 0u;
  native->presentFlags = 0u;
  native->frameIndex   = native->swapchain->lpVtbl->GetCurrentBackBufferIndex(
    native->swapchain
  );
  if (!dx12__createBackBuffers(device,
                               native,
                               info->format,
                               info->width,
                               info->height)) {
    dx12__destroySwapchainState(native);
    free(swapchain);
    return NULL;
  }

  swapchain->_priv = native;
  return swapchain;
}

GPU_HIDE
GPUResult
dx12_resizeSwapchain(GPUSwapchain *swapchain, GPUExtent2D size) {
  GPUSwapchainDX12 *native;
  GPUDevice        *device;
  GPUFormat         format;
  HRESULT           result;

  native = swapchain ? swapchain->_priv : NULL;
  device = swapchain ? swapchain->device : NULL;
  if (!native || !device || size.width == 0u || size.height == 0u ||
      native->frameActive || !dx12_waitCommandQueueIdle(native->queue)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  format = native->frames[0].target.format;
  dx12__releaseBackBuffers(native);
  result = native->swapchain->lpVtbl->ResizeBuffers(native->swapchain,
                                                     native->imageCount,
                                                     size.width,
                                                     size.height,
                                                     native->format,
                                                     0u);
  if (FAILED(result) ||
      !dx12__createBackBuffers(device,
                               native,
                               format,
                               size.width,
                               size.height)) {
    if (FAILED(result)) {
      dx12__logSwapchainError("resize", result);
    }
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->frameIndex = native->swapchain->lpVtbl->GetCurrentBackBufferIndex(
    native->swapchain
  );
  return GPU_OK;
}

GPU_HIDE
void
dx12_destroySwapchain(GPUSwapchain *swapchain) {
  if (!swapchain) {
    return;
  }

  dx12__destroySwapchainState(swapchain->_priv);
  free(swapchain);
}

GPU_HIDE
void
dx12_initSwapchain(GPUApiSwapchain *api) {
  api->createSwapchain  = dx12_createSwapchain;
  api->resizeSwapchain  = dx12_resizeSwapchain;
  api->destroySwapchain = dx12_destroySwapchain;
}
