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

#ifndef webgpu_common_h
#define webgpu_common_h

#include "../../common.h"
#include "../../api/adapter_internal.h"
#include "../../api/buffer_internal.h"
#include "../../api/cmdqueue_internal.h"
#include "../../api/compute_internal.h"
#include "../../api/descr/descriptor_internal.h"
#include "../../api/device_internal.h"
#include "../../api/frame_internal.h"
#include "../../api/instance_internal.h"
#include "../../api/library_internal.h"
#include "../../api/render/pipeline_internal.h"
#include "../../api/sampler_internal.h"
#include "../../api/surface_internal.h"
#include "../../api/swapchain_internal.h"
#include "../../api/texture_internal.h"

#include <webgpu/webgpu.h>

#include <stdatomic.h>

enum {
  GPU_WEBGPU_COMMAND_SLOT_COUNT      = 8u,
  GPU_WEBGPU_MAX_SURFACE_FORMATS     = 16u,
  GPU_WEBGPU_MAX_PRESENT_MODES       = 4u
};

typedef struct GPUInstanceWebGPU {
  WGPUInstance instance;
} GPUInstanceWebGPU;

typedef struct GPUAdapterWebGPU {
  WGPUAdapter adapter;
  char        name[128];
} GPUAdapterWebGPU;

typedef struct GPUSurfaceWebGPU {
  WGPUSurface surface;
  uint32_t    formats[GPU_WEBGPU_MAX_SURFACE_FORMATS];
  uint32_t    presentModes[GPU_WEBGPU_MAX_PRESENT_MODES];
  uint32_t    formatCount;
  uint32_t    presentModeCount;
} GPUSurfaceWebGPU;

typedef struct GPUSwapchainWebGPU GPUSwapchainWebGPU;

typedef struct GPUCommandWebGPU {
  WGPUCommandEncoder                   encoder;
  WGPUCommandBuffer                    submitted;
  WGPURenderPassEncoder                renderEncoder;
  WGPUComputePassEncoder               computeEncoder;
  WGPUBuffer                           boundIndexBuffer;
  GPUSwapchainWebGPU                  *present;
  GPUCommandBuffer                     command;
  GPURenderPassDesc                    renderPass;
  GPURenderPassEncoder                 render;
  GPUComputePassEncoder                compute;
  GPUCopyPassEncoder                   copy;
  WGPURenderPassDescriptor             renderPassDesc;
  uint64_t                             boundIndexOffset;
  WGPUIndexFormat                      boundIndexFormat;
  atomic_bool                          inUse;
  bool                                 copyDebugGroup;
  WGPURenderPassColorAttachment        colorAttachments[
    GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS
  ];
  WGPURenderPassDepthStencilAttachment depthStencilAttachment;
} GPUCommandWebGPU;

typedef struct GPUDeviceWebGPU {
  WGPUDevice       device;
  WGPUQueue        queue;
  void            *errorContext;
  GPUQueue         queueHandle;
  GPUCommandWebGPU commands[GPU_WEBGPU_COMMAND_SLOT_COUNT];
} GPUDeviceWebGPU;

typedef struct GPUPipelineLayoutWebGPU {
  WGPUPipelineLayout layout;
  WGPUBindGroup      emptyGroups[GPU_ENCODER_MAX_BIND_GROUPS];
  uint32_t           emptyGroupMask;
} GPUPipelineLayoutWebGPU;

typedef struct GPURenderPipelineWebGPU {
  WGPURenderPipeline      pipeline;
  GPUPipelineLayoutWebGPU layout;
} GPURenderPipelineWebGPU;

typedef struct GPUComputePipelineWebGPU {
  GPUComputePipelineState base;
  WGPUComputePipeline     pipeline;
  GPUPipelineLayoutWebGPU layout;
} GPUComputePipelineWebGPU;

struct GPUSwapchainWebGPU {
  WGPUSurface      surface;
  WGPUDevice       device;
  WGPUTexture      currentTexture;
  WGPUTextureView  currentView;
  GPUFrame         frame;
  GPUTexture       texture;
  GPUTextureView   view;
  WGPUTextureFormat format;
  WGPUPresentMode   presentMode;
  bool              acquired;
};

static GPU_INLINE WGPUStringView
gpu_webgpuString(const char *text) {
  WGPUStringView result = WGPU_STRING_VIEW_INIT;

  if (text) {
    result.data   = text;
    result.length = WGPU_STRLEN;
  }
  return result;
}

static GPU_INLINE WGPUStringView
gpu_webgpuStringSize(const void *text, uint64_t size) {
  WGPUStringView result = WGPU_STRING_VIEW_INIT;

  result.data   = text;
  result.length = (size_t)size;
  return result;
}

static GPU_INLINE GPUInstanceWebGPU *
gpu_webgpuInstance(const GPUInstance *instance) {
  return instance ? instance->_priv : NULL;
}

static GPU_INLINE GPUAdapterWebGPU *
gpu_webgpuAdapter(const GPUAdapter *adapter) {
  return adapter ? adapter->_priv : NULL;
}

static GPU_INLINE GPUDeviceWebGPU *
gpu_webgpuDevice(const GPUDevice *device) {
  return device ? device->_priv : NULL;
}

static GPU_INLINE GPUSurfaceWebGPU *
gpu_webgpuSurface(const GPUSurface *surface) {
  return surface ? surface->_priv : NULL;
}

static GPU_INLINE GPUSwapchainWebGPU *
gpu_webgpuSwapchain(const GPUSwapchain *swapchain) {
  return swapchain ? swapchain->_priv : NULL;
}

static GPU_INLINE GPUCommandWebGPU *
gpu_webgpuCommand(const GPUCommandBuffer *cmdb) {
  return cmdb ? cmdb->_priv : NULL;
}

WGPUTextureFormat gpu_webgpuFormat(GPUFormat format);
GPUFormat gpu_webgpuGPUFormat(WGPUTextureFormat format);
WGPUPresentMode gpu_webgpuPresentMode(GPUPresentMode mode);

GPUResult
gpu_webgpuCreatePipelineLayout(GPUDevice               *device,
                               GPUPipelineLayout       *logicalLayout,
                               uint32_t                 requiredGroupMask,
                               GPUPipelineLayoutWebGPU *outLayout);

void
gpu_webgpuDestroyPipelineLayout(GPUPipelineLayoutWebGPU *layout);

void
gpu_webgpuBindRenderEmptyGroups(GPURenderPassEncoder          *pass,
                                const GPUPipelineLayoutWebGPU *layout);

void
gpu_webgpuBindComputeEmptyGroups(GPUComputePassEncoder         *pass,
                                 const GPUPipelineLayoutWebGPU *layout);

#endif /* webgpu_common_h */
