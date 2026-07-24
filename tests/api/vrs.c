#include "test.h"

#include <math.h>

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

static GPUShadingRateEXT
gpu_vrsExecutionRate(const GPUVRSCapabilitiesEXT *caps) {
  static const GPUShadingRateEXT rates[] = {
    GPU_SHADING_RATE_2X2_EXT,
    GPU_SHADING_RATE_1X2_EXT,
    GPU_SHADING_RATE_2X1_EXT,
    GPU_SHADING_RATE_2X4_EXT,
    GPU_SHADING_RATE_4X2_EXT,
    GPU_SHADING_RATE_4X4_EXT,
    GPU_SHADING_RATE_1X1_EXT
  };

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(rates); i++) {
    if ((caps->rates & (1u << rates[i])) != 0u) {
      return rates[i];
    }
  }
  return GPU_SHADING_RATE_1X1_EXT;
}

static int
gpu_vrsPixelsMatch(const uint8_t *pixels, uint32_t width) {
  static const uint32_t points[][2] = {
    {16u, 16u},
    {48u, 16u},
    {16u, 48u},
    {48u, 48u}
  };

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(points); i++) {
    const uint8_t *pixel;

    pixel = &pixels[(points[i][1] * width + points[i][0]) * 4u];
    if (pixel[0] < 250u || pixel[1] > 4u ||
        pixel[2] > 4u || pixel[3] < 250u) {
      return 0;
    }
  }
  return 1;
}

