#include "test.h"

static int
gpu_test_sampler_feedback_mode(GPUDevice                  *device,
                               GPUSamplerFeedbackModeEXT   mode) {
  GPUTextureCreateInfo                 textureInfo = {0};
  GPUSamplerFeedbackMapCreateInfoEXT   mapInfo     = {0};
  GPUSamplerFeedbackDecodeInfoEXT      decodeInfo  = {0};
  GPUQueueSubmitInfo                   submitInfo  = {0};
  GPUQueue                            *queue;
  GPUTexture                          *target;
  GPUTexture                          *decoded;
  GPUSamplerFeedbackMapEXT            *map;
  GPUCommandBuffer                    *cmdb;
  GPUFence                            *fence;
  int                                  ok;

  queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  target  = NULL;
  decoded = NULL;
  map     = NULL;
  cmdb    = NULL;
  fence   = NULL;
  ok      = queue != NULL;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "sampler-feedback-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 64u;
  textureInfo.height           = 64u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 4u;
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
  mapInfo.mipRegionWidth   = 4u;
  mapInfo.mipRegionHeight  = 4u;
  ok = ok && GPUCreateSamplerFeedbackMapEXT(device, &mapInfo, &map) == GPU_OK &&
       map &&
       GPUGetSamplerFeedbackDecodeInfoEXT(map, &decodeInfo) == GPU_OK &&
       decodeInfo.format == GPU_FORMAT_R8_UINT &&
       decodeInfo.width == 16u && decodeInfo.height == 16u &&
       decodeInfo.arrayLayerCount == 1u &&
       decodeInfo.mipLevelCount ==
         (mode == GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT ? 1u : 4u);

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

  if (!ok || GPUAcquireCommandBuffer(queue,
                                     "sampler-feedback",
                                     &cmdb) != GPU_OK ||
      !cmdb || GPUClearSamplerFeedbackEXT(cmdb, map) != GPU_OK ||
      GPUDecodeSamplerFeedbackEXT(cmdb, map, decoded) != GPU_OK ||
      GPUEncodeSamplerFeedbackEXT(cmdb, decoded, map) != GPU_OK ||
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

cleanup:
  GPUDestroyFence(fence);
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
