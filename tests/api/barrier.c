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
#include "../../src/backend/api/gpudef.h"
#include "../../src/api/cmdqueue_internal.h"
#include "../../src/api/device_internal.h"
#include "../../src/api/texture_internal.h"

static GPUCommandBuffer       *gLastBarrierCmdb;
static const GPUBarrierBatch  *gLastBarrierBatch;
static GPUBarrierBatch         gCapturedBarrierBatch;
static GPUTextureBarrier       gCapturedTextureBarrier;
static uint32_t                gBarrierForwardCount;

static void
count_barriers(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers) {
  gBarrierForwardCount++;
  gLastBarrierCmdb      = cmdb;
  gLastBarrierBatch     = barriers;
  gCapturedBarrierBatch = *barriers;

  if (barriers->textureBarrierCount == 1u
      && barriers->pTextureBarriers) {
    gCapturedTextureBarrier = barriers->pTextureBarriers[0];
  }
}

static int
check_barrier_forwarding(GPUDevice *device) {
  GPUApi              *api;
  GPUBuffer           *buffer  = NULL;
  GPUTexture          *texture = NULL;

  void (*savedEncodeBarriers)(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers);

  GPUQueue             fakeQueue      = {0};
  GPUCommandBuffer     fakeCmdb       = {0};
  GPUBufferCreateInfo  bufferInfo     = {0};
  GPUTextureCreateInfo textureInfo    = {0};
  GPUBufferBarrier     bufferBarrier  = {0};
  GPUTextureBarrier    textureBarrier = {0};
  GPUBarrierBatch      batch          = {0};
  int                  ok             = 0;

  api = deviceApi(device);

  if (!api) {
    fprintf(stderr, "barrier test active api missing\n");
    return 0;
  }

  savedEncodeBarriers            = api->renderPass.encodeBarriers;
  api->renderPass.encodeBarriers = count_barriers;
  gBarrierForwardCount           = 0u;
  gLastBarrierCmdb               = NULL;
  gLastBarrierBatch              = NULL;
  gCapturedBarrierBatch          = (GPUBarrierBatch){0};
  gCapturedTextureBarrier        = (GPUTextureBarrier){0};
  fakeQueue._device              = device;
  fakeCmdb._queue                = &fakeQueue;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = 256u;
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    fprintf(stderr, "barrier test buffer create failed\n");
    goto done;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 4u;
  textureInfo.height           = 4u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;

  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "barrier test texture create failed\n");
    goto done;
  }

  bufferBarrier.buffer    = buffer;
  bufferBarrier.srcAccess = GPU_ACCESS_TRANSFER_WRITE;
  bufferBarrier.dstAccess = GPU_ACCESS_SHADER_READ;
  bufferBarrier.offset    = 0u;
  bufferBarrier.sizeBytes = 256u;

  textureBarrier.texture    = texture;
  textureBarrier.srcAccess  = GPU_ACCESS_TRANSFER_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_SHADER_READ;
  textureBarrier.baseMip    = 0u;
  textureBarrier.mipCount   = 1u;
  textureBarrier.baseLayer  = 0u;
  textureBarrier.layerCount = 1u;

  batch.srcStages           = GPU_STAGE_TRANSFER;
  batch.dstStages           = GPU_STAGE_FRAGMENT;
  batch.bufferBarrierCount  = 1u;
  batch.pBufferBarriers     = &bufferBarrier;
  batch.textureBarrierCount = 1u;
  batch.pTextureBarriers    = &textureBarrier;

  GPUEncodeBarriers(NULL, &batch);
  GPUEncodeBarriers(&fakeCmdb, NULL);

  fakeCmdb._submitted = true;
  GPUEncodeBarriers(&fakeCmdb, &batch);
  fakeCmdb._submitted     = false;
  fakeCmdb._activeEncoder = true;
  GPUEncodeBarriers(&fakeCmdb, &batch);
  fakeCmdb._activeEncoder = false;
  GPUEncodeBarriers(&fakeCmdb, &batch);

  batch.srcStages = 0u;
  GPUEncodeBarriers(&fakeCmdb, &batch);

  batch.srcStages         = GPU_STAGE_TRANSFER;
  bufferBarrier.sizeBytes = 0u;
  GPUEncodeBarriers(&fakeCmdb, &batch);

  bufferBarrier.sizeBytes = 256u;
  bufferBarrier.offset    = 240u;
  bufferBarrier.sizeBytes = 32u;
  GPUEncodeBarriers(&fakeCmdb, &batch);
  bufferBarrier.offset    = 0u;
  bufferBarrier.sizeBytes = 256u;
  textureBarrier.mipCount = 0u;
  GPUEncodeBarriers(&fakeCmdb, &batch);
  textureBarrier.mipCount = 1u;
  textureBarrier.baseMip  = 1u;
  GPUEncodeBarriers(&fakeCmdb, &batch);
  textureBarrier.baseMip   = 0u;
  textureBarrier.baseLayer = 1u;
  GPUEncodeBarriers(&fakeCmdb, &batch);
  textureBarrier.baseLayer = 0u;
  textureBarrier.dstAccess = GPU_ACCESS_INDIRECT_READ;
  GPUEncodeBarriers(&fakeCmdb, &batch);

  textureBarrier.dstAccess = GPU_ACCESS_SHADER_READ;
  textureBarrier.srcAccess = GPU_ACCESS_COLOR_WRITE;
  GPUEncodeBarriers(&fakeCmdb, &batch);

  textureBarrier.srcAccess = GPU_ACCESS_TRANSFER_WRITE;

  if (gBarrierForwardCount != 1u
      || gLastBarrierCmdb != &fakeCmdb
      || gLastBarrierBatch != &batch) {
    fprintf(stderr, "barrier test callback count mismatch\n");
    goto done;
  }

  GPUTransitionTexture(&fakeCmdb,
                       texture,
                       GPU_ACCESS_TRANSFER_WRITE,
                       GPU_ACCESS_SHADER_READ);

  if (gBarrierForwardCount != 2u
      || gCapturedBarrierBatch.srcStages != GPU_STAGE_TRANSFER
      || gCapturedBarrierBatch.dstStages != (GPU_STAGE_VERTEX |
                                             GPU_STAGE_FRAGMENT |
                                             GPU_STAGE_COMPUTE)
      || gCapturedBarrierBatch.textureBarrierCount != 1u
      || gCapturedTextureBarrier.texture != texture
      || gCapturedTextureBarrier.srcAccess != GPU_ACCESS_TRANSFER_WRITE
      || gCapturedTextureBarrier.dstAccess != GPU_ACCESS_SHADER_READ
      || gCapturedTextureBarrier.baseMip != 0u
      || gCapturedTextureBarrier.mipCount != 1u
      || gCapturedTextureBarrier.baseLayer != 0u
      || gCapturedTextureBarrier.layerCount != 1u) {
    fprintf(stderr, "texture transition inference mismatch\n");
    goto done;
  }

  GPUTransitionTexture(&fakeCmdb,
                       texture,
                       GPU_ACCESS_NONE,
                       GPU_ACCESS_TRANSFER_WRITE);

  if (gBarrierForwardCount != 3u
      || gCapturedBarrierBatch.srcStages != GPU_STAGE_TOP
      || gCapturedBarrierBatch.dstStages != GPU_STAGE_TRANSFER) {
    fprintf(stderr, "texture transition initial state mismatch\n");
    goto done;
  }

  GPUTransitionTexture(&fakeCmdb,
                       texture,
                       GPU_ACCESS_TRANSFER_WRITE,
                       GPU_ACCESS_INDIRECT_READ);

  if (gBarrierForwardCount != 3u) {
    fprintf(stderr, "invalid texture transition should not forward\n");
    goto done;
  }

  api->renderPass.encodeBarriers = NULL;
  GPUEncodeBarriers(&fakeCmdb, &batch);

  if (gBarrierForwardCount != 3u) {
    fprintf(stderr, "barrier test null backend callback should not forward\n");
    goto done;
  }

  ok = 1;