static int
gpu_vrsExecutionSmoke(GPUDevice                    *device,
                      const GPUVRSCapabilitiesEXT *caps,
                      const char                  *bytecodePath) {
  enum {
    VRS_TARGET_WIDTH  = 64u,
    VRS_TARGET_HEIGHT = 64u,
    VRS_TARGET_BYTES  = VRS_TARGET_WIDTH * VRS_TARGET_HEIGHT * 4u
  };
  static const float vertices[] = {
    -1.0f, -1.0f, 0.0f, 1.0f,
     3.0f, -1.0f, 0.0f, 1.0f,
    -1.0f,  3.0f, 0.0f, 1.0f
  };
  GPUQueue                     *queue;
  GPUShaderLibrary             *library;
  GPUShaderLayout              *shaderLayout;
  GPURenderPipeline            *pipeline;
  GPUBuffer                    *vertexBuffer;
  GPUBuffer                    *readbackBuffer;
  GPUTexture                   *target;
  GPUTextureView               *targetView;
  GPUTexture                   *rateTexture;
  GPUTextureView               *rateView;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *renderPass;
  GPUCopyPassEncoder           *copyPass;
  GPUFence                     *fence;
  GPUColorTargetState           colorTarget    = {0};
  GPUVertexAttribute            attribute      = {0};
  GPUVertexBufferLayout         vertexLayout   = {0};
  GPURenderPipelineCreateInfo   pipelineInfo   = {0};
  GPUBufferCreateInfo           bufferInfo     = {0};
  GPUTextureCreateInfo          textureInfo    = {0};
  GPUTextureViewCreateInfo      viewInfo       = {0};
  GPUTextureWriteRegion         writeRegion    = {0};
  GPUShadingRateAttachmentEXT   shadingRate    = {0};
  GPURenderPassColorAttachment  color          = {0};
  GPURenderPassCreateInfo       passInfo       = {0};
  GPUBufferBinding              vertexBinding  = {0};
  GPUViewport                   viewport       = {0};
  GPUScissorRect                scissor        = {0};
  GPUTextureBarrier             textureBarrier = {0};
  GPUBarrierBatch               barrierBatch   = {0};
  GPUBufferTextureCopyRegion    copyRegion     = {0};
  GPUQueueSubmitInfo            submitInfo     = {0};
  uint8_t                       rates[VRS_TARGET_WIDTH * VRS_TARGET_HEIGHT] = {0};
  uint8_t                       pixels[VRS_TARGET_BYTES] = {0};
  void                         *bytecode;
  uint64_t                      bytecodeSize;
  uint32_t                      rateWidth;
  uint32_t                      rateHeight;
  int                           ok;

  if ((caps->modes & (GPU_VRS_DRAW_RATE_BIT_EXT |
                      GPU_VRS_ATTACHMENT_BIT_EXT)) == 0u) {
    return 1;
  }

  queue          = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  library        = NULL;
  shaderLayout   = NULL;
  pipeline       = NULL;
  vertexBuffer   = NULL;
  readbackBuffer = NULL;
  target         = NULL;
  targetView     = NULL;
  rateTexture    = NULL;
  rateView       = NULL;
  cmdb           = NULL;
  renderPass     = NULL;
  copyPass       = NULL;
  fence          = NULL;
  bytecodeSize   = 0u;
  bytecode       = gpu_test_read_file(bytecodePath, &bytecodeSize);
  rateWidth      = 0u;
  rateHeight     = 0u;
  ok = queue && bytecode &&
       GPUCreateShaderLibraryFromUSL(device,
                                     bytecode,
                                     bytecodeSize,
                                     &library) == GPU_OK &&
       library &&
       GPUCreateShaderLayout(device, library, &shaderLayout) == GPU_OK &&
       shaderLayout && shaderLayout->pipelineLayout;

  colorTarget.format          = GPU_FORMAT_RGBA8_UNORM;
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;
  attribute.shaderLocation    = 0u;
  attribute.format            = GPU_VERTEX_FORMAT_FLOAT32X4;
  vertexLayout.strideBytes    = 4u * sizeof(float);
  vertexLayout.stepMode       = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount = 1u;
  vertexLayout.pAttributes    = &attribute;
  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "api-vrs-pipeline";
  pipelineInfo.layout           = shaderLayout
                                ? shaderLayout->pipelineLayout
                                : NULL;
  pipelineInfo.library          = library;
  pipelineInfo.vertexEntry      = "coordinate_position_vs";
  pipelineInfo.fragmentEntry    = "coordinate_red_fs";
  pipelineInfo.vertex.bufferLayoutCount = 1u;
  pipelineInfo.vertex.pBufferLayouts     = &vertexLayout;
  pipelineInfo.colorTargetCount          = 1u;
  pipelineInfo.pColorTargets             = &colorTarget;
  pipelineInfo.primitiveTopology         =
    GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode                  = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace                 = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount   = 1u;
  pipelineInfo.multisample.sampleMask    = UINT32_MAX;
  ok = ok && GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) ==
               GPU_OK &&
       pipeline;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-vrs-vertices";
  bufferInfo.sizeBytes        = sizeof(vertices);
  bufferInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_COPY_DST;
  ok = ok && GPUCreateBuffer(device, &bufferInfo, &vertexBuffer) == GPU_OK &&
       vertexBuffer &&
       GPUQueueWriteBuffer(queue,
                           vertexBuffer,
                           0u,
                           vertices,
                           sizeof(vertices)) == GPU_OK;

  bufferInfo.label     = "api-vrs-readback";
  bufferInfo.sizeBytes = sizeof(pixels);
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_DST |
                         GPU_BUFFER_USAGE_COPY_SRC;
  ok = ok && GPUCreateBuffer(device, &bufferInfo, &readbackBuffer) == GPU_OK &&
       readbackBuffer;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "api-vrs-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = VRS_TARGET_WIDTH;
  textureInfo.height           = VRS_TARGET_HEIGHT;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET |
                                 GPU_TEXTURE_USAGE_COPY_SRC;
  ok = ok && GPUCreateTexture(device, &textureInfo, &target) == GPU_OK &&
       target;

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "api-vrs-target-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  ok = ok && GPUCreateTextureView(target, &viewInfo, &targetView) == GPU_OK &&
       targetView;

  if (ok && (caps->modes & GPU_VRS_ATTACHMENT_BIT_EXT) != 0u) {
    shadingRate.chain.sType      =
      GPU_STRUCTURE_TYPE_SHADING_RATE_ATTACHMENT_EXT;
    shadingRate.chain.structSize = sizeof(shadingRate);
    shadingRate.texelSize        = caps->minAttachmentTexelSize;
    rateWidth = (VRS_TARGET_WIDTH - 1u) / shadingRate.texelSize.width + 1u;
    rateHeight = (VRS_TARGET_HEIGHT - 1u) / shadingRate.texelSize.height + 1u;

    textureInfo.label         = "api-vrs-rate-image";
    textureInfo.format        = GPU_FORMAT_R8_UINT;
    textureInfo.width         = rateWidth;
    textureInfo.height        = rateHeight;
    textureInfo.usage         = GPU_TEXTURE_USAGE_COPY_DST |
                                GPU_TEXTURE_USAGE_SHADING_RATE_ATTACHMENT_EXT;
    ok = GPUCreateTexture(device, &textureInfo, &rateTexture) == GPU_OK &&
         rateTexture;

    writeRegion.width        = rateWidth;
    writeRegion.height       = rateHeight;
    writeRegion.depth        = 1u;
    writeRegion.layerCount   = 1u;
    writeRegion.bytesPerRow  = rateWidth;
    writeRegion.rowsPerImage = rateHeight;
    ok = ok && GPUQueueWriteTexture(queue,
                                    rateTexture,
                                    &writeRegion,
                                    rates,
                                    (uint64_t)rateWidth * rateHeight) == GPU_OK;

    viewInfo.label  = "api-vrs-rate-image-view";
    viewInfo.format = GPU_FORMAT_R8_UINT;
    ok = ok && GPUCreateTextureView(rateTexture,
                                    &viewInfo,
                                    &rateView) == GPU_OK &&
         rateView;
    shadingRate.view = rateView;
  }

  if (!ok || GPUAcquireCommandBuffer(queue, "api-vrs-execution", &cmdb) !=
               GPU_OK ||
      !cmdb) {
    goto cleanup;
  }

  color.view                    = targetView;
  color.loadOp                  = GPU_LOAD_OP_CLEAR;
  color.storeOp                 = GPU_STORE_OP_STORE;
  color.clearColor.float32[3]   = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.chain.pNext          = rateView ? &shadingRate.chain : NULL;
  passInfo.label                = "api-vrs-execution";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    ok = 0;
    goto cleanup;
  }

  if ((caps->modes & GPU_VRS_DRAW_RATE_BIT_EXT) != 0u) {
    GPUSetFragmentShadingRateEXT(
      renderPass,
      gpu_vrsExecutionRate(caps),
      GPU_SHADING_RATE_COMBINER_KEEP_EXT,
      rateView ? GPU_SHADING_RATE_COMBINER_REPLACE_EXT
               : GPU_SHADING_RATE_COMBINER_KEEP_EXT
    );
  }
  viewport.width    = (float)VRS_TARGET_WIDTH;
  viewport.height   = (float)VRS_TARGET_HEIGHT;
  viewport.maxDepth = 1.0f;
  scissor.width     = VRS_TARGET_WIDTH;
  scissor.height    = VRS_TARGET_HEIGHT;
  vertexBinding.buffer  = vertexBuffer;
  GPUBindRenderPipeline(renderPass, pipeline);
  GPUBindVertexBuffers(renderPass, 0u, 1u, &vertexBinding);
  GPUSetViewport(renderPass, &viewport);
  GPUSetScissor(renderPass, &scissor);
  GPUDraw(renderPass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  textureBarrier.texture    = target;
  textureBarrier.srcAccess  = GPU_ACCESS_COLOR_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_TRANSFER_READ;
  textureBarrier.mipCount   = 1u;
  textureBarrier.layerCount = 1u;
  barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 1u;
  barrierBatch.pTextureBarriers    = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "api-vrs-readback");
  if (!copyPass) {
    ok = 0;
    goto cleanup;
  }
  copyRegion.bytesPerRow        = VRS_TARGET_WIDTH * 4u;
  copyRegion.rowsPerImage       = VRS_TARGET_HEIGHT;
  copyRegion.texture.width      = VRS_TARGET_WIDTH;
  copyRegion.texture.height     = VRS_TARGET_HEIGHT;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass, target, readbackBuffer, &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    ok = 0;
    goto cleanup;
  }
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  ok = GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
       GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
  cmdb = NULL;
  ok = ok && GPUQueueReadBuffer(queue,
                                readbackBuffer,
                                0u,
                                pixels,
                                sizeof(pixels)) == GPU_OK &&
       gpu_vrsPixelsMatch(pixels, VRS_TARGET_WIDTH);

