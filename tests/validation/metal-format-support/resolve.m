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

#include <gpu/gpu.h>
#include "../../../src/backend/mt/common.h"

typedef union ResolvePixels {
  float    scalars[128];
  uint32_t packed[128];
} ResolvePixels;

typedef struct ResolveCase {
  GPUFormat format;
  uint32_t  channels;
  uint32_t  packed;
  bool      integer;
} ResolveCase;

static const ResolveCase cases[] = {
  {GPU_FORMAT_R32_FLOAT, 1u, 0u, false},
  {GPU_FORMAT_RG32_FLOAT, 2u, 0u, false},
  {GPU_FORMAT_RGBA32_FLOAT, 4u, 0u, false},
  {GPU_FORMAT_BGRA8_UNORM, 0u, 0xffffffffu, false},
  {GPU_FORMAT_RGB10A2_UNORM, 0u, 0xffffffffu, false},
  {GPU_FORMAT_RG11B10_UFLOAT, 0u, 0x781e03c0u, false},
  {GPU_FORMAT_RGB9E5_UFLOAT, 0u, 0x84020100u, false},
  {GPU_FORMAT_R8_UINT, 0u, 0u, true},
  {GPU_FORMAT_R8_SINT, 0u, 0u, true},
  {GPU_FORMAT_R16_UINT, 0u, 0u, true},
  {GPU_FORMAT_R16_SINT, 0u, 0u, true},
  {GPU_FORMAT_RG8_UINT, 0u, 0u, true},
  {GPU_FORMAT_RG8_SINT, 0u, 0u, true},
  {GPU_FORMAT_R32_UINT, 0u, 0u, true},
  {GPU_FORMAT_R32_SINT, 0u, 0u, true},
  {GPU_FORMAT_RG16_UINT, 0u, 0u, true},
  {GPU_FORMAT_RG16_SINT, 0u, 0u, true},
  {GPU_FORMAT_RGBA8_UINT, 0u, 0u, true},
  {GPU_FORMAT_RGBA8_SINT, 0u, 0u, true},
  {GPU_FORMAT_RGB10A2_UINT, 0u, 0u, true},
  {GPU_FORMAT_RG32_UINT, 0u, 0u, true},
  {GPU_FORMAT_RG32_SINT, 0u, 0u, true},
  {GPU_FORMAT_RGBA16_UINT, 0u, 0u, true},
  {GPU_FORMAT_RGBA16_SINT, 0u, 0u, true},
  {GPU_FORMAT_RGBA32_UINT, 0u, 0u, true},
  {GPU_FORMAT_RGBA32_SINT, 0u, 0u, true}
};
static const float expected[] = {0.25f, 0.5f, -0.5f, 2.0f};

