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

enum {
  MIP_TEST_LEVELS       = 3u,
  MIP_TEST_BASE_SIZE    = 4u,
  MIP_TEST_PIXEL_BYTES  = 4u,
  MIP_TEST_ROW_PITCH    = 256u,
  MIP_TEST_LAYER_ROWS   = 7u,
  MIP_TEST_CHAIN_BYTES  = 84u
};

typedef struct MipmapCase {
  const char        *label;
  GPUTextureViewType viewType;
  uint32_t           layers;
} MipmapCase;

static const uint32_t mipOffsets[MIP_TEST_LEVELS] = {0u, 64u, 80u};
static const uint32_t rowOffsets[MIP_TEST_LEVELS] = {0u, 4u, 6u};

static const MipmapCase mipCases[] = {
  {"api-mipmaps-array",      GPU_TEXTURE_VIEW_2D_ARRAY,   3u},
  {"api-mipmaps-cube",       GPU_TEXTURE_VIEW_CUBE,       6u},
  {"api-mipmaps-cube-array", GPU_TEXTURE_VIEW_CUBE_ARRAY, 12u}
};

static void
mipmap_reference(uint8_t *pixels, uint32_t layer, uint32_t seed) {
  uint8_t  *source;
  uint8_t  *destination;
  uint32_t  mip, size, x, y, channel, dx, dy, sum;

  for (y = 0u; y < MIP_TEST_BASE_SIZE; y++) {
    for (x = 0u; x < MIP_TEST_BASE_SIZE; x++) {
      destination    = pixels + (y * MIP_TEST_BASE_SIZE + x) * MIP_TEST_PIXEL_BYTES;
      destination[0] = (uint8_t)(8u * layer + 4u * (x + 4u * y) + 4u * seed);
      destination[1] = (uint8_t)(4u * layer + 8u * x + 4u * y + 8u * seed);
      destination[2] = (uint8_t)(4u * layer + 4u * x + 8u * y + 12u * seed);
      destination[3] = (uint8_t)(64u + 4u * layer + 8u * x + 8u * y + 16u * seed);
    }
  }

  /* each 2x2 average is integral, so no rounding tolerance is needed. */
  for (mip = 1u; mip < MIP_TEST_LEVELS; mip++) {
    size        = MIP_TEST_BASE_SIZE >> mip;
    source      = pixels + mipOffsets[mip - 1u];
    destination = pixels + mipOffsets[mip];

    for (y = 0u; y < size; y++) {
      for (x = 0u; x < size; x++) {
        for (channel = 0u; channel < MIP_TEST_PIXEL_BYTES; channel++) {
          sum = 0u;

          for (dy = 0u; dy < 2u; dy++) {
            for (dx = 0u; dx < 2u; dx++) {
              sum += source[((2u * y + dy) * (2u * size) + 2u * x + dx) *
                            MIP_TEST_PIXEL_BYTES + channel];
            }
          }

          destination[(y * size + x) * MIP_TEST_PIXEL_BYTES + channel] = (uint8_t)(sum / 4u);
        }
      }
    }
  }
}

