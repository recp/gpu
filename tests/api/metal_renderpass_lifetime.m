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

#include "../../src/backend/mt/common.h"
#include "../../src/api/vrs_internal.h"
#import <objc/runtime.h>

enum { COMMANDS = 4, EXTENT = 64 };

typedef struct Resources {
  GPUTexture                 *color;
  GPUTexture                 *resolve;
  GPUTexture                 *depth;
  GPUTextureView             *colorView;
  GPUTextureView             *resolveView;
  GPUTextureView             *depthView;
  GPUQuerySet                *query;
  GPURasterizationRateMapEXT *map;
} Resources;

static dispatch_semaphore_t releaseSignal;
static atomic_uint          released;
static char                 releaseKey;

@interface ReleaseObserver : NSObject
@end

@implementation ReleaseObserver
- (void)dealloc {
  atomic_fetch_add_explicit(&released, 1u, memory_order_release);
  dispatch_semaphore_signal(releaseSignal);
  [super dealloc];
}
@end

static void
observe(id allocation) {
  ReleaseObserver *observer;

  observer = [ReleaseObserver new];
  objc_setAssociatedObject(allocation, &releaseKey, observer, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  [observer release];
}

static bool
wait_releases(uint32_t expected) {
  dispatch_time_t deadline;

  deadline = dispatch_time(DISPATCH_TIME_NOW, INT64_C(1000000000));
  while (atomic_load_explicit(&released, memory_order_acquire) < expected) {
    if (dispatch_semaphore_wait(releaseSignal, deadline) != 0)
      return false;
  }
  return atomic_load_explicit(&released, memory_order_acquire) == expected;
}

static bool
texture(GPUDevice       *device,
        GPUFormat        format,
        uint32_t         samples,
        GPUTexture     **outTexture,
        GPUTextureView **outView) {
  GPUTextureCreateInfo     info = {0};
  GPUTextureViewCreateInfo view = {0};
  GPUTextureMT            *native;

  info.dimension     = GPU_TEXTURE_DIMENSION_2D;
  info.format        = format;
  info.width         = EXTENT;
  info.height        = EXTENT;
  info.depthOrLayers = 1u;
  info.mipLevelCount = 1u;
  info.sampleCount   = samples;
  info.usage         = format == GPU_FORMAT_RGBA8_UNORM
                          ? GPU_TEXTURE_USAGE_COLOR_TARGET | GPU_TEXTURE_USAGE_COPY_SRC
                          : GPU_TEXTURE_USAGE_DEPTH_STENCIL;
  if (samples > 1u)
    info.usage &= ~GPU_TEXTURE_USAGE_COPY_SRC;

  view.viewType        = GPU_TEXTURE_VIEW_2D;
  view.format          = format;
  view.mipLevelCount   = 1u;
  view.arrayLayerCount = 1u;

  if (GPUCreateTexture(device, &info, outTexture) != GPU_OK
      || GPUCreateTextureView(*outTexture, &view, outView) != GPU_OK)
    return false;

  native = (*outTexture)->_priv;
  observe(native->texture);
  return true;
}

static void
destroy(Resources *resources) {
  GPUDestroyTextureView(resources->colorView);
  GPUDestroyTextureView(resources->resolveView);
  GPUDestroyTextureView(resources->depthView);
  GPUDestroyTexture(resources->color);
  GPUDestroyTexture(resources->resolve);
  GPUDestroyTexture(resources->depth);
  GPUDestroyQuerySet(resources->query);
  GPUDestroyRasterizationRateMapEXT(resources->map);
}

static bool
encode(GPUDevice *device, GPUCommandBuffer *command, Resources *resources, uint32_t kind) {
  GPURenderPassColorAttachment        color = {0};
  GPURenderPassDepthStencilAttachment depth = {0};
  GPURenderPassCreateInfo             passInfo = {0};
  GPUQuerySetCreateInfo               query = {0};
  GPURasterizationRateMapCreateInfoEXT mapInfo = {0};
  GPURasterizationRateMapRenderPassEXT rate = {0};
  GPURasterizationRateLayerEXT         layer = {0};
  const float                         rates[] = {1.0f, 1.0f};
  GPURenderPassEncoder               *pass;
  MTQuerySet                         *nativeQuery;
  GPUFormat                           format;

  if (!texture(device, GPU_FORMAT_RGBA8_UNORM, kind == 1u ? 4u : 1u,
                &resources->color, &resources->colorView))
    return false;

  color.view                  = resources->colorView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[1] = 1.0f;
  color.clearColor.float32[3] = 1.0f;

  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;

  if (kind == 1u) {
    if (!texture(device, GPU_FORMAT_RGBA8_UNORM, 1u, &resources->resolve, &resources->resolveView))
      return false;
    color.resolveView = resources->resolveView;
  } else if (kind >= 2u && kind <= 4u) {
    format = kind == 2u ? GPU_FORMAT_DEPTH32_FLOAT
                       : kind == 3u ? GPU_FORMAT_STENCIL8 : GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
    if (!texture(device, format, 1u, &resources->depth, &resources->depthView))
      return false;
    depth.view           = resources->depthView;
    depth.depthLoadOp    = kind == 3u ? GPU_LOAD_OP_DONT_CARE : GPU_LOAD_OP_CLEAR;
    depth.depthStoreOp   = kind == 3u ? GPU_STORE_OP_DONT_CARE : GPU_STORE_OP_STORE;
    depth.stencilLoadOp  = kind == 2u ? GPU_LOAD_OP_DONT_CARE : GPU_LOAD_OP_CLEAR;
    depth.stencilStoreOp = kind == 2u ? GPU_STORE_OP_DONT_CARE : GPU_STORE_OP_STORE;
    depth.clearDepth     = 0.5f;
    depth.clearStencil   = 3u;
    passInfo.pDepthStencilAttachment = &depth;
  } else if (kind == 5u) {
    query.type  = GPU_QUERY_OCCLUSION;
    query.count = 1u;
    if (GPUCreateQuerySet(device, &query, &resources->query) != GPU_OK)
      return false;
    nativeQuery = resources->query->_priv;
    observe(nativeQuery->visibility);
    passInfo.occlusionQuerySet = resources->query;
  } else if (kind == 6u) {
    layer.pHorizontal     = rates;
    layer.pVertical       = rates;
    layer.horizontalCount = 2u;
    layer.verticalCount   = 2u;
    mapInfo.chain.sType      = GPU_STRUCTURE_TYPE_RASTERIZATION_RATE_MAP_CREATE_INFO_EXT;
    mapInfo.chain.structSize = sizeof(mapInfo);
    mapInfo.pLayers          = &layer;
    mapInfo.layerCount       = 1u;
    mapInfo.screenSize       = (GPUExtent2D){EXTENT, EXTENT};
    if (GPUCreateRasterizationRateMapEXT(device, &mapInfo, &resources->map) != GPU_OK)
      return false;
    observe((id)resources->map->_priv);
    rate.chain.sType      = GPU_STRUCTURE_TYPE_RASTERIZATION_RATE_MAP_RENDER_PASS_EXT;
    rate.chain.structSize = sizeof(rate);
    rate.map              = resources->map;
    passInfo.chain.pNext  = &rate.chain;
  }

  if (!(pass = GPUBeginRenderPass(command, &passInfo)))
    return false;
  GPUEndRenderPass(pass);
  return true;
}

static bool
check(GPUDevice *device, uint32_t kind, bool submit) {
  Resources             resources[COMMANDS] = {{0}};
  GPUCommandBuffer     *commands[COMMANDS] = {0};
  GPUQueueSubmitExInfo   submission = {0};
  GPUQueueSemaphoreWait wait = {0};
  GPUSemaphore         *gate = NULL;
  GPUQueue             *queue;
  MTCommandQueue       *native;
  uint32_t              before;
  uint32_t              expected;
  uint32_t              i;
  bool                  ok = false;

  queue    = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  native   = mt_commandQueue(queue);
  before   = atomic_load_explicit(&released, memory_order_acquire);
  expected = COMMANDS * (kind == 0u ? 1u : 2u);

  @autoreleasepool {
    for (i = 0u; i < COMMANDS; i++) {
      if (GPUAcquireCommandBuffer(queue, "renderpass lifetime", &commands[i]) != GPU_OK
          || !encode(device, commands[i], &resources[i], kind)) {
        fprintf(stderr, "renderpass lifetime: setup failed kind=%u command=%u\n", kind, i);
        goto cleanup;
      }
    }

    if (submit) {
      if (GPUCreateSemaphore(device, NULL, &gate) != GPU_OK)
        goto cleanup;
      wait.semaphore  = gate;
      wait.value      = 1u;
      wait.waitStages = GPU_STAGE_FRAGMENT;
      submission.ppCommandBuffers   = commands;
      submission.commandBufferCount = COMMANDS;
      submission.pWaits             = &wait;
      submission.waitCount          = 1u;
      if (GPUQueueSubmitEx(queue, &submission) != GPU_OK)
        goto cleanup;
      memset(commands, 0, sizeof(commands));

      /* cleanup must not run while the commands are blocked on the event. */
      if (atomic_load_explicit(&released, memory_order_acquire) != before
          || dispatch_group_wait(native->inFlightGroup, DISPATCH_TIME_NOW) == 0)
        goto cleanup;
      [(id<MTLSharedEvent>)gate->_priv setSignaledValue:1u];
      if (dispatch_group_wait(native->inFlightGroup,
                              dispatch_time(DISPATCH_TIME_NOW, INT64_C(5000000000))) != 0)
        goto cleanup;
    }
    ok = true;

cleanup:
    if (gate)
      [(id<MTLSharedEvent>)gate->_priv setSignaledValue:1u];
    if (dispatch_group_wait(native->inFlightGroup,
                            dispatch_time(DISPATCH_TIME_NOW, INT64_C(5000000000))) != 0)
      abort();
    for (i = 0u; i < COMMANDS; i++) {
      if (commands[i])
        GPUDiscardCommandBuffer(commands[i]);
      destroy(&resources[i]);
    }
    GPUDestroySemaphore(gate);
  }
  ok = wait_releases(before + expected) && ok;
  printf("renderpass lifetime: kind=%u submit=%u native releases=%u/%u %s\n", kind, submit,
         atomic_load_explicit(&released, memory_order_acquire) - before, expected, ok ? "pass" : "fail");
  return ok;
}

static bool
check_reuse(GPUDevice *device) {
  Resources                    resources = {0};
  GPURenderPassColorAttachment  color = {0};
  GPURenderPassCreateInfo       passInfo = {0};
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUBufferTextureCopyRegion    copy = {0};
  GPUTextureBarrier            transition = {0};
  GPUBarrierBatch              barrier = {0};
  GPUQueueSubmitInfo           submission = {0};
  GPUBuffer                   *readback = NULL;
  GPUCommandBuffer            *command = NULL;
  GPURenderPassEncoder        *pass;
  GPUTransferPassEncoder      *transfer;
  GPUQueue                    *queue;
  MTCommandQueue              *native;
  const uint8_t               *bytes;
  uint32_t                     epoch;
  uint32_t                     i;
  bool                         ok = false;

  queue  = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  native = mt_commandQueue(queue);
  bufferInfo.sizeBytes = EXTENT * EXTENT * 4u;
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  if (!texture(device, GPU_FORMAT_RGBA8_UNORM, 1u, &resources.color, &resources.colorView)
      || GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK)
    goto cleanup;

  color.view                  = resources.colorView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[1] = 1.0f;
  color.clearColor.float32[3] = 1.0f;

  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  transition.texture    = resources.color;
  transition.srcAccess  = GPU_ACCESS_COLOR_WRITE;
  transition.dstAccess  = GPU_ACCESS_TRANSFER_READ;
  transition.mipCount   = 1u;
  transition.layerCount = 1u;
  barrier.pTextureBarriers     = &transition;
  barrier.textureBarrierCount = 1u;
  barrier.srcStages           = GPU_STAGE_FRAGMENT;
  barrier.dstStages           = GPU_STAGE_TRANSFER;
  copy.bytesPerRow         = EXTENT * 4u;
  copy.rowsPerImage        = EXTENT;
  copy.texture.width      = EXTENT;
  copy.texture.height     = EXTENT;
  copy.texture.depth      = 1u;
  copy.texture.layerCount = 1u;

  for (epoch = 0u; epoch < 8u; epoch++) {
    @autoreleasepool {
      color.clearColor.float32[0] = (float)((epoch / 2u) & 1u);
      if (GPUAcquireCommandBuffer(queue, "renderpass reuse", &command) != GPU_OK
          || !(pass = GPUBeginRenderPass(command, &passInfo)))
        goto cleanup;
      GPUEndRenderPass(pass);
      GPUEncodeBarriers(command, &barrier);
      if (!(transfer = GPUBeginTransferPass(command, "readback")))
        goto cleanup;
      GPUCopyTextureToBuffer(transfer, resources.color, readback, &copy);
      GPUEndTransferPass(transfer);
      submission.ppCommandBuffers   = &command;
      submission.commandBufferCount = 1u;
      if (GPUQueueSubmit(queue, &submission) != GPU_OK)
        goto cleanup;
      command = NULL;
      if (dispatch_group_wait(native->inFlightGroup,
                              dispatch_time(DISPATCH_TIME_NOW, INT64_C(5000000000))) != 0
          || !(bytes = GPUGetBufferHostAddress(readback)))
        goto cleanup;
      for (i = 0u; i < EXTENT * EXTENT; i++) {
        if (bytes[i * 4u] != ((epoch / 2u) & 1u) * 255u || bytes[i * 4u + 1u] != 255u
            || bytes[i * 4u + 2u] != 0u || bytes[i * 4u + 3u] != 255u)
          goto cleanup;
      }
    }
  }
  ok = true;

cleanup:
  if (command)
    GPUDiscardCommandBuffer(command);
  if (dispatch_group_wait(native->inFlightGroup,
                          dispatch_time(DISPATCH_TIME_NOW, INT64_C(5000000000))) != 0)
    abort();
  GPUDestroyBuffer(readback);
  destroy(&resources);
  printf("renderpass reuse: eight exact RGBA8 clears/readbacks %s\n", ok ? "pass" : "fail");
  return ok;
}

int
main(void) {
  GPUAdapterProperties properties = {0};
  GPUDeviceCreateInfo  info = {0};
  GPUVRSCapabilitiesEXT caps = {0};
  GPUInstance         *instance = NULL;
  GPUAdapter          *adapter;
  GPUDevice           *device = NULL;
  MTCommandQueue      *queue;
  const char          *requested;
  GPUFeature           feature = GPU_FEATURE_VARIABLE_RATE_SHADING;
  GPUResult            result;
  uint32_t             kind;
  uint32_t             submit;
  int                  status = 1;
  bool                 rateMap;
  bool                 ok = true;

  requested = getenv("GPU_METAL_MODE");
  atomic_init(&released, 0u);
  releaseSignal = dispatch_semaphore_create(0);
  @autoreleasepool {
    if (GPUCreateInstance(NULL, &instance) != GPU_OK
        || !(adapter = GPUGetAutoSelectedAdapter(instance)))
      goto cleanup;

    if (GPUGetAdapterProperties(adapter, &properties) != GPU_OK
        || properties.backend != GPU_BACKEND_METAL) {
      status = 77;
      goto cleanup;
    }
    rateMap = GPUIsFeatureSupported(adapter, feature)
              && GPUGetVRSCapabilitiesEXT(adapter, &caps) == GPU_OK
              && (caps.modes & GPU_VRS_RATE_MAP_BIT_EXT) != 0u;
    info.required.pFeatures    = rateMap ? &feature : NULL;
    info.required.featureCount = rateMap ? 1u : 0u;
    result = GPUCreateDevice(adapter, &info, &device);
    if (result != GPU_OK) {
      if (result == GPU_ERROR_UNSUPPORTED && requested && !strcmp(requested, "metal4"))
        status = 77;
      goto cleanup;
    }
    queue = mt_commandQueue(GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u));
    if (requested && !strcmp(requested, "metal4") && queue->mode != MTCommandMode4) {
      status = 77;
      goto cleanup;
    }

    for (kind = 0u; kind < (rateMap ? 7u : 6u); kind++) {
      for (submit = 0u; submit < 2u; submit++) {
        ok = check(device, kind, submit != 0u) && ok;
      }
    }
    ok = check_reuse(device) && ok;

    if (!rateMap)
      puts("renderpass lifetime: native rate maps unsupported; remaining cases checked");

    status = ok ? 0 : 1;

cleanup:
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
  }
  dispatch_release(releaseSignal);
  return status;
}
