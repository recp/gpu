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

#include "../common.h"
#include "../../../api/descr/descriptor_internal.h"
#include "../../../api/library_internal.h"
#include "../../../api/render/pipeline_internal.h"
#include "pipeline_cache.h"

static VkCullModeFlags
vk__cullMode(GPUCullMode mode) {
  switch (mode) {
    case GPU_CULL_MODE_FRONT:
      return VK_CULL_MODE_FRONT_BIT;
    case GPU_CULL_MODE_BACK:
      return VK_CULL_MODE_BACK_BIT;
    case GPU_CULL_MODE_NONE:
    default:
      return VK_CULL_MODE_NONE;
  }
}

static VkFrontFace
vk__frontFace(GPUFrontFace face) {
  return face == GPU_FRONT_FACE_CW ?
    VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

static VkPrimitiveTopology
vk__primitiveTopology(GPUPrimitiveTopology topology) {
  static const VkPrimitiveTopology topologies[] = {
    [GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST]  = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    [GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    [GPU_PRIMITIVE_TOPOLOGY_LINE_LIST]      = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    [GPU_PRIMITIVE_TOPOLOGY_LINE_STRIP]     = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
    [GPU_PRIMITIVE_TOPOLOGY_POINT_LIST]     = VK_PRIMITIVE_TOPOLOGY_POINT_LIST
  };

  return (uint32_t)topology < GPU_ARRAY_LEN(topologies)
           ? topologies[topology]
           : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

static VkCompareOp
vk__compareOp(GPUCompareOp op) {
  static const VkCompareOp operations[] = {
    [GPU_COMPARE_NEVER]         = VK_COMPARE_OP_NEVER,
    [GPU_COMPARE_LESS]          = VK_COMPARE_OP_LESS,
    [GPU_COMPARE_EQUAL]         = VK_COMPARE_OP_EQUAL,
    [GPU_COMPARE_LESS_EQUAL]    = VK_COMPARE_OP_LESS_OR_EQUAL,
    [GPU_COMPARE_GREATER]       = VK_COMPARE_OP_GREATER,
    [GPU_COMPARE_NOT_EQUAL]     = VK_COMPARE_OP_NOT_EQUAL,
    [GPU_COMPARE_GREATER_EQUAL] = VK_COMPARE_OP_GREATER_OR_EQUAL,
    [GPU_COMPARE_ALWAYS]        = VK_COMPARE_OP_ALWAYS
  };

  return (uint32_t)op < GPU_ARRAY_LEN(operations)
           ? operations[op]
           : VK_COMPARE_OP_NEVER;
}

static VkSampleCountFlagBits
vk__sampleCount(uint32_t count) {
  static const VkSampleCountFlagBits counts[] = {
    [1] = VK_SAMPLE_COUNT_1_BIT,
    [2] = VK_SAMPLE_COUNT_2_BIT,
    [4] = VK_SAMPLE_COUNT_4_BIT,
    [8] = VK_SAMPLE_COUNT_8_BIT
  };

  return count < GPU_ARRAY_LEN(counts) ? counts[count] : 0;
}

static VkStencilOp
vk__stencilOp(GPUStencilOp op) {
  static const VkStencilOp operations[] = {
    [GPU_STENCIL_OP_KEEP]            = VK_STENCIL_OP_KEEP,
    [GPU_STENCIL_OP_ZERO]            = VK_STENCIL_OP_ZERO,
    [GPU_STENCIL_OP_REPLACE]         = VK_STENCIL_OP_REPLACE,
    [GPU_STENCIL_OP_INCREMENT_CLAMP] = VK_STENCIL_OP_INCREMENT_AND_CLAMP,
    [GPU_STENCIL_OP_DECREMENT_CLAMP] = VK_STENCIL_OP_DECREMENT_AND_CLAMP,
    [GPU_STENCIL_OP_INVERT]          = VK_STENCIL_OP_INVERT,
    [GPU_STENCIL_OP_INCREMENT_WRAP]  = VK_STENCIL_OP_INCREMENT_AND_WRAP,
    [GPU_STENCIL_OP_DECREMENT_WRAP]  = VK_STENCIL_OP_DECREMENT_AND_WRAP
  };

  return (uint32_t)op < GPU_ARRAY_LEN(operations)
           ? operations[op]
           : VK_STENCIL_OP_KEEP;
}

static void
vk__fillStencilState(VkStencilOpState          *native,
                     const GPUStencilFaceState *state,
                     uint32_t                   compareMask,
                     uint32_t                   writeMask) {
  native->failOp      = vk__stencilOp(state->failOp);
  native->passOp      = vk__stencilOp(state->passOp);
  native->depthFailOp = vk__stencilOp(state->depthFailOp);
  native->compareOp   = vk__compareOp(state->compare);
  native->compareMask = compareMask;
  native->writeMask   = writeMask;
}

static bool
vk__vertexFormat(GPUVertexFormat format, VkFormat *outFormat) {
  static const VkFormat formats[GPU_VERTEX_FORMAT_COUNT] = {
    [GPU_VERTEX_FORMAT_UNDEFINED]       = VK_FORMAT_UNDEFINED,
    [GPU_VERTEX_FORMAT_UINT8]           = VK_FORMAT_R8_UINT,
    [GPU_VERTEX_FORMAT_UINT8X2]         = VK_FORMAT_R8G8_UINT,
    [GPU_VERTEX_FORMAT_UINT8X4]         = VK_FORMAT_R8G8B8A8_UINT,
    [GPU_VERTEX_FORMAT_SINT8]           = VK_FORMAT_R8_SINT,
    [GPU_VERTEX_FORMAT_SINT8X2]         = VK_FORMAT_R8G8_SINT,
    [GPU_VERTEX_FORMAT_SINT8X4]         = VK_FORMAT_R8G8B8A8_SINT,
    [GPU_VERTEX_FORMAT_UNORM8]          = VK_FORMAT_R8_UNORM,
    [GPU_VERTEX_FORMAT_UNORM8X2]        = VK_FORMAT_R8G8_UNORM,
    [GPU_VERTEX_FORMAT_UNORM8X4]        = VK_FORMAT_R8G8B8A8_UNORM,
    [GPU_VERTEX_FORMAT_SNORM8]          = VK_FORMAT_R8_SNORM,
    [GPU_VERTEX_FORMAT_SNORM8X2]        = VK_FORMAT_R8G8_SNORM,
    [GPU_VERTEX_FORMAT_SNORM8X4]        = VK_FORMAT_R8G8B8A8_SNORM,
    [GPU_VERTEX_FORMAT_UINT16]          = VK_FORMAT_R16_UINT,
    [GPU_VERTEX_FORMAT_UINT16X2]        = VK_FORMAT_R16G16_UINT,
    [GPU_VERTEX_FORMAT_UINT16X4]        = VK_FORMAT_R16G16B16A16_UINT,
    [GPU_VERTEX_FORMAT_SINT16]          = VK_FORMAT_R16_SINT,
    [GPU_VERTEX_FORMAT_SINT16X2]        = VK_FORMAT_R16G16_SINT,
    [GPU_VERTEX_FORMAT_SINT16X4]        = VK_FORMAT_R16G16B16A16_SINT,
    [GPU_VERTEX_FORMAT_UNORM16]         = VK_FORMAT_R16_UNORM,
    [GPU_VERTEX_FORMAT_UNORM16X2]       = VK_FORMAT_R16G16_UNORM,
    [GPU_VERTEX_FORMAT_UNORM16X4]       = VK_FORMAT_R16G16B16A16_UNORM,
    [GPU_VERTEX_FORMAT_SNORM16]         = VK_FORMAT_R16_SNORM,
    [GPU_VERTEX_FORMAT_SNORM16X2]       = VK_FORMAT_R16G16_SNORM,
    [GPU_VERTEX_FORMAT_SNORM16X4]       = VK_FORMAT_R16G16B16A16_SNORM,
    [GPU_VERTEX_FORMAT_FLOAT16]         = VK_FORMAT_R16_SFLOAT,
    [GPU_VERTEX_FORMAT_FLOAT16X2]       = VK_FORMAT_R16G16_SFLOAT,
    [GPU_VERTEX_FORMAT_FLOAT16X4]       = VK_FORMAT_R16G16B16A16_SFLOAT,
    [GPU_VERTEX_FORMAT_FLOAT32]         = VK_FORMAT_R32_SFLOAT,
    [GPU_VERTEX_FORMAT_FLOAT32X2]       = VK_FORMAT_R32G32_SFLOAT,
    [GPU_VERTEX_FORMAT_FLOAT32X3]       = VK_FORMAT_R32G32B32_SFLOAT,
    [GPU_VERTEX_FORMAT_FLOAT32X4]       = VK_FORMAT_R32G32B32A32_SFLOAT,
    [GPU_VERTEX_FORMAT_SINT32]          = VK_FORMAT_R32_SINT,
    [GPU_VERTEX_FORMAT_SINT32X2]        = VK_FORMAT_R32G32_SINT,
    [GPU_VERTEX_FORMAT_SINT32X3]        = VK_FORMAT_R32G32B32_SINT,
    [GPU_VERTEX_FORMAT_SINT32X4]        = VK_FORMAT_R32G32B32A32_SINT,
    [GPU_VERTEX_FORMAT_UINT32]          = VK_FORMAT_R32_UINT,
    [GPU_VERTEX_FORMAT_UINT32X2]        = VK_FORMAT_R32G32_UINT,
    [GPU_VERTEX_FORMAT_UINT32X3]        = VK_FORMAT_R32G32B32_UINT,
    [GPU_VERTEX_FORMAT_UINT32X4]        = VK_FORMAT_R32G32B32A32_UINT,
    [GPU_VERTEX_FORMAT_UNORM10_10_10_2] =
      VK_FORMAT_A2B10G10R10_UNORM_PACK32,
    [GPU_VERTEX_FORMAT_UNORM8X4_BGRA]   = VK_FORMAT_B8G8R8A8_UNORM
  };
  VkFormat result;

  if (!outFormat || (uint32_t)format >= GPU_ARRAY_LEN(formats)) {
    return false;
  }

  result = formats[format];
  if (result == VK_FORMAT_UNDEFINED) {
    return false;
  }

  *outFormat = result;
  return true;
}

static VkColorComponentFlags
vk__colorMask(GPUColorWriteMaskFlags mask) {
  VkColorComponentFlags result;

  if (mask == GPU_COLOR_WRITE_DEFAULT) {
    mask = GPU_COLOR_WRITE_ALL;
  } else if (mask == GPU_COLOR_WRITE_NONE) {
    return 0u;
  }

  result = 0u;
  if ((mask & GPU_COLOR_WRITE_R) != 0u) result |= VK_COLOR_COMPONENT_R_BIT;
  if ((mask & GPU_COLOR_WRITE_G) != 0u) result |= VK_COLOR_COMPONENT_G_BIT;
  if ((mask & GPU_COLOR_WRITE_B) != 0u) result |= VK_COLOR_COMPONENT_B_BIT;
  if ((mask & GPU_COLOR_WRITE_A) != 0u) result |= VK_COLOR_COMPONENT_A_BIT;
  return result;
}

static VkBlendFactor
vk__blendFactor(GPUBlendFactor factor) {
  static const VkBlendFactor factors[] = {
    [GPU_BLEND_FACTOR_ZERO]                = VK_BLEND_FACTOR_ZERO,
    [GPU_BLEND_FACTOR_ONE]                 = VK_BLEND_FACTOR_ONE,
    [GPU_BLEND_FACTOR_SRC_ALPHA]           = VK_BLEND_FACTOR_SRC_ALPHA,
    [GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA] =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
  };

  return (uint32_t)factor < GPU_ARRAY_LEN(factors)
           ? factors[factor]
           : VK_BLEND_FACTOR_ZERO;
}

static VkBlendOp
vk__blendOp(GPUBlendOp op) {
  static const VkBlendOp operations[] = {
    [GPU_BLEND_OP_ADD]              = VK_BLEND_OP_ADD,
    [GPU_BLEND_OP_SUBTRACT]         = VK_BLEND_OP_SUBTRACT,
    [GPU_BLEND_OP_REVERSE_SUBTRACT] = VK_BLEND_OP_REVERSE_SUBTRACT,
    [GPU_BLEND_OP_MIN]              = VK_BLEND_OP_MIN,
    [GPU_BLEND_OP_MAX]              = VK_BLEND_OP_MAX
  };

  return (uint32_t)op < GPU_ARRAY_LEN(operations)
           ? operations[op]
           : VK_BLEND_OP_ADD;
}

static const GPUMeshPipelineEXT*
vk__meshPipelineInfo(const GPURenderPipelineCreateInfo *info) {
  const GPUChainedStruct *chain;

  chain = info ? info->chain.pNext : NULL;
  while (chain) {
    if (chain->sType == GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT) {
      return (const GPUMeshPipelineEXT *)chain;
    }
    chain = chain->pNext;
  }
  return NULL;
}

static void
vk__fillBlendState(VkPipelineColorBlendAttachmentState *native,
                   const GPUBlendState                  *blend) {
  native->blendEnable         = blend->enabled;
  native->srcColorBlendFactor = vk__blendFactor(blend->color.srcFactor);
  native->dstColorBlendFactor = vk__blendFactor(blend->color.dstFactor);
  native->colorBlendOp        = vk__blendOp(blend->color.op);
  native->srcAlphaBlendFactor = vk__blendFactor(blend->alpha.srcFactor);
  native->dstAlphaBlendFactor = vk__blendFactor(blend->alpha.dstFactor);
  native->alphaBlendOp        = vk__blendOp(blend->alpha.op);
  native->colorWriteMask      = vk__colorMask(blend->writeMask);
}

static VkResult
vk__createPipelineRenderPass(VkDevice device,
                             VkFormat format,
                             VkRenderPass *outRenderPass) {
  VkAttachmentDescription attachment = {0};
  VkAttachmentReference   colorRef = {0};
  VkSubpassDescription    subpass = {0};
  VkSubpassDependency     dependency = {0};
  VkRenderPassCreateInfo  info = {0};

  attachment.format         = format;
  attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  colorRef.attachment = 0u;
  colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1u;
  subpass.pColorAttachments    = &colorRef;

  dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass    = 0u;
  dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 1u;
  info.pAttachments    = &attachment;
  info.subpassCount    = 1u;
  info.pSubpasses      = &subpass;
  info.dependencyCount = 1u;
  info.pDependencies   = &dependency;
  return vkCreateRenderPass(device, &info, NULL, outRenderPass);
}

GPU_HIDE
GPUResult
vk_createRenderPipeline(GPUDevice                         *device,
                        const GPURenderPipelineCreateInfo *info,
                        uint32_t                           requiredBindGroupMask,
                        GPURenderPipeline                 *pipeline) {
  GPUDeviceVk                       *deviceVk;
  GPUShaderLibraryVk                      *library;
  GPUPipelineLayoutVk               *layout;
  GPURenderPipelineVk               *native;
  const GPUMeshPipelineEXT          *mesh;
  const GPUDepthStencilState        *depthState;
  VkVertexInputBindingDescription   *vertexBindings;
  VkVertexInputAttributeDescription *vertexAttributes;
  VkFormat                           colorFormats[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  VkFormat                           depthFormat;
  VkFormat                           stencilFormat;
  VkPipelineShaderStageCreateInfo    stages[3] = {{0}};
  VkPipelineVertexInputStateCreateInfo vertexInput = {0};
  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0};
  VkPipelineViewportStateCreateInfo viewport = {0};
  VkPipelineRasterizationStateCreateInfo raster = {0};
  VkPipelineMultisampleStateCreateInfo multisample = {0};
  VkPipelineDepthStencilStateCreateInfo depthStencil = {0};
  VkPipelineColorBlendAttachmentState colorBlends[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS] = {{0}};
  VkPipelineColorBlendStateCreateInfo blend = {0};
  VkDynamicState                    dynamicStates[4];
  VkPipelineDynamicStateCreateInfo  dynamic = {0};
  VkPipelineRenderingCreateInfoKHR  rendering = {0};
  VkGraphicsPipelineCreateInfo      pipelineInfo = {0};
  VkPipelineCache                   pipelineCache;
  VkResult                          result;
  uint32_t                          vertexAttributeCount;
  uint32_t                          stageCount;
  VkSampleCountFlagBits             sampleCount;

  if (!device || !device->_priv || !info || !pipeline ||
      !info->library || !info->library->_priv ||
      !info->layout || !info->layout->_native) {
    return GPU_ERROR_UNSUPPORTED;
  }
  deviceVk = device->_priv;
  mesh     = vk__meshPipelineInfo(info);
  if (mesh && (!deviceVk->meshShader ||
               (mesh->taskEntry && !deviceVk->taskShader))) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!deviceVk->dynamicRendering && info->colorTargetCount != 1u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  sampleCount = vk__sampleCount(info->multisample.sampleCount
                                  ? info->multisample.sampleCount
                                  : 1u);
  if (!sampleCount ||
      (sampleCount > VK_SAMPLE_COUNT_1_BIT && !deviceVk->dynamicRendering) ||
      (info->colorTargetCount > 0u &&
       (deviceVk->colorSampleCounts & sampleCount) == 0u) ||
      (info->depthStencilFormat != GPU_FORMAT_UNDEFINED &&
       (deviceVk->depthSampleCounts & sampleCount) == 0u)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!deviceVk->dynamicRendering &&
      info->depthStencilFormat != GPU_FORMAT_UNDEFINED) {
    return GPU_ERROR_UNSUPPORTED;
  }
  for (uint32_t i = 0u; i < info->colorTargetCount; i++) {
    if (!vk_formatFromGPU(info->pColorTargets[i].format, &colorFormats[i])) {
      return GPU_ERROR_UNSUPPORTED;
    }
    vk__fillBlendState(&colorBlends[i], &info->pColorTargets[i].blend);
    if (!deviceVk->independentBlend && i > 0u &&
        memcmp(&colorBlends[0],
               &colorBlends[i],
               sizeof(colorBlends[i])) != 0) {
      return GPU_ERROR_UNSUPPORTED;
    }
  }
  depthFormat   = VK_FORMAT_UNDEFINED;
  stencilFormat = VK_FORMAT_UNDEFINED;
  if (info->depthStencilFormat != GPU_FORMAT_UNDEFINED &&
      !vk_formatFromGPU(info->depthStencilFormat, &depthFormat)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (info->depthStencilFormat == GPU_FORMAT_STENCIL8 ||
      info->depthStencilFormat == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
      info->depthStencilFormat == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8) {
    stencilFormat = depthFormat;
  }
  if (info->depthStencilFormat == GPU_FORMAT_STENCIL8) {
    depthFormat = VK_FORMAT_UNDEFINED;
  }

  vertexBindings       = NULL;
  vertexAttributes     = NULL;
  vertexAttributeCount = 0u;
  for (uint32_t i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    if (info->vertex.pBufferLayouts[i].attributeCount >
        UINT32_MAX - vertexAttributeCount) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    vertexAttributeCount += info->vertex.pBufferLayouts[i].attributeCount;
  }
  if (info->vertex.bufferLayoutCount > 0u) {
    vertexBindings = calloc(info->vertex.bufferLayoutCount,
                            sizeof(*vertexBindings));
    if (!vertexBindings) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }
  }
  if (vertexAttributeCount > 0u) {
    vertexAttributes = calloc(vertexAttributeCount,
                              sizeof(*vertexAttributes));
    if (!vertexAttributes) {
      free(vertexBindings);
      return GPU_ERROR_OUT_OF_MEMORY;
    }
  }

  vertexAttributeCount = 0u;
  for (uint32_t i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    const GPUVertexBufferLayout *bufferLayout;

    bufferLayout = &info->vertex.pBufferLayouts[i];
    vertexBindings[i].binding   = i;
    vertexBindings[i].stride    = bufferLayout->strideBytes;
    vertexBindings[i].inputRate = bufferLayout->stepMode ==
                                    GPU_VERTEX_STEP_MODE_INSTANCE
                                      ? VK_VERTEX_INPUT_RATE_INSTANCE
                                      : VK_VERTEX_INPUT_RATE_VERTEX;
    for (uint32_t j = 0u; j < bufferLayout->attributeCount; j++) {
      const GPUVertexAttribute *attribute;
      VkVertexInputAttributeDescription *nativeAttribute;

      attribute       = &bufferLayout->pAttributes[j];
      nativeAttribute = &vertexAttributes[vertexAttributeCount++];
      nativeAttribute->location = attribute->shaderLocation;
      nativeAttribute->binding  = i;
      nativeAttribute->offset   = attribute->offset;
      if (!vk__vertexFormat(attribute->format, &nativeAttribute->format)) {
        free(vertexAttributes);
        free(vertexBindings);
        return GPU_ERROR_UNSUPPORTED;
      }
    }
  }

  library  = info->library->_priv;
  layout   = info->layout->_native;
  depthState = info->pDepthStencilState;
  if (!layout->layout) {
    free(vertexAttributes);
    free(vertexBindings);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    free(vertexAttributes);
    free(vertexBindings);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  GPU__UNUSED(requiredBindGroupMask);
  native->device = deviceVk->device;
  if (vk_createShaderLayout(device,
                            info->layout,
                            info->library,
                            &native->shaderLayout) != GPU_OK) {
    free(vertexAttributes);
    free(vertexBindings);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (!deviceVk->dynamicRendering &&
      vk__createPipelineRenderPass(native->device,
                                   colorFormats[0],
                                   &native->renderPass) != VK_SUCCESS) {
    free(vertexAttributes);
    free(vertexBindings);
    vk_destroyShaderLayout(&native->shaderLayout);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  stageCount = 0u;
  if (mesh) {
#ifdef VK_EXT_mesh_shader
    if (mesh->taskEntry) {
      stages[stageCount].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[stageCount].stage  = VK_SHADER_STAGE_TASK_BIT_EXT;
      stages[stageCount].module = library->module;
      stages[stageCount].pName  = mesh->taskEntry;
      stageCount++;
    }
    stages[stageCount].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[stageCount].stage  = VK_SHADER_STAGE_MESH_BIT_EXT;
    stages[stageCount].module = library->module;
    stages[stageCount].pName  = mesh->meshEntry;
    stageCount++;
#else
    free(vertexAttributes);
    free(vertexBindings);
    vkDestroyRenderPass(native->device, native->renderPass, NULL);
    vk_destroyShaderLayout(&native->shaderLayout);
    free(native);
    return GPU_ERROR_UNSUPPORTED;
#endif
  } else {
    stages[stageCount].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[stageCount].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[stageCount].module = library->module;
    stages[stageCount].pName  = info->vertexEntry;
    stageCount++;
  }
  stages[stageCount].sType =
    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[stageCount].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[stageCount].module = library->module;
  stages[stageCount].pName  = info->fragmentEntry;
  stageCount++;

  vertexInput.sType                           =
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount   =
    info->vertex.bufferLayoutCount;
  vertexInput.pVertexBindingDescriptions      = vertexBindings;
  vertexInput.vertexAttributeDescriptionCount = vertexAttributeCount;
  vertexInput.pVertexAttributeDescriptions    = vertexAttributes;

  inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = vk__primitiveTopology(info->primitiveTopology);

  viewport.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport.viewportCount = 1u;
  viewport.scissorCount  = 1u;

  raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode    = vk__cullMode(info->cullMode);
  raster.frontFace   = vk__frontFace(info->frontFace);
  raster.lineWidth   = 1.0f;

  multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = sampleCount;
  multisample.alphaToCoverageEnable =
    info->multisample.alphaToCoverageEnable;
  multisample.pSampleMask          = info->multisample.sampleMask ?
    &info->multisample.sampleMask : NULL;

  depthStencil.sType =
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable =
    depthState && (depthState->depthTestEnable ||
                   depthState->depthWriteEnable);
  depthStencil.depthWriteEnable =
    depthState && depthState->depthWriteEnable;
  depthStencil.depthCompareOp =
    depthState && depthState->depthTestEnable
      ? vk__compareOp(depthState->depthCompare)
      : VK_COMPARE_OP_ALWAYS;
  depthStencil.stencilTestEnable =
    depthState && depthState->stencilTestEnable;
  if (depthState) {
    vk__fillStencilState(&depthStencil.front,
                         &depthState->front,
                         depthState->stencilReadMask,
                         depthState->stencilWriteMask);
    vk__fillStencilState(&depthStencil.back,
                         &depthState->back,
                         depthState->stencilReadMask,
                         depthState->stencilWriteMask);
  }

  blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount = info->colorTargetCount;
  blend.pAttachments    = colorBlends;

  dynamicStates[0]          = VK_DYNAMIC_STATE_VIEWPORT;
  dynamicStates[1]          = VK_DYNAMIC_STATE_SCISSOR;
  dynamicStates[2]          = VK_DYNAMIC_STATE_BLEND_CONSTANTS;
  dynamicStates[3]          = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
  dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic.dynamicStateCount = 4u;
  dynamic.pDynamicStates    = dynamicStates;

  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
  rendering.colorAttachmentCount = info->colorTargetCount;
  rendering.pColorAttachmentFormats = colorFormats;
  rendering.depthAttachmentFormat = depthFormat;
  rendering.stencilAttachmentFormat = stencilFormat;

  pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.pNext               = deviceVk->dynamicRendering
                                       ? &rendering
                                       : NULL;
  pipelineInfo.stageCount          = stageCount;
  pipelineInfo.pStages             = stages;
  pipelineInfo.pVertexInputState   = mesh ? NULL : &vertexInput;
  pipelineInfo.pInputAssemblyState = mesh ? NULL : &inputAssembly;
  pipelineInfo.pViewportState      = &viewport;
  pipelineInfo.pRasterizationState = &raster;
  pipelineInfo.pMultisampleState   = &multisample;
  pipelineInfo.pDepthStencilState  = &depthStencil;
  pipelineInfo.pColorBlendState    = &blend;
  pipelineInfo.pDynamicState       = &dynamic;
  pipelineInfo.layout              = native->shaderLayout.layout;
  pipelineInfo.renderPass          = deviceVk->dynamicRendering
                                       ? VK_NULL_HANDLE
                                       : native->renderPass;
  pipelineInfo.subpass             = 0u;
  pipelineCache = vk_lockCache(info->cache);
  result = vkCreateGraphicsPipelines(native->device,
                                     pipelineCache,
                                     1u,
                                     &pipelineInfo,
                                     NULL,
                                     &native->pipeline);
  vk_unlockCache(info->cache);
  if (result != VK_SUCCESS) {
    free(vertexAttributes);
    free(vertexBindings);
    vkDestroyRenderPass(native->device, native->renderPass, NULL);
    vk_destroyShaderLayout(&native->shaderLayout);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  free(vertexAttributes);
  free(vertexBindings);

  pipeline->_priv  = native;
  pipeline->_state = native;
  return GPU_OK;
}

GPU_HIDE
void
vk_destroyRenderPipeline(GPURenderPipeline *pipeline) {
  GPURenderPipelineVk *native;

  if (!pipeline) {
    return;
  }

  native = pipeline->_priv;
  if (native) {
    if (native->pipeline) {
      vkDestroyPipeline(native->device, native->pipeline, NULL);
    }
    if (native->renderPass) {
      vkDestroyRenderPass(native->device, native->renderPass, NULL);
    }
    vk_destroyShaderLayout(&native->shaderLayout);
    free(native);
  }
  free(pipeline);
}

GPU_HIDE
void
vk_initRenderPipeline(GPUApiRender *api) {
  api->createPipeline         = vk_createRenderPipeline;
  api->destroyRenderPipeline = vk_destroyRenderPipeline;
}
