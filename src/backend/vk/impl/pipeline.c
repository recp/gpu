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

static VkShaderStageFlags
vk__shaderStages(GPUShaderStageFlags stages) {
  VkShaderStageFlags result;

  result = 0u;
  if ((stages & GPU_SHADER_STAGE_VERTEX_BIT) != 0u) {
    result |= VK_SHADER_STAGE_VERTEX_BIT;
  }
  if ((stages & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0u) {
    result |= VK_SHADER_STAGE_FRAGMENT_BIT;
  }
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
  attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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
  GPUDeviceVk                  *deviceVk;
  GPULibraryVk                 *library;
  GPURenderPipelineVk          *native;
  VkFormat                      colorFormat;
  VkPushConstantRange           pushRange = {0};
  VkPipelineLayoutCreateInfo    layoutInfo = {0};
  VkPipelineShaderStageCreateInfo stages[2] = {{0}};
  VkPipelineVertexInputStateCreateInfo vertexInput = {0};
  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0};
  VkPipelineViewportStateCreateInfo viewport = {0};
  VkPipelineRasterizationStateCreateInfo raster = {0};
  VkPipelineMultisampleStateCreateInfo multisample = {0};
  VkPipelineColorBlendAttachmentState colorBlend = {0};
  VkPipelineColorBlendStateCreateInfo blend = {0};
  VkDynamicState dynamicStates[2];
  VkPipelineDynamicStateCreateInfo dynamic = {0};
  VkGraphicsPipelineCreateInfo pipelineInfo = {0};
  uint32_t pushConstantSize;
  GPUShaderStageFlags pushConstantStages;

  if (!device || !device->_priv || !info || !pipeline ||
      !info->library || !info->library->_priv ||
      requiredBindGroupMask != 0u ||
      info->vertex.bufferLayoutCount != 0u ||
      info->colorTargetCount != 1u ||
      info->depthStencilFormat != GPU_FORMAT_UNDEFINED ||
      (info->multisample.sampleCount != 0u &&
       info->multisample.sampleCount != 1u)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!vk_formatFromGPU(info->pColorTargets[0].format, &colorFormat)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  deviceVk = device->_priv;
  library  = info->library->_priv;
  native   = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->device = deviceVk->device;

  gpuGetPipelineLayoutPushConstants(info->layout,
                                    &pushConstantSize,
                                    &pushConstantStages);
  if (pushConstantSize > 0u) {
    pushRange.stageFlags = vk__shaderStages(pushConstantStages);
    pushRange.size       = pushConstantSize;
    if (pushRange.stageFlags == 0u) {
      free(native);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.pushConstantRangeCount = pushConstantSize > 0u ? 1u : 0u;
  layoutInfo.pPushConstantRanges    = pushConstantSize > 0u ? &pushRange : NULL;
  if (vkCreatePipelineLayout(native->device,
                             &layoutInfo,
                             NULL,
                             &native->layout) != VK_SUCCESS ||
      vk__createPipelineRenderPass(native->device,
                                   colorFormat,
                                   &native->renderPass) != VK_SUCCESS) {
    if (native->layout) {
      vkDestroyPipelineLayout(native->device, native->layout, NULL);
    }
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

  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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

  colorBlend.colorWriteMask = vk__colorMask(info->pColorTargets[0].blend.writeMask);
  blend.sType               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount     = 1u;
  blend.pAttachments        = &colorBlend;

  dynamicStates[0]       = VK_DYNAMIC_STATE_VIEWPORT;
  dynamicStates[1]       = VK_DYNAMIC_STATE_SCISSOR;
  dynamic.sType          = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic.dynamicStateCount = 2u;
  dynamic.pDynamicStates = dynamicStates;

  pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount          = 2u;
  pipelineInfo.pStages             = stages;
  pipelineInfo.pVertexInputState   = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState      = &viewport;
  pipelineInfo.pRasterizationState = &raster;
  pipelineInfo.pMultisampleState   = &multisample;
  pipelineInfo.pColorBlendState    = &blend;
  pipelineInfo.pDynamicState       = &dynamic;
  pipelineInfo.layout              = native->layout;
  pipelineInfo.renderPass          = native->renderPass;
  pipelineInfo.subpass             = 0u;
  if (vkCreateGraphicsPipelines(native->device,
                                VK_NULL_HANDLE,
                                1u,
                                &pipelineInfo,
                                NULL,
                                &native->pipeline) != VK_SUCCESS) {
    vkDestroyRenderPass(native->device, native->renderPass, NULL);
    vkDestroyPipelineLayout(native->device, native->layout, NULL);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

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
    if (native->layout) {
      vkDestroyPipelineLayout(native->device, native->layout, NULL);
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
