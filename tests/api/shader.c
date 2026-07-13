#include "test.h"
#include "../../src/backend/api/gpudef.h"
#include "../../src/api/buffer_internal.h"
#include "../../src/api/compute_internal.h"
#include "../../src/api/render/pipeline_internal.h"
#include "../../src/api/texture_internal.h"

static int
shader_reflection_has_resource(const GPUShaderReflection *reflection,
                               GPUBindingType bindingType,
                               GPUShaderStageFlags visibility,
                               uint32_t groupIndex,
                               uint32_t binding,
                               int hasDynamicOffset) {
  if (!reflection || (!reflection->pResources && reflection->resourceCount > 0u)) {
    return 0;
  }

  for (uint32_t i = 0; i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *item = &reflection->pResources[i];
    if (item->groupIndex == groupIndex &&
        item->binding == binding &&
        item->bindingType == bindingType &&
        item->visibility == visibility &&
        item->arrayCount == 1u &&
        (item->hasDynamicOffset ? 1 : 0) == (hasDynamicOffset ? 1 : 0)) {
      return 1;
    }
  }

  return 0;
}

static int
layout_has_typed_entry(const GPUBindGroupLayoutEntry *entries,
                       uint32_t count,
                       GPUBindStage stage,
                       GPUBindKind kind,
                       GPUBindingType bindingType,
                       uint32_t binding,
                       int hasDynamicOffset) {
  if (!entries && count > 0u) {
    return 0;
  }

  for (uint32_t i = 0; i < count; i++) {
    if (entries[i].stage == stage &&
        entries[i].kind == kind &&
        entries[i].bindingType == bindingType &&
        entries[i].binding == binding &&
        (entries[i].hasDynamicOffset ? 1 : 0) == (hasDynamicOffset ? 1 : 0)) {
      return 1;
    }
  }

  return 0;
}

static int
check_compute_pipeline_workgroup_size(GPUDevice *device,
                                      GPUShaderLibrary *library,
                                      GPUPipelineLayout *layout) {
  GPUComputePipelineCreateInfo info = {0};
  GPUComputePipelineState *state;
  GPUComputePipeline *pipeline;
  int ok;

  info.chain.sType = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label = "api-reflection-compute-workgroup";
  info.layout = layout;
  info.library = library;
  info.entryPoint = "reflect_cs";

  pipeline = NULL;
  if (GPUCreateComputePipeline(device, &info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "failed to create workgroup reflection compute pipeline\n");
    GPUDestroyComputePipeline(pipeline);
    return 0;
  }

  state = pipeline->_state;
  ok = state &&
       state->workgroupSize[0] == 8u &&
       state->workgroupSize[1] == 8u &&
       state->workgroupSize[2] == 1u &&
       pipeline->_requiredBindGroupMask == 2u;
  GPUDestroyComputePipeline(pipeline);

  if (!ok) {
    fprintf(stderr, "compute pipeline did not inherit reflected metadata\n");
    return 0;
  }

  return 1;
}

static int
expect_reflected_compute_pipeline_error(GPUDevice *device,
                                        const GPUComputePipelineCreateInfo *info,
                                        const char *message) {
  GPUComputePipeline *pipeline;

  pipeline = (GPUComputePipeline *)(uintptr_t)1u;
  if (GPUCreateComputePipeline(device, info, &pipeline) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      pipeline != NULL) {
    fprintf(stderr, "%s\n", message);
    GPUDestroyComputePipeline(pipeline);
    return 0;
  }

  return 1;
}

static int
expect_reflected_render_pipeline_error(GPUDevice *device,
                                       const GPURenderPipelineCreateInfo *info,
                                       const char *message) {
  GPURenderPipeline *pipeline;

  pipeline = (GPURenderPipeline *)(uintptr_t)1u;
  if (GPUCreateRenderPipeline(device, info, &pipeline) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      pipeline != NULL) {
    fprintf(stderr, "%s\n", message);
    GPUDestroyRenderPipeline(pipeline);
    return 0;
  }

  return 1;
}

