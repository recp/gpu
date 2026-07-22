#include "test.h"

enum {
  SAMPLER_FEEDBACK_TARGET_WIDTH   = 64u,
  SAMPLER_FEEDBACK_TARGET_HEIGHT  = 64u,
  SAMPLER_FEEDBACK_REGION_SIZE    = 4u,
  SAMPLER_FEEDBACK_DECODE_WIDTH   = 16u,
  SAMPLER_FEEDBACK_DECODE_HEIGHT  = 16u,
  SAMPLER_FEEDBACK_MIP_COUNT      = 4u,
  SAMPLER_FEEDBACK_ROW_PITCH      = 256u,
  SAMPLER_FEEDBACK_READBACK_BYTES =
    SAMPLER_FEEDBACK_ROW_PITCH * (16u + 8u + 4u + 2u)
};

static uint32_t
gpu_test_sampler_feedback_mip_extent(uint32_t extent, uint32_t mipLevel) {
  extent >>= mipLevel;
  return extent ? extent : 1u;
}

static int
gpu_test_sampler_feedback_clear_result(
  const uint8_t                         *bytes,
  const GPUSamplerFeedbackDecodeInfoEXT *decodeInfo,
  GPUSamplerFeedbackModeEXT              mode) {
  const uint8_t expected = mode == GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT
                             ? UINT8_MAX
                             : 0u;
  uint64_t offset;

  offset = 0u;
  for (uint32_t mip = 0u; mip < decodeInfo->mipLevelCount; mip++) {
    uint32_t width;
    uint32_t height;

    width  = gpu_test_sampler_feedback_mip_extent(decodeInfo->width, mip);
    height = gpu_test_sampler_feedback_mip_extent(decodeInfo->height, mip);
    for (uint32_t row = 0u; row < height; row++) {
      for (uint32_t column = 0u; column < width; column++) {
        uint8_t actual;

        actual = bytes[offset + (uint64_t)row * SAMPLER_FEEDBACK_ROW_PITCH +
                       column];
        if (actual != expected) {
          fprintf(stderr,
                  "sampler feedback clear mismatch at mip %u, (%u, %u): "
                  "expected 0x%02x, got 0x%02x\n",
                  mip,
                  column,
                  row,
                  expected,
                  actual);
          return 0;
        }
      }
    }
    offset += (uint64_t)SAMPLER_FEEDBACK_ROW_PITCH * height;
  }
  return 1;
}

