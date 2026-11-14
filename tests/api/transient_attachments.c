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
#include <math.h>
#include <stdio.h>
#include <string.h>

#if GPU_TEST_METAL_TRANSIENT
#  include "../../src/backend/mt/common.h"
#endif

#define CHECK(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expr); \
    goto cleanup; \
  } \
} while (0)

static bool
check_storage(GPUTexture *texture, bool memoryless) {
#if GPU_TEST_METAL_TRANSIENT
  id<MTLTexture> native;

  native = ((GPUTextureMT *)texture->_priv)->texture;
  return native.storageMode == (memoryless ? MTLStorageModeMemoryless : MTLStorageModePrivate)
         && (memoryless ? native.allocatedSize == 0u : native.allocatedSize > 0u);
#else
  (void)texture;
  (void)memoryless;
  return true;
#endif
}

static bool
check_attachments(GPUDevice *device, bool memoryless, GPUFormat depthFormat) {
  uint8_t pixels[1024];
  const GPUTextureUsageFlags invalidUsage[] = {GPU_TEXTURE_USAGE_SAMPLED, GPU_TEXTURE_USAGE_STORAGE,
                                               GPU_TEXTURE_USAGE_COPY_SRC, GPU_TEXTURE_USAGE_COPY_DST};
  const uint8_t expected[] = {64u, 128u, 191u, 255u};
  GPURenderPassColorAttachment        color     = {0};
  GPURenderPassDepthStencilAttachment depthInfo = {0};
  GPURenderPassCreateInfo             passInfo  = {0};
  GPUTextureViewCreateInfo            viewInfo  = {0};
  GPUBufferTextureCopyRegion          copy      = {0};
  GPUTextureCreateInfo                info      = {0};
  GPUTextureCreateInfo                invalid;
  GPUQueueSubmitInfo                 submit    = {0};
  GPUBufferCreateInfo                 bufferInfo = {0};
  GPUMemoryRequirements              requirements;
  GPUTexture             *msaa = NULL, *depth = NULL, *resolve = NULL, *bad = NULL;
  GPUTextureView         *msaaView = NULL, *depthView = NULL, *resolveView = NULL;
  GPUBuffer              *buffer = NULL;
  GPUFence               *fence = NULL;
  GPUCommandBuffer       *command = NULL;
  GPURenderPassEncoder   *pass;
  GPUTransferPassEncoder *transfer;
  GPUQueue               *queue;
  uint32_t                i, x, y, c;
  bool                    ok = false;

  queue              = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  info.dimension     = GPU_TEXTURE_DIMENSION_2D;
  info.format        = GPU_FORMAT_RGBA8_UNORM;
  info.width         = info.height = 4u;
  info.depthOrLayers = info.mipLevelCount = 1u;
  info.sampleCount   = 4u;
  info.usage         = GPU_TEXTURE_USAGE_COLOR_TARGET | GPU_TEXTURE_USAGE_TRANSIENT_ATTACHMENT;
  for (i = 0u; i < sizeof(invalidUsage) / sizeof(invalidUsage[0]); i++) {
    invalid = info;
    invalid.usage |= invalidUsage[i];
    CHECK(GPUCreateTexture(device, &invalid, &bad) == GPU_ERROR_INVALID_ARGUMENT && !bad);
  }
  invalid               = info;
  invalid.sampleCount   = 1u;
  invalid.mipLevelCount = 2u;
  CHECK(GPUCreateTexture(device, &invalid, &bad) == GPU_ERROR_INVALID_ARGUMENT && !bad);
  invalid           = info;
  invalid.dimension = GPU_TEXTURE_DIMENSION_3D;
  CHECK(GPUCreateTexture(device, &invalid, &bad) == GPU_ERROR_INVALID_ARGUMENT && !bad);
  CHECK(GPUGetTextureMemoryRequirements(device, &info, &requirements) == GPU_ERROR_UNSUPPORTED);
  CHECK(GPUGetSparseTextureRequirements(device, &info, &(GPUSparseTextureRequirements){0}) == GPU_ERROR_UNSUPPORTED);
  CHECK(GPUCreateTexture(device, &info, &msaa) == GPU_OK && check_storage(msaa, memoryless));
  viewInfo.viewType      = GPU_TEXTURE_VIEW_2D;
  viewInfo.format        = info.format;
  viewInfo.mipLevelCount = viewInfo.arrayLayerCount = 1u;
  CHECK(GPUCreateTextureView(msaa, &viewInfo, &msaaView) == GPU_OK);

  info.format     = depthFormat;
  info.usage      = GPU_TEXTURE_USAGE_DEPTH_STENCIL | GPU_TEXTURE_USAGE_TRANSIENT_ATTACHMENT;
  viewInfo.format = info.format;
  CHECK(GPUCreateTexture(device, &info, &depth) == GPU_OK && check_storage(depth, memoryless));
  CHECK(GPUCreateTextureView(depth, &viewInfo, &depthView) == GPU_OK);

  info.format      = GPU_FORMAT_RGBA8_UNORM;
  info.sampleCount = 1u;
  info.usage       = GPU_TEXTURE_USAGE_COLOR_TARGET | GPU_TEXTURE_USAGE_COPY_SRC;
  viewInfo.format  = info.format;
  CHECK(GPUCreateTexture(device, &info, &resolve) == GPU_OK);
  CHECK(GPUCreateTextureView(resolve, &viewInfo, &resolveView) == GPU_OK);
  bufferInfo.sizeBytes = sizeof(pixels);
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  CHECK(GPUCreateBuffer(device, &bufferInfo, &buffer) == GPU_OK);
  memset(pixels, 0xa5, sizeof(pixels));
  CHECK(GPUQueueWriteBuffer(queue, buffer, 0u, pixels, sizeof(pixels)) == GPU_OK);
  CHECK(GPUCreateFence(device, NULL, &fence) == GPU_OK);
  CHECK(GPUAcquireCommandBuffer(queue, "transient clear and resolve", &command) == GPU_OK);

  color.view                       = msaaView;
  color.resolveView                = resolveView;
  color.loadOp                     = GPU_LOAD_OP_CLEAR;
  color.storeOp                    = GPU_STORE_OP_DONT_CARE;
  color.clearColor.float32[0]      = 0.25f;
  color.clearColor.float32[1]      = 0.5f;
  color.clearColor.float32[2]      = 0.75f;
  color.clearColor.float32[3]      = 1.0f;
  depthInfo.view                   = depthView;
  depthInfo.depthLoadOp            = GPU_LOAD_OP_CLEAR;
  depthInfo.depthStoreOp           = GPU_STORE_OP_DONT_CARE;
  depthInfo.stencilLoadOp          = depthFormat == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8
                                      ? GPU_LOAD_OP_CLEAR : GPU_LOAD_OP_DONT_CARE;
  depthInfo.stencilStoreOp         = GPU_STORE_OP_DONT_CARE;
  depthInfo.clearDepth             = 1.0f;
  depthInfo.clearStencil           = 31u;
  passInfo.pColorAttachments       = &color;
  passInfo.colorAttachmentCount    = 1u;
  passInfo.pDepthStencilAttachment = &depthInfo;

  color.loadOp = GPU_LOAD_OP_LOAD;
  CHECK(!GPUBeginRenderPass(command, &passInfo));
  color.loadOp  = GPU_LOAD_OP_CLEAR;
  color.storeOp = GPU_STORE_OP_STORE;
  CHECK(!GPUBeginRenderPass(command, &passInfo));
  color.storeOp         = GPU_STORE_OP_DONT_CARE;
  depthInfo.depthLoadOp = GPU_LOAD_OP_LOAD;
  CHECK(!GPUBeginRenderPass(command, &passInfo));
  depthInfo.depthLoadOp = GPU_LOAD_OP_CLEAR;
  depthInfo.clearDepth  = NAN;
  CHECK(!GPUBeginRenderPass(command, &passInfo));
  depthInfo.clearDepth = 1.0f;
  depthInfo.depthStoreOp = GPU_STORE_OP_STORE;
  CHECK(!GPUBeginRenderPass(command, &passInfo));
  depthInfo.depthStoreOp = GPU_STORE_OP_DONT_CARE;
  if (depthFormat == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8) {
    depthInfo.stencilLoadOp = GPU_LOAD_OP_LOAD;
    CHECK(!GPUBeginRenderPass(command, &passInfo));
    depthInfo.stencilLoadOp = GPU_LOAD_OP_CLEAR;
    depthInfo.stencilStoreOp = GPU_STORE_OP_STORE;
    CHECK(!GPUBeginRenderPass(command, &passInfo));
    depthInfo.stencilStoreOp = GPU_STORE_OP_DONT_CARE;
    depthInfo.clearStencil = 256u;
    CHECK(!GPUBeginRenderPass(command, &passInfo));
    depthInfo.clearStencil = 31u;
  }
  CHECK((pass = GPUBeginRenderPass(command, &passInfo)) != NULL);
  GPUEndRenderPass(pass);

  GPUTransitionTexture(command, resolve, GPU_ACCESS_COLOR_WRITE, GPU_ACCESS_TRANSFER_READ);

  copy.texture.width = copy.texture.height = 4u;
  copy.texture.depth = copy.texture.layerCount = 1u;
  copy.bytesPerRow   = 256u;
  CHECK((transfer = GPUBeginTransferPass(command, "resolved pixels")) != NULL);
  GPUCopyTextureToBuffer(transfer, resolve, buffer, &copy);
  GPUEndTransferPass(transfer);
  submit.ppCommandBuffers   = &command;
  submit.commandBufferCount = 1u;
  submit.fence              = fence;
  CHECK(GPUQueueSubmit(queue, &submit) == GPU_OK);
  command = NULL;
  CHECK(GPUWaitFence(fence, 2000000000ull) == GPU_OK);
  CHECK(GPUQueueReadBuffer(queue, buffer, 0u, pixels, sizeof(pixels)) == GPU_OK);
  for (y = 0u; y < 4u; y++) {
    for (x = 0u; x < 4u; x++) {
      for (c = 0u; c < 4u; c++) {
        if (pixels[y * 256u + x * 4u + c] != expected[c])
          fprintf(stderr, "pixel %u,%u channel %u: got %u expected %u\n", x, y, c,
                  pixels[y * 256u + x * 4u + c], expected[c]);
        CHECK(pixels[y * 256u + x * 4u + c] == expected[c]);
      }
    }
    for (x = 16u; x < 256u; x++) {
      CHECK(pixels[y * 256u + x] == 0xa5u);
    }
  }
  ok = true;

cleanup:
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUDestroyFence(fence);
  GPUDestroyBuffer(buffer);
  GPUDestroyTextureView(resolveView);
  GPUDestroyTextureView(depthView);
  GPUDestroyTextureView(msaaView);
  GPUDestroyTexture(bad);
  GPUDestroyTexture(resolve);
  GPUDestroyTexture(depth);
  GPUDestroyTexture(msaa);
  printf("transient %s depth format %u: creation/lifetime/clear bounds,64 components,960 padding guards=%s\n",
         memoryless ? "native" : "ordinary fallback", (unsigned)depthFormat, ok ? "pass" : "fail");
  return ok;
}