static bool
check_format(GPUDevice         *device,
             GPUQueue          *queue,
             GPUFence          *fence,
             GPUBuffer         *readback,
             const ResolveCase *test) {
  ResolvePixels                 pixels;
  GPUTextureCreateInfo          textureInfo = {0};
  GPUTextureViewCreateInfo      viewInfo    = {0};
  GPURenderPassColorAttachment  color       = {0};
  GPURenderPassCreateInfo       passInfo    = {0};
  GPUBufferTextureCopyRegion    copyRegion  = {0};
  GPUQueueSubmitInfo            submitInfo  = {0};
  GPUFormatCapabilities         caps;
  GPUCommandBuffer             *buffers[1];
  GPUTexture                   *textures[2] = {0};
  GPUTextureView               *views[2]    = {0};
  GPUAdapterMT                 *native;
  GPUCommandBuffer             *cmdb       = NULL;
  GPURenderPassEncoder          *render     = NULL;
  GPUTransferPassEncoder        *copy       = NULL;
  GPUAccessMask                 resolveAccess = GPU_ACCESS_NONE;
  GPUFormat                     format;
  uint32_t                      i, flags, x, y, c;
  uint32_t                      channels;
  uint32_t                      failures     = 0u;
  uint32_t                      values       = 0u;
  uint32_t                      packedValues = 0u;
  bool                          actualSupport;
  bool                          supported;
  bool                          resolve;
  bool                          ok          = false;

  format   = test->format;
  channels = test->channels;

  if (GPUGetFormatCapabilities(device->adapter, format, &caps) != GPU_OK) {
    return false;
  }

  if (!(caps.supportedSampleCounts & GPU_SAMPLE_COUNT_4_BIT)) {
    printf("format-resolve: format=%u has no 4x allocation support\n", format);
    return true;
  }

  native        = device->adapter->_priv;
  actualSupport = native->msaa32Supported;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = format;
  textureInfo.width            = 2u;
  textureInfo.height           = 2u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = format;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;

  for (i = 0u; i < 2u; i++) {
    textureInfo.sampleCount = i == 0u ? 4u : 1u;
    textureInfo.usage       = GPU_TEXTURE_USAGE_COLOR_TARGET;

    if (i == 1u) {
      textureInfo.usage |= GPU_TEXTURE_USAGE_COPY_SRC;
    }

    if (GPUCreateTexture(device, &textureInfo, &textures[i]) != GPU_OK
        || GPUCreateTextureView(textures[i], &viewInfo, &views[i]) != GPU_OK) {
      fprintf(stderr, "format-resolve: format=%u texture setup failed\n", format);
      goto cleanup;
    }
  }

  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;

  for (flags = 0u; flags < 8u; flags++) {
    if (GPUAcquireCommandBuffer(queue, "format-resolve", &cmdb) != GPU_OK) {
      goto cleanup;
    }

    GPUTransitionTexture(cmdb, textures[1], resolveAccess, GPU_ACCESS_COLOR_WRITE);
    resolveAccess = GPU_ACCESS_COLOR_WRITE;

    if (flags == 0u) {
      GPUTransitionTexture(cmdb, textures[0], GPU_ACCESS_NONE, GPU_ACCESS_COLOR_WRITE);
    }

    /* prime the destination so a missing resolve cannot satisfy the readback. */
    color.view        = views[1];
    color.resolveView = NULL;
    color.loadOp      = GPU_LOAD_OP_CLEAR;
    color.storeOp     = GPU_STORE_OP_STORE;

    memset(&color.clearColor, 0, sizeof(color.clearColor));

    if (!(render = GPUBeginRenderPass(cmdb, &passInfo))) {
      goto cleanup;
    }

    GPUEndRenderPass(render);
    render = NULL;

    GPUTransitionTexture(cmdb, textures[1], GPU_ACCESS_COLOR_WRITE, GPU_ACCESS_COLOR_WRITE);
    GPUTransitionTexture(cmdb, textures[0], GPU_ACCESS_COLOR_WRITE, GPU_ACCESS_COLOR_WRITE);

    resolve           = (flags & 2u) != 0u;
    color.view        = views[0];
    color.resolveView = resolve ? views[1] : NULL;
    color.storeOp     = (flags & 4u) ? GPU_STORE_OP_STORE : GPU_STORE_OP_DONT_CARE;

    if (channels) {
      memcpy(color.clearColor.float32, expected, sizeof(expected));
    } else if (test->integer) {
      /* positive values share the signed/unsigned representation. */
      color.clearColor.uint32[0] = 37u;
      color.clearColor.uint32[1] = 53u;
      color.clearColor.uint32[2] = 71u;
      color.clearColor.uint32[3] = 2u;
    } else {
      for (c = 0u; c < 4u; c++) {
        color.clearColor.float32[c] = 1.0f;
      }
    }

    /* never turn on a native capability that the real device lacks. */
    native->msaa32Supported = actualSupport && (flags & 1u);
    supported = !resolve
                || (!test->integer && (channels == 0u || native->msaa32Supported));
    render    = GPUBeginRenderPass(cmdb, &passInfo);
    native->msaa32Supported = actualSupport;

    if ((render != NULL) != supported) {
      fprintf(stderr, "format-resolve: format=%u flags=%u accepted=%u expected=%u\n",
              format, flags, render != NULL, supported);
      failures++;
    }

    if (render) {
      GPUEndRenderPass(render);
      render = NULL;
    }

    if (resolve && supported) {
      GPUTransitionTexture(cmdb, textures[1], GPU_ACCESS_COLOR_WRITE, GPU_ACCESS_TRANSFER_READ);
      resolveAccess = GPU_ACCESS_TRANSFER_READ;

      if (!(copy = GPUBeginTransferPass(cmdb, "format-resolve-readback"))) {
        goto cleanup;
      }

      copyRegion.bytesPerRow        = 256u;
      copyRegion.rowsPerImage       = 2u;
      copyRegion.texture.width      = 2u;
      copyRegion.texture.height     = 2u;
      copyRegion.texture.depth      = 1u;
      copyRegion.texture.layerCount = 1u;
      GPUCopyTextureToBuffer(copy, textures[1], readback, &copyRegion);
      GPUEndTransferPass(copy);
      copy = NULL;
    }

    buffers[0]                    = cmdb;
    submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize   = sizeof(submitInfo);
    submitInfo.ppCommandBuffers   = buffers;
    submitInfo.commandBufferCount = 1u;
    submitInfo.fence              = fence;

    if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
      goto cleanup;
    }

    cmdb = NULL;

    if (GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
      goto cleanup;
    }

    if (resolve && supported) {
      if (GPUQueueReadBuffer(queue, readback, 0u, &pixels, sizeof(pixels)) != GPU_OK) {
        goto cleanup;
      }

      for (y = 0u; y < 2u; y++) {
        for (x = 0u; x < 2u; x++) {
          if (test->packed) {
            if (pixels.packed[y * 64u + x] != test->packed) {
              fprintf(stderr,
                      "format-resolve: format=%u flags=%u pixel=%u,%u packed=%08x expected=%08x\n",
                      format, flags, x, y, pixels.packed[y * 64u + x], test->packed);
              goto cleanup;
            }

            packedValues++;
          }

          for (c = 0u; c < channels; c++) {
            if (pixels.scalars[y * 64u + x * channels + c] != expected[c]) {
              fprintf(stderr,
                      "format-resolve: format=%u flags=%u pixel=%u,%u channel=%u actual=%g expected=%g\n",
                      format, flags, x, y, c, pixels.scalars[y * 64u + x * channels + c], expected[c]);
              goto cleanup;
            }

            values++;
          }
        }
      }
    }
  }

  printf("format-resolve: format=%u eight cases, %u failures, %u exact scalars, %u packed pixels\n",
         format, failures, values, packedValues);
  ok = failures == 0u;