static int
gpu_test_sampler_feedback_mode(GPUDevice                  *device,
                               GPUSamplerFeedbackModeEXT   mode) {
  GPUTextureCreateInfo                 textureInfo = {0};
  GPUBufferCreateInfo                  bufferInfo  = {0};
  GPUSamplerFeedbackMapCreateInfoEXT   mapInfo     = {0};
  GPUSamplerFeedbackDecodeInfoEXT      decodeInfo  = {0};
  GPUBufferTextureCopyRegion           copyRegion  = {0};
  GPUQueueSubmitInfo                   submitInfo  = {0};
  GPUQueue                            *queue;
  GPUTexture                          *target;
  GPUTexture                          *decoded;
  GPUBuffer                           *readback;
  GPUSamplerFeedbackMapEXT            *map;
  GPUCommandBuffer                    *cmdb;
  GPUCopyPassEncoder                  *copyPass;
  GPUFence                            *fence;
  uint8_t                              readbackBytes[
    SAMPLER_FEEDBACK_READBACK_BYTES] = {0};
  uint64_t                             readbackSize;
  int                                  ok;

  queue        = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  target       = NULL;
  decoded      = NULL;
  readback     = NULL;
  map          = NULL;
  cmdb         = NULL;
  copyPass     = NULL;
  fence        = NULL;
  readbackSize = 0u;
  ok           = queue != NULL;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "sampler-feedback-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = SAMPLER_FEEDBACK_TARGET_WIDTH;
  textureInfo.height           = SAMPLER_FEEDBACK_TARGET_HEIGHT;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = SAMPLER_FEEDBACK_MIP_COUNT;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED;
  ok = ok && GPUCreateTexture(device, &textureInfo, &target) == GPU_OK &&
       target;

  mapInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_SAMPLER_FEEDBACK_MAP_CREATE_INFO_EXT;
  mapInfo.chain.structSize = sizeof(mapInfo);
  mapInfo.label            = "sampler-feedback-map";
  mapInfo.texture          = target;
  mapInfo.mode             = mode;
  mapInfo.mipRegionWidth   = SAMPLER_FEEDBACK_REGION_SIZE;
  mapInfo.mipRegionHeight  = SAMPLER_FEEDBACK_REGION_SIZE;
  ok = ok && GPUCreateSamplerFeedbackMapEXT(device, &mapInfo, &map) == GPU_OK &&
       map &&
       GPUGetSamplerFeedbackDecodeInfoEXT(map, &decodeInfo) == GPU_OK &&
       decodeInfo.format == GPU_FORMAT_R8_UINT &&
       decodeInfo.width == SAMPLER_FEEDBACK_DECODE_WIDTH &&
       decodeInfo.height == SAMPLER_FEEDBACK_DECODE_HEIGHT &&
       decodeInfo.arrayLayerCount == 1u &&
       decodeInfo.mipLevelCount ==
         (mode == GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT
            ? 1u
            : SAMPLER_FEEDBACK_MIP_COUNT);

  textureInfo.label         = "sampler-feedback-decoded";
  textureInfo.format        = decodeInfo.format;
  textureInfo.width         = decodeInfo.width;
  textureInfo.height        = decodeInfo.height;
  textureInfo.depthOrLayers = decodeInfo.arrayLayerCount;
  textureInfo.mipLevelCount = decodeInfo.mipLevelCount;
  textureInfo.usage         = GPU_TEXTURE_USAGE_COPY_SRC |
                              GPU_TEXTURE_USAGE_COPY_DST;
  ok = ok && GPUCreateTexture(device, &textureInfo, &decoded) == GPU_OK &&
       decoded;

  for (uint32_t mip = 0u; mip < decodeInfo.mipLevelCount; mip++) {
    readbackSize +=
      (uint64_t)SAMPLER_FEEDBACK_ROW_PITCH *
      gpu_test_sampler_feedback_mip_extent(decodeInfo.height, mip);
  }
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "sampler-feedback-readback";
  bufferInfo.sizeBytes        = readbackSize;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  ok = ok && readbackSize <= sizeof(readbackBytes) &&
       GPUCreateBuffer(device, &bufferInfo, &readback) == GPU_OK && readback;
  memset(readbackBytes, 0xa5, sizeof(readbackBytes));
  ok = ok && GPUQueueWriteBuffer(queue,
                                 readback,
                                 0u,
                                 readbackBytes,
                                 readbackSize) == GPU_OK;

  if (!ok || GPUAcquireCommandBuffer(queue,
                                     "sampler-feedback",
                                     &cmdb) != GPU_OK ||
      !cmdb || GPUClearSamplerFeedbackEXT(cmdb, map) != GPU_OK ||
      GPUDecodeSamplerFeedbackEXT(cmdb, map, decoded) != GPU_OK) {
    ok = 0;
    goto cleanup;
  }

  copyPass = GPUBeginCopyPass(cmdb, "sampler-feedback-readback");
  if (!copyPass) {
    ok = 0;
    goto cleanup;
  }
  copyRegion.bytesPerRow        = SAMPLER_FEEDBACK_ROW_PITCH;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  for (uint32_t mip = 0u; mip < decodeInfo.mipLevelCount; mip++) {
    uint32_t width;
    uint32_t height;

    width  = gpu_test_sampler_feedback_mip_extent(decodeInfo.width, mip);
    height = gpu_test_sampler_feedback_mip_extent(decodeInfo.height, mip);
    copyRegion.rowsPerImage            = height;
    copyRegion.texture.texture.mipLevel = mip;
    copyRegion.texture.width           = width;
    copyRegion.texture.height          = height;
    GPUCopyTextureToBuffer(copyPass, decoded, readback, &copyRegion);
    copyRegion.bufferOffset +=
      (uint64_t)SAMPLER_FEEDBACK_ROW_PITCH * height;
  }
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUEncodeSamplerFeedbackEXT(cmdb, decoded, map) != GPU_OK ||
      GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    ok = 0;
    goto cleanup;
  }

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.commandBufferCount = 1u;
  submitInfo.fence              = fence;
  ok = GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
       GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
  cmdb = NULL;
  if (!ok ||
      GPUQueueReadBuffer(queue,
                         readback,
                         0u,
                         readbackBytes,
                         readbackSize) != GPU_OK ||
      !gpu_test_sampler_feedback_clear_result(readbackBytes,
                                              &decodeInfo,
                                              mode)) {
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  if (cmdb) {
    GPUDiscardCommandBuffer(cmdb);
  }
  GPUDestroyFence(fence);
  GPUDestroyBuffer(readback);
  GPUDestroyTexture(decoded);
  GPUDestroySamplerFeedbackMapEXT(map);
  GPUDestroyTexture(target);
  return ok;
}

