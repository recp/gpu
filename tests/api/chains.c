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

#include "test.h"
#include "../../src/api/adapter_internal.h"
#include "../../src/api/buffer_internal.h"
#include "../../src/api/cmdqueue_internal.h"
#include "../../src/api/descr/descriptor_internal.h"
#include "../../src/api/execution_graph_internal.h"
#include "../../src/api/library_internal.h"
#include "../../src/api/memory_internal.h"
#include "../../src/api/surface_internal.h"
#include "../../src/api/texture_internal.h"

#define RUN(name, call, output, ordinary, calls) \
  do { \
    gCalls = 0u; \
    result = (call); \
    ok = check_call(name, result, expected, ordinary, output, calls) && ok; \
  } while (0)

static uint32_t gCalls;
static uint32_t gCallbacks;

static GPUInstance*
create_instance(GPUApi *api, const GPUInstanceCreateInfo *info) {
  (void)api;
  (void)info;
  gCalls++;
  return NULL;
}

static GPUDevice*
create_device(GPUAdapter *adapter, const GPUQueueCreateInfo *queues, uint32_t count, uint64_t features) {
  (void)adapter;
  (void)queues;
  (void)count;
  (void)features;
  gCalls++;
  return NULL;
}

static GPUResult
request_adapter(GPUInstance                     *instance,
                GPUPowerPreference               preference,
                GPUBackendAdapterRequestCallback callback,
                void                            *data) {
  (void)instance;
  (void)preference;
  (void)callback;
  (void)data;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
request_device(GPUAdapter                     *adapter,
               const GPUQueueCreateInfo       *queues,
               uint32_t                        count,
               uint64_t                        features,
               GPUBackendDeviceRequestCallback callback,
               void                           *data) {
  (void)adapter;
  (void)queues;
  (void)count;
  (void)features;
  (void)callback;
  (void)data;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
create_buffer(GPUDevice *device, const GPUBufferCreateInfo *info, GPUBuffer **buffer) {
  (void)device;
  (void)info;
  (void)buffer;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
create_query(GPUDevice *device, const GPUQuerySetCreateInfo *info, GPUQuerySet *set) {
  (void)device;
  (void)info;
  (void)set;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
create_semaphore(GPUDevice *device, const GPUSemaphoreCreateInfo *info, GPUSemaphore *semaphore) {
  (void)device;
  (void)info;
  (void)semaphore;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
create_layout(GPUDevice *device, GPUPipelineLayout *layout) {
  (void)device;
  (void)layout;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
create_group(GPUDevice *device, GPUBindGroup *group) {
  (void)device;
  (void)group;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUShaderLibrary*
create_library(GPUDevice *device, const char *source, uint64_t size, uint32_t flags) {
  (void)device;
  (void)source;
  (void)size;
  (void)flags;
  gCalls++;
  return NULL;
}

static GPUResult
commit_buffer(GPUCommandBuffer *cmdb) {
  (void)cmdb;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
submit_ex(GPUQueue *queue, const GPUQueueSubmitExInfo *info) {
  (void)queue;
  (void)info;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
submit_sparse(GPUQueue *queue, const GPUQueueSparseSubmitInfo *info) {
  (void)queue;
  (void)info;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUComputePassEncoder*
compute_pass(GPUCommandBuffer *cmdb, const GPUComputePassCreateInfo *info) {
  (void)cmdb;
  (void)info;
  gCalls++;
  return NULL;
}

static GPURenderPassDesc*
render_pass(GPUCommandBuffer *cmdb, const GPURenderPassCreateInfo *info) {
  (void)cmdb;
  (void)info;
  gCalls++;
  return NULL;
}

static GPURenderPassEncoder*
render_encoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *desc) {
  (void)cmdb;
  (void)desc;
  gCalls++;
  return NULL;
}

static GPUResult
graph_instance(GPUDevice                                    *device,
               const GPUExecutionGraphInstanceCreateInfoEXT *info,
               GPUExecutionGraphInstanceEXT                 *instance) {
  (void)device;
  (void)info;
  (void)instance;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
surface_caps(const GPUAdapter *adapter, GPUSurface *surface, GPUSurfaceCapabilities *caps) {
  (void)adapter;
  (void)surface;
  (void)caps;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUSurface*
create_surface(GPUApi *api, GPUInstance *instance, const GPUSurfaceNativeInfo *info) {
  (void)api;
  (void)instance;
  (void)info;
  gCalls++;
  return NULL;
}

static GPUResult
create_cache(GPUDevice *device, const GPUPipelineCacheCreateInfo *info, GPUPipelineCache *cache) {
  (void)device;
  (void)info;
  (void)cache;
  gCalls++;
  return GPU_ERROR_BACKEND_FAILURE;
}

static void
destroy_cache(GPUPipelineCache *cache) {
  (void)cache;
}

static void
destroy_semaphore(GPUSemaphore *semaphore) {
  (void)semaphore;
}

static void
adapter_ready(GPUResult result, GPUAdapter *adapter, void *data) {
  (void)result;
  (void)adapter;
  (void)data;
  gCallbacks++;
}

static void
device_ready(GPUResult result, GPUDevice *device, void *data) {
  (void)result;
  (void)device;
  (void)data;
  gCallbacks++;
}

static void
format_caps(const GPUAdapter *adapter, GPUFormat format, GPUFormatCapabilities *caps) {
  (void)adapter;
  (void)format;
  caps->colorAttachment = true;
  gCalls++;
}

static void
apply_state(GPURenderPassEncoder *pass, GPUDynamicStateMask mask, const GPUDynamicStateApplyInfo *info) {
  (void)pass;
  (void)mask;
  (void)info;
  gCalls++;
}

static int
check_call(const char *name,
           GPUResult   result,
           GPUResult   expected,
           GPUResult   ordinary,
           const void *output,
           uint32_t    ordinaryCalls) {
  uint32_t calls;

  calls = expected == GPU_OK ? ordinaryCalls : 0u;

  if (result != (expected == GPU_OK ? ordinary : expected) || output || gCalls != calls) {
    fprintf(stderr,
            "chain %s: result=%d expected=%d output=%d calls=%u expected-calls=%u\n",
            name,
            result,
            expected == GPU_OK ? ordinary : expected,
            output != NULL,
            gCalls,
            calls);
    return 0;
  }

  return 1;
}

static int
check_surface_chains(GPUInstance *instance, GPUAdapter *adapter) {
  GPUNativeSurfaceCreateInfo nativeInfo = {0};
  GPUSurfaceCreateInfo       info       = {0};
  GPUChainedStruct           foreign    = {0};
  GPUSurface                *surface;
  GPUResult                  result;
  uint32_t                   i;
  int                        ok = 1;

  nativeInfo.chain.sType      = GPU_STRUCTURE_TYPE_NATIVE_SURFACE_CREATE_INFO;
  nativeInfo.chain.structSize = sizeof(nativeInfo);
  nativeInfo.adapter          = adapter;
  nativeInfo.nativeHandle     = (void *)(uintptr_t)1u;
  nativeInfo.type             = GPU_SURFACE_APPLE_NSVIEW;
  nativeInfo.scale            = 1.0f;

  info.chain.sType            = GPU_STRUCTURE_TYPE_SURFACE_CREATE_INFO;
  info.chain.structSize       = sizeof(info);
  foreign.sType               = (GPUStructureType)UINT32_MAX;
  foreign.structSize          = sizeof(foreign);

  for (i = 0u; i < 7u; i++) {
    nativeInfo.chain.structSize = i == 5u ? 1u : sizeof(nativeInfo);
    nativeInfo.chain.pNext      = i == 2u || i == 4u ? &foreign : i == 6u ? &nativeInfo.chain : NULL;
    foreign.pNext               = i == 1u ? &nativeInfo.chain : i == 3u || i == 4u ? &foreign : NULL;
    info.chain.pNext            = i == 1u || i == 3u ? (const void *)&foreign : &nativeInfo;
    surface                    = (GPUSurface *)(uintptr_t)1u;
    gCalls                     = 0u;
    result                     = GPUCreateSurface(instance, &info, &surface);

    ok = check_call("native surface",
                    result,
                    i == 0u ? GPU_OK : GPU_ERROR_INVALID_ARGUMENT,
                    GPU_ERROR_BACKEND_FAILURE,
                    surface,
                    1u) && ok;
  }

  return ok;
}

int
gpu_test_chains(GPUDevice *nativeDevice) {
  GPUExecutionGraphInstanceCreateInfoEXT graphInstanceInfo = {0};
  GPUExecutionGraphCreateInfoEXT         graphInfo         = {0};
  GPUTransientAllocatorConfig            transientInfo     = {0};
  GPUDynamicStateApplyInfo               stateInfo         = {0};
  GPURenderPassCreateInfo                renderInfo        = {0};
  GPURenderPassColorAttachment           color             = {0};
  GPUPipelineCacheCreateInfo             cacheInfo         = {0};
  GPUPipelineLayoutCreateInfo            layoutInfo        = {0};
  GPUShaderLibraryCreateInfo             libraryInfo       = {0};
  GPUAdapterRequestOptions               adapterInfo       = {0};
  GPUBindGroupLayoutPriv                 layoutPriv        = {0};
  GPUBindGroupCreateInfo                 groupInfo         = {0};
  GPUComputePassCreateInfo               computeInfo       = {0};
  GPUSemaphoreCreateInfo                 semaphoreInfo     = {0};
  GPUSwapchainCreateInfo                 swapchainInfo     = {0};
  GPUQueueSparseSubmitInfo               sparseInfo        = {0};
  GPUQueueSubmitExInfo                   submitExInfo      = {0};
  GPUQueueSemaphoreWait                  wait              = {0};
  GPUInstanceCreateInfo                  instanceInfo      = {0};
  GPUDeviceCreateInfo                    deviceInfo        = {0};
  GPUQuerySetCreateInfo                  queryInfo         = {0};
  GPUQueueSubmitInfo                     submitInfo        = {0};
  GPUFenceCreateInfo                     fenceInfo         = {0};
  GPURuntimeConfig                       runtimeInfo       = {0};
  GPUSparseBufferMapping                 mapping           = {0};
  GPURenderPassEncoder                   render            = {0};
  GPUExecutionGraphEXT                   graph             = {0};
  GPUCommandBuffer                       cmdb              = {0};
  GPUShaderLibrary                       library           = {0};
  GPUBindGroupLayout                     groupLayout       = {0};
  GPUPipelineLayout                      pipelineLayout    = {0};
  GPUTextureView                         view              = {0};
  GPUInstanceApi                         savedInstance;
  GPUChainedStruct                       foreign[4]        = {0};
  GPUInstance                            instance          = {0};
  GPUAdapter                             adapter           = {0};
  GPUDevice                              device            = {0};
  GPUTexture                             texture           = {0};
  GPUSemaphore                           semaphore         = {0};
  GPUSurface                             surface           = {0};
  GPUBuffer                              buffer            = {0};
  GPUQueue                               queue             = {0};
  GPUHeap                                heap              = {0};
  GPUApi                                 api               = {0};
  GPUChainedStruct                      *headers[]         = {
    &instanceInfo.chain, &adapterInfo.chain, &deviceInfo.chain, &deviceInfo.queues.chain,
    &runtimeInfo.chain, &transientInfo.chain, &queryInfo.chain, &fenceInfo.chain,
    &semaphoreInfo.chain, &swapchainInfo.chain, &layoutInfo.chain, &groupInfo.chain,
    &libraryInfo.chain, &submitInfo.chain, &submitExInfo.chain, &sparseInfo.chain,
    &computeInfo.chain, &stateInfo.chain, &graphInfo.chain, &graphInstanceInfo.chain,
    &renderInfo.chain, &cacheInfo.chain
  };
  uint32_t                               sizes[]           = {
    sizeof(instanceInfo), sizeof(adapterInfo), sizeof(deviceInfo), sizeof(deviceInfo.queues),
    sizeof(runtimeInfo), sizeof(transientInfo), sizeof(queryInfo), sizeof(fenceInfo),
    sizeof(semaphoreInfo), sizeof(swapchainInfo), sizeof(layoutInfo), sizeof(groupInfo),
    sizeof(libraryInfo), sizeof(submitInfo), sizeof(submitExInfo), sizeof(sparseInfo),
    sizeof(computeInfo), sizeof(stateInfo), sizeof(graphInfo), sizeof(graphInstanceInfo),
    sizeof(renderInfo), sizeof(cacheInfo)
  };
  GPUStructureType                       types[]           = {
    GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    GPU_STRUCTURE_TYPE_ADAPTER_REQUEST_OPTIONS,
    GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    GPU_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    GPU_STRUCTURE_TYPE_RUNTIME_CONFIG,
    GPU_STRUCTURE_TYPE_TRANSIENT_ALLOCATOR_CONFIG,
    GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO,
    GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    GPU_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    GPU_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO,
    GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
    GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO,
    GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO,
    GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO,
    GPU_STRUCTURE_TYPE_QUEUE_SPARSE_SUBMIT_INFO,
    GPU_STRUCTURE_TYPE_COMPUTE_PASS_CREATE_INFO,
    GPU_STRUCTURE_TYPE_DYNAMIC_STATE_APPLY_INFO,
    GPU_STRUCTURE_TYPE_EXECUTION_GRAPH_CREATE_INFO_EXT,
    GPU_STRUCTURE_TYPE_EXECUTION_GRAPH_INSTANCE_CREATE_INFO_EXT,
    GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
  };
  GPUCommandBuffer                      *buffers[]         = {&cmdb};
  GPUExecutionGraphInstanceEXT          *outGraphInstance;
  GPUExecutionGraphEXT                  *outGraph;
  GPUPipelineCache                      *outCache;
  GPUPipelineLayout                     *outLayout;
  GPUShaderLibrary                      *outLibrary;
  GPUSwapchain                          *outSwapchain;
  GPUBindGroup                          *outGroup;
  GPUInstance                           *outInstance;
  GPUQuerySet                           *outQuery;
  GPUSemaphore                          *outSemaphore;
  GPUDevice                             *outDevice;
  GPUFence                              *outFence;
  GPUComputePassEncoder                 *compute;
  GPURenderPassEncoder                  *renderEncoder;
  GPUApi                                *nativeApi;
  const void                            *chain;
  GPUResult                              expected;
  GPUResult                              result;
  uint32_t                               mode;
  uint32_t                               i;
  uint32_t                               checks            = 0u;
  int                                    ok                = 1;
  bool                                   ordinary;

  _Static_assert(GPU_ARRAY_LEN(headers) == GPU_ARRAY_LEN(sizes)
                 && GPU_ARRAY_LEN(headers) == GPU_ARRAY_LEN(types),
                 "chain headers need matching metadata");

  nativeApi = deviceApi(nativeDevice);

  if (!nativeApi) {
    return 0;
  }

  /* replace the synchronous factory callback and restore the table before returning. */

  savedInstance                      = nativeApi->instance;
  nativeApi->instance.createInstance = create_instance;

  api.backend                         = GPU_BACKEND_METAL;
  api.device.createDevice             = create_device;
  api.device.requestDevice            = request_device;
  api.device.requestAdapter           = request_adapter;
  api.device.getFormatCapabilities    = format_caps;
  api.buf.create                      = create_buffer;
  api.cmdbuf.createQuerySet           = create_query;
  api.cmdque.createSemaphore          = create_semaphore;
  api.cmdque.destroySemaphore         = destroy_semaphore;
  api.cmdque.commit                   = commit_buffer;
  api.cmdque.submitEx                 = submit_ex;
  api.memory.submitSparse             = submit_sparse;
  api.descriptor.createPipelineLayout = create_layout;
  api.descriptor.createBindGroup      = create_group;
  api.library.newLibraryWithSource    = create_library;
  api.compute.computeCommandEncoder   = compute_pass;
  api.renderPass.beginRenderPass      = render_pass;
  api.rce.renderCommandEncoder        = render_encoder;
  api.rce.applyDynamicState           = apply_state;
  api.executionGraph.createInstance   = graph_instance;
  api.surface.createSurface           = create_surface;
  api.pipelineCache.create            = create_cache;
  api.pipelineCache.destroy           = destroy_cache;
  api.surface.getCapabilities         = surface_caps;

  instance._api                   = &api;
  adapter.inst                    = &instance;
  adapter.supportedFeatureState   = 2u;
  device._api                     = &api;
  device.inst                     = &instance;
  device.adapter                  = &adapter;
  device.enabledFeatureMask       = UINT64_MAX;
  queue._device                   = &device;
  cmdb._queue                     = &queue;
  semaphore._device               = &device;
  surface.inst                    = &instance;

  instanceInfo.preferredBackend   = nativeApi->backend;
  queryInfo.type                  = GPU_QUERY_OCCLUSION;
  queryInfo.count                 = 1u;
  fenceInfo.signaled              = true;
  runtimeInfo.enableStats         = true;
  transientInfo.ringBytesPerFrame = 64u;
  transientInfo.framesInFlight    = 1u;

  cacheInfo.enableDiskCache          = true;
  cacheInfo.cachePath                = "ignored-by-spy";

  groupLayout._device             = &device;
  groupLayout._priv               = &layoutPriv;
  groupInfo.layout                = &groupLayout;
  libraryInfo.sourceKind          = GPU_SHADER_SOURCE_MSL_TEXT;
  libraryInfo.sourceData          = "kernel void test() {}";
  libraryInfo.sourceSize          = strlen(libraryInfo.sourceData);

  swapchainInfo.surface           = &surface;
  swapchainInfo.width             = 2u;
  swapchainInfo.height            = 2u;
  swapchainInfo.format            = GPU_FORMAT_RGBA8_UNORM;
  swapchainInfo.presentMode       = GPU_PRESENT_MODE_FIFO;

  submitInfo.commandBufferCount   = 1u;
  submitInfo.ppCommandBuffers     = buffers;
  submitExInfo.commandBufferCount = 1u;
  submitExInfo.ppCommandBuffers   = buffers;
  submitExInfo.pWaits             = &wait;

  wait.semaphore                  = &semaphore;
  wait.waitStages                 = GPU_STAGE_COMPUTE;

  heap.device                     = &device;
  heap.usage                      = GPU_HEAP_USAGE_SPARSE;
  buffer.device                   = &device;
  buffer._heap                    = &heap;
  buffer._sparse                  = true;

  buffer._sparseRequirements.tileCount = 1u;

  mapping.buffer                  = &buffer;
  mapping.heap                    = &heap;
  mapping.tileCount               = 1u;
  mapping.mode                    = GPU_SPARSE_MAPPING_UNMAP;

  sparseInfo.bufferMappingCount   = 1u;
  sparseInfo.pBufferMappings      = &mapping;

  stateInfo.mask                  = GPU_DYNAMIC_STATE_STENCIL_REFERENCE_BIT;
  stateInfo.stencilReference      = 73u;
  render._api                     = &api;
  render._device                  = &device;
  render._cmdb                    = &cmdb;

  library._device                 = &device;
  library._api                    = &api;
  pipelineLayout._device          = &device;
  graphInfo.library               = &library;
  graphInfo.layout                = &pipelineLayout;
  graph._api                      = &api;
  graph.device                    = &device;
  graphInstanceInfo.graph         = &graph;

  texture.device                  = &device;
  texture.usage                   = GPU_TEXTURE_USAGE_COLOR_TARGET;
  texture.dimension               = GPU_TEXTURE_DIMENSION_2D;
  texture.format                  = GPU_FORMAT_RGBA8_UNORM;
  texture.width                   = 2u;
  texture.height                  = 2u;
  texture.depthOrLayers           = 1u;
  texture.mipLevelCount           = 1u;
  texture.sampleCount             = 1u;

  view._texture                   = &texture;
  view.format                     = GPU_FORMAT_RGBA8_UNORM;
  view.viewType                   = GPU_TEXTURE_VIEW_2D;
  view.mipLevelCount              = 1u;
  view.arrayLayerCount            = 1u;

  color.view                      = &view;

  renderInfo.colorAttachmentCount = 1u;
  renderInfo.pColorAttachments    = &color;

  foreign[0].sType                = GPU_STRUCTURE_TYPE_BUFFER_HOST_MEMORY_EXT;
  foreign[0].structSize           = sizeof(foreign[0]);
  foreign[1].sType                = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_MIN_LOD_EXT;
  foreign[1].structSize           = 1u;
  foreign[2].sType                = GPU_STRUCTURE_TYPE_NONE;
  foreign[2].structSize           = sizeof(foreign[2]);
  foreign[2].pNext                = &foreign[2];

  foreign[3].sType                = (GPUStructureType)UINT32_MAX;
  foreign[3].structSize           = sizeof(foreign[3]);

  for (mode = 0u; mode < 8u; mode++) {
    ordinary = mode == 0u || mode == 7u;
    chain    = ordinary ? NULL : &foreign[(mode - 1u) % GPU_ARRAY_LEN(foreign)];
    expected = ordinary ? GPU_OK : mode == 6u ? GPU_ERROR_INVALID_ARGUMENT : GPU_ERROR_UNSUPPORTED;

    for (i = 0u; i < GPU_ARRAY_LEN(headers); i++) {
      headers[i]->sType      = mode == 7u ? GPU_STRUCTURE_TYPE_NONE : types[i];
      headers[i]->structSize = mode == 6u ? 1u : mode == 7u ? 0u : sizes[i];
      headers[i]->pNext      = mode == 5u ? headers[i] : chain;
    }

    outInstance = (GPUInstance *)(uintptr_t)1u;
    RUN("instance", GPUCreateInstance(&instanceInfo, &outInstance), outInstance, GPU_ERROR_BACKEND_FAILURE, 1u);

    gCallbacks = 0u;
    RUN("adapter",
        GPURequestAdapter(&instance, &adapterInfo, adapter_ready, NULL),
        NULL,
        GPU_ERROR_BACKEND_FAILURE,
        1u);
    ok = gCallbacks == 0u && ok;

    deviceInfo.queues.chain.pNext      = NULL;
    deviceInfo.queues.chain.structSize = mode == 7u ? 0u : sizeof(deviceInfo.queues);
    outDevice                          = (GPUDevice *)(uintptr_t)1u;
    RUN("device", GPUCreateDevice(&adapter, &deviceInfo, &outDevice), outDevice, GPU_ERROR_BACKEND_FAILURE, 1u);
    RUN("device async",
        GPURequestDevice(&adapter, &deviceInfo, device_ready, NULL),
        NULL,
        GPU_ERROR_BACKEND_FAILURE,
        1u);
    ok = gCallbacks == 0u && ok;

    api.device.requestDevice = NULL;
    RUN("device fallback",
        GPURequestDevice(&adapter, &deviceInfo, device_ready, NULL),
        NULL,
        GPU_ERROR_BACKEND_FAILURE,
        1u);
    ok                       = gCallbacks == 1u && ok;
    api.device.requestDevice = request_device;
    gCallbacks               = 0u;

    deviceInfo.chain.pNext             = NULL;
    deviceInfo.chain.structSize        = mode == 7u ? 0u : sizeof(deviceInfo);
    deviceInfo.queues.chain.pNext      = mode == 5u ? &deviceInfo.queues.chain : chain;
    deviceInfo.queues.chain.structSize = mode == 6u ? 1u : mode == 7u ? 0u : sizeof(deviceInfo.queues);
    outDevice                          = (GPUDevice *)(uintptr_t)1u;
    RUN("device queues", GPUCreateDevice(&adapter, &deviceInfo, &outDevice), outDevice, GPU_ERROR_BACKEND_FAILURE, 1u);
    RUN("device queues async",
        GPURequestDevice(&adapter, &deviceInfo, device_ready, NULL),
        NULL,
        GPU_ERROR_BACKEND_FAILURE,
        1u);
    ok = gCallbacks == 0u && ok;

    RUN("runtime", GPUConfigureRuntime(&device, &runtimeInfo), NULL, GPU_OK, 0u);
    ok                               = device.runtimeConfig.enableStats == ordinary && ok;
    device.runtimeConfig.enableStats = false;
    RUN("transient", GPUConfigureTransientAllocator(&device, &transientInfo), NULL, GPU_ERROR_BACKEND_FAILURE, 2u);
    ok = !device.transientConfigured && !device.transientBuffer && ok;

    outQuery = (GPUQuerySet *)(uintptr_t)1u;
    RUN("query", GPUCreateQuerySet(&device, &queryInfo, &outQuery), outQuery, GPU_ERROR_BACKEND_FAILURE, 1u);

    outSemaphore = (GPUSemaphore *)(uintptr_t)1u;
    RUN("semaphore",
        GPUCreateSemaphore(&device, &semaphoreInfo, &outSemaphore),
        outSemaphore,
        GPU_ERROR_BACKEND_FAILURE,
        1u);

    outFence = (GPUFence *)(uintptr_t)1u;
    RUN("fence", GPUCreateFence(&device, &fenceInfo, &outFence), ordinary ? NULL : outFence, GPU_OK, 0u);

    if (result == GPU_OK) {
      ok = outFence && GPUIsFenceSignaled(outFence) && ok;
      GPUDestroyFence(outFence);
    }

    outSwapchain = (GPUSwapchain *)(uintptr_t)1u;
    RUN("swapchain",
        GPUCreateSwapchain(&device, &swapchainInfo, &outSwapchain),
        outSwapchain,
        GPU_ERROR_BACKEND_FAILURE,
        2u);

    outLayout = (GPUPipelineLayout *)(uintptr_t)1u;
    RUN("pipeline layout",
        GPUCreatePipelineLayout(&device, &layoutInfo, &outLayout),
        outLayout,
        GPU_ERROR_BACKEND_FAILURE,
        1u);

    outGroup = (GPUBindGroup *)(uintptr_t)1u;
    RUN("bind group", GPUCreateBindGroup(&device, &groupInfo, &outGroup), outGroup, GPU_ERROR_BACKEND_FAILURE, 1u);

    outLibrary = (GPUShaderLibrary *)(uintptr_t)1u;
    RUN("shader library",
        GPUCreateShaderLibrary(&device, &libraryInfo, &outLibrary),
        outLibrary,
        GPU_ERROR_BACKEND_FAILURE,
        1u);

    cmdb._submitted = false;
    RUN("submit", GPUQueueSubmit(&queue, &submitInfo), NULL, GPU_ERROR_BACKEND_FAILURE, 1u);
    ok                     = (ordinary || !cmdb._submitted) && ok;
    cmdb._submitted        = false;
    submitExInfo.waitCount = 1u;
    RUN("submit ex", GPUQueueSubmitEx(&queue, &submitExInfo), NULL, GPU_ERROR_BACKEND_FAILURE, 1u);
    ok                     = (ordinary || !cmdb._submitted) && ok;
    cmdb._submitted        = false;
    submitExInfo.waitCount = 0u;
    RUN("submit ex fallback", GPUQueueSubmitEx(&queue, &submitExInfo), NULL, GPU_ERROR_BACKEND_FAILURE, 1u);
    ok = (ordinary || !cmdb._submitted) && ok;
    RUN("submit sparse", GPUQueueSubmitSparse(&queue, &sparseInfo), NULL, GPU_ERROR_BACKEND_FAILURE, 1u);

    cmdb._submitted = false;
    gCalls          = 0u;
    compute         = GPUBeginComputePassWithInfo(&cmdb, &computeInfo);
    ok              = !compute && !cmdb._activeEncoder && gCalls == (ordinary ? 1u : 0u) && ok;

    gCalls        = 0u;
    renderEncoder = GPUBeginRenderPass(&cmdb, &renderInfo);
    ok            = !renderEncoder && !cmdb._activeEncoder && gCalls == (ordinary ? 1u : 0u) && ok;

    render._dynamicStateMask = 0u;
    render._stencilReference = 0u;
    gCalls                   = 0u;
    GPUApplyDynamicState(&render, &stateInfo);
    ok = gCalls == (ordinary ? 1u : 0u)
         && render._stencilReference == (ordinary ? stateInfo.stencilReference : 0u) && ok;

    outGraph = (GPUExecutionGraphEXT *)(uintptr_t)1u;
    RUN("graph", GPUCreateExecutionGraphEXT(&device, &graphInfo, &outGraph), outGraph, GPU_ERROR_INVALID_ARGUMENT, 0u);

    outGraphInstance = (GPUExecutionGraphInstanceEXT *)(uintptr_t)1u;
    RUN("graph instance",
        GPUCreateExecutionGraphInstanceEXT(&device, &graphInstanceInfo, &outGraphInstance),
        outGraphInstance,
        GPU_ERROR_BACKEND_FAILURE,
        1u);

    outCache = (GPUPipelineCache *)(uintptr_t)1u;
    RUN("pipeline cache",
        GPUCreatePipelineCache(&device, &cacheInfo, &outCache),
        outCache,
        GPU_ERROR_BACKEND_FAILURE,
        1u);

    checks += 26u;
  }

  ok = check_surface_chains(&instance, &adapter) && ok;

  checks += 7u;

  nativeApi->instance = savedInstance;
  printf("descriptor chains: %u boundary checks; callbacks, outputs and state verified; ok=%d\n", checks, ok);

  return ok;
}

#undef RUN