static int
run(void) {
  GPUDeviceCreateInfo    info = {0};
  GPUFormatCapabilities stencilCaps;
  GPUInstance          *instance = NULL;
  GPUAdapter           *adapter = NULL;
  GPUDevice            *device = NULL;
  const GPUFeature      feature = GPU_FEATURE_MEMORYLESS_ATTACHMENTS;
  uint32_t              native;
  bool                  supported, ok = true;

  if (GPUCreateInstance(NULL, &instance) != GPU_OK
      || gpu_test_request_adapter(instance, &adapter) != GPU_OK) {
    GPUDestroyInstance(instance);
    return 1;
  }

  supported = GPUIsFeatureSupported(adapter, feature);
  memset(&stencilCaps, 0, sizeof(stencilCaps));
  GPUGetFormatCapabilities(adapter, GPU_FORMAT_DEPTH32_FLOAT_STENCIL8, &stencilCaps);
  for (native = 0u; native <= (uint32_t)supported; native++) {
    info.required.pFeatures    = &feature;
    info.required.featureCount = native;
    if (gpu_test_create_device(adapter, &info, &device) != GPU_OK) {
      ok = false;
      break;
    }
    ok = check_attachments(device, native != 0u, GPU_FORMAT_DEPTH32_FLOAT) && ok;
    if (stencilCaps.depthStencil && (stencilCaps.supportedSampleCounts & 4u))
      ok = check_attachments(device, native != 0u, GPU_FORMAT_DEPTH32_FLOAT_STENCIL8) && ok;
    GPUDestroyDevice(device);
    device = NULL;
  }
  if (!supported)
    printf("native memoryless capability unavailable; ordinary fallback checked\n");

  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  return ok ? 0 : 1;
}

int
main(void) {
#if GPU_TEST_METAL_TRANSIENT
  @autoreleasepool {
    return run();
  }
#else
  return run();
#endif
}