int
gpu_test_sampler_feedback(GPUAdapter *adapter, GPUDevice *defaultDevice) {
  static const char * const entries[] = {
    "GPUCreateSamplerFeedbackMapEXT",
    "GPUDestroySamplerFeedbackMapEXT",
    "GPUGetSamplerFeedbackDecodeInfoEXT",
    "GPUClearSamplerFeedbackEXT",
    "GPUDecodeSamplerFeedbackEXT",
    "GPUEncodeSamplerFeedbackEXT"
  };
  GPUSamplerFeedbackPropertiesEXT properties = {0};
  GPUDeviceCreateInfo             deviceInfo = {0};
  GPUDevice                      *device;
  GPUFeature                      feature;
  GPUResult                       result;
  int                             supported;
  int                             ok;

  if (!adapter || !defaultDevice) {
    return 0;
  }

  feature   = GPU_FEATURE_SAMPLER_FEEDBACK;
  supported = GPUIsFeatureSupported(adapter, feature);
  result    = GPUGetSamplerFeedbackPropertiesEXT(adapter, &properties);
  if (!supported) {
    deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.chain.structSize      = sizeof(deviceInfo);
    deviceInfo.required.pFeatures    = &feature;
    deviceInfo.required.featureCount = 1u;
    device = NULL;
    if (result != GPU_ERROR_UNSUPPORTED ||
        properties.tier != GPU_SAMPLER_FEEDBACK_TIER_NONE_EXT ||
        GPUCreateDevice(adapter, &deviceInfo, &device) !=
          GPU_ERROR_UNSUPPORTED ||
        device) {
      fprintf(stderr, "unsupported sampler feedback was exposed\n");
      GPUDestroyDevice(device);
      return 0;
    }
    puts("sampler-feedback execution skipped: unsupported adapter");
    return 1;
  }

  if (result != GPU_OK ||
      (properties.tier != GPU_SAMPLER_FEEDBACK_TIER_0_9_EXT &&
       properties.tier != GPU_SAMPLER_FEEDBACK_TIER_1_0_EXT)) {
    fprintf(stderr, "sampler feedback properties mismatch\n");
    return 0;
  }
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(entries); i++) {
    if (GPUGetProcAddr(defaultDevice, entries[i])) {
      fprintf(stderr, "sampler feedback entry enabled by default: %s\n",
              entries[i]);
      return 0;
    }
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  device = NULL;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, feature)) {
    fprintf(stderr, "sampler feedback feature enablement failed\n");
    GPUDestroyDevice(device);
    return 0;
  }
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(entries); i++) {
    if (!GPUGetProcAddr(device, entries[i])) {
      fprintf(stderr, "sampler feedback entry unavailable: %s\n", entries[i]);
      GPUDestroyDevice(device);
      return 0;
    }
  }

  ok = gpu_test_sampler_feedback_mode(device,
                                      GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT) &&
       gpu_test_sampler_feedback_mode(
         device,
         GPU_SAMPLER_FEEDBACK_MIP_REGION_USED_EXT
       );
  GPUDestroyDevice(device);
  return ok;
}
