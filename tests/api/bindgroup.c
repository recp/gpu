#include "test.h"
#include <gpu/api/gpudef.h>
#include "../../src/api/device_internal.h"
#include "../../src/api/texture_internal.h"

typedef struct GPUDescriptorHookCounts {
  uint32_t createLayout;
  uint32_t destroyLayout;
  uint32_t createPipeline;
  uint32_t destroyPipeline;
  uint32_t createGroup;
  uint32_t destroyGroup;
  uint32_t bindRender;
  uint32_t bindCompute;
} GPUDescriptorHookCounts;

static GPUDescriptorHookCounts descriptorHookCounts;

static GPUResult
test_createBindGroupLayout(GPUDevice *device, GPUBindGroupLayout *layout) {
  if (!device || !layout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  descriptorHookCounts.createLayout++;
  return GPU_OK;
}

static void
test_destroyBindGroupLayout(GPUBindGroupLayout *layout) {
  if (layout) {
    descriptorHookCounts.destroyLayout++;
  }
}

static GPUResult
test_createPipelineLayout(GPUDevice *device, GPUPipelineLayout *layout) {
  if (!device || !layout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  descriptorHookCounts.createPipeline++;
  return GPU_OK;
}

static void
test_destroyPipelineLayout(GPUPipelineLayout *layout) {
  if (layout) {
    descriptorHookCounts.destroyPipeline++;
  }
}

static GPUResult
test_createBindGroup(GPUDevice *device, GPUBindGroup *group) {
  if (!device || !group) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  descriptorHookCounts.createGroup++;
  return GPU_OK;
}

static void
test_destroyBindGroup(GPUBindGroup *group) {
  if (group) {
    descriptorHookCounts.destroyGroup++;
  }
}

static bool
test_bindRenderGroup(GPURenderPassEncoder *pass,
                     GPUPipelineLayout    *pipelineLayout,
                     uint32_t              groupIndex,
                     GPUBindGroup         *group,
                     uint32_t              dynamicOffsetCount,
                     const uint32_t       *dynamicOffsets) {
  (void)dynamicOffsets;
  if (!pass || !pipelineLayout || !group || groupIndex != 0u ||
      dynamicOffsetCount != 0u) {
    return false;
  }
  descriptorHookCounts.bindRender++;
  return true;
}

static bool
test_bindComputeGroup(GPUComputePassEncoder *pass,
                      GPUPipelineLayout     *pipelineLayout,
                      uint32_t               groupIndex,
                      GPUBindGroup          *group,
                      uint32_t               dynamicOffsetCount,
                      const uint32_t        *dynamicOffsets) {
  (void)dynamicOffsets;
  if (!pass || !pipelineLayout || !group || groupIndex != 0u ||
      dynamicOffsetCount != 0u) {
    return false;
  }
  descriptorHookCounts.bindCompute++;
  return true;
}

static int
check_backend_descriptor_hooks(GPUDevice *device) {
  GPUApiDescriptor hooks = {0};
  GPUApiDescriptor saved;
  GPUBindGroupLayoutCreateInfo layoutInfo = {0};
  GPUPipelineLayoutCreateInfo pipelineInfo = {0};
  GPUBindGroupCreateInfo groupInfo = {0};
  GPUBindGroupLayout *layouts[1];
  GPUBindGroupLayout *layout = NULL;
  GPUPipelineLayout *pipelineLayout = NULL;
  GPUBindGroup *group = NULL;
  GPURenderPassEncoder renderPass = {0};
  GPUComputePassEncoder computePass = {0};
  GPUApi *api;
  int ok;

  api = gpuDeviceApi(device);
  if (!api) {
    return 0;
  }

  hooks.createBindGroupLayout = test_createBindGroupLayout;
  hooks.destroyBindGroupLayout = test_destroyBindGroupLayout;
  hooks.createPipelineLayout = test_createPipelineLayout;
  hooks.destroyPipelineLayout = test_destroyPipelineLayout;
  hooks.createBindGroup = test_createBindGroup;
  hooks.destroyBindGroup = test_destroyBindGroup;
  hooks.bindRenderGroup = test_bindRenderGroup;
  hooks.bindComputeGroup = test_bindComputeGroup;

  saved = api->descriptor;
  api->descriptor = hooks;
  memset(&descriptorHookCounts, 0, sizeof(descriptorHookCounts));

  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK || !layout) {
    goto cleanup;
  }

  layouts[0] = layout;
  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.bindGroupLayoutCount = 1u;
  pipelineInfo.ppBindGroupLayouts = layouts;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    goto cleanup;
  }

  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout = layout;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    goto cleanup;
  }

  renderPass._pipelineLayout = pipelineLayout;
  computePass._pipelineLayout = pipelineLayout;
  GPUBindRenderGroup(&renderPass, 0u, group, 0u, NULL);
  GPUBindComputeGroup(&computePass, 0u, group, 0u, NULL);