static int
check_mipmap_layers(GPUDevice *device, const MipmapCase *test) {
  GPUTextureCreateInfo       textureInfo = {0};
  GPUTextureViewCreateInfo   viewInfo    = {0};
  GPUTextureWriteRegion      writeRegion = {0};
  GPUBufferCreateInfo        bufferInfo  = {0};
  GPUBufferTextureCopyRegion readRegion  = {0};
  GPUTextureBarrier          barrier     = {0};
  GPUBarrierBatch            batch       = {0};
  GPUQueueSubmitInfo         submit      = {0};
  GPUCommandBuffer          *buffers[1];
  uint8_t                    expected[MIP_TEST_CHAIN_BYTES];
  uint8_t                    zeroPixels[64u] = {0};
  GPUQueue                  *queue;
  GPUTexture                *texture;
  GPUTextureView            *view;
  GPUBuffer                 *readback;
  GPUFence                  *fence;
  GPUCommandBuffer          *cmdb;
  GPUTransferPassEncoder    *pass;
  uint8_t                   *result;
  const uint8_t             *actual;
  uint64_t                   offset, resultSize;
  uint32_t                   layer, mip, seed, size, row;
  int                        ok;

  queue      = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  texture    = NULL;
  view       = NULL;
  readback   = NULL;
  fence      = NULL;
  cmdb       = NULL;
  pass       = NULL;
  result     = NULL;
  resultSize = (uint64_t)test->layers * MIP_TEST_LAYER_ROWS * MIP_TEST_ROW_PITCH;
  ok         = 0;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = test->label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = MIP_TEST_BASE_SIZE;
  textureInfo.height           = MIP_TEST_BASE_SIZE;
  textureInfo.depthOrLayers    = test->layers;
  textureInfo.mipLevelCount    = MIP_TEST_LEVELS;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COLOR_TARGET |
                                 GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = test->label;
  viewInfo.viewType         = test->viewType;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = MIP_TEST_LEVELS;
  viewInfo.arrayLayerCount  = test->layers;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = test->label;
  bufferInfo.sizeBytes        = resultSize;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;

  if (!queue
      || !(result = malloc((size_t)resultSize))
      || GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK
      || GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK
      || GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK
      || GPUCreateFence(device, NULL, &fence) != GPU_OK) {
    fprintf(stderr, "%s resource creation failed\n", test->label);
    goto cleanup;
  }

  writeRegion.depth      = 1u;
  writeRegion.layerCount = 1u;

  for (seed = 0u; seed < 2u; seed++) {
    for (layer = 0u; layer < test->layers; layer++) {
      mipmap_reference(expected, layer, seed);
      writeRegion.baseArrayLayer = layer;

      /* the second run must overwrite the previous generated chain. */
      for (mip = 0u; mip < (seed == 0u ? MIP_TEST_LEVELS : 1u); mip++) {
        size                     = MIP_TEST_BASE_SIZE >> mip;
        writeRegion.mipLevel     = mip;
        writeRegion.width        = size;
        writeRegion.height       = size;
        writeRegion.bytesPerRow  = size * MIP_TEST_PIXEL_BYTES;
        writeRegion.rowsPerImage = size;

        if (GPUQueueWriteTexture(queue,
                                 texture,
                                 &writeRegion,
                                 mip == 0u ? expected : zeroPixels,
                                 size * size * MIP_TEST_PIXEL_BYTES) != GPU_OK) {
          fprintf(stderr, "%s upload failed: seed=%u layer=%u mip=%u\n", test->label, seed, layer, mip);
          goto cleanup;
        }
      }
    }

    if (GPUAcquireCommandBuffer(queue, test->label, &cmdb) != GPU_OK || !cmdb) {
      fprintf(stderr, "%s command buffer creation failed\n", test->label);
      goto cleanup;
    }

    GPUGenerateMipmaps(cmdb, texture);

    barrier.texture           = texture;
    barrier.srcAccess         = GPU_ACCESS_COLOR_WRITE | GPU_ACCESS_TRANSFER_WRITE;
    barrier.dstAccess         = GPU_ACCESS_TRANSFER_READ;
    barrier.mipCount          = MIP_TEST_LEVELS;
    barrier.layerCount        = test->layers;
    batch.pTextureBarriers    = &barrier;
    batch.textureBarrierCount = 1u;
    batch.srcStages           = GPU_STAGE_FRAGMENT | GPU_STAGE_TRANSFER;
    batch.dstStages           = GPU_STAGE_TRANSFER;
    GPUEncodeBarriers(cmdb, &batch);

    if (!(pass = GPUBeginTransferPass(cmdb, test->label))) {
      fprintf(stderr, "%s readback pass failed\n", test->label);
      goto cleanup;
    }

    readRegion.bytesPerRow        = MIP_TEST_ROW_PITCH;
    readRegion.texture.depth      = 1u;
    readRegion.texture.layerCount = 1u;

    for (layer = 0u; layer < test->layers; layer++) {
      for (mip = 0u; mip < MIP_TEST_LEVELS; mip++) {
        size   = MIP_TEST_BASE_SIZE >> mip;
        offset = (uint64_t)(layer * MIP_TEST_LAYER_ROWS + rowOffsets[mip]) * MIP_TEST_ROW_PITCH;

        readRegion.bufferOffset                   = offset;
        readRegion.rowsPerImage                   = size;
        readRegion.texture.texture.mipLevel       = mip;
        readRegion.texture.texture.baseArrayLayer = layer;
        readRegion.texture.width                  = size;
        readRegion.texture.height                 = size;
        GPUCopyTextureToBuffer(pass, texture, readback, &readRegion);
      }
    }

    GPUEndTransferPass(pass);
    pass = NULL;

    buffers[0]                = cmdb;
    submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submit.chain.structSize   = sizeof(submit);
    submit.ppCommandBuffers   = buffers;
    submit.commandBufferCount = 1u;
    submit.fence              = fence;

    if (GPUQueueSubmit(queue, &submit) != GPU_OK
        || GPUWaitFence(fence, UINT64_MAX) != GPU_OK
        || GPUQueueReadBuffer(queue, readback, 0u, result, resultSize) != GPU_OK) {
      fprintf(stderr, "%s submit or readback failed\n", test->label);
      goto cleanup;
    }

    cmdb = NULL;

    for (layer = 0u; layer < test->layers; layer++) {
      mipmap_reference(expected, layer, seed);

      for (mip = 0u; mip < MIP_TEST_LEVELS; mip++) {
        size   = MIP_TEST_BASE_SIZE >> mip;
        offset = (uint64_t)(layer * MIP_TEST_LAYER_ROWS + rowOffsets[mip]) * MIP_TEST_ROW_PITCH;

        for (row = 0u; row < size; row++) {
          actual = result + offset + row * MIP_TEST_ROW_PITCH;

          if (memcmp(actual,
                     expected + mipOffsets[mip] + row * size * MIP_TEST_PIXEL_BYTES,
                     size * MIP_TEST_PIXEL_BYTES) != 0) {
            fprintf(stderr,
                    "%s mismatch: seed=%u layer=%u mip=%u row=%u\n",
                    test->label, seed, layer, mip, row);
            goto cleanup;
          }
        }
      }
    }
  }

  printf("%s passed\n", test->label);
  ok = 1;

cleanup:
  if (pass) {
    GPUEndTransferPass(pass);
  }

  GPUDestroyFence(fence);
  GPUDestroyBuffer(readback);
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);
  free(result);
  return ok;
}

int
gpu_test_mipmaps(GPUDevice *device) {
  uint32_t i;

  for (i = 0u; i < GPU_ARRAY_LEN(mipCases); i++) {
    if (!check_mipmap_layers(device, &mipCases[i])) {
      return 0;
    }
  }

  return 1;
}
