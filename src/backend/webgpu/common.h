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
#include "../../api/query_internal.h"
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
  GPU_WEBGPU_MAX_PRESENT_MODES       = 4u,
  GPU_WEBGPU_QUERY_RESOLVE_CAPACITY  = 64u * 1024u,
  GPU_WEBGPU_PUSH_CONSTANT_GROUP     = 3u,
  GPU_WEBGPU_PUSH_CONSTANT_BINDING   = 0u,
  GPU_WEBGPU_PUSH_CONSTANT_ALIGNMENT = 256u,
  GPU_WEBGPU_PUSH_CONSTANT_CAPACITY  = 1024u * 1024u
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
  void       *ownedPlatformHandle;
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
  WGPUBuffer                           queryResolveScratch;
  WGPUBuffer                           pushConstantBuffer;
  WGPUBindGroup                        pushConstantGroup;
  GPUSwapchainWebGPU                  *present;
  GPUCommandBuffer                     command;
  GPURenderPassDesc                    renderPass;
  GPURenderPassEncoder                 render;
  GPUComputePassEncoder                compute;
  GPUCopyPassEncoder                   copy;
  WGPURenderPassDescriptor             renderPassDesc;
  uint64_t                             boundIndexOffset;
  WGPUIndexFormat                      boundIndexFormat;
  uint32_t                             pushConstantCursor;
  atomic_bool                          inUse;
  bool                                 copyDebugGroup;
  WGPURenderPassColorAttachment        colorAttachments[
    GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS
  ];
  WGPURenderPassDepthStencilAttachment depthStencilAttachment;
} GPUCommandWebGPU;

typedef struct GPUDeviceWebGPU {
  WGPUDevice          device;
  WGPUQueue           queue;
  WGPUBindGroupLayout pushConstantLayout;
  void               *errorContext;
  GPUQueue            queueHandle;
  GPUCommandWebGPU    commands[GPU_WEBGPU_COMMAND_SLOT_COUNT];
} GPUDeviceWebGPU;

typedef struct GPUPipelineLayoutWebGPU {
  WGPUPipelineLayout layout;
  WGPUBindGroup      automaticGroups[GPU_ENCODER_MAX_BIND_GROUPS];
  uint32_t           automaticGroupMask;
  uint32_t           pushConstantSizeBytes;
} GPUPipelineLayoutWebGPU;

typedef struct GPUBindGroupLayoutWebGPU {
  WGPUBindGroupLayout layout;
  WGPUSampler        *immutableSamplers;
  uint32_t            immutableSamplerCount;
  uint32_t            nativeEntryCount;
} GPUBindGroupLayoutWebGPU;

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

WGPUSampler
gpu_webgpuCreateSampler(GPUDevice           *device,
                        const GPUSamplerDesc *desc,
                        const char           *label);

GPUResult
gpu_webgpuCreatePipelineLayout(GPUDevice               *device,
                               GPUPipelineLayout       *logicalLayout,
                               uint32_t                 requiredGroupMask,
                               GPUPipelineLayoutWebGPU *outLayout);

GPUResult
gpu_webgpuInitPushConstants(GPUDeviceWebGPU *device);

void
gpu_webgpuDestroyPushConstants(GPUDeviceWebGPU *device);

bool
gpu_webgpuUploadPushConstants(GPUCommandWebGPU *command,
                              const void        *data,
                              uint32_t           sizeBytes,
                              uint32_t          *outDynamicOffset);

void
gpu_webgpuDestroyPipelineLayout(GPUPipelineLayoutWebGPU *layout);

void
gpu_webgpuBindRenderAutomaticGroups(GPURenderPassEncoder          *pass,
                                    const GPUPipelineLayoutWebGPU *layout);

void
gpu_webgpuBindComputeAutomaticGroups(GPUComputePassEncoder         *pass,
                                     const GPUPipelineLayoutWebGPU *layout);

#endif /* webgpu_common_h */
