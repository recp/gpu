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

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
#  include <webgpu/wgpu.h>
#else
#  include <webgpu/webgpu.h>
#endif

#include <stdatomic.h>

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
#  if defined(_WIN32) || defined(WIN32)
#    include <windows.h>
#  else
#    include <pthread.h>
#  endif
#  define GPU_WEBGPU_FEATURE_NORM16 \
    ((WGPUFeatureName)WGPUNativeFeature_TextureFormat16bitNorm)
#  define GPU_WEBGPU_FEATURE_MULTI_DRAW \
    ((WGPUFeatureName)WGPUNativeFeature_MultiDrawIndirectCount)
#else
#  define GPU_WEBGPU_FEATURE_NORM16 WGPUFeatureName_Unorm16TextureFormats
#  define GPU_WEBGPU_FEATURE_MULTI_DRAW WGPUFeatureName_MultiDrawIndirect
#endif

enum {
  GPU_WEBGPU_COMMAND_SLOT_COUNT      = 16u,
  GPU_WEBGPU_MAX_SURFACE_FORMATS     = 16u,
  GPU_WEBGPU_MAX_PRESENT_MODES       = 4u,
  GPU_WEBGPU_MAX_QUERY_COUNT         = 4096u,
  GPU_WEBGPU_QUERY_RESOLVE_CAPACITY  = GPU_WEBGPU_MAX_QUERY_COUNT * sizeof(uint64_t),
  GPU_WEBGPU_PUSH_CONSTANT_GROUP     = 3u,
  GPU_WEBGPU_PUSH_CONSTANT_BINDING   = 0u,
  GPU_WEBGPU_PUSH_CONSTANT_ALIGNMENT = 256u,
  GPU_WEBGPU_PUSH_CONSTANT_CAPACITY  = 1024u * 1024u
};

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
typedef struct WebGPUPipelineError {
  struct WebGPUPipelineError    *previous;
  GPUDevice                    *device;
  GPUDeviceErrorType            type;
  GPUResult                     result;
  char                          message[512];
} WebGPUPipelineError;
#endif

typedef struct InstanceWebGPU {
  WGPUInstance instance;
#if GPU_WEBGPU_PROVIDER_DAWN && !defined(__EMSCRIPTEN__)
  bool         timedWaitAny;
#endif
} InstanceWebGPU;

typedef struct AdapterWebGPU {
  WGPUAdapter adapter;
  char        name[128];
} AdapterWebGPU;

typedef struct SurfaceWebGPU {
  WGPUSurface surface;
  void       *ownedPlatformHandle;
  uint32_t    formats[GPU_WEBGPU_MAX_SURFACE_FORMATS];
  uint32_t    presentModes[GPU_WEBGPU_MAX_PRESENT_MODES];
  uint32_t    formatCount;
  uint32_t    presentModeCount;
} SurfaceWebGPU;

typedef struct SwapchainWebGPU    SwapchainWebGPU;

