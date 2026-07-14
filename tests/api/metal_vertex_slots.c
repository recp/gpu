#include "test.h"
#include "../../src/api/device_internal.h"
#include "../../src/backend/mt/binding_limits.h"

static void
init_vertex_layout(GPUVertexAttribute    *attribute,
                   GPUVertexBufferLayout *layout) {
  memset(attribute, 0, sizeof(*attribute));
  memset(layout, 0, sizeof(*layout));

  attribute->shaderLocation = 0u;
  attribute->format         = GPU_VERTEX_FORMAT_FLOAT32X2;
  layout->strideBytes       = 8u;
  layout->stepMode          = GPU_VERTEX_STEP_MODE_VERTEX;
  layout->attributeCount    = 1u;
  layout->pAttributes       = attribute;
}

static void
init_pipeline_info(GPURenderPipelineCreateInfo *info,
                   GPUPipelineLayout           *layout,
                   GPUShaderLibrary            *library,
                   const char                  *vertexEntry,
                   const char                  *fragmentEntry,
                   GPUColorTargetState         *colorTarget) {
  memset(info, 0, sizeof(*info));
  memset(colorTarget, 0, sizeof(*colorTarget));

  colorTarget->format                = GPU_FORMAT_BGRA8_UNORM;
  info->chain.sType                  = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info->chain.structSize             = sizeof(*info);
  info->layout                       = layout;
  info->library                      = library;
  info->vertexEntry                  = vertexEntry;
  info->fragmentEntry                = fragmentEntry;
  info->colorTargetCount             = 1u;
  info->pColorTargets                = colorTarget;
  info->primitiveTopology            = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info->cullMode                     = GPU_CULL_MODE_NONE;
  info->frontFace                    = GPU_FRONT_FACE_CCW;
  info->multisample.sampleCount      = 1u;
}