static int
check_reflected_pipeline_entry_stages(GPUDevice *device,
                                      GPUShaderLibrary *library,
                                      GPUPipelineLayout *layout) {
  GPUComputePipelineCreateInfo computeInfo = {0};
  GPURenderPipelineCreateInfo renderInfo = {0};
  GPUPipelineLayoutCreateInfo emptyLayoutInfo = {0};
  GPURenderPipeline *pipeline = NULL;
  GPUPipelineLayout *emptyLayout = NULL;
  GPUColorTargetState colorTarget = {0};
  GPUVertexAttribute attrs[2] = {{0}};
  GPUVertexBufferLayout vertexLayout = {0};

  computeInfo.chain.sType = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  computeInfo.chain.structSize = sizeof(computeInfo);
  computeInfo.label = "api-reflection-compute-stage";
  computeInfo.layout = layout;
  computeInfo.library = library;

  computeInfo.entryPoint = "reflect_vs";
  if (!expect_reflected_compute_pipeline_error(
        device,
        &computeInfo,
        "compute pipeline accepted reflected vertex entry")) {
    return 0;
  }

  computeInfo.entryPoint = "reflect_fs";
  if (!expect_reflected_compute_pipeline_error(
        device,
        &computeInfo,
        "compute pipeline accepted reflected fragment entry")) {
    return 0;
  }

  colorTarget.format = GPU_FORMAT_BGRA8_UNORM;
  attrs[0].shaderLocation = 0u;
  attrs[0].format = GPUFloat2;
  attrs[0].offset = 0u;
  attrs[1].shaderLocation = 1u;
  attrs[1].format = GPUFloat2;
  attrs[1].offset = 8u;
  vertexLayout.strideBytes = 16u;
  vertexLayout.stepMode = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount = (uint32_t)GPU_ARRAY_LEN(attrs);
  vertexLayout.pAttributes = attrs;

  renderInfo.chain.sType = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  renderInfo.chain.structSize = sizeof(renderInfo);
  renderInfo.label = "api-reflection-render-stage";
  renderInfo.layout = layout;
  renderInfo.library = library;
  renderInfo.vertexEntry = "reflect_vs";
  renderInfo.fragmentEntry = "reflect_fs";
  renderInfo.vertex.bufferLayoutCount = 1u;
  renderInfo.vertex.pBufferLayouts = &vertexLayout;
  renderInfo.colorTargetCount = 1u;
  renderInfo.pColorTargets = &colorTarget;
  renderInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  renderInfo.cullMode = GPU_CULL_MODE_NONE;
  renderInfo.frontFace = GPU_FRONT_FACE_CCW;
  renderInfo.multisample.sampleCount = 1u;

  emptyLayoutInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  emptyLayoutInfo.chain.structSize = sizeof(emptyLayoutInfo);
  emptyLayoutInfo.label = "api-reflection-empty-layout";
  if (GPUCreatePipelineLayout(device, &emptyLayoutInfo, &emptyLayout) != GPU_OK ||
      !emptyLayout) {
    fprintf(stderr, "failed to create empty reflection pipeline layout\n");
    return 0;
  }

  computeInfo.layout = emptyLayout;
  computeInfo.entryPoint = "reflect_cs";
  if (!expect_reflected_compute_pipeline_error(
        device,
        &computeInfo,
        "compute pipeline accepted layout missing reflected resources")) {
    GPUDestroyPipelineLayout(emptyLayout);
    return 0;
  }
  computeInfo.layout = layout;

  renderInfo.layout = emptyLayout;
  if (!expect_reflected_render_pipeline_error(
        device,
        &renderInfo,
        "render pipeline accepted layout missing reflected resources")) {
    GPUDestroyPipelineLayout(emptyLayout);
    return 0;
  }
  renderInfo.layout = layout;

  renderInfo.layout = emptyLayout;
  renderInfo.fragmentEntry = "reflect_plain_fs";
  if (GPUCreateRenderPipeline(device, &renderInfo, &pipeline) != GPU_OK ||
      !pipeline ||
      pipeline->_requiredBindGroupMask != 0u) {
    fprintf(stderr, "render pipeline rejected no-resource entry with empty layout\n");
    GPUDestroyRenderPipeline(pipeline);
    GPUDestroyPipelineLayout(emptyLayout);
    return 0;
  }
  GPUDestroyRenderPipeline(pipeline);
  pipeline = NULL;

  renderInfo.layout = layout;
  renderInfo.fragmentEntry = "reflect_plain_fs";
  if (GPUCreateRenderPipeline(device, &renderInfo, &pipeline) != GPU_OK ||
      !pipeline ||
      pipeline->_requiredBindGroupMask != 0u) {
    fprintf(stderr, "render pipeline required binds for no-resource entry\n");
    GPUDestroyRenderPipeline(pipeline);
    GPUDestroyPipelineLayout(emptyLayout);
    return 0;
  }
  GPUDestroyRenderPipeline(pipeline);
  pipeline = NULL;

  renderInfo.layout = layout;
  renderInfo.fragmentEntry = "reflect_fs";
  if (GPUCreateRenderPipeline(device, &renderInfo, &pipeline) != GPU_OK ||
      !pipeline ||
      pipeline->_requiredBindGroupMask != 3u) {
    fprintf(stderr,
            "render pipeline did not record reflected entry bind mask: %u\n",
            pipeline ? pipeline->_requiredBindGroupMask : 0u);
    GPUDestroyRenderPipeline(pipeline);
    GPUDestroyPipelineLayout(emptyLayout);
    return 0;
  }
  GPUDestroyRenderPipeline(pipeline);
  pipeline = NULL;

  renderInfo.vertexEntry = "reflect_cs";
  if (!expect_reflected_render_pipeline_error(
        device,
        &renderInfo,
        "render pipeline accepted reflected compute vertex entry")) {
    GPUDestroyPipelineLayout(emptyLayout);
    return 0;
  }

  renderInfo.vertexEntry = "reflect_vs";
  renderInfo.fragmentEntry = "reflect_cs";
  if (!expect_reflected_render_pipeline_error(
        device,
        &renderInfo,
        "render pipeline accepted reflected compute fragment entry")) {
    GPUDestroyPipelineLayout(emptyLayout);
    return 0;
  }

  renderInfo.vertexEntry = "reflect_fs";
  renderInfo.fragmentEntry = "reflect_vs";
  if (!expect_reflected_render_pipeline_error(
        device,
        &renderInfo,
        "render pipeline accepted swapped reflected render entries")) {
    GPUDestroyPipelineLayout(emptyLayout);
    return 0;
  }

  GPUDestroyPipelineLayout(emptyLayout);
  return 1;
}

