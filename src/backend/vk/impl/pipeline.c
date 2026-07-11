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
  VkFormat result;

  if (!outFormat) {
    return false;
  }

  switch (format) {
    case GPUUChar:
      result = VK_FORMAT_R8_UINT;
      break;
    case GPUUChar2:
      result = VK_FORMAT_R8G8_UINT;
      break;
    case GPUUChar3:
      result = VK_FORMAT_R8G8B8_UINT;
      break;
    case GPUUChar4:
      result = VK_FORMAT_R8G8B8A8_UINT;
      break;
    case GPUChar:
      result = VK_FORMAT_R8_SINT;
      break;
    case GPUChar2:
      result = VK_FORMAT_R8G8_SINT;
      break;
    case GPUChar3:
      result = VK_FORMAT_R8G8B8_SINT;
      break;
    case GPUChar4:
      result = VK_FORMAT_R8G8B8A8_SINT;
      break;
    case GPUUCharNormalized:
      result = VK_FORMAT_R8_UNORM;
      break;
    case GPUUChar2Normalized:
      result = VK_FORMAT_R8G8_UNORM;
      break;
    case GPUUChar3Normalized:
      result = VK_FORMAT_R8G8B8_UNORM;
      break;
    case GPUUChar4Normalized:
      result = VK_FORMAT_R8G8B8A8_UNORM;
      break;
    case GPUCharNormalized:
      result = VK_FORMAT_R8_SNORM;
      break;
    case GPUChar2Normalized:
      result = VK_FORMAT_R8G8_SNORM;
      break;
    case GPUChar3Normalized:
      result = VK_FORMAT_R8G8B8_SNORM;
      break;
    case GPUChar4Normalized:
      result = VK_FORMAT_R8G8B8A8_SNORM;
      break;
    case GPUUShort:
      result = VK_FORMAT_R16_UINT;
      break;
    case GPUUShort2:
      result = VK_FORMAT_R16G16_UINT;
      break;
    case GPUUShort3:
      result = VK_FORMAT_R16G16B16_UINT;
      break;
    case GPUUShort4:
      result = VK_FORMAT_R16G16B16A16_UINT;
      break;
    case GPUShort:
      result = VK_FORMAT_R16_SINT;
      break;
    case GPUShort2:
      result = VK_FORMAT_R16G16_SINT;
      break;
    case GPUShort3:
      result = VK_FORMAT_R16G16B16_SINT;
      break;
    case GPUShort4:
      result = VK_FORMAT_R16G16B16A16_SINT;
      break;
    case GPUUShortNormalized:
      result = VK_FORMAT_R16_UNORM;
      break;
    case GPUUShort2Normalized:
      result = VK_FORMAT_R16G16_UNORM;
      break;
    case GPUUShort3Normalized:
      result = VK_FORMAT_R16G16B16_UNORM;
      break;
    case GPUUShort4Normalized:
      result = VK_FORMAT_R16G16B16A16_UNORM;
      break;
    case GPUShortNormalized:
      result = VK_FORMAT_R16_SNORM;
      break;
    case GPUShort2Normalized:
      result = VK_FORMAT_R16G16_SNORM;
      break;
    case GPUShort3Normalized:
      result = VK_FORMAT_R16G16B16_SNORM;
      break;
    case GPUShort4Normalized:
      result = VK_FORMAT_R16G16B16A16_SNORM;
      break;
    case GPUVertexFormatHalf:
      result = VK_FORMAT_R16_SFLOAT;
      break;
    case GPUHalf2:
      result = VK_FORMAT_R16G16_SFLOAT;
      break;
    case GPUHalf3:
      result = VK_FORMAT_R16G16B16_SFLOAT;
      break;
    case GPUHalf4:
      result = VK_FORMAT_R16G16B16A16_SFLOAT;
      break;
    case GPUFloat:
      result = VK_FORMAT_R32_SFLOAT;
      break;
    case GPUFloat2:
      result = VK_FORMAT_R32G32_SFLOAT;
      break;
    case GPUFloat3:
      result = VK_FORMAT_R32G32B32_SFLOAT;
      break;
    case GPUFloat4:
      result = VK_FORMAT_R32G32B32A32_SFLOAT;
      break;
    case GPUInt:
      result = VK_FORMAT_R32_SINT;
      break;
    case GPUInt2:
      result = VK_FORMAT_R32G32_SINT;
      break;
    case GPUInt3:
      result = VK_FORMAT_R32G32B32_SINT;
      break;
    case GPUInt4:
      result = VK_FORMAT_R32G32B32A32_SINT;
      break;
    case GPUUInt:
      result = VK_FORMAT_R32_UINT;
      break;
    case GPUUInt2:
      result = VK_FORMAT_R32G32_UINT;
      break;
    case GPUUInt3:
      result = VK_FORMAT_R32G32B32_UINT;
      break;
    case GPUUInt4:
      result = VK_FORMAT_R32G32B32A32_UINT;
      break;
    case GPUUChar4Normalized_BGRA:
      result = VK_FORMAT_B8G8R8A8_UNORM;
      break;
    default:
      return false;
  }

  *outFormat = result;
  return true;
}