cleanup:
  GPUDestroyBindGroup(group);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(layout);
  api->descriptor = saved;

  ok = descriptorHookCounts.createLayout == 1u &&
       descriptorHookCounts.destroyLayout == 1u &&
       descriptorHookCounts.createPipeline == 1u &&
       descriptorHookCounts.destroyPipeline == 1u &&
       descriptorHookCounts.createGroup == 1u &&
       descriptorHookCounts.destroyGroup == 1u &&
       descriptorHookCounts.bindRender == 1u &&
       descriptorHookCounts.bindCompute == 1u;
  if (!ok) {
    fprintf(stderr, "backend descriptor hooks were not routed consistently\n");
  }
  return ok;
}

static GPUSamplerDesc
valid_sampler_desc(void) {
  GPUSamplerDesc desc;

  memset(&desc, 0, sizeof(desc));
  desc.minFilter = GPU_FILTER_LINEAR;
  desc.magFilter = GPU_FILTER_LINEAR;
  desc.mipFilter = GPU_MIP_FILTER_LINEAR;
  desc.addressU = GPU_ADDRESS_MODE_REPEAT;
  desc.addressV = GPU_ADDRESS_MODE_REPEAT;
  desc.addressW = GPU_ADDRESS_MODE_REPEAT;
  return desc;
}

