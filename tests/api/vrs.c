#include "test.h"

static int
gpu_vrsCapabilitiesValid(const GPUVRSCapabilitiesEXT *caps) {
  const GPUVRSModeFlagsEXT knownModes =
    GPU_VRS_DRAW_RATE_BIT_EXT |
    GPU_VRS_ATTACHMENT_BIT_EXT |
    GPU_VRS_RATE_MAP_BIT_EXT;
  const GPUShadingRateFlagsEXT knownRates =
    GPU_SHADING_RATE_1X1_BIT_EXT |
    GPU_SHADING_RATE_1X2_BIT_EXT |
    GPU_SHADING_RATE_2X1_BIT_EXT |
    GPU_SHADING_RATE_2X2_BIT_EXT |
    GPU_SHADING_RATE_2X4_BIT_EXT |
    GPU_SHADING_RATE_4X2_BIT_EXT |
    GPU_SHADING_RATE_4X4_BIT_EXT;
  const GPUShadingRateCombinerFlagsEXT knownCombiners =
    GPU_SHADING_RATE_COMBINER_KEEP_BIT_EXT |
    GPU_SHADING_RATE_COMBINER_REPLACE_BIT_EXT |
    GPU_SHADING_RATE_COMBINER_MIN_BIT_EXT |
    GPU_SHADING_RATE_COMBINER_MAX_BIT_EXT;

  if (!caps || caps->modes == 0u || (caps->modes & ~knownModes) != 0u ||
      (caps->rates & ~knownRates) != 0u ||
      (caps->combiners & ~knownCombiners) != 0u) {
    return 0;
  }
  if ((caps->modes & (GPU_VRS_DRAW_RATE_BIT_EXT |
                      GPU_VRS_ATTACHMENT_BIT_EXT)) != 0u &&
      (((caps->rates & GPU_SHADING_RATE_1X1_BIT_EXT) == 0u) ||
       ((caps->combiners & GPU_SHADING_RATE_COMBINER_KEEP_BIT_EXT) == 0u))) {
    return 0;
  }
  if ((caps->modes & GPU_VRS_ATTACHMENT_BIT_EXT) != 0u &&
      (caps->combiners & GPU_SHADING_RATE_COMBINER_REPLACE_BIT_EXT) == 0u) {
    return 0;
  }
  if ((caps->modes & GPU_VRS_ATTACHMENT_BIT_EXT) != 0u &&
      (caps->minAttachmentTexelSize.width == 0u ||
       caps->minAttachmentTexelSize.height == 0u ||
       caps->maxAttachmentTexelSize.width <
         caps->minAttachmentTexelSize.width ||
       caps->maxAttachmentTexelSize.height <
         caps->minAttachmentTexelSize.height)) {
    return 0;
  }
  if ((caps->modes & GPU_VRS_RATE_MAP_BIT_EXT) != 0u &&
      caps->maxRateMapLayers == 0u) {
    return 0;
  }
  return 1;
}

int
gpu_test_vrs(GPUAdapter *adapter, GPUDevice *defaultDevice) {
  const float                           horizontal[2] = {1.0f, 0.5f};
  const float                           vertical[2]   = {1.0f, 0.5f};
  GPURasterizationRateLayerEXT          layer         = {0};
  GPURasterizationRateMapCreateInfoEXT  mapInfo       = {0};
  GPUDeviceCreateInfo                   deviceInfo    = {0};
  GPUVRSCapabilitiesEXT                 caps          = {0};
  GPURasterizationRateMapEXT           *map           = NULL;
  GPUDevice                            *device        = NULL;
  GPUExtent2D                           physicalSize  = {0};
  GPUFeature                            feature       =
    GPU_FEATURE_VARIABLE_RATE_SHADING;
  GPUResult                             result;
  int                                   supported;
  int                                   ok = 0;

  if (!adapter || !defaultDevice ||
      GPU_SHADING_RATE_1X1_EXT != 0x0 ||
      GPU_SHADING_RATE_1X2_EXT != 0x1 ||
      GPU_SHADING_RATE_2X1_EXT != 0x4 ||
      GPU_SHADING_RATE_2X2_EXT != 0x5 ||
      GPU_SHADING_RATE_2X4_EXT != 0x6 ||
      GPU_SHADING_RATE_4X2_EXT != 0x9 ||
      GPU_SHADING_RATE_4X4_EXT != 0xA) {
    fprintf(stderr, "VRS attachment encoding is invalid\n");
    return 0;
  }
  if (GPUGetVRSCapabilitiesEXT(NULL, &caps) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetVRSCapabilitiesEXT(adapter, NULL) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetRasterizationRateMapPhysicalSizeEXT(NULL, 0u, &physicalSize) !=
        GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "VRS query accepted invalid arguments\n");
    return 0;
  }

  supported = GPUIsFeatureSupported(
    adapter,
    GPU_FEATURE_VARIABLE_RATE_SHADING
  );
  result = GPUGetVRSCapabilitiesEXT(adapter, &caps);
  if ((!supported && (result != GPU_ERROR_UNSUPPORTED || caps.modes != 0u)) ||
      (supported && (result != GPU_OK || !gpu_vrsCapabilitiesValid(&caps)))) {
    fprintf(stderr, "VRS feature and capabilities disagree\n");
    return 0;
  }
  if (GPUGetProcAddr(defaultDevice, "GPUSetFragmentShadingRateEXT")) {
    fprintf(stderr, "VRS entry point resolved without feature enablement\n");
    return 0;
  }
  if (!supported) {
    return 1;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.featureCount = 1u;
  deviceInfo.required.pFeatures    = &feature;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, feature) ||
      !GPUGetProcAddr(device, "GPUSetFragmentShadingRateEXT")) {
    fprintf(stderr, "VRS feature enablement failed\n");
    goto cleanup;
  }

  layer.pHorizontal     = horizontal;
  layer.pVertical       = vertical;
  layer.horizontalCount = (uint32_t)GPU_ARRAY_LEN(horizontal);
  layer.verticalCount   = (uint32_t)GPU_ARRAY_LEN(vertical);
  mapInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_RASTERIZATION_RATE_MAP_CREATE_INFO_EXT;
  mapInfo.chain.structSize = sizeof(mapInfo);
  mapInfo.label            = "api-vrs-rate-map";
  mapInfo.pLayers          = &layer;
  mapInfo.screenSize       = (GPUExtent2D){64u, 64u};
  mapInfo.layerCount       = 1u;

  result = GPUCreateRasterizationRateMapEXT(device, &mapInfo, &map);
  if ((caps.modes & GPU_VRS_RATE_MAP_BIT_EXT) == 0u) {
    if (result != GPU_ERROR_UNSUPPORTED || map) {
      fprintf(stderr, "VRS rate map accepted by a different native mode\n");
      goto cleanup;
    }
    ok = 1;
    goto cleanup;
  }
  if (result != GPU_OK || !map ||
      GPUGetRasterizationRateMapPhysicalSizeEXT(map, 0u, &physicalSize) !=
        GPU_OK ||
      physicalSize.width == 0u || physicalSize.height == 0u ||
      GPUGetRasterizationRateMapPhysicalSizeEXT(map, 1u, &physicalSize) !=
        GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "VRS rate map validation failed\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  GPUDestroyRasterizationRateMapEXT(map);
  GPUDestroyDevice(device);
  return ok;
}