static VkColorComponentFlags
vk__colorMask(GPUColorWriteMaskFlags mask) {
  VkColorComponentFlags result;

  if (mask == 0u) {
    mask = GPU_COLOR_WRITE_ALL;
  }

  result = 0u;
  if ((mask & GPU_COLOR_WRITE_R) != 0u) result |= VK_COLOR_COMPONENT_R_BIT;
  if ((mask & GPU_COLOR_WRITE_G) != 0u) result |= VK_COLOR_COMPONENT_G_BIT;
  if ((mask & GPU_COLOR_WRITE_B) != 0u) result |= VK_COLOR_COMPONENT_B_BIT;
  if ((mask & GPU_COLOR_WRITE_A) != 0u) result |= VK_COLOR_COMPONENT_A_BIT;
  return result;
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
  GPULibraryVk                      *library;
  GPUPipelineLayoutVk               *layout;
  GPURenderPipelineVk               *native;
  const GPUDepthStencilState        *depthState;
  VkVertexInputBindingDescription   *vertexBindings;
  VkVertexInputAttributeDescription *vertexAttributes;
  VkFormat                           colorFormat;
  VkFormat                           depthFormat;
  VkFormat                           stencilFormat;
  VkPipelineShaderStageCreateInfo    stages[2] = {{0}};
  VkPipelineVertexInputStateCreateInfo vertexInput = {0};
  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0};
  VkPipelineViewportStateCreateInfo viewport = {0};
  VkPipelineRasterizationStateCreateInfo raster = {0};
  VkPipelineMultisampleStateCreateInfo multisample = {0};
  VkPipelineDepthStencilStateCreateInfo depthStencil = {0};
  VkPipelineColorBlendAttachmentState colorBlend = {0};
  VkPipelineColorBlendStateCreateInfo blend = {0};
  VkDynamicState                    dynamicStates[3];
  VkPipelineDynamicStateCreateInfo  dynamic = {0};
  VkPipelineRenderingCreateInfoKHR  rendering = {0};
  VkGraphicsPipelineCreateInfo      pipelineInfo = {0};
  uint32_t                          vertexAttributeCount;

  if (!device || !device->_priv || !info || !pipeline ||
      !info->library || !info->library->_priv ||
      !info->layout || !info->layout->_native ||
      info->colorTargetCount != 1u ||
      (info->multisample.sampleCount != 0u &&
       info->multisample.sampleCount != 1u)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  deviceVk = device->_priv;
  if (!deviceVk->dynamicRendering &&
      info->depthStencilFormat != GPU_FORMAT_UNDEFINED) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!vk_formatFromGPU(info->pColorTargets[0].format, &colorFormat)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  depthFormat   = VK_FORMAT_UNDEFINED;
  stencilFormat = VK_FORMAT_UNDEFINED;
  if (info->depthStencilFormat != GPU_FORMAT_UNDEFINED &&
      !vk_formatFromGPU(info->depthStencilFormat, &depthFormat)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (info->depthStencilFormat == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
      info->depthStencilFormat == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8) {
    stencilFormat = depthFormat;
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
  native->layout = layout->layout;
  if (!deviceVk->dynamicRendering &&
      vk__createPipelineRenderPass(native->device,
                                   colorFormat,
                                   &native->renderPass) != VK_SUCCESS) {
    free(vertexAttributes);
    free(vertexBindings);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = library->module;
  stages[0].pName  = info->vertexEntry;
  stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = library->module;
  stages[1].pName  = info->fragmentEntry;

  vertexInput.sType                           =
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount   =
    info->vertex.bufferLayoutCount;
  vertexInput.pVertexBindingDescriptions      = vertexBindings;
  vertexInput.vertexAttributeDescriptionCount = vertexAttributeCount;
  vertexInput.pVertexAttributeDescriptions    = vertexAttributes;

  inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  viewport.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport.viewportCount = 1u;
  viewport.scissorCount  = 1u;

  raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode    = vk__cullMode(info->cullMode);
  raster.frontFace   = vk__frontFace(info->frontFace);
  raster.lineWidth   = 1.0f;

  multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
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

  colorBlend.colorWriteMask = vk__colorMask(info->pColorTargets[0].blend.writeMask);
  blend.sType               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount     = 1u;
  blend.pAttachments        = &colorBlend;

  dynamicStates[0]       = VK_DYNAMIC_STATE_VIEWPORT;
  dynamicStates[1]       = VK_DYNAMIC_STATE_SCISSOR;
  dynamicStates[2]       = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
  dynamic.sType          = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic.dynamicStateCount = 3u;
  dynamic.pDynamicStates = dynamicStates;

  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
  rendering.colorAttachmentCount = 1u;
  rendering.pColorAttachmentFormats = &colorFormat;
  rendering.depthAttachmentFormat = depthFormat;
  rendering.stencilAttachmentFormat = stencilFormat;

  pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.pNext               = deviceVk->dynamicRendering
                                       ? &rendering
                                       : NULL;
  pipelineInfo.stageCount          = 2u;
  pipelineInfo.pStages             = stages;
  pipelineInfo.pVertexInputState   = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState      = &viewport;
  pipelineInfo.pRasterizationState = &raster;
  pipelineInfo.pMultisampleState   = &multisample;
  pipelineInfo.pDepthStencilState  = &depthStencil;
  pipelineInfo.pColorBlendState    = &blend;
  pipelineInfo.pDynamicState       = &dynamic;
  pipelineInfo.layout              = native->layout;
  pipelineInfo.renderPass          = deviceVk->dynamicRendering
                                       ? VK_NULL_HANDLE
                                       : native->renderPass;
  pipelineInfo.subpass             = 0u;
  if (vkCreateGraphicsPipelines(native->device,
                                VK_NULL_HANDLE,
                                1u,
                                &pipelineInfo,
                                NULL,
                                &native->pipeline) != VK_SUCCESS) {
    free(vertexAttributes);
    free(vertexBindings);
    vkDestroyRenderPass(native->device, native->renderPass, NULL);
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