static int
check_reflected_dynamic_offset_validation(GPUDevice *device,
                                          GPUShaderLibrary *library) {
  GPUBindGroupLayout *layouts[2] = {0};
  GPUBindGroupLayout *badLayouts[2] = {0};
  GPUBindGroupLayoutEntry badGroup1Entries[2] = {{0}};
  GPUBindGroupLayoutCreateInfo layoutInfo = {0};
  GPUPipelineLayoutCreateInfo pipelineInfo = {0};
  GPUPipelineLayout *pipelineLayout = NULL;
  GPURenderPipelineCreateInfo renderInfo = {0};
  GPUComputePipelineCreateInfo computeInfo = {0};
  GPUComputePipeline *computePipeline = NULL;
  GPUColorTargetState colorTarget = {0};
  GPUVertexAttribute attrs[2] = {{0}};
  GPUVertexBufferLayout vertexLayout = {0};
  uint32_t layoutCount;
  int ok = 0;

  layoutCount = (uint32_t)GPU_ARRAY_LEN(layouts);
  if (GPUCreateBindGroupLayoutsFromReflection(device, library, &layoutCount, layouts) !=
        GPU_OK ||
      layoutCount != (uint32_t)GPU_ARRAY_LEN(layouts) ||
      !layouts[0] ||
      !layouts[1]) {
    fprintf(stderr, "dynamic offset validation could not get reflection layouts\n");
    goto cleanup;
  }

  badGroup1Entries[0].binding = 0u;
  badGroup1Entries[0].bindingType = GPU_BINDING_UNIFORM_BUFFER;
  badGroup1Entries[0].visibility = GPU_SHADER_STAGE_FRAGMENT_BIT;
  badGroup1Entries[0].arrayCount = 1u;
  badGroup1Entries[0].hasDynamicOffset = false;

  badGroup1Entries[1].binding = 1u;
  badGroup1Entries[1].bindingType = GPU_BINDING_STORAGE_TEXTURE;
  badGroup1Entries[1].visibility = GPU_SHADER_STAGE_COMPUTE_BIT;
  badGroup1Entries[1].arrayCount = 1u;

  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label = "api-reflection-bad-dynamic-offset-group1";
  layoutInfo.entryCount = (uint32_t)GPU_ARRAY_LEN(badGroup1Entries);
  layoutInfo.pEntries = badGroup1Entries;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &badLayouts[1]) != GPU_OK ||
      !badLayouts[1]) {
    fprintf(stderr, "dynamic offset validation could not create mismatch layout\n");
    goto cleanup;
  }
  badLayouts[0] = layouts[0];

  pipelineLayout = (GPUPipelineLayout *)(uintptr_t)1u;
  if (GPUCreatePipelineLayoutFromReflection(device,
                                            library,
                                            (uint32_t)GPU_ARRAY_LEN(badLayouts),
                                            badLayouts,
                                            &pipelineLayout) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      pipelineLayout != NULL) {
    fprintf(stderr, "reflection pipeline layout accepted dynamic offset mismatch\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    pipelineLayout = NULL;
    goto cleanup;
  }

  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label = "api-reflection-manual-dynamic-offset-mismatch";
  pipelineInfo.bindGroupLayoutCount = (uint32_t)GPU_ARRAY_LEN(badLayouts);
  pipelineInfo.ppBindGroupLayouts = badLayouts;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "dynamic offset validation could not create manual mismatch layout\n");
    goto cleanup;
  }

  colorTarget.format = GPU_FORMAT_BGRA8_UNORM;
  attrs[0].shaderLocation = 0u;
  attrs[0].format = GPUFloat2;
  attrs[0].offset = 0u;
  attrs[1].shaderLocation = 1u;
  attrs[1].format = GPUFloat2;
  attrs[1].offset = 8u;
  vertexLayout.strideBytes = 16u;
  vertexLayout.stepMode = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount = (uint32_t)GPU_ARRAY_LEN(attrs);
  vertexLayout.pAttributes = attrs;

  renderInfo.chain.sType = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  renderInfo.chain.structSize = sizeof(renderInfo);
  renderInfo.label = "api-reflection-render-dynamic-offset-mismatch";
  renderInfo.layout = pipelineLayout;
  renderInfo.library = library;
  renderInfo.vertexEntry = "reflect_vs";
  renderInfo.fragmentEntry = "reflect_fs";
  renderInfo.vertex.bufferLayoutCount = 1u;
  renderInfo.vertex.pBufferLayouts = &vertexLayout;
  renderInfo.colorTargetCount = 1u;
  renderInfo.pColorTargets = &colorTarget;
  renderInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  renderInfo.cullMode = GPU_CULL_MODE_NONE;
  renderInfo.frontFace = GPU_FRONT_FACE_CCW;
  renderInfo.multisample.sampleCount = 1u;
  if (!expect_reflected_render_pipeline_error(
        device,
        &renderInfo,
        "render pipeline accepted reflected dynamic offset mismatch")) {
    goto cleanup;
  }

  computeInfo.chain.sType = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  computeInfo.chain.structSize = sizeof(computeInfo);
  computeInfo.label = "api-reflection-compute-ignores-fragment-dynamic-offset";
  computeInfo.layout = pipelineLayout;
  computeInfo.library = library;
  computeInfo.entryPoint = "reflect_cs";
  if (GPUCreateComputePipeline(device, &computeInfo, &computePipeline) != GPU_OK ||
      !computePipeline ||
      computePipeline->_requiredBindGroupMask != 2u) {
    fprintf(stderr, "compute pipeline rejected unrelated fragment dynamic offset mismatch\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  GPUDestroyComputePipeline(computePipeline);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(badLayouts[1]);
  GPUDestroyBindGroupLayout(layouts[1]);
  GPUDestroyBindGroupLayout(layouts[0]);
  return ok;
}

static int
check_shader_layout_after_library_destroy(GPUDevice *device,
                                          GPUShaderLayout *shaderLayout) {
  const GPUBindGroupLayoutEntry *entries;
  GPUBuffer fakeBufferStorage;
  GPUTexture fakeFragmentTextureStorage;
  GPUTexture fakeComputeTextureStorage;
  GPUTextureView fakeFragmentTextureView;
  GPUTextureView fakeComputeTextureView;
  GPUBindGroupEntry group0Entries[2];
  GPUBindGroupEntry group1Entries[2];
  GPUBindGroupCreateInfo groupInfo = {0};
  GPUBindGroup *group0Group;
  GPUBindGroup *group1Group;
  GPUApiDescriptor savedDescriptor;
  GPUApi *api;
  uint32_t count;
  int ok;

  if (!shaderLayout ||
      shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    return 0;
  }

  entries = GPUGetBindGroupLayoutEntries(shaderLayout->bindGroupLayouts[0], &count);
  ok = count == 2u &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindBuffer,
                              GPU_BINDING_UNIFORM_BUFFER,
                              0u,
                              0) &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindTexture,
                              GPU_BINDING_SAMPLED_TEXTURE,
                              1u,
                              0);
  if (ok) {
    entries = GPUGetBindGroupLayoutEntries(shaderLayout->bindGroupLayouts[1], &count);
    ok = count == 2u &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindBuffer,
                              GPU_BINDING_UNIFORM_BUFFER,
                              0u,
                              1) &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageCompute,
                              GPUBindKindTexture,
                              GPU_BINDING_STORAGE_TEXTURE,
                              1u,
                              0);
  }
  if (!ok) {
    fprintf(stderr, "unexpected canonical shader layout entries\n");
    return 0;
  }

  memset(group0Entries, 0, sizeof(group0Entries));
  memset(group1Entries, 0, sizeof(group1Entries));
  memset(&fakeBufferStorage, 0, sizeof(fakeBufferStorage));
  memset(&fakeFragmentTextureStorage, 0, sizeof(fakeFragmentTextureStorage));
  memset(&fakeComputeTextureStorage, 0, sizeof(fakeComputeTextureStorage));
  memset(&fakeFragmentTextureView, 0, sizeof(fakeFragmentTextureView));
  memset(&fakeComputeTextureView, 0, sizeof(fakeComputeTextureView));
  fakeBufferStorage.sizeBytes = 16u;
  fakeBufferStorage.usage = GPU_BUFFER_USAGE_UNIFORM;
  fakeFragmentTextureStorage.usage = GPU_TEXTURE_USAGE_SAMPLED;
  fakeComputeTextureStorage.usage = GPU_TEXTURE_USAGE_STORAGE;
  fakeFragmentTextureView._texture = &fakeFragmentTextureStorage;
  fakeComputeTextureView._texture = &fakeComputeTextureStorage;

  group0Entries[0].binding = 0u;
  group0Entries[0].stage = GPUBindStageFragment;
  group0Entries[0].kind = GPUBindKindBuffer;
  group0Entries[0].buffer.buffer = &fakeBufferStorage;
  group0Entries[0].buffer.size = 16u;

  group0Entries[1].binding = 1u;
  group0Entries[1].stage = GPUBindStageFragment;
  group0Entries[1].kind = GPUBindKindTexture;
  group0Entries[1].textureView = &fakeFragmentTextureView;

  group1Entries[0].binding = 0u;
  group1Entries[0].bindingType = GPU_BINDING_UNIFORM_BUFFER;
  group1Entries[0].stage = GPUBindStageFragment;
  group1Entries[0].kind = GPUBindKindBuffer;
  group1Entries[0].buffer.buffer = &fakeBufferStorage;
  group1Entries[0].buffer.size = 16u;

  group1Entries[1].binding = 1u;
  group1Entries[1].stage = GPUBindStageCompute;
  group1Entries[1].kind = GPUBindKindTexture;
  group1Entries[1].textureView = &fakeComputeTextureView;

  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label = "api-shader-layout-lifetime-group0";
  groupInfo.layout = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount = (uint32_t)GPU_ARRAY_LEN(group0Entries);
  groupInfo.pEntries = group0Entries;

  api = gpuDeviceApi(device);
  if (!api) {
    return 0;
  }
  savedDescriptor = api->descriptor;
  api->descriptor.createBindGroup = NULL;

  group0Group = NULL;
  if (GPUCreateBindGroup(device, &groupInfo, &group0Group) != GPU_OK || !group0Group) {
    fprintf(stderr, "shader layout group0 failed after library destroy\n");
    api->descriptor = savedDescriptor;
    GPUDestroyBindGroup(group0Group);
    return 0;
  }

  groupInfo.label = "api-shader-layout-lifetime-group1";
  groupInfo.layout = shaderLayout->bindGroupLayouts[1];
  groupInfo.entryCount = (uint32_t)GPU_ARRAY_LEN(group1Entries);
  groupInfo.pEntries = group1Entries;

  group1Group = NULL;
  if (GPUCreateBindGroup(device, &groupInfo, &group1Group) != GPU_OK || !group1Group) {
    fprintf(stderr, "shader layout group1 failed after library destroy\n");
    api->descriptor = savedDescriptor;
    GPUDestroyBindGroup(group1Group);
    GPUDestroyBindGroup(group0Group);
    return 0;
  }

  api->descriptor = savedDescriptor;
  GPUDestroyBindGroup(group1Group);
  GPUDestroyBindGroup(group0Group);
  return 1;
}

