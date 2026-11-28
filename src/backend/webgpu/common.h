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

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#  include "../surface_apple.h"
#endif

#if !defined(__EMSCRIPTEN__)
#  if defined(_WIN32) || defined(WIN32)
#    include <windows.h>
#  else
#    include <pthread.h>
#  endif
#endif

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
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
typedef struct GPUWebGPUPipelineError {
  struct GPUWebGPUPipelineError *previous;
  GPUDevice                    *device;
  GPUDeviceErrorType            type;
  GPUResult                     result;
  char                          message[512];
} GPUWebGPUPipelineError;
#endif

typedef struct GPUInstanceWebGPU {
  WGPUInstance instance;
#if GPU_WEBGPU_PROVIDER_DAWN && !defined(__EMSCRIPTEN__)
  bool         timedWaitAny;
#endif
} GPUInstanceWebGPU;

typedef struct GPUAdapterWebGPU {
  WGPUAdapter adapter;
  char        name[128];
} GPUAdapterWebGPU;

typedef struct GPUSurfaceWebGPU {
  WGPUSurface surface;
  void       *ownedPlatformHandle;
  void       *shown;               /* appleWatchView's: frames only while the system shows the view */
  uint32_t    formats[GPU_WEBGPU_MAX_SURFACE_FORMATS];
  uint32_t    presentModes[GPU_WEBGPU_MAX_PRESENT_MODES];
  uint32_t    formatCount;
  uint32_t    presentModeCount;
} GPUSurfaceWebGPU;

typedef struct GPUSwapchainWebGPU GPUSwapchainWebGPU;

typedef struct GPUCommandWebGPU {
  WGPUCommandEncoder                   encoder;
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
} GPUCommandWebGPU;

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
typedef WGPUSubmissionIndex GPUWebGPUCompletion;
#elif !defined(__EMSCRIPTEN__)
typedef WGPUFuture GPUWebGPUCompletion;
#endif

typedef struct GPUDeviceWebGPU {
  WGPUDevice          device;
  WGPUQueue           queue;
  WGPUBindGroupLayout pushConstantLayout;
  void               *errorContext;
#if !defined(__EMSCRIPTEN__)
#  if GPU_WEBGPU_PROVIDER_DAWN
  WGPUInstance        completionInstance;
#  endif
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
  bool                norm16Resolve;
  WGPULimits          limits;
  _Atomic(uint64_t)   writtenBytes; /* queue writes since the last submit (webgpuQueueWrite) */
  GPUQueue            queueHandle;
  GPUCommandWebGPU    commands[GPU_WEBGPU_COMMAND_SLOT_COUNT];
#if !defined(__EMSCRIPTEN__)
  GPUWebGPUCompletion completions[GPU_WEBGPU_COMMAND_SLOT_COUNT];
  uint32_t            completionHead;
  uint32_t            completionTail;
  uint32_t            completionCount;
  bool                completionWorkerStarted;
  bool                stoppingCompletionWorker;
#endif
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
  GPUPipelineLayoutWebGPU  layout;
} GPUComputePipelineWebGPU;

struct GPUSwapchainWebGPU {
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
                           GPUPipelineLayoutWebGPU *outLayout);

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
GPU_HIDE
void
webgpuBeginPipelineError(GPUDevice              *device,
                            GPUWebGPUPipelineError *error);

GPU_HIDE
GPUResult
webgpuEndPipelineError(GPUWebGPUPipelineError *error);
#endif

GPUResult
webgpuInitPushConstants(GPUDeviceWebGPU *device);

void
webgpuDestroyPushConstants(GPUDeviceWebGPU *device);

#if !defined(__EMSCRIPTEN__)
bool
webgpuStartCompletionWorker(GPUDeviceWebGPU *device);

GPU_HIDE
bool
webgpuWaitCommandSlot(GPUDeviceWebGPU *device);

GPU_HIDE
void
webgpuWaitInFlight(GPUDeviceWebGPU *device);

GPU_HIDE
void
webgpuCommandSlotFreed(GPUDeviceWebGPU *device);

void
webgpuQueueCompletion(GPUDeviceWebGPU    *device,
                      GPUWebGPUCompletion completion);