typedef struct CommandWebGPU {
  WGPUCommandEncoder                   encoder;
  WGPURenderPassEncoder                renderEncoder;
  WGPUComputePassEncoder               computeEncoder;
  WGPUBuffer                           boundIndexBuffer;
  WGPUBuffer                           queryResolveScratch;
  WGPUBuffer                           pushConstantBuffer;
  WGPUBindGroup                        pushConstantGroup;
  SwapchainWebGPU                     *present;
  GPUCommandBuffer                     command;
  RenderPassDesc                       renderPass;
  GPURenderPassEncoder                 render;
  GPUComputePassEncoder                compute;
  GPUTransferPassEncoder               copy;
  WGPURenderPassDescriptor             renderPassDesc;
  WGPUPassTimestampWrites              timestampWrites;
  uint64_t                             boundIndexOffset;
  WGPUIndexFormat                      boundIndexFormat;
  uint32_t                             renderWidth;
  uint32_t                             renderHeight;
  uint32_t                             pushConstantCursor;
  _Atomic(WGPUCommandBuffer)           submitted;
  atomic_bool                          inUse;
  bool                                 copyDebugGroup;
  WGPURenderPassColorAttachment        colorAttachments[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  WGPURenderPassDepthStencilAttachment depthStencilAttachment;
} CommandWebGPU;

typedef struct DeviceWebGPU {
  WGPUDevice          device;
  WGPUQueue           queue;
  WGPUBindGroupLayout pushConstantLayout;
  void               *errorContext;
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
#  if defined(_WIN32) || defined(WIN32)
  HANDLE              completionWorker;
  CRITICAL_SECTION    completionLock;
  CONDITION_VARIABLE  completionCondition;
#  else
  pthread_t           completionWorker;
  pthread_mutex_t     completionLock;
  pthread_cond_t      completionCondition;
#  endif
#endif
  WGPULimits          limits;
  GPUQueue            queueHandle;
  CommandWebGPU       commands[GPU_WEBGPU_COMMAND_SLOT_COUNT];
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  WGPUSubmissionIndex completionSubmissions[GPU_WEBGPU_COMMAND_SLOT_COUNT];
  uint32_t            completionHead;
  uint32_t            completionTail;
  uint32_t            completionCount;
  bool                completionWorkerStarted;
  bool                stoppingCompletionWorker;
#endif
} DeviceWebGPU;

typedef struct PipelineLayoutWebGPU {
  WGPUPipelineLayout layout;
  WGPUBindGroup      automaticGroups[GPU_ENCODER_MAX_BIND_GROUPS];
  uint32_t           automaticGroupMask;
  uint32_t           pushConstantSizeBytes;
} PipelineLayoutWebGPU;

typedef struct BindGroupLayoutWebGPU {
  WGPUBindGroupLayout layout;
  WGPUSampler        *immutableSamplers;
  uint32_t            immutableSamplerCount;
  uint32_t            nativeEntryCount;
} BindGroupLayoutWebGPU;

typedef struct RenderPipelineWebGPU {
  WGPURenderPipeline      pipeline;
  PipelineLayoutWebGPU    layout;
} RenderPipelineWebGPU;

typedef struct ComputePipelineWebGPU {
  ComputePipelineState    base;
  WGPUComputePipeline     pipeline;
  PipelineLayoutWebGPU     layout;
} ComputePipelineWebGPU;

struct SwapchainWebGPU {
  WGPUSurface       surface;
  WGPUDevice        device;
  WGPUTexture       currentTexture;
  WGPUTextureView   currentView;
  GPUFrame          frame;
  GPUTexture        texture;
  GPUTextureView    view;
  WGPUTextureFormat format;
  WGPUPresentMode   presentMode;
  bool              acquired;
};

WGPUTextureFormat
webgpuFormat(GPUFormat format);

GPUFormat
webgpuGPUFormat(WGPUTextureFormat format);

WGPUPresentMode
webgpuPresentMode(GPUPresentMode mode);

WGPUSampler
webgpuCreateSampler(GPUDevice                *device,
                       const GPUSamplerDesc     *desc,
                       const char               *label,
                       const GPUSamplerLODClamp *lod);

GPUResult
webgpuCreatePipelineLayout(GPUDevice               *device,
                           GPUPipelineLayout       *logicalLayout,
                           uint32_t                 requiredGroupMask,
                           uint32_t                 automaticGroupMask,
                           PipelineLayoutWebGPU    *outLayout);

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
GPU_HIDE
void
webgpuBeginPipelineError(GPUDevice              *device,
                            WebGPUPipelineError    *error);

GPU_HIDE
GPUResult
webgpuEndPipelineError(WebGPUPipelineError    *error);
#endif

GPUResult
webgpuInitPushConstants(DeviceWebGPU    *device);

void
webgpuDestroyPushConstants(DeviceWebGPU    *device);

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
bool
webgpuStartCompletionWorker(DeviceWebGPU    *device);

void
webgpuQueueCompletion(DeviceWebGPU       *device,
                      WGPUSubmissionIndex submission);

void
webgpuStopCompletionWorker(DeviceWebGPU    *device);
#endif

bool
webgpuUploadPushConstants(CommandWebGPU    *command,
                          const void       *data,
                          uint32_t          sizeBytes,
                          uint32_t         *outDynamicOffset);

void
webgpuDestroyPipelineLayout(PipelineLayoutWebGPU    *layout);

void
webgpuBindRenderAutomaticGroups(GPURenderPassEncoder          *pass,
                                const PipelineLayoutWebGPU    *layout);

void
webgpuBindComputeAutomaticGroups(GPUComputePassEncoder         *pass,
                                 const PipelineLayoutWebGPU    *layout);

static GPU_INLINE WGPUStringView
webgpuString(const char *text) {
  WGPUStringView result = WGPU_STRING_VIEW_INIT;

  if (text) {
    result.data   = text;
    result.length = WGPU_STRLEN;
  }

  return result;
}

static GPU_INLINE WGPUStringView
webgpuStringSize(const void *text, uint64_t size) {
  WGPUStringView result = WGPU_STRING_VIEW_INIT;

  result.data   = text;
  result.length = (size_t)size;

  return result;
}

static GPU_INLINE InstanceWebGPU*
webgpuInstance(const GPUInstance *instance) {
  return instance ? instance->_priv : NULL;
}

static GPU_INLINE AdapterWebGPU*
webgpuAdapter(const GPUAdapter *adapter) {
  return adapter ? adapter->_priv : NULL;
}

static GPU_INLINE DeviceWebGPU*
webgpuDevice(const GPUDevice *device) {
  return device ? device->_priv : NULL;
}

static GPU_INLINE SurfaceWebGPU*
webgpuSurface(const GPUSurface *surface) {
  return surface ? surface->_priv : NULL;
}

static GPU_INLINE SwapchainWebGPU*
webgpuSwapchain(const GPUSwapchain *swapchain) {
  return swapchain ? swapchain->_priv : NULL;
}

static GPU_INLINE CommandWebGPU*
webgpuCommand(const GPUCommandBuffer *cmdb) {
  return cmdb ? cmdb->_priv : NULL;
}

static GPU_INLINE void
webgpuMultiDrawIndirect(WGPURenderPassEncoder encoder,
                        WGPUBuffer            buffer,
                        uint64_t              offset,
                        uint32_t              count) {
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  wgpuRenderPassEncoderMultiDrawIndirect(encoder, buffer, offset, count);
#else
  wgpuRenderPassEncoderMultiDrawIndirect(encoder,
                                         buffer,
                                         offset,
                                         count,
                                         NULL,
                                         0u);
#endif
}

static GPU_INLINE void
webgpuMultiDrawIndexedIndirect(WGPURenderPassEncoder encoder,
                               WGPUBuffer            buffer,
                               uint64_t              offset,
                               uint32_t              count) {
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  wgpuRenderPassEncoderMultiDrawIndexedIndirect(encoder,
                                                buffer,
                                                offset,
                                                count);
#else
  wgpuRenderPassEncoderMultiDrawIndexedIndirect(encoder,
                                                buffer,
                                                offset,
                                                count,
                                                NULL,
                                                0u);
#endif
}

#endif /* webgpu_common_h */