cleanup:
  if (copyPass) GPUEndCopyPass(copyPass);
  if (renderPass) GPUEndRenderPass(renderPass);
  free(bytecode);
  GPUDestroyFence(fence);
  GPUDestroyTextureView(rateView);
  GPUDestroyTexture(rateTexture);
  GPUDestroyTextureView(targetView);
  GPUDestroyTexture(target);
  GPUDestroyBuffer(readbackBuffer);
  GPUDestroyBuffer(vertexBuffer);
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  return ok;
}

int
gpu_test_vrs(GPUAdapter *adapter,
             GPUDevice  *defaultDevice,
             const char *bytecodePath) {
  GPURasterizationRateMapEXT             *map             = NULL;
  GPUBuffer                              *parameterBuffer = NULL;
  GPUDevice                              *device          = NULL;
  const float                             horizontal[2]   = {1.0f, 0.5f};
  const float                             vertical[2]     = {1.0f, 0.5f};
  GPURasterizationRateLayerEXT            layer           = {0};
  GPURasterizationRateMapCreateInfoEXT    mapInfo         = {0};
  GPURasterizationRateMapParameterInfoEXT parameterInfo   = {0};
  GPUBufferCreateInfo                     bufferInfo      = {0};
  GPUDeviceCreateInfo                     deviceInfo      = {0};
  GPUVRSCapabilitiesEXT                   caps            = {0};
  GPUExtent2D                             physicalSize    = {0};
  GPUCoordinate2D                         physical        = {0};
  GPUCoordinate2D                         roundTrip       = {0};
  GPUCoordinate2D                         screen          = {32.0f, 32.0f};
  GPUFeature                              feature         =
    GPU_FEATURE_VARIABLE_RATE_SHADING;
  GPUResult                               result;
  int                                     supported;
  int                                     ok = 0;

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
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUMapRasterizationRateScreenToPhysicalEXT(NULL,
                                                  0u,
                                                  screen,
                                                  &physical) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUMapRasterizationRatePhysicalToScreenEXT(NULL,
                                                  0u,
                                                  physical,
                                                  &roundTrip) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetRasterizationRateMapParameterInfoEXT(NULL, &parameterInfo) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUCopyRasterizationRateMapParametersEXT(NULL, NULL, 0u) !=
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
    puts("VRS execution skipped: unsupported adapter");
    return 1;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.featureCount = 1u;
  deviceInfo.required.pFeatures    = &feature;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, feature) ||
      !GPUGetProcAddr(device, "GPUSetFragmentShadingRateEXT") ||
      !GPUGetProcAddr(device,
                      "GPUMapRasterizationRateScreenToPhysicalEXT") ||
      !GPUGetProcAddr(device,
                      "GPUMapRasterizationRatePhysicalToScreenEXT") ||
      !GPUGetProcAddr(device,
                      "GPUGetRasterizationRateMapParameterInfoEXT") ||
      !GPUGetProcAddr(device,
                      "GPUCopyRasterizationRateMapParametersEXT")) {
    fprintf(stderr, "VRS feature enablement failed\n");
    goto cleanup;
  }

  if (!gpu_vrsExecutionSmoke(device, &caps, bytecodePath)) {
    fprintf(stderr, "VRS execution smoke failed\n");
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

  if (GPUMapRasterizationRateScreenToPhysicalEXT(map,
                                                  0u,
                                                  screen,
                                                  &physical) != GPU_OK ||
      physical.x < 0.0f || physical.y < 0.0f ||
      physical.x > screen.x || physical.y > screen.y ||
      GPUMapRasterizationRatePhysicalToScreenEXT(map,
                                                  0u,
                                                  physical,
                                                  &roundTrip) != GPU_OK ||
      fabsf(roundTrip.x - screen.x) > 1.0f ||
      fabsf(roundTrip.y - screen.y) > 1.0f ||
      GPUMapRasterizationRateScreenToPhysicalEXT(
        map,
        0u,
        (GPUCoordinate2D){65.0f, 0.0f},
        &physical
      ) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "VRS rate map coordinate mapping failed\n");
    goto cleanup;
  }

  if (GPUGetRasterizationRateMapParameterInfoEXT(map, &parameterInfo) !=
        GPU_OK ||
      parameterInfo.sizeBytes == 0u || parameterInfo.alignment == 0u ||
      (parameterInfo.alignment & (parameterInfo.alignment - 1u)) != 0u) {
    fprintf(stderr, "VRS rate map parameter info failed\n");
    goto cleanup;
  }
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-vrs-rate-map-parameters";
  bufferInfo.sizeBytes        = parameterInfo.sizeBytes;
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM;
  if (GPUCreateBuffer(device, &bufferInfo, &parameterBuffer) != GPU_OK ||
      !parameterBuffer ||
      GPUCopyRasterizationRateMapParametersEXT(map,
                                                parameterBuffer,
                                                0u) != GPU_OK ||
      GPUCopyRasterizationRateMapParametersEXT(map,
                                                parameterBuffer,
                                                parameterInfo.alignment) !=
        GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "VRS rate map parameter copy failed\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  GPUDestroyBuffer(parameterBuffer);
  GPUDestroyRasterizationRateMapEXT(map);
  GPUDestroyDevice(device);
  return ok;
}
