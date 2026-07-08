#include "test.h"

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
check_bind_group_layout_validation(GPUDevice *device) {
  unsigned char fakeSamplerStorage;
  GPUBindGroupLayoutEntry entry;
  GPUBindGroupLayoutCreateInfo layoutInfo;
  GPUBindGroupLayout *layout;
  GPUBindGroupEntry runtimeSamplerEntry;
  GPUBindGroupCreateInfo groupInfo;
  GPUBindGroup *group;
  GPUPipelineLayoutCreateInfo pipelineInfo;
  GPUBindGroupLayout *layouts[1];
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

  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(layout);
  return 1;
}

int
gpu_test_bindgroup(GPUDevice *device) {
  return check_bind_group_layout_validation(device);
}