cleanup:
  native->msaa32Supported = actualSupport;

  if (!ok && failures == 0u) {
    fprintf(stderr, "format-resolve: format=%u execution failed\n", format);
  }

  if (copy) {
    GPUEndTransferPass(copy);
  }

  if (render) {
    GPUEndRenderPass(render);
  }

  if (cmdb) {
    GPUDiscardCommandBuffer(cmdb);
  }

  for (i = 0u; i < 2u; i++) {
    GPUDestroyTextureView(views[i]);
    GPUDestroyTexture(textures[i]);
  }

  return ok;
}

bool
check_resolve(GPUAdapter *adapter) {
  GPUBufferCreateInfo info     = {0};
  GPUDevice          *device   = NULL;
  GPUBuffer          *readback = NULL;
  GPUFence           *fence    = NULL;
  GPUQueue           *queue;
  uint32_t            i;
  bool                ok = false;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.sizeBytes        = 512u;
  info.usage            = GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC;

  if (!(device = GPUCreateDeviceWithDefaultQueues(adapter))
      || !(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u))
      || GPUCreateBuffer(device, &info, &readback) != GPU_OK
      || GPUCreateFence(device, NULL, &fence) != GPU_OK) {
    fprintf(stderr, "format-resolve: device/readback setup failed\n");
    goto cleanup;
  }

  ok = true;

  for (i = 0u; i < GPU_ARRAY_LEN(cases); i++) {
    ok = check_format(device, queue, fence, readback, &cases[i]) && ok;
  }

cleanup:
  GPUDestroyFence(fence);
  GPUDestroyBuffer(readback);
  GPUDestroyDevice(device);
  return ok;
}