static int
check_pipeline_layout_bind_validation(GPUDevice *device,
                                      const GPUBindGroupLayoutCreateInfo *layoutInfo,
                                      GPUBindGroupLayout *layout,
                                      GPUPipelineLayout *pipelineLayout) {
  GPUApiDescriptor saved;
  GPUApi *api;
  GPUBindGroupCreateInfo groupInfo = {0};
  GPUBindGroupLayoutCreateInfo secondLayoutInfo = {0};
  GPUPipelineLayoutCreateInfo otherPipelineInfo = {0};
  GPUPipelineLayoutCreateInfo twoGroupPipelineInfo = {0};
  GPUBindGroupLayoutEntry secondEntry = {0};
  GPUBindGroupLayout *otherLayout = NULL;
  GPUBindGroupLayout *otherLayouts[1];
  GPUBindGroupLayout *twoGroupLayouts[2];
  GPUPipelineLayout *otherPipelineLayout = NULL;
  GPUPipelineLayout *twoGroupPipelineLayout = NULL;
  GPUBindGroup *group = NULL;
  GPUBindGroup *secondGroup = NULL;
  GPURenderPassEncoder renderPass = {0};
  GPUComputePassEncoder computePass = {0};
  uint32_t dynamicOffset = 0u;

  api = gpuDeviceApi(device);
  if (!api) {
    return 0;
  }
  saved = api->descriptor;
  api->descriptor.bindRenderGroup  = NULL;
  api->descriptor.bindComputeGroup = NULL;

  secondLayoutInfo = *layoutInfo;
  if (!layoutInfo || layoutInfo->entryCount != 1u || !layoutInfo->pEntries) {
    fprintf(stderr, "bind compatibility setup needs one layout entry\n");
    goto fail;
  }
  secondEntry = layoutInfo->pEntries[0];
  secondEntry.binding++;
  secondLayoutInfo.pEntries = &secondEntry;

  if (GPUCreateBindGroupLayout(device, &secondLayoutInfo, &otherLayout) != GPU_OK ||
      !otherLayout) {
    fprintf(stderr, "bind compatibility setup failed to create second layout\n");
    goto fail;
  }

  otherLayouts[0] = otherLayout;
  otherPipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  otherPipelineInfo.chain.structSize = sizeof(otherPipelineInfo);
  otherPipelineInfo.bindGroupLayoutCount = 1u;
  otherPipelineInfo.ppBindGroupLayouts = otherLayouts;
  if (GPUCreatePipelineLayout(device,
                              &otherPipelineInfo,
                              &otherPipelineLayout) != GPU_OK ||
      !otherPipelineLayout) {
    fprintf(stderr, "bind compatibility setup failed to create second pipeline layout\n");
    goto fail;
  }

  twoGroupLayouts[0] = layout;
  twoGroupLayouts[1] = otherLayout;
  twoGroupPipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  twoGroupPipelineInfo.chain.structSize = sizeof(twoGroupPipelineInfo);
  twoGroupPipelineInfo.bindGroupLayoutCount = 2u;
  twoGroupPipelineInfo.ppBindGroupLayouts = twoGroupLayouts;
  if (GPUCreatePipelineLayout(device,
                              &twoGroupPipelineInfo,
                              &twoGroupPipelineLayout) != GPU_OK ||
      !twoGroupPipelineLayout) {
    fprintf(stderr, "bind compatibility setup failed to create two-group layout\n");
    goto fail;
  }

  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout = layout;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "bind compatibility setup failed to create group\n");
    goto fail;
  }

  groupInfo.layout = otherLayout;
  if (GPUCreateBindGroup(device, &groupInfo, &secondGroup) != GPU_OK ||
      !secondGroup) {
    fprintf(stderr, "bind compatibility setup failed to create second group\n");
    goto fail;
  }

  renderPass._pipelineLayout = otherPipelineLayout;
  GPUBindRenderGroup(&renderPass, 0u, group, 0u, NULL);
  if (renderPass._boundGroupLayouts[0]) {
    fprintf(stderr, "render bind accepted group from wrong pipeline layout\n");
    goto fail;
  }

  renderPass._pipelineLayout = pipelineLayout;
  GPUBindRenderGroup(&renderPass, 0u, group, 1u, &dynamicOffset);
  if (renderPass._boundGroupLayouts[0]) {
    fprintf(stderr, "render bind accepted wrong dynamic offset count\n");
    goto fail;
  }
  GPUBindRenderGroup(&renderPass, 0u, group, 0u, NULL);
  if (renderPass._boundGroupLayouts[0] != layout) {
    fprintf(stderr, "render bind rejected compatible pipeline layout\n");
    goto fail;
  }

  memset(&renderPass, 0, sizeof(renderPass));
  renderPass._pipelineLayout = twoGroupPipelineLayout;
  GPUBindRenderGroup(&renderPass, 1u, group, 0u, NULL);
  if (renderPass._boundGroupLayouts[1]) {
    fprintf(stderr, "render bind accepted group 1 with wrong layout\n");
    goto fail;
  }
  GPUBindRenderGroup(&renderPass, 0u, group, 0u, NULL);
  GPUBindRenderGroup(&renderPass, 1u, secondGroup, 0u, NULL);
  if (renderPass._boundGroupLayouts[0] != layout ||
      renderPass._boundGroupLayouts[1] != otherLayout) {
    fprintf(stderr, "render bind rejected compatible multi-group layout\n");
    goto fail;
  }

  computePass._pipelineLayout = otherPipelineLayout;
  GPUBindComputeGroup(&computePass, 0u, group, 0u, NULL);
  if (computePass._boundGroupLayouts[0]) {
    fprintf(stderr, "compute bind accepted group from wrong pipeline layout\n");
    goto fail;
  }

  computePass._pipelineLayout = pipelineLayout;
  GPUBindComputeGroup(&computePass, 0u, group, 0u, NULL);
  if (computePass._boundGroupLayouts[0] != layout) {
    fprintf(stderr, "compute bind rejected compatible pipeline layout\n");
    goto fail;
  }

  memset(&computePass, 0, sizeof(computePass));
  computePass._pipelineLayout = twoGroupPipelineLayout;
  GPUBindComputeGroup(&computePass, 1u, group, 0u, NULL);
  if (computePass._boundGroupLayouts[1]) {
    fprintf(stderr, "compute bind accepted group 1 with wrong layout\n");
    goto fail;
  }
  GPUBindComputeGroup(&computePass, 0u, group, 0u, NULL);
  GPUBindComputeGroup(&computePass, 1u, secondGroup, 0u, NULL);
  if (computePass._boundGroupLayouts[0] != layout ||
      computePass._boundGroupLayouts[1] != otherLayout) {
    fprintf(stderr, "compute bind rejected compatible multi-group layout\n");
    goto fail;
  }

  GPUDestroyBindGroup(secondGroup);
  GPUDestroyBindGroup(group);
  GPUDestroyPipelineLayout(twoGroupPipelineLayout);
  GPUDestroyPipelineLayout(otherPipelineLayout);
  GPUDestroyBindGroupLayout(otherLayout);
  api->descriptor = saved;
  return 1;

fail:
  api->descriptor = saved;
  GPUDestroyBindGroup(secondGroup);
  GPUDestroyBindGroup(group);
  GPUDestroyPipelineLayout(twoGroupPipelineLayout);
  GPUDestroyPipelineLayout(otherPipelineLayout);
  GPUDestroyBindGroupLayout(otherLayout);
  return 0;
}