static int
check_reflection_objects_after_library_destroy(GPUDevice *device,
                                               GPUBindGroupLayout **layouts,
                                               uint32_t layoutCount) {
  const GPUBindGroupLayoutEntry *entries;
  GPUBuffer fakeBufferStorage;
  GPUTexture fakeFragmentTextureStorage;
  GPUTexture fakeComputeTextureStorage;
  GPUTextureView fakeFragmentTextureView;
  GPUTextureView fakeComputeTextureView;
  GPUBindGroupEntry group0Entries[2];
  GPUBindGroupEntry group1Entries[2];
  GPUBindGroupCreateInfo groupInfo = {0};
  GPUPipelineLayoutCreateInfo pipelineInfo = {0};
  GPUPipelineLayout *pipelineLayout = NULL;
  GPUBindGroup *group0Group = NULL;
  GPUBindGroup *group1Group = NULL;
  GPUApiDescriptor savedDescriptor;
  GPUApi *api;
  int descriptorHookDisabled = 0;
  uint32_t count;
  int ok = 0;

  if (!layouts ||
      layoutCount != 2u ||
      !layouts[0] ||
      !layouts[1]) {
    fprintf(stderr, "reflection layout ownership setup is incomplete\n");
    return 0;
  }

  entries = GPUGetBindGroupLayoutEntries(layouts[0], &count);
  if (count != 2u ||
      !layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindBuffer,
                              GPU_BINDING_UNIFORM_BUFFER,
                              0u,
                              0) ||
      !layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindTexture,
                              GPU_BINDING_SAMPLED_TEXTURE,
                              1u,
                              0)) {
    fprintf(stderr, "reflection group 0 layout changed after library destroy\n");
    return 0;
  }

  entries = GPUGetBindGroupLayoutEntries(layouts[1], &count);
  if (count != 2u ||
      !layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindBuffer,
                              GPU_BINDING_UNIFORM_BUFFER,
                              0u,
                              1) ||
      !layout_has_typed_entry(entries,
                              count,
                              GPUBindStageCompute,
                              GPUBindKindTexture,
                              GPU_BINDING_STORAGE_TEXTURE,
                              1u,
                              0)) {
    fprintf(stderr, "reflection group 1 layout changed after library destroy\n");
    return 0;
  }

  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.bindGroupLayoutCount = layoutCount;
  pipelineInfo.ppBindGroupLayouts = layouts;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "reflection layouts could not create pipeline layout after library destroy\n");
    return 0;
  }

  memset(group0Entries, 0, sizeof(group0Entries));
  memset(group1Entries, 0, sizeof(group1Entries));
  memset(&fakeBufferStorage, 0, sizeof(fakeBufferStorage));
  memset(&fakeFragmentTextureStorage, 0, sizeof(fakeFragmentTextureStorage));
  memset(&fakeComputeTextureStorage, 0, sizeof(fakeComputeTextureStorage));
  memset(&fakeFragmentTextureView, 0, sizeof(fakeFragmentTextureView));
  memset(&fakeComputeTextureView, 0, sizeof(fakeComputeTextureView));

  fakeBufferStorage.sizeBytes = 16u;
  fakeBufferStorage.usage = GPU_BUFFER_USAGE_UNIFORM;
  fakeFragmentTextureStorage.usage = GPU_TEXTURE_USAGE_SAMPLED;
  fakeComputeTextureStorage.usage = GPU_TEXTURE_USAGE_STORAGE;
  fakeFragmentTextureView._texture = &fakeFragmentTextureStorage;
  fakeComputeTextureView._texture = &fakeComputeTextureStorage;

  group0Entries[0].binding = 0u;
  group0Entries[0].bindingType = GPU_BINDING_UNIFORM_BUFFER;
  group0Entries[0].buffer.buffer = &fakeBufferStorage;
  group0Entries[0].buffer.size = 16u;

  group0Entries[1].binding = 1u;
  group0Entries[1].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  group0Entries[1].textureView = &fakeFragmentTextureView;

  group1Entries[0].binding = 0u;
  group1Entries[0].bindingType = GPU_BINDING_UNIFORM_BUFFER;
  group1Entries[0].buffer.buffer = &fakeBufferStorage;
  group1Entries[0].buffer.size = 16u;

  group1Entries[1].binding = 1u;
  group1Entries[1].bindingType = GPU_BINDING_STORAGE_TEXTURE;
  group1Entries[1].textureView = &fakeComputeTextureView;

  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label = "api-reflection-lifetime-group0";
  groupInfo.layout = layouts[0];
  groupInfo.entryCount = (uint32_t)GPU_ARRAY_LEN(group0Entries);
  groupInfo.pEntries = group0Entries;
  api = gpuDeviceApi(device);
  if (!api) {
    goto cleanup;
  }
  savedDescriptor = api->descriptor;
  api->descriptor.createBindGroup = NULL;
  descriptorHookDisabled = 1;
  if (GPUCreateBindGroup(device, &groupInfo, &group0Group) != GPU_OK || !group0Group) {
    fprintf(stderr, "reflection group 0 failed after library destroy\n");
    goto cleanup;
  }

  groupInfo.label = "api-reflection-lifetime-group1";
  groupInfo.layout = layouts[1];
  groupInfo.entryCount = (uint32_t)GPU_ARRAY_LEN(group1Entries);
  groupInfo.pEntries = group1Entries;
  if (GPUCreateBindGroup(device, &groupInfo, &group1Group) != GPU_OK || !group1Group) {
    fprintf(stderr, "reflection group 1 failed after library destroy\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (descriptorHookDisabled) {
    api->descriptor = savedDescriptor;
  }
  GPUDestroyBindGroup(group1Group);
  GPUDestroyBindGroup(group0Group);
  GPUDestroyPipelineLayout(pipelineLayout);
  return ok;
}

