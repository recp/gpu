#include "test.h"
#include "../../src/api/cmdqueue_internal.h"

static int
check_barrier_noops(GPUDevice *device) {
  GPUCommandBuffer fakeCmdb = {0};
  GPUBufferCreateInfo bufferInfo = {0};
  GPUTextureCreateInfo textureInfo = {0};
  GPUBufferBarrier bufferBarrier = {0};
  GPUTextureBarrier textureBarrier = {0};
  GPUBarrierBatch batch = {0};
  GPUBuffer *buffer = NULL;
  GPUTexture *texture = NULL;

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes = 256u;
  bufferInfo.usage = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    fprintf(stderr, "barrier test buffer create failed\n");
    return 0;
  }

  textureInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width = 4u;
  textureInfo.height = 4u;
  textureInfo.depthOrLayers = 1u;
  textureInfo.mipLevelCount = 1u;
  textureInfo.sampleCount = 1u;
  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "barrier test texture create failed\n");
    GPUDestroyBuffer(buffer);
    return 0;
  }

  bufferBarrier.buffer = buffer;
  bufferBarrier.srcAccess = GPU_ACCESS_TRANSFER_WRITE;
  bufferBarrier.dstAccess = GPU_ACCESS_SHADER_READ;
  bufferBarrier.offset = 0u;
  bufferBarrier.sizeBytes = 256u;

  textureBarrier.texture = texture;
  textureBarrier.srcAccess = GPU_ACCESS_TRANSFER_WRITE;
  textureBarrier.dstAccess = GPU_ACCESS_SHADER_READ;
  textureBarrier.baseMip = 0u;
  textureBarrier.mipCount = 1u;
  textureBarrier.baseLayer = 0u;
  textureBarrier.layerCount = 1u;

  batch.srcStages = GPU_STAGE_TRANSFER;
  batch.dstStages = GPU_STAGE_FRAGMENT;
  batch.bufferBarrierCount = 1u;
  batch.pBufferBarriers = &bufferBarrier;
  batch.textureBarrierCount = 1u;
  batch.pTextureBarriers = &textureBarrier;

  GPUEncodeBarriers(NULL, &batch);
  GPUEncodeBarriers(&fakeCmdb, NULL);

  fakeCmdb._submitted = true;
  GPUEncodeBarriers(&fakeCmdb, &batch);
  fakeCmdb._submitted = false;
  fakeCmdb._activeEncoder = true;
  GPUEncodeBarriers(&fakeCmdb, &batch);
  fakeCmdb._activeEncoder = false;
  GPUEncodeBarriers(&fakeCmdb, &batch);

  batch.srcStages = 0u;
  GPUEncodeBarriers(&fakeCmdb, &batch);
  batch.srcStages = GPU_STAGE_TRANSFER;
  bufferBarrier.sizeBytes = 0u;
  GPUEncodeBarriers(&fakeCmdb, &batch);
  bufferBarrier.sizeBytes = 256u;
  textureBarrier.mipCount = 0u;
  GPUEncodeBarriers(&fakeCmdb, &batch);

  GPUDestroyTexture(texture);
  GPUDestroyBuffer(buffer);
  return 1;
}

int
gpu_test_barrier(GPUDevice *device) {
  return check_barrier_noops(device);
}