static int
check_bind_group_layout_validation(GPUDevice *device) {
  unsigned char fakeSamplerStorage;
  GPUBindGroupLayoutEntry entry;
  GPUBindGroupLayoutEntry duplicateEntries[2];
  GPUBindGroupLayoutCreateInfo layoutInfo;
  GPUBindGroupLayout *layout;
  GPUBindGroupEntry runtimeSamplerEntry;
  GPUBindGroupEntry bufferEntry;
  GPUBindGroupEntry textureEntry;
  GPUBindGroupEntry textureGroupEntries[2];
  GPUBindGroupCreateInfo groupInfo;
  GPUBufferCreateInfo bufferInfo;
  GPUTextureCreateInfo textureInfo;
  GPUTextureViewCreateInfo viewInfo;
  GPUBindGroup *group;
  GPUBuffer *uniformBuffer;
  GPUBuffer *storageOnlyBuffer;
  GPUTexture storageTexture;
  GPUTextureView storageView;
  GPUTexture *sampledTexture;
  GPUTexture *dualUseTexture;
  GPUTextureView *sampledView;
  GPUTextureView *dualUseView;
  GPUBindGroupLayoutEntry textureLayoutEntries[2];
  GPUBindGroupLayout *dualTextureLayout;
  GPUPipelineLayoutCreateInfo pipelineInfo;
  GPUBindGroupLayout *layouts[1];
  GPUBindGroupLayout *tooManyLayouts[GPU_ENCODER_MAX_BIND_GROUPS + 1u];
  GPUPipelineLayout *pipelineLayout;

  memset(&entry, 0, sizeof(entry));
  entry.binding = 0u;
  entry.bindingType = GPU_BINDING_SAMPLER;
  entry.visibility = GPU_SHADER_STAGE_FRAGMENT_BIT;
  entry.arrayCount = 1u;
  entry.immutableSampler = true;
  entry.immutableSamplerDesc = valid_sampler_desc();

  memset(&layoutInfo, 0, sizeof(layoutInfo));
  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label = "immutable-sampler-layout";
  layoutInfo.entryCount = 1u;
  layoutInfo.pEntries = &entry;

  if (GPUCreateBindGroupLayout(device, NULL, &layout) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUCreateBindGroupLayout(device, &layoutInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "bind group layout accepted null input\n");
    return 0;
  }

  layout = (GPUBindGroupLayout *)(uintptr_t)1u;
  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_ERROR_INVALID_ARGUMENT ||
      layout != NULL) {
    fprintf(stderr, "bind group layout accepted wrong sType\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layout = (GPUBindGroupLayout *)(uintptr_t)1u;
  layoutInfo.chain.structSize = (uint32_t)(sizeof(layoutInfo) - 1u);
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_ERROR_INVALID_ARGUMENT ||
      layout != NULL) {
    fprintf(stderr, "bind group layout accepted short structSize\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  layoutInfo.chain.structSize = sizeof(layoutInfo);
  entry.bindingType = GPU_BINDING_UNIFORM_BUFFER;
  layout = (GPUBindGroupLayout *)(uintptr_t)1u;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_ERROR_INVALID_ARGUMENT ||
      layout != NULL) {
    fprintf(stderr, "bind group layout accepted immutable non-sampler\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  entry.bindingType = GPU_BINDING_SAMPLER;
  entry.immutableSamplerDesc.mipFilter = (GPUMipFilter)99;
  layout = (GPUBindGroupLayout *)(uintptr_t)1u;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_ERROR_INVALID_ARGUMENT ||
      layout != NULL) {
    fprintf(stderr, "bind group layout accepted invalid immutable sampler desc\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  entry.immutableSamplerDesc = valid_sampler_desc();
  entry.hasDynamicOffset = true;
  layout = (GPUBindGroupLayout *)(uintptr_t)1u;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_ERROR_INVALID_ARGUMENT ||
      layout != NULL) {
    fprintf(stderr, "bind group layout accepted dynamic offset on sampler\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  entry.immutableSampler = false;
  entry.bindingType = GPU_BINDING_UNIFORM_BUFFER;
  entry.hasDynamicOffset = false;
  duplicateEntries[0] = entry;
  duplicateEntries[0].binding = 3u;
  duplicateEntries[0].visibility = GPU_SHADER_STAGE_VERTEX_BIT;
  duplicateEntries[1] = entry;
  duplicateEntries[1].binding = 3u;
  duplicateEntries[1].bindingType = GPU_BINDING_SAMPLER;
  duplicateEntries[1].visibility = GPU_SHADER_STAGE_FRAGMENT_BIT;
  layoutInfo.entryCount = 2u;
  layoutInfo.pEntries = duplicateEntries;
  layout = (GPUBindGroupLayout *)(uintptr_t)1u;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_ERROR_INVALID_ARGUMENT ||
      layout != NULL) {
    fprintf(stderr, "bind group layout accepted duplicate logical binding\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  entry.hasDynamicOffset = true;
  layoutInfo.entryCount = 1u;
  layoutInfo.pEntries = &entry;
  layout = NULL;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK || !layout) {
    fprintf(stderr, "bind group layout rejected valid dynamic buffer entry\n");
    return 0;
  }

  memset(&bufferInfo, 0, sizeof(bufferInfo));
  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes = 32u;
  bufferInfo.usage = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  uniformBuffer = NULL;
  if (GPUCreateBuffer(device, &bufferInfo, &uniformBuffer) != GPU_OK || !uniformBuffer) {
    fprintf(stderr, "bind group buffer setup failed\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  bufferInfo.usage = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_DST;
  storageOnlyBuffer = NULL;
  if (GPUCreateBuffer(device, &bufferInfo, &storageOnlyBuffer) != GPU_OK ||
      !storageOnlyBuffer) {
    fprintf(stderr, "bind group storage buffer setup failed\n");
    GPUDestroyBuffer(uniformBuffer);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  memset(&bufferEntry, 0, sizeof(bufferEntry));
  bufferEntry.binding = 0u;
  bufferEntry.bindingType = GPU_BINDING_UNIFORM_BUFFER;
  bufferEntry.buffer.buffer = uniformBuffer;
  bufferEntry.buffer.offset = 0u;
  bufferEntry.buffer.size = 16u;

  memset(&groupInfo, 0, sizeof(groupInfo));
  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label = "uniform-buffer-group";
  groupInfo.layout = layout;
  groupInfo.entryCount = 1u;
  groupInfo.pEntries = &bufferEntry;
  if (GPUCreateBindGroup(device, NULL, &group) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUCreateBindGroup(device, &groupInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "bind group accepted null input\n");
    GPUDestroyBuffer(storageOnlyBuffer);
    GPUDestroyBuffer(uniformBuffer);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  group = (GPUBindGroup *)(uintptr_t)1u;
  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_ERROR_INVALID_ARGUMENT ||
      group != NULL) {
    fprintf(stderr, "bind group accepted wrong sType\n");
    GPUDestroyBindGroup(group);
    GPUDestroyBuffer(storageOnlyBuffer);
    GPUDestroyBuffer(uniformBuffer);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  group = NULL;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "bind group rejected valid uniform buffer binding\n");
    GPUDestroyBuffer(storageOnlyBuffer);
    GPUDestroyBuffer(uniformBuffer);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  GPUDestroyBindGroup(group);

  bufferEntry.buffer.offset = 24u;
  group = (GPUBindGroup *)(uintptr_t)1u;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_ERROR_INVALID_ARGUMENT ||
      group != NULL) {
    fprintf(stderr, "bind group accepted out-of-range buffer binding\n");
    GPUDestroyBindGroup(group);
    GPUDestroyBuffer(storageOnlyBuffer);
    GPUDestroyBuffer(uniformBuffer);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  bufferEntry.buffer.offset = 0u;
  bufferEntry.buffer.buffer = storageOnlyBuffer;
  group = (GPUBindGroup *)(uintptr_t)1u;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_ERROR_INVALID_ARGUMENT ||
      group != NULL) {
    fprintf(stderr, "bind group accepted buffer without required usage\n");
    GPUDestroyBindGroup(group);
    GPUDestroyBuffer(storageOnlyBuffer);
    GPUDestroyBuffer(uniformBuffer);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  GPUDestroyBuffer(storageOnlyBuffer);
  GPUDestroyBuffer(uniformBuffer);
  GPUDestroyBindGroupLayout(layout);

  entry.bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  entry.hasDynamicOffset = false;
  layout = NULL;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK || !layout) {
    fprintf(stderr, "bind group layout rejected valid sampled texture entry\n");
    return 0;
  }

  memset(&storageTexture, 0, sizeof(storageTexture));
  memset(&storageView, 0, sizeof(storageView));
  sampledTexture       = NULL;
  dualUseTexture       = NULL;
  sampledView          = NULL;
  dualUseView          = NULL;
  storageTexture.usage = GPU_TEXTURE_USAGE_STORAGE;
  storageView._texture = &storageTexture;

  memset(&textureInfo, 0, sizeof(textureInfo));
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "bind-group-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 1u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED;
  memset(&viewInfo, 0, sizeof(viewInfo));
  viewInfo.chain.sType         = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize    = sizeof(viewInfo);
  viewInfo.label               = "bind-group-texture-view";
  viewInfo.viewType            = GPU_TEXTURE_VIEW_2D;
  viewInfo.format              = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount       = 1u;
  viewInfo.arrayLayerCount     = 1u;
  if (GPUCreateTexture(device, &textureInfo, &sampledTexture) != GPU_OK ||
      !sampledTexture ||
      GPUCreateTextureView(sampledTexture, &viewInfo, &sampledView) != GPU_OK ||
      !sampledView) {
    fprintf(stderr, "sampled texture setup failed\n");
    GPUDestroyTextureView(sampledView);
    GPUDestroyTexture(sampledTexture);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_STORAGE;
  if (GPUCreateTexture(device, &textureInfo, &dualUseTexture) != GPU_OK ||
      !dualUseTexture ||
      GPUCreateTextureView(dualUseTexture, &viewInfo, &dualUseView) != GPU_OK ||
      !dualUseView) {
    fprintf(stderr, "sampled/storage texture setup failed\n");
    GPUDestroyTextureView(dualUseView);
    GPUDestroyTexture(dualUseTexture);
    GPUDestroyTextureView(sampledView);
    GPUDestroyTexture(sampledTexture);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  memset(&textureEntry, 0, sizeof(textureEntry));
  textureEntry.binding = 0u;
  textureEntry.textureView = sampledView;

  memset(&groupInfo, 0, sizeof(groupInfo));
  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label = "sampled-texture-group";
  groupInfo.layout = layout;
  groupInfo.entryCount = 1u;
  groupInfo.pEntries = &textureEntry;

  group = NULL;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "bind group rejected valid sampled texture view\n");
    GPUDestroyTextureView(dualUseView);
    GPUDestroyTexture(dualUseTexture);
    GPUDestroyTextureView(sampledView);
    GPUDestroyTexture(sampledTexture);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }
  GPUDestroyBindGroup(group);

  textureEntry.textureView = &storageView;
  group = (GPUBindGroup *)(uintptr_t)1u;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_ERROR_INVALID_ARGUMENT ||
      group != NULL) {
    fprintf(stderr, "bind group accepted texture view without sampled usage\n");
    GPUDestroyBindGroup(group);
    GPUDestroyTextureView(dualUseView);
    GPUDestroyTexture(dualUseTexture);
    GPUDestroyTextureView(sampledView);
    GPUDestroyTexture(sampledTexture);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }
  GPUDestroyBindGroupLayout(layout);

  textureLayoutEntries[0] = entry;
  textureLayoutEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  textureLayoutEntries[1] = entry;
  textureLayoutEntries[1].binding = 1u;
  textureLayoutEntries[1].bindingType = GPU_BINDING_STORAGE_TEXTURE;
  textureLayoutEntries[1].visibility = GPU_SHADER_STAGE_COMPUTE_BIT;
  layoutInfo.entryCount = 2u;
  layoutInfo.pEntries = textureLayoutEntries;
  dualTextureLayout = NULL;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &dualTextureLayout) != GPU_OK ||
      !dualTextureLayout) {
    fprintf(stderr, "bind group layout rejected sampled/storage texture pair\n");
    GPUDestroyTextureView(dualUseView);
    GPUDestroyTexture(dualUseTexture);
    GPUDestroyTextureView(sampledView);
    GPUDestroyTexture(sampledTexture);
    return 0;
  }

  textureEntry.textureView = dualUseView;
  memset(&groupInfo, 0, sizeof(groupInfo));
  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label = "incomplete-texture-group";
  groupInfo.layout = dualTextureLayout;
  groupInfo.entryCount = 1u;
  groupInfo.pEntries = &textureEntry;
  group = (GPUBindGroup *)(uintptr_t)1u;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_ERROR_INVALID_ARGUMENT ||
      group != NULL) {
    fprintf(stderr, "bind group accepted missing storage texture binding\n");
    GPUDestroyBindGroup(group);
    GPUDestroyBindGroupLayout(dualTextureLayout);
    GPUDestroyTextureView(dualUseView);
    GPUDestroyTexture(dualUseTexture);
    GPUDestroyTextureView(sampledView);
    GPUDestroyTexture(sampledTexture);
    return 0;
  }

  memset(textureGroupEntries, 0, sizeof(textureGroupEntries));
  textureGroupEntries[0].binding = 0u;
  textureGroupEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  textureGroupEntries[0].textureView = dualUseView;
  textureGroupEntries[1].binding = 1u;
  textureGroupEntries[1].bindingType = GPU_BINDING_STORAGE_TEXTURE;
  textureGroupEntries[1].textureView = dualUseView;
  groupInfo.label = "typed-texture-group";
  groupInfo.entryCount = 2u;
  groupInfo.pEntries = textureGroupEntries;
  group = NULL;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "bind group rejected explicit sampled/storage texture bindings\n");
    GPUDestroyBindGroupLayout(dualTextureLayout);
    GPUDestroyTextureView(dualUseView);
    GPUDestroyTexture(dualUseTexture);
    GPUDestroyTextureView(sampledView);
    GPUDestroyTexture(sampledTexture);
    return 0;
  }
  GPUDestroyBindGroup(group);
  GPUDestroyBindGroupLayout(dualTextureLayout);
  GPUDestroyTextureView(dualUseView);
  GPUDestroyTexture(dualUseTexture);
  GPUDestroyTextureView(sampledView);
  GPUDestroyTexture(sampledTexture);

  layoutInfo.entryCount = 1u;
  layoutInfo.pEntries = &entry;
  entry.bindingType = GPU_BINDING_SAMPLER;
  entry.hasDynamicOffset = false;
  entry.immutableSampler = true;
  entry.immutableSamplerDesc = valid_sampler_desc();
  layout = NULL;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK || !layout) {
    fprintf(stderr, "bind group layout rejected valid immutable sampler\n");
    return 0;
  }

  memset(&groupInfo, 0, sizeof(groupInfo));
  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label = "immutable-sampler-group";
  groupInfo.layout = layout;

  group = NULL;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "bind group rejected omitted immutable sampler binding\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }
  GPUDestroyBindGroup(group);

  memset(&runtimeSamplerEntry, 0, sizeof(runtimeSamplerEntry));
  runtimeSamplerEntry.binding = 0u;
  runtimeSamplerEntry.sampler = (GPUSampler *)(void *)&fakeSamplerStorage;
  groupInfo.entryCount = 1u;
  groupInfo.pEntries = &runtimeSamplerEntry;
  group = (GPUBindGroup *)(uintptr_t)1u;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_ERROR_INVALID_ARGUMENT ||
      group != NULL) {
    fprintf(stderr, "bind group accepted runtime immutable sampler binding\n");
    GPUDestroyBindGroup(group);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  layouts[0] = layout;
  memset(&pipelineInfo, 0, sizeof(pipelineInfo));
  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.chain.structSize = (uint32_t)(sizeof(pipelineInfo) - 1u);
  pipelineInfo.bindGroupLayoutCount = 1u;
  pipelineInfo.ppBindGroupLayouts = layouts;
  if (GPUCreatePipelineLayout(device, NULL, &pipelineLayout) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUCreatePipelineLayout(device, &pipelineInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "pipeline layout accepted null input\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  pipelineLayout = (GPUPipelineLayout *)(uintptr_t)1u;
  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) !=
      GPU_ERROR_INVALID_ARGUMENT ||
      pipelineLayout != NULL) {
    fprintf(stderr, "pipeline layout accepted wrong sType\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.chain.structSize = (uint32_t)(sizeof(pipelineInfo) - 1u);
  pipelineLayout = (GPUPipelineLayout *)(uintptr_t)1u;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) != GPU_ERROR_INVALID_ARGUMENT ||
      pipelineLayout != NULL) {
    fprintf(stderr, "pipeline layout accepted short structSize\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineLayout = NULL;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "pipeline layout rejected valid bind group layout\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }
  if (!check_pipeline_layout_bind_validation(device,
                                             &layoutInfo,
                                             layout,
                                             pipelineLayout)) {
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  GPUDestroyPipelineLayout(pipelineLayout);

  layouts[0] = NULL;
  pipelineInfo.pushConstantSizeBytes = 0u;
  pipelineInfo.pushConstantStages = 0u;
  pipelineLayout = (GPUPipelineLayout *)(uintptr_t)1u;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) !=
      GPU_ERROR_INVALID_ARGUMENT ||
      pipelineLayout != NULL) {
    fprintf(stderr, "pipeline layout accepted null bind group layout\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(tooManyLayouts); i++) {
    tooManyLayouts[i] = layout;
  }
  pipelineInfo.bindGroupLayoutCount = (uint32_t)GPU_ARRAY_LEN(tooManyLayouts);
  pipelineInfo.ppBindGroupLayouts = tooManyLayouts;
  pipelineLayout = (GPUPipelineLayout *)(uintptr_t)1u;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) !=
      GPU_ERROR_INVALID_ARGUMENT ||
      pipelineLayout != NULL) {
    fprintf(stderr, "pipeline layout accepted too many bind group layouts\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  layouts[0] = layout;
  pipelineInfo.bindGroupLayoutCount = 1u;
  pipelineInfo.ppBindGroupLayouts = layouts;
  pipelineInfo.pushConstantSizeBytes = 16u;
  pipelineInfo.pushConstantStages = 0u;
  pipelineLayout = (GPUPipelineLayout *)(uintptr_t)1u;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) !=
      GPU_ERROR_INVALID_ARGUMENT ||
      pipelineLayout != NULL) {
    fprintf(stderr, "pipeline layout accepted push constants without stages\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  pipelineInfo.pushConstantSizeBytes = 4097u;
  pipelineInfo.pushConstantStages = GPU_SHADER_STAGE_VERTEX_BIT;
  pipelineLayout = (GPUPipelineLayout *)(uintptr_t)1u;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) !=
      GPU_ERROR_INVALID_ARGUMENT ||
      pipelineLayout != NULL) {
    fprintf(stderr, "pipeline layout accepted oversized push constants\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  pipelineInfo.pushConstantSizeBytes = 16u;
  pipelineInfo.pushConstantStages = GPU_SHADER_STAGE_VERTEX_BIT;
  pipelineLayout = NULL;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "pipeline layout rejected valid push constants\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(layout);
  return 1;
}

static int
check_dynamic_offset_bind_validation(GPUDevice *device) {
  GPUApiDescriptor saved;
  GPUApi *api;
  GPUBindGroupLayoutEntry entry = {0};
  GPUBindGroupLayoutCreateInfo layoutInfo = {0};
  GPUBindGroupLayout *layout = NULL;
  GPUPipelineLayoutCreateInfo pipelineInfo = {0};
  GPUBindGroupLayout *layouts[1];
  GPUPipelineLayout *pipelineLayout = NULL;
  GPUBufferCreateInfo bufferInfo = {0};
  GPUBuffer *buffer = NULL;
  GPUBindGroupEntry groupEntry = {0};
  GPUBindGroupCreateInfo groupInfo = {0};
  GPUBindGroup *group = NULL;
  GPURenderPassEncoder renderPass = {0};
  GPUComputePassEncoder computePass = {0};
  uint32_t validOffset = 256u;
  uint32_t invalidOffset = 497u;
  uint32_t extraOffsets[2] = { 256u, 0u };
  int ok = 0;

  api = gpuDeviceApi(device);
  if (!api) {
    return 0;
  }
  saved = api->descriptor;
  api->descriptor.bindRenderGroup  = NULL;
  api->descriptor.bindComputeGroup = NULL;

  entry.binding = 0u;
  entry.bindingType = GPU_BINDING_UNIFORM_BUFFER;
  entry.visibility = GPU_SHADER_STAGE_VERTEX_BIT |
                     GPU_SHADER_STAGE_FRAGMENT_BIT |
                     GPU_SHADER_STAGE_COMPUTE_BIT;
  entry.arrayCount = 1u;
  entry.hasDynamicOffset = true;

  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label = "dynamic-offset-layout";
  layoutInfo.entryCount = 1u;
  layoutInfo.pEntries = &entry;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK || !layout) {
    fprintf(stderr, "dynamic offset layout setup failed\n");
    goto cleanup;
  }

  layouts[0] = layout;
  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.bindGroupLayoutCount = 1u;
  pipelineInfo.ppBindGroupLayouts = layouts;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "dynamic offset pipeline layout setup failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes = 512u;
  bufferInfo.usage = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    fprintf(stderr, "dynamic offset buffer setup failed\n");
    goto cleanup;
  }

  groupEntry.binding = 0u;
  groupEntry.bindingType = GPU_BINDING_UNIFORM_BUFFER;
  groupEntry.buffer.buffer = buffer;
  groupEntry.buffer.offset = 0u;
  groupEntry.buffer.size = 16u;

  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label = "dynamic-offset-group";
  groupInfo.layout = layout;
  groupInfo.entryCount = 1u;
  groupInfo.pEntries = &groupEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "dynamic offset group setup failed\n");
    goto cleanup;
  }

  renderPass._pipelineLayout = pipelineLayout;
  GPUBindRenderGroup(&renderPass, 0u, group, 1u, NULL);
  GPUBindRenderGroup(&renderPass, 0u, group, 0u, NULL);
  GPUBindRenderGroup(&renderPass, 0u, group, 2u, extraOffsets);
  GPUBindRenderGroup(&renderPass, 0u, group, 1u, &invalidOffset);
  if (renderPass._boundGroupLayouts[0]) {
    fprintf(stderr, "render bind accepted invalid dynamic offset\n");
    goto cleanup;
  }

  GPUBindRenderGroup(&renderPass, 0u, group, 1u, &validOffset);
  if (renderPass._boundGroupLayouts[0] != layout) {
    fprintf(stderr, "render bind rejected valid dynamic offset\n");
    goto cleanup;
  }

  computePass._pipelineLayout = pipelineLayout;
  GPUBindComputeGroup(&computePass, 0u, group, 1u, NULL);
  GPUBindComputeGroup(&computePass, 0u, group, 0u, NULL);
  GPUBindComputeGroup(&computePass, 0u, group, 2u, extraOffsets);
  GPUBindComputeGroup(&computePass, 0u, group, 1u, &invalidOffset);
  if (computePass._boundGroupLayouts[0]) {
    fprintf(stderr, "compute bind accepted invalid dynamic offset\n");
    goto cleanup;
  }

  GPUBindComputeGroup(&computePass, 0u, group, 1u, &validOffset);
  if (computePass._boundGroupLayouts[0] != layout) {
    fprintf(stderr, "compute bind rejected valid dynamic offset\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  api->descriptor = saved;
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(buffer);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(layout);
  return ok;
}

int
gpu_test_bindgroup(GPUDevice *device) {
  return check_backend_descriptor_hooks(device) &&
         check_bind_group_layout_validation(device) &&
         check_dynamic_offset_bind_validation(device);
}