static int
check_direct_msl_slots(GPUDevice *device) {
  static const char source[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct SlotVertexIn { float2 position [[attribute(0)]]; };\n"
    "vertex float4 slot_vs(SlotVertexIn input [[stage_in]], "
    "constant float4& offset [[buffer(0)]]) {\n"
    "  return float4(input.position + offset.xy, offset.z, 1.0);\n"
    "}\n"
    "fragment float4 slot_fs() { return float4(1.0); }\n";
  GPUShaderLibrary            *library;
  GPUBindGroupLayout          *groupLayout;
  GPUPipelineLayout           *pipelineLayout;
  GPURenderPipeline           *pipeline;
  GPUBindGroupLayout          *groups[1];
  GPUShaderLibraryCreateInfo   libraryInfo = {0};
  GPUBindGroupLayoutEntry      groupEntry = {0};
  GPUBindGroupLayoutCreateInfo groupInfo = {0};
  GPUPipelineLayoutCreateInfo  layoutInfo = {0};
  GPUVertexAttribute           attribute;
  GPUVertexBufferLayout        vertexLayout;
  GPUVertexBufferLayout        vertexLayouts[MT_VERTEX_BUFFER_COUNT] = {{0}};
  GPUColorTargetState          colorTarget;
  GPURenderPipelineCreateInfo  pipelineInfo;
  int                          ok;

  library        = NULL;
  groupLayout    = NULL;
  pipelineLayout = NULL;
  pipeline       = NULL;
  ok             = 0;

  libraryInfo.chain.sType      = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  libraryInfo.chain.structSize = sizeof(libraryInfo);
  libraryInfo.label            = "metal-vertex-resource-slots";
  libraryInfo.sourceKind       = GPU_SHADER_SOURCE_MSL_TEXT;
  libraryInfo.sourceData       = source;
  libraryInfo.sourceSize       = sizeof(source) - 1u;
  if (GPUCreateShaderLibrary(device, &libraryInfo, &library) != GPU_OK ||
      !library) {
    fprintf(stderr, "Metal vertex/resource slot shader setup failed\n");
    goto cleanup;
  }

  groupEntry.binding     = 0u;
  groupEntry.bindingType = GPU_BINDING_UNIFORM_BUFFER;
  groupEntry.visibility  = GPU_SHADER_STAGE_VERTEX_BIT;
  groupEntry.arrayCount  = 1u;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "metal-vertex-resource-group";
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &groupEntry;
  if (GPUCreateBindGroupLayout(device, &groupInfo, &groupLayout) != GPU_OK ||
      !groupLayout) {
    fprintf(stderr, "Metal vertex/resource slot group setup failed\n");
    goto cleanup;
  }

  groups[0]                       = groupLayout;
  layoutInfo.chain.sType          = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize     = sizeof(layoutInfo);
  layoutInfo.label                = "metal-vertex-resource-layout";
  layoutInfo.bindGroupLayoutCount = 1u;
  layoutInfo.ppBindGroupLayouts   = groups;
  if (GPUCreatePipelineLayout(device, &layoutInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "Metal vertex/resource pipeline layout setup failed\n");
    goto cleanup;
  }

  init_vertex_layout(&attribute, &vertexLayout);
  init_pipeline_info(&pipelineInfo,
                     pipelineLayout,
                     library,
                     "slot_vs",
                     "slot_fs",
                     &colorTarget);
  pipelineInfo.label                    = "metal-vertex-resource-pipeline";
  pipelineInfo.vertex.bufferLayoutCount = 1u;
  pipelineInfo.vertex.pBufferLayouts    = &vertexLayout;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "Metal vertex input collided with resource slot 0\n");
    goto cleanup;
  }
  GPUDestroyRenderPipeline(pipeline);
  pipeline = (GPURenderPipeline *)(uintptr_t)1u;

  vertexLayouts[MT_VERTEX_BUFFER_COUNT - 1u] = vertexLayout;
  pipelineInfo.vertex.bufferLayoutCount      = MT_VERTEX_BUFFER_COUNT;
  pipelineInfo.vertex.pBufferLayouts         = vertexLayouts;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) !=
        GPU_ERROR_UNSUPPORTED ||
      pipeline != NULL) {
    fprintf(stderr, "Metal accepted overlapping vertex/resource slots\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(groupLayout);
  GPUDestroyShaderLibrary(library);
  return ok;
}

static int
create_manual_slot_layout(GPUDevice              *device,
                          GPUBindGroupLayout     **outEmptyGroup,
                          GPUBindGroupLayout     **outResourceGroup,
                          GPUPipelineLayout      **outPipelineLayout) {
  GPUBindGroupLayout          *groups[2];
  GPUBindGroupLayoutEntry      entry = {0};
  GPUBindGroupLayoutCreateInfo groupInfo = {0};
  GPUPipelineLayoutCreateInfo  layoutInfo = {0};

  *outEmptyGroup     = NULL;
  *outResourceGroup  = NULL;
  *outPipelineLayout = NULL;

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "metal-empty-group";
  if (GPUCreateBindGroupLayout(device, &groupInfo, outEmptyGroup) != GPU_OK) {
    return 0;
  }

  entry.binding     = 0u;
  entry.bindingType = GPU_BINDING_UNIFORM_BUFFER;
  entry.visibility  = GPU_SHADER_STAGE_VERTEX_BIT;
  entry.arrayCount  = 1u;
  groupInfo.label      = "metal-manual-resource-group";
  groupInfo.entryCount = 1u;
  groupInfo.pEntries   = &entry;
  if (GPUCreateBindGroupLayout(device, &groupInfo, outResourceGroup) != GPU_OK) {
    return 0;
  }

  groups[0]                       = *outEmptyGroup;
  groups[1]                       = *outResourceGroup;
  layoutInfo.chain.sType          = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize     = sizeof(layoutInfo);
  layoutInfo.label                = "metal-manual-slot-layout";
  layoutInfo.bindGroupLayoutCount = 2u;
  layoutInfo.ppBindGroupLayouts   = groups;
  return GPUCreatePipelineLayout(device,
                                 &layoutInfo,
                                 outPipelineLayout) == GPU_OK;
}

static int
check_usl_slot_plan(GPUDevice *device, const char *bytecodePath) {
  GPUShaderLibrary            *library;
  GPUBindGroupLayout          *reflectionGroups[2];
  GPUBindGroupLayout          *emptyGroup;
  GPUBindGroupLayout          *manualGroup;
  GPUPipelineLayout           *reflectionLayout;
  GPUPipelineLayout           *manualLayout;
  GPURenderPipeline           *pipeline;
  GPUVertexBufferLayout        vertexLayouts[MT_VERTEX_BUFFER_COUNT] = {{0}};
  GPUVertexAttribute           attribute;
  GPUVertexBufferLayout        vertexLayout;
  GPUColorTargetState          colorTarget;
  GPURenderPipelineCreateInfo  pipelineInfo;
  uint64_t                     bytecodeSize;
  uint32_t                     groupCount;
  void                        *bytecode;
  int                          ok;

  library             = NULL;
  reflectionGroups[0] = NULL;
  reflectionGroups[1] = NULL;
  emptyGroup          = NULL;
  manualGroup         = NULL;
  reflectionLayout    = NULL;
  manualLayout        = NULL;
  pipeline            = NULL;
  bytecode            = NULL;
  ok                  = 0;

  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  if (!bytecode ||
      GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library) {
    fprintf(stderr, "Metal USL slot library setup failed\n");
    goto cleanup;
  }

  groupCount = (uint32_t)GPU_ARRAY_LEN(reflectionGroups);
  if (GPUCreateBindGroupLayoutsFromReflection(device,
                                               library,
                                               &groupCount,
                                               reflectionGroups) != GPU_OK ||
      groupCount != (uint32_t)GPU_ARRAY_LEN(reflectionGroups) ||
      GPUCreatePipelineLayoutFromReflection(device,
                                            library,
                                            groupCount,
                                            reflectionGroups,
                                            &reflectionLayout) != GPU_OK ||
      !reflectionLayout) {
    fprintf(stderr, "Metal USL reflection slot layout setup failed\n");
    goto cleanup;
  }

  init_vertex_layout(&attribute, &vertexLayout);
  init_pipeline_info(&pipelineInfo,
                     reflectionLayout,
                     library,
                     "api_slot_vs",
                     "api_fs",
                     &colorTarget);
  pipelineInfo.label = "metal-usl-sparse-slot-pipeline";
  vertexLayouts[MT_VERTEX_BUFFER_COUNT - 1u] = vertexLayout;
  pipelineInfo.vertex.bufferLayoutCount      = MT_VERTEX_BUFFER_COUNT;
  pipelineInfo.vertex.pBufferLayouts         = vertexLayouts;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "Metal rejected non-overlapping sparse USL slots\n");
    goto cleanup;
  }
  GPUDestroyRenderPipeline(pipeline);
  pipeline = (GPURenderPipeline *)(uintptr_t)1u;

  vertexLayouts[MT_VERTEX_BUFFER_COUNT - 1u] = (GPUVertexBufferLayout){0};
  vertexLayouts[MT_VERTEX_BUFFER_COUNT - 2u] = vertexLayout;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) !=
        GPU_ERROR_UNSUPPORTED ||
      pipeline != NULL) {
    fprintf(stderr, "Metal accepted a USL vertex/resource slot collision\n");
    goto cleanup;
  }

  if (!create_manual_slot_layout(device,
                                 &emptyGroup,
                                 &manualGroup,
                                 &manualLayout)) {
    fprintf(stderr, "Metal manual USL slot layout setup failed\n");
    goto cleanup;
  }

  vertexLayouts[MT_VERTEX_BUFFER_COUNT - 2u] = (GPUVertexBufferLayout){0};
  vertexLayouts[MT_VERTEX_BUFFER_COUNT - 1u] = vertexLayout;
  pipelineInfo.layout                        = manualLayout;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      pipeline != NULL) {
    fprintf(stderr, "Metal accepted a mismatched manual USL slot plan\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  free(bytecode);
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyPipelineLayout(manualLayout);
  GPUDestroyBindGroupLayout(manualGroup);
  GPUDestroyBindGroupLayout(emptyGroup);
  GPUDestroyPipelineLayout(reflectionLayout);
  GPUDestroyBindGroupLayout(reflectionGroups[1]);
  GPUDestroyBindGroupLayout(reflectionGroups[0]);
  GPUDestroyShaderLibrary(library);
  return ok;
}

int
gpu_test_metal_vertex_slots(GPUDevice *device, const char *bytecodePath) {
  GPUApi *api;

  api = gpuDeviceApi(device);
  if (!api || api->backend != GPU_BACKEND_METAL) {
    return 1;
  }

  return check_direct_msl_slots(device) &&
         check_usl_slot_plan(device, bytecodePath);
}