void
webgpuStopCompletionWorker(GPUDeviceWebGPU *device);
#endif

bool
webgpuUploadPushConstants(GPUCommandWebGPU *command,
                          const void       *data,
                          uint32_t          sizeBytes,
                          uint32_t         *outDynamicOffset);

void
webgpuDestroyPipelineLayout(GPUPipelineLayoutWebGPU *layout);

void
webgpuBindRenderAutomaticGroups(GPURenderPassEncoder          *pass,
                                const GPUPipelineLayoutWebGPU *layout);

void
webgpuBindComputeAutomaticGroups(GPUComputePassEncoder         *pass,
                                 const GPUPipelineLayoutWebGPU *layout);

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

static GPU_INLINE GPUInstanceWebGPU*
webgpuInstance(const GPUInstance *instance) {
  return instance ? instance->_priv : NULL;
}

static GPU_INLINE GPUAdapterWebGPU*
webgpuAdapter(const GPUAdapter *adapter) {
  return adapter ? adapter->_priv : NULL;
}

static GPU_INLINE GPUDeviceWebGPU*
webgpuDevice(const GPUDevice *device) {
  return device ? device->_priv : NULL;
}

static GPU_INLINE GPUSurfaceWebGPU*
webgpuSurface(const GPUSurface *surface) {
  return surface ? surface->_priv : NULL;
}

/* iOS: the app in the background. The system refuses its GPU work, and Dawn
 * loses the device to one refused command buffer: nothing goes to it there
 * that it would commit (a submit, a present), and what can't wait fails
 * alone, a backend failure */
static GPU_INLINE bool
webgpuGPURefused(void) {
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  return appleGPURefused();
#else
  return false;
#endif
}

/* no frame where the system doesn't show the view (nextDrawable would wait
 * out its second), or from the background */
static GPU_INLINE bool
webgpuFramesRefused(const GPUSwapchain *swapchain) {
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  GPUSurfaceWebGPU *surface;

  surface = swapchain ? webgpuSurface(swapchain->surface) : NULL;
  return appleGPURefused() || (surface && appleViewHidden(surface->shown));
#else
  (void)swapchain;
  return false;
#endif
}

/* Dawn keeps queue writes for the next submit, and commits them itself once
 * 16 MB are waiting, or when a wait needs them: from the background a write
 * past a few MB since the last submit is refused (false), as is a map read
 * behind any (its submit couldn't go either). Every write counts; a submit
 * clears the count */
enum { GPU_WEBGPU_REFUSED_WRITE_BUDGET = 4u << 20 };

static GPU_INLINE bool
webgpuQueueWrite(GPUDeviceWebGPU *device, uint64_t sizeBytes) {
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  uint64_t written;

  written = atomic_fetch_add_explicit(&device->writtenBytes, sizeBytes, memory_order_relaxed);
  if (webgpuGPURefused() && written + sizeBytes > GPU_WEBGPU_REFUSED_WRITE_BUDGET) {
    atomic_fetch_sub_explicit(&device->writtenBytes, sizeBytes, memory_order_relaxed);
    return false;
  }
#else
  (void)device;
  (void)sizeBytes;
#endif

  return true;
}

/* a submit takes the writes before it along */
static GPU_INLINE void
webgpuQueueWritesSubmitted(GPUDeviceWebGPU *device) {
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  atomic_store_explicit(&device->writtenBytes, 0u, memory_order_relaxed);
#else
  (void)device;
#endif
}

static GPU_INLINE bool
webgpuQueueWritesWaiting(GPUDeviceWebGPU *device) {
  return atomic_load_explicit(&device->writtenBytes, memory_order_relaxed) > 0u;
}

static GPU_INLINE bool
webgpuMapReadRefused(GPUDeviceWebGPU *device) {
  return webgpuGPURefused() && webgpuQueueWritesWaiting(device);
}

static GPU_INLINE GPUSwapchainWebGPU*
webgpuSwapchain(const GPUSwapchain *swapchain) {
  return swapchain ? swapchain->_priv : NULL;
}

static GPU_INLINE GPUCommandWebGPU*
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