static int
check_reflection_layout_api(GPUDevice *device, GPUShaderLibrary *library) {
  const GPUBindGroupLayoutEntry *entries;
  GPUBindGroupLayout *layouts[2] = {0};
  GPUBindGroupLayout *smallLayouts[1] = {0};
  GPUBindGroupLayout *reversedLayouts[2] = {0};
  GPUPipelineLayout *pipelineLayout;
  uint32_t count;
  int ok;

  count = 0u;
  if (GPUCreateBindGroupLayoutsFromReflection(device, NULL, &count, NULL) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUCreateBindGroupLayoutsFromReflection(device, library, NULL, NULL) !=
        GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "reflection layout accepted null input\n");
    return 0;
  }

  if (GPUCreatePipelineLayoutFromReflection(device,
                                            NULL,
                                            0u,
                                            NULL,
                                            &pipelineLayout) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUCreatePipelineLayoutFromReflection(device,
                                            library,
                                            0u,
                                            NULL,
                                            NULL) !=
        GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "reflection pipeline layout accepted null input\n");
    return 0;
  }

  count = 0u;
  if (GPUCreateBindGroupLayoutsFromReflection(device, library, &count, NULL) !=
        GPU_OK ||
      count != 2u) {
    fprintf(stderr, "reflection layout count query failed\n");
    return 0;
  }

  count = 1u;
  if (GPUCreateBindGroupLayoutsFromReflection(device,
                                              library,
                                              &count,
                                              smallLayouts) !=
        GPU_ERROR_INSUFFICIENT_CAPACITY ||
      count != 2u) {
    fprintf(stderr, "reflection layout capacity validation failed\n");
    return 0;
  }

  count = (uint32_t)GPU_ARRAY_LEN(layouts);
  if (GPUCreateBindGroupLayoutsFromReflection(device, library, &count, layouts) !=
        GPU_OK ||
      count != 2u ||
      !layouts[0] ||
      !layouts[1]) {
    fprintf(stderr, "reflection layout fill failed\n");
    ok = 0;
    goto cleanup;
  }

  entries = GPUGetBindGroupLayoutEntries(layouts[0], &count);
  ok = count == 2u &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindBuffer,
                              GPU_BINDING_UNIFORM_BUFFER,
                              0u,
                              0) &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindTexture,
                              GPU_BINDING_SAMPLED_TEXTURE,
                              1u,
                              0);
  if (ok) {
    entries = GPUGetBindGroupLayoutEntries(layouts[1], &count);
    ok = count == 2u &&
         layout_has_typed_entry(entries,
                                count,
                                GPUBindStageFragment,
                                GPUBindKindBuffer,
                                GPU_BINDING_UNIFORM_BUFFER,
                                0u,
                                1) &&
         layout_has_typed_entry(entries,
                                count,
                                GPUBindStageCompute,
                                GPUBindKindTexture,
                                GPU_BINDING_STORAGE_TEXTURE,
                                1u,
                                0);
  }
  if (!ok) {
    fprintf(stderr, "reflection layouts are not ordered by group index\n");
    goto cleanup;
  }

  pipelineLayout = (GPUPipelineLayout *)(uintptr_t)1u;
  if (GPUCreatePipelineLayoutFromReflection(device,
                                            library,
                                            1u,
                                            layouts,
                                            &pipelineLayout) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      pipelineLayout != NULL) {
    fprintf(stderr, "reflection pipeline layout accepted too few layouts\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    ok = 0;
    goto cleanup;
  }

  pipelineLayout = NULL;
  if (GPUCreatePipelineLayoutFromReflection(device,
                                            library,
                                            2u,
                                            layouts,
                                            &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "reflection pipeline layout rejected ordered layouts\n");
    ok = 0;
    goto cleanup;
  }
  GPUDestroyPipelineLayout(pipelineLayout);
  pipelineLayout = NULL;

  reversedLayouts[0] = layouts[1];
  reversedLayouts[1] = layouts[0];
  pipelineLayout = (GPUPipelineLayout *)(uintptr_t)1u;
  if (GPUCreatePipelineLayoutFromReflection(device,
                                            library,
                                            2u,
                                            reversedLayouts,
                                            &pipelineLayout) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      pipelineLayout != NULL) {
    fprintf(stderr, "reflection pipeline layout accepted reversed layouts\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    ok = 0;
    goto cleanup;
  }

  ok = 1;

cleanup:
  GPUDestroyBindGroupLayout(layouts[1]);
  GPUDestroyBindGroupLayout(layouts[0]);
  return ok;
}

static int
check_canonical_shader_library(GPUDevice *device,
                               const void *bytecode,
                               uint64_t bytecodeSize) {
  GPUShaderLibraryCreateInfo createInfo = {0};
  GPUShaderReflection reflection;
  GPUBindGroupLayout *reflectionLayouts[2] = {0};
  GPUShaderLayout *shaderLayout;
  GPUPipelineLayout *reflectionPipelineLayout;
  GPUShaderLibrary *library;
  uint32_t reflectionLayoutCount;
  int ok;

  createInfo.chain.sType = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  createInfo.chain.structSize = sizeof(createInfo);
  createInfo.label = "api-reflection.us";
  createInfo.sourceKind = GPU_SHADER_SOURCE_USL_BYTECODE;
  createInfo.sourceData = bytecode;
  createInfo.sourceSize = bytecodeSize;
  createInfo.generateReflection = true;

  library = NULL;
  if (GPUCreateShaderLibrary(device, &createInfo, &library) != GPU_OK || !library) {
    fprintf(stderr, "failed to create canonical shader library\n");
    return 0;
  }

  memset(&reflection, 0, sizeof(reflection));
  shaderLayout = NULL;
  reflectionPipelineLayout = NULL;
  reflectionLayoutCount = (uint32_t)GPU_ARRAY_LEN(reflectionLayouts);
  ok = GPUGetShaderReflection(library, &reflection) == GPU_OK &&
       reflection.resourceCount == 4u &&
       shader_reflection_has_resource(&reflection,
                                      GPU_BINDING_UNIFORM_BUFFER,
                                      GPU_SHADER_STAGE_FRAGMENT_BIT,
                                      0u,
                                      0u,
                                      0) &&
       shader_reflection_has_resource(&reflection,
                                      GPU_BINDING_SAMPLED_TEXTURE,
                                      GPU_SHADER_STAGE_FRAGMENT_BIT,
                                      0u,
                                      1u,
                                      0) &&
       shader_reflection_has_resource(&reflection,
                                      GPU_BINDING_UNIFORM_BUFFER,
                                      GPU_SHADER_STAGE_FRAGMENT_BIT,
                                      1u,
                                      0u,
                                      1) &&
       shader_reflection_has_resource(&reflection,
                                      GPU_BINDING_STORAGE_TEXTURE,
                                      GPU_SHADER_STAGE_COMPUTE_BIT,
                                      1u,
                                      1u,
                                      0) &&
       check_reflection_layout_api(device, library) &&
       GPUCreateShaderLayout(device, library, &shaderLayout) == GPU_OK &&
       shaderLayout != NULL &&
       GPUCreateBindGroupLayoutsFromReflection(device,
                                               library,
                                               &reflectionLayoutCount,
                                               reflectionLayouts) == GPU_OK &&
       reflectionLayoutCount == (uint32_t)GPU_ARRAY_LEN(reflectionLayouts) &&
       GPUCreatePipelineLayoutFromReflection(device,
                                             library,
                                             reflectionLayoutCount,
                                             reflectionLayouts,
                                             &reflectionPipelineLayout) == GPU_OK &&
       reflectionPipelineLayout != NULL &&
       check_compute_pipeline_workgroup_size(device,
                                             library,
                                             shaderLayout->pipelineLayout) &&
       check_reflected_pipeline_entry_stages(device,
                                             library,
                                             shaderLayout->pipelineLayout) &&
       check_reflected_dynamic_offset_validation(device, library);

  GPUDestroyShaderLibrary(library);
  library = NULL;

  if (ok) {
    ok = check_shader_layout_after_library_destroy(device, shaderLayout) &&
         check_reflection_objects_after_library_destroy(device,
                                                        reflectionLayouts,
                                                        reflectionLayoutCount);
  }

  GPUDestroyPipelineLayout(reflectionPipelineLayout);
  GPUDestroyBindGroupLayout(reflectionLayouts[1]);
  GPUDestroyBindGroupLayout(reflectionLayouts[0]);
  GPUDestroyShaderLayout(shaderLayout);
  GPUFreeShaderReflection(&reflection);

  if (!ok) {
    fprintf(stderr, "unexpected canonical shader library data\n");
    return 0;
  }

  return 1;
}

static int
check_usl_shader_library_helper(GPUDevice *device,
                                const void *bytecode,
                                uint64_t bytecodeSize) {
  GPUShaderLibrary *library;
  GPUShaderReflection reflection;
  GPUShaderLayout *shaderLayout;
  int ok;

  library = (GPUShaderLibrary *)(uintptr_t)1u;
  if (GPUCreateShaderLibraryFromUSL(NULL, bytecode, bytecodeSize, &library) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      library != NULL) {
    fprintf(stderr, "USL shader helper accepted null device\n");
    return 0;
  }

  library = (GPUShaderLibrary *)(uintptr_t)1u;
  if (GPUCreateShaderLibraryFromUSL(device, NULL, bytecodeSize, &library) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      library != NULL) {
    fprintf(stderr, "USL shader helper accepted null bytecode\n");
    return 0;
  }

  library = (GPUShaderLibrary *)(uintptr_t)1u;
  if (GPUCreateShaderLibraryFromUSL(device, bytecode, 0u, &library) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      library != NULL) {
    fprintf(stderr, "USL shader helper accepted empty bytecode\n");
    return 0;
  }

  if (GPUCreateShaderLibraryFromUSL(device, bytecode, bytecodeSize, NULL) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "USL shader helper accepted null output\n");
    return 0;
  }

  library = NULL;
  if (GPUCreateShaderLibraryFromUSL(device, bytecode, bytecodeSize, &library) !=
        GPU_OK ||
      !library) {
    fprintf(stderr, "USL shader helper failed to create library\n");
    return 0;
  }

  memset(&reflection, 0, sizeof(reflection));
  shaderLayout = NULL;
  ok = GPUGetShaderReflection(library, &reflection) == GPU_OK &&
       reflection.resourceCount == 4u &&
       GPUCreateShaderLayout(device, library, &shaderLayout) == GPU_OK &&
       shaderLayout != NULL &&
       shaderLayout->bindGroupLayoutCount == 2u &&
       shaderLayout->pipelineLayout != NULL;

  GPUDestroyShaderLayout(shaderLayout);
  GPUFreeShaderReflection(&reflection);
  GPUDestroyShaderLibrary(library);

  if (!ok) {
    fprintf(stderr, "unexpected USL shader helper data\n");
    return 0;
  }

  return 1;
}

int
gpu_test_shader(GPUDevice *device, const char *bytecodePath) {
  uint64_t bytecodeSize;
  void *bytecode;
  int ok;

  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  if (!bytecode) {
    fprintf(stderr, "failed to read shader bytecode: %s\n", bytecodePath);
    return 0;
  }

  ok = check_canonical_shader_library(device,
                                      bytecode,
                                      bytecodeSize) &&
       check_usl_shader_library_helper(device,
                                       bytecode,
                                       bytecodeSize);

  free(bytecode);
  return ok;
}