done:
  api->renderPass.encodeBarriers = savedEncodeBarriers;
  GPUDestroyTexture(texture);
  GPUDestroyBuffer(buffer);
  return ok;
}

static int
check_mipmap_barrier(GPUDevice *device) {
  GPUTexture        texture = {0};
  GPUQueue          queue   = {0};
  GPUCommandBuffer  cmdb    = {0};
  GPUTextureBarrier barrier = {0};
  GPUBarrierBatch   batch   = {0};
  GPUApi           *api;

  void (*saved)(GPUCommandBuffer *, const GPUBarrierBatch *);

  uint32_t          invalid;
  int               ok;

  api = deviceApi(device);

  if (!api)
    return 0;

  saved                         = api->renderPass.encodeBarriers;
  api->renderPass.encodeBarriers = count_barriers;
  gBarrierForwardCount          = 0u;
  queue._device                 = device;
  cmdb._queue                   = &queue;

  texture.device                = device;
  texture.dimension             = GPU_TEXTURE_DIMENSION_2D;
  texture.sampleCount           = 1u;
  texture.mipLevelCount         = 3u;
  texture.depthOrLayers         = 2u;
  texture.usage                 = GPU_TEXTURE_USAGE_SAMPLED |
                                  GPU_TEXTURE_USAGE_COLOR_TARGET;

  barrier.texture               = &texture;
  barrier.srcAccess             = GPU_ACCESS_COLOR_WRITE | GPU_ACCESS_TRANSFER_WRITE;
  barrier.dstAccess             = GPU_ACCESS_SHADER_READ | GPU_ACCESS_TRANSFER_READ;
  barrier.mipCount              = 1u;
  barrier.layerCount            = 1u;

  batch.pTextureBarriers        = &barrier;
  batch.textureBarrierCount     = 1u;
  batch.srcStages               = GPU_STAGE_FRAGMENT | GPU_STAGE_TRANSFER;
  batch.dstStages               = GPU_STAGE_FRAGMENT | GPU_STAGE_TRANSFER;

  GPUEncodeBarriers(&cmdb, &batch);
  GPUTransitionTexture(&cmdb, &texture, barrier.srcAccess, barrier.dstAccess);

  ok = gBarrierForwardCount == 2u;

  for (invalid = 0u; invalid < 7u; invalid++) {
    texture.dimension     = GPU_TEXTURE_DIMENSION_2D;
    texture.sampleCount   = 1u;
    texture.mipLevelCount = 3u;
    texture.usage         = GPU_TEXTURE_USAGE_SAMPLED |
                            GPU_TEXTURE_USAGE_COLOR_TARGET;

    barrier.srcAccess     = GPU_ACCESS_COLOR_WRITE | GPU_ACCESS_TRANSFER_WRITE;
    barrier.dstAccess     = GPU_ACCESS_SHADER_READ | GPU_ACCESS_TRANSFER_READ;

    switch (invalid) {
      case 0u: texture.usage &= ~GPU_TEXTURE_USAGE_COLOR_TARGET; break;
      case 1u: texture.usage &= ~GPU_TEXTURE_USAGE_SAMPLED; break;
      case 2u: texture.sampleCount = 4u; break;
      case 3u: texture.dimension = GPU_TEXTURE_DIMENSION_3D; break;
      case 4u: texture.mipLevelCount = 1u; break;
      case 5u: barrier.srcAccess = GPU_ACCESS_TRANSFER_WRITE; break;
      case 6u: barrier.dstAccess = GPU_ACCESS_TRANSFER_READ; break;
    }

    GPUEncodeBarriers(&cmdb, &batch);

    ok = ok && gBarrierForwardCount == 2u;
  }

  api->renderPass.encodeBarriers = saved;

  if (!ok)
    fprintf(stderr, "mipmap barrier access contract mismatch\n");

  return ok;
}

int
gpu_test_barrier(GPUDevice *device) {
  return check_barrier_forwarding(device) && check_mipmap_barrier(device);
}
