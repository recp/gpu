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
#include "../impl.h"
#include "../../../api/buffer_internal.h"
#include "../../../api/texture_internal.h"
#include "../../../api/vrs_internal.h"

enum {
  GPU_VK_BARRIER_CHUNK_SIZE = 16u,
  GPU_VK_TRANSFER_CHUNK_SIZE = 64u * 1024u
};

typedef struct GPUClassicRenderPassVk {
  struct GPUClassicRenderPassVk *next;
  VkRenderPass                   renderPass;
  VkFormat                       colorFormats[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  VkFormat                       depthStencilFormat;
  VkSampleCountFlagBits          sampleCount;
  VkAttachmentLoadOp             colorLoadOps[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  VkAttachmentStoreOp            colorStoreOps[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  VkAttachmentLoadOp             depthLoadOp;
  VkAttachmentStoreOp            depthStoreOp;
  VkAttachmentLoadOp             stencilLoadOp;
  VkAttachmentStoreOp            stencilStoreOp;
  uint32_t                       colorCount;
  uint32_t                       resolveMask;
  uint32_t                       presentMask;
} GPUClassicRenderPassVk;

typedef struct GPUClassicFramebufferVk {
  struct GPUClassicFramebufferVk *next;
  VkFramebuffer                   framebuffer;
  VkRenderPass                    renderPass;
  VkImageView                     colorViews[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  VkImageView                     resolveViews[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  VkImageView                     depthStencilView;
  VkExtent2D                      extent;
  uint32_t                        colorCount;
  uint32_t                        layerCount;
} GPUClassicFramebufferVk;

typedef struct GPUClassicRenderTargetInfoVk {
  const VkFormat    *colorFormats;
  const VkImageView *colorViews;
  const VkImageView *resolveViews;
  const GPULoadOp   *colorLoadOps;
  const GPUStoreOp  *colorStoreOps;
  VkImageView        depthStencilView;
  VkFormat           depthStencilFormat;
  VkExtent2D         extent;
  GPULoadOp          depthLoadOp;
  GPUStoreOp         depthStoreOp;
  GPULoadOp          stencilLoadOp;
  GPUStoreOp         stencilStoreOp;
  uint32_t           colorCount;
  uint32_t           layerCount;
  uint32_t           sampleCount;
  uint32_t           resolveMask;
  uint32_t           presentMask;
} GPUClassicRenderTargetInfoVk;

#ifdef VK_KHR_copy_memory_indirect
_Static_assert(sizeof(GPUIndirectMemoryCopyCommandEXT) == sizeof(VkCopyMemoryIndirectCommandKHR),
               "indirect memory-copy command ABI mismatch");
_Static_assert(offsetof(GPUIndirectMemoryCopyCommandEXT, srcAddress)
                 == offsetof(VkCopyMemoryIndirectCommandKHR, srcAddress)
               && offsetof(GPUIndirectMemoryCopyCommandEXT, dstAddress)
                   == offsetof(VkCopyMemoryIndirectCommandKHR, dstAddress)
               && offsetof(GPUIndirectMemoryCopyCommandEXT, sizeBytes)
                   == offsetof(VkCopyMemoryIndirectCommandKHR, size),
               "indirect memory-copy command layout mismatch");
_Static_assert(sizeof(GPUIndirectTextureSubresourceEXT) == sizeof(VkImageSubresourceLayers),
               "indirect texture subresource ABI mismatch");
_Static_assert(offsetof(GPUIndirectTextureSubresourceEXT, aspectMask) == offsetof(VkImageSubresourceLayers, aspectMask)
               && offsetof(GPUIndirectTextureSubresourceEXT, mipLevel) == offsetof(VkImageSubresourceLayers, mipLevel)
               && offsetof(GPUIndirectTextureSubresourceEXT, baseArrayLayer)
                   == offsetof(VkImageSubresourceLayers, baseArrayLayer)
               && offsetof(GPUIndirectTextureSubresourceEXT, layerCount)
                   == offsetof(VkImageSubresourceLayers, layerCount),
               "indirect texture subresource layout mismatch");
_Static_assert(sizeof(GPUIndirectMemoryToTextureCommandEXT) == sizeof(VkCopyMemoryToImageIndirectCommandKHR),
               "indirect memory-to-texture command ABI mismatch");
_Static_assert(offsetof(GPUIndirectMemoryToTextureCommandEXT, srcAddress)
                 == offsetof(VkCopyMemoryToImageIndirectCommandKHR, srcAddress)
               && offsetof(GPUIndirectMemoryToTextureCommandEXT, bufferRowLength)
                   == offsetof(VkCopyMemoryToImageIndirectCommandKHR, bufferRowLength)
               && offsetof(GPUIndirectMemoryToTextureCommandEXT, bufferImageHeight)
                   == offsetof(VkCopyMemoryToImageIndirectCommandKHR, bufferImageHeight)
               && offsetof(GPUIndirectMemoryToTextureCommandEXT, texture)
                   == offsetof(VkCopyMemoryToImageIndirectCommandKHR, imageSubresource)
               && offsetof(GPUIndirectMemoryToTextureCommandEXT, x)
                   == offsetof(VkCopyMemoryToImageIndirectCommandKHR, imageOffset)
               && offsetof(GPUIndirectMemoryToTextureCommandEXT, width)
                   == offsetof(VkCopyMemoryToImageIndirectCommandKHR, imageExtent),
               "indirect memory-to-texture command layout mismatch");
_Static_assert(GPU_ADDRESS_COPY_DEVICE_LOCAL_BIT_EXT == VK_ADDRESS_COPY_DEVICE_LOCAL_BIT_KHR
               && GPU_ADDRESS_COPY_SPARSE_BIT_EXT == VK_ADDRESS_COPY_SPARSE_BIT_KHR
               && GPU_ADDRESS_COPY_PROTECTED_BIT_EXT == VK_ADDRESS_COPY_PROTECTED_BIT_KHR,
               "indirect address-copy flags mismatch");
_Static_assert(GPU_INDIRECT_TEXTURE_ASPECT_COLOR_BIT_EXT == VK_IMAGE_ASPECT_COLOR_BIT
               && GPU_INDIRECT_TEXTURE_ASPECT_DEPTH_BIT_EXT == VK_IMAGE_ASPECT_DEPTH_BIT
               && GPU_INDIRECT_TEXTURE_ASPECT_STENCIL_BIT_EXT == VK_IMAGE_ASPECT_STENCIL_BIT,
               "indirect texture aspect flags mismatch");
#endif

static const VkAttachmentLoadOp vk_loadOps[] = {
  [GPU_LOAD_OP_LOAD]      = VK_ATTACHMENT_LOAD_OP_LOAD,
  [GPU_LOAD_OP_CLEAR]     = VK_ATTACHMENT_LOAD_OP_CLEAR,
  [GPU_LOAD_OP_DONT_CARE] = VK_ATTACHMENT_LOAD_OP_DONT_CARE
};

static VkAttachmentLoadOp
vk__loadOp(GPULoadOp op) {
  return (uint32_t)op < GPU_ARRAY_LEN(vk_loadOps) ? vk_loadOps[op] : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

static VkAttachmentStoreOp
vk__storeOp(GPUStoreOp op) {
  return op == GPU_STORE_OP_STORE ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

static void
vk__lockClassicRenderTargets(GPUDeviceVk *device) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&device->classicRenderLock);
#else
  pthread_mutex_lock(&device->classicRenderLock);
#endif
}

static void
vk__unlockClassicRenderTargets(GPUDeviceVk *device) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&device->classicRenderLock);
#else
  pthread_mutex_unlock(&device->classicRenderLock);
#endif
}

static GPUClassicRenderPassVk*
vk__findClassicRenderPass(GPUDeviceVk                        *device,
                          const GPUClassicRenderTargetInfoVk *info,
                          VkSampleCountFlagBits               sampleCount,
                          VkAttachmentLoadOp                  depthLoadOp,
                          VkAttachmentStoreOp                 depthStoreOp,
                          VkAttachmentLoadOp                  stencilLoadOp,
                          VkAttachmentStoreOp                 stencilStoreOp) {
  GPUClassicRenderPassVk *entry;
  uint32_t                i;
  bool                    matches;

  for (entry = device->classicRenderPasses; entry; entry = entry->next) {
    matches = entry->depthStencilFormat == info->depthStencilFormat && entry->sampleCount == sampleCount
              && entry->depthLoadOp == depthLoadOp
              && entry->depthStoreOp == depthStoreOp
              && entry->stencilLoadOp == stencilLoadOp
              && entry->stencilStoreOp == stencilStoreOp
              && entry->colorCount == info->colorCount
              && entry->resolveMask == info->resolveMask
              && entry->presentMask == info->presentMask;

    for (i = 0u; matches && i < info->colorCount; i++) {
      matches = entry->colorFormats[i] == info->colorFormats[i]
                && entry->colorLoadOps[i] == vk__loadOp(info->colorLoadOps[i])
                && entry->colorStoreOps[i] == vk__storeOp(info->colorStoreOps[i]);
    }

    if (matches) {
      return entry;
    }
  }

  return NULL;
}

static GPUClassicRenderPassVk*
vk__createClassicRenderPass(GPUDeviceVk                        *device,
                            const GPUClassicRenderTargetInfoVk *target,
                            VkSampleCountFlagBits               sampleCount,
                            VkAttachmentLoadOp                  depthLoadOp,
                            VkAttachmentStoreOp                 depthStoreOp,
                            VkAttachmentLoadOp                  stencilLoadOp,
                            VkAttachmentStoreOp                 stencilStoreOp) {
  VkAttachmentDescription  attachments[GPU_VK_MAX_RENDER_ATTACHMENTS]         = { {0} };
  VkAttachmentReference    colors[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS]   = { {0} };
  VkAttachmentReference    resolves[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS] = { {0} };
  VkAttachmentReference    depthStencil                                       = {0};
  VkSubpassDescription     subpass                                            = {0};
  VkSubpassDependency      dependency                                         = {0};
  VkRenderPassCreateInfo   info                                               = {0};
  GPUClassicRenderPassVk  *entry;
  VkAttachmentDescription *colorAttachment;
  VkAttachmentDescription *resolveAttachment;
  uint32_t                 attachmentCount;
  uint32_t                 depthIndex;
  uint32_t                 colorIndex;
  uint32_t                 resolveIndex;
  uint32_t                 cacheIndex;
  bool                     resolvesColor;
  bool                     presentsColor;

  if (!(entry = calloc(1, sizeof(*entry)))) {
    return NULL;
  }

  attachmentCount = 0u;

  for (colorIndex = 0u; colorIndex < target->colorCount; colorIndex++) {
    resolvesColor                   = (target->resolveMask & (1u << colorIndex)) != 0u;
    presentsColor                   = (target->presentMask & (1u << colorIndex)) != 0u;
    colorAttachment                 = &attachments[attachmentCount];
    colorAttachment->format         = target->colorFormats[colorIndex];
    colorAttachment->samples        = sampleCount;
    colorAttachment->loadOp         = vk__loadOp(target->colorLoadOps[colorIndex]);
    colorAttachment->storeOp        = vk__storeOp(target->colorStoreOps[colorIndex]);
    colorAttachment->stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment->initialLayout  = colorAttachment->loadOp == VK_ATTACHMENT_LOAD_OP_LOAD
                                          ? (presentsColor && !resolvesColor ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                                             : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                                          : VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment->finalLayout    =
           presentsColor && !resolvesColor ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colors[colorIndex].attachment   = attachmentCount++;
    colors[colorIndex].layout       = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    resolves[colorIndex].attachment = VK_ATTACHMENT_UNUSED;
    resolves[colorIndex].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }

  for (resolveIndex = 0u; resolveIndex < target->colorCount; resolveIndex++) {
    if ((target->resolveMask & (1u << resolveIndex)) == 0u) {
      continue;
    }

    resolveAttachment                 = &attachments[attachmentCount];
    resolveAttachment->format         = target->colorFormats[resolveIndex];
    resolveAttachment->samples        = VK_SAMPLE_COUNT_1_BIT;
    resolveAttachment->loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolveAttachment->storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    resolveAttachment->stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolveAttachment->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    resolveAttachment->initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    resolveAttachment->finalLayout    = (target->presentMask & (1u << resolveIndex)) != 0u
                                            ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    resolves[resolveIndex].attachment = attachmentCount++;
  }

  if (target->depthStencilFormat != VK_FORMAT_UNDEFINED) {
    depthIndex                             = attachmentCount++;
    attachments[depthIndex].format         = target->depthStencilFormat;
    attachments[depthIndex].samples        = sampleCount;
    attachments[depthIndex].loadOp         = depthLoadOp;
    attachments[depthIndex].storeOp        = depthStoreOp;
    attachments[depthIndex].stencilLoadOp  = stencilLoadOp;
    attachments[depthIndex].stencilStoreOp = stencilStoreOp;
    attachments[depthIndex].initialLayout  =
         depthLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD || stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD
             ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
             : VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[depthIndex].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthStencil.attachment                = depthIndex;
    depthStencil.layout                    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    subpass.pDepthStencilAttachment        = &depthStencil;
  }

  subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = target->colorCount;
  subpass.pColorAttachments    = target->colorCount ? colors : NULL;
  subpass.pResolveAttachments  = target->resolveMask ? resolves : NULL;

  dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass    = 0u;
  dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                             | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask  = dependency.srcStageMask;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                             | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                             | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = attachmentCount;
  info.pAttachments    = attachments;
  info.subpassCount    = 1u;
  info.pSubpasses      = &subpass;
  info.dependencyCount = 1u;
  info.pDependencies   = &dependency;

  if (vkCreateRenderPass(device->device,
                         &info,
                         NULL,
                         &entry->renderPass) != VK_SUCCESS) {
    free(entry);
    return NULL;
  }

  entry->depthStencilFormat = target->depthStencilFormat;
  entry->sampleCount        = sampleCount;
  entry->depthLoadOp        = depthLoadOp;
  entry->depthStoreOp       = depthStoreOp;
  entry->stencilLoadOp      = stencilLoadOp;
  entry->stencilStoreOp     = stencilStoreOp;
  entry->colorCount         = target->colorCount;
  entry->resolveMask        = target->resolveMask;
  entry->presentMask        = target->presentMask;

  for (cacheIndex = 0u; cacheIndex < target->colorCount; cacheIndex++) {
    entry->colorFormats[cacheIndex]  = target->colorFormats[cacheIndex];
    entry->colorLoadOps[cacheIndex]  = vk__loadOp(target->colorLoadOps[cacheIndex]);
    entry->colorStoreOps[cacheIndex] = vk__storeOp(target->colorStoreOps[cacheIndex]);
  }

  entry->next                 = device->classicRenderPasses;
  device->classicRenderPasses = entry;

  return entry;
}

static GPUResult
vk__getClassicRenderTarget(GPUDeviceVk                        *device,
                           const GPUClassicRenderTargetInfoVk *target,
                           VkRenderPass                       *outRenderPass,
                           VkFramebuffer                      *outFramebuffer) {
  VkImageView              attachments[GPU_VK_MAX_RENDER_ATTACHMENTS];
  VkFramebufferCreateInfo  info = {0};
  GPUClassicRenderPassVk  *pass;
  GPUClassicFramebufferVk *framebuffer;
  VkSampleCountFlagBits    nativeSampleCount;
  VkAttachmentLoadOp       nativeDepthLoad;
  VkAttachmentStoreOp      nativeDepthStore;
  VkAttachmentLoadOp       nativeStencilLoad;
  VkAttachmentStoreOp      nativeStencilStore;
  uint32_t                 validColorMask;
  uint32_t                 attachmentCount;
  uint32_t                 validationIndex;
  uint32_t                 matchIndex;
  uint32_t                 attachmentIndex;
  uint32_t                 resolveIndex;
  uint32_t                 cacheIndex;
  bool                     hasResolve;
  bool                     matches;

  if (!device || !device->classicRenderLockInitialized || !target
      || target->colorCount > GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS
      || (target->colorCount > 0u
          && (!target->colorFormats || !target->colorViews || !target->resolveViews || !target->colorLoadOps
              || !target->colorStoreOps))
      || (target->colorCount == 0u && !target->depthStencilView)
      || (!!target->depthStencilView != (target->depthStencilFormat != VK_FORMAT_UNDEFINED))
      || target->extent.width == 0u
      || target->extent.height == 0u
      || target->layerCount == 0u
      || !outRenderPass
      || !outFramebuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  switch (target->sampleCount) {
    case 1u:
      nativeSampleCount = VK_SAMPLE_COUNT_1_BIT;
      break;
    case 2u:
      nativeSampleCount = VK_SAMPLE_COUNT_2_BIT;
      break;
    case 4u:
      nativeSampleCount = VK_SAMPLE_COUNT_4_BIT;
      break;
    case 8u:
      nativeSampleCount = VK_SAMPLE_COUNT_8_BIT;
      break;
    default:
      return GPU_ERROR_INVALID_ARGUMENT;
  }

  validColorMask = target->colorCount ? (1u << target->colorCount) - 1u : 0u;

  if ((target->resolveMask | target->presentMask) & ~validColorMask) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if ((nativeSampleCount == VK_SAMPLE_COUNT_1_BIT && target->resolveMask != 0u)
      || (nativeSampleCount > VK_SAMPLE_COUNT_1_BIT && target->resolveMask != validColorMask)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  for (validationIndex = 0u; validationIndex < target->colorCount; validationIndex++) {
    hasResolve = (target->resolveMask & (1u << validationIndex)) != 0u;

    if (!target->colorViews[validationIndex] || target->colorFormats[validationIndex] == VK_FORMAT_UNDEFINED
        || (!!target->resolveViews[validationIndex] != hasResolve)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  nativeDepthLoad    = target->depthStencilView ? vk__loadOp(target->depthLoadOp) : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  nativeDepthStore   = target->depthStencilView ? vk__storeOp(target->depthStoreOp) : VK_ATTACHMENT_STORE_OP_DONT_CARE;
  nativeStencilLoad  = target->depthStencilView ? vk__loadOp(target->stencilLoadOp) : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  nativeStencilStore =
      target->depthStencilView ? vk__storeOp(target->stencilStoreOp) : VK_ATTACHMENT_STORE_OP_DONT_CARE;

  vk__lockClassicRenderTargets(device);
  pass = vk__findClassicRenderPass(device,
                                   target,
                                   nativeSampleCount,
                                   nativeDepthLoad,
                                   nativeDepthStore,
                                   nativeStencilLoad,
                                   nativeStencilStore);

  if (!pass) {
    pass = vk__createClassicRenderPass(device,
                                       target,
                                       nativeSampleCount,
                                       nativeDepthLoad,
                                       nativeDepthStore,
                                       nativeStencilLoad,
                                       nativeStencilStore);
  }

  if (!pass) {
    vk__unlockClassicRenderTargets(device);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  for (framebuffer = device->classicFramebuffers; framebuffer; framebuffer = framebuffer->next) {
    matches = framebuffer->renderPass == pass->renderPass && framebuffer->depthStencilView == target->depthStencilView
              && framebuffer->extent.width == target->extent.width
              && framebuffer->extent.height == target->extent.height
              && framebuffer->colorCount == target->colorCount
              && framebuffer->layerCount == target->layerCount;

    for (matchIndex = 0u; matches && matchIndex < target->colorCount; matchIndex++) {
      matches = framebuffer->colorViews[matchIndex] == target->colorViews[matchIndex]
                && framebuffer->resolveViews[matchIndex] == target->resolveViews[matchIndex];
    }

    if (matches) {
      *outRenderPass  = pass->renderPass;
      *outFramebuffer = framebuffer->framebuffer;
      vk__unlockClassicRenderTargets(device);
      return GPU_OK;
    }
  }

  if (!(framebuffer = calloc(1, sizeof(*framebuffer)))) {
    vk__unlockClassicRenderTargets(device);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  attachmentCount = 0u;

  for (attachmentIndex = 0u; attachmentIndex < target->colorCount; attachmentIndex++) {
    attachments[attachmentCount++] = target->colorViews[attachmentIndex];
  }

  for (resolveIndex = 0u; resolveIndex < target->colorCount; resolveIndex++) {
    if (target->resolveViews[resolveIndex]) {
      attachments[attachmentCount++] = target->resolveViews[resolveIndex];
    }
  }

  if (target->depthStencilView) {
    attachments[attachmentCount++] = target->depthStencilView;
  }

  info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  info.renderPass      = pass->renderPass;
  info.attachmentCount = attachmentCount;
  info.pAttachments    = attachments;
  info.width           = target->extent.width;
  info.height          = target->extent.height;
  info.layers          = target->layerCount;

  if (vkCreateFramebuffer(device->device,
                          &info,
                          NULL,
                          &framebuffer->framebuffer) != VK_SUCCESS) {
    free(framebuffer);
    vk__unlockClassicRenderTargets(device);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  framebuffer->renderPass       = pass->renderPass;
  framebuffer->depthStencilView = target->depthStencilView;
  framebuffer->extent           = target->extent;
  framebuffer->colorCount       = target->colorCount;
  framebuffer->layerCount       = target->layerCount;

  for (cacheIndex = 0u; cacheIndex < target->colorCount; cacheIndex++) {
    framebuffer->colorViews[cacheIndex]   = target->colorViews[cacheIndex];
    framebuffer->resolveViews[cacheIndex] = target->resolveViews[cacheIndex];
  }

  framebuffer->next           = device->classicFramebuffers;
  device->classicFramebuffers = framebuffer;
  *outRenderPass              = pass->renderPass;
  *outFramebuffer             = framebuffer->framebuffer;
  vk__unlockClassicRenderTargets(device);

  return GPU_OK;
}

static VkClearColorValue
vk__clearColor(const GPUClearColorValue *color, GPUFormat format) {
  VkClearColorValue result;

  switch (gpuFormatNumericType(format)) {
    case GPU_FORMAT_NUMERIC_UINT:
      memcpy(result.uint32, color->uint32, sizeof(result.uint32));
      break;
    case GPU_FORMAT_NUMERIC_SINT:
      memcpy(result.int32, color->sint32, sizeof(result.int32));
      break;
    default:
      memcpy(result.float32, color->float32, sizeof(result.float32));
      break;
  }

  return result;
}

static void
vk__layoutAccess(VkImageLayout layout, VkPipelineStageFlags *outStage, VkAccessFlags *outAccess) {
  switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
      *outStage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      *outAccess = 0u;
      break;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      *outStage  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
      *outAccess = 0u;
      break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      *outAccess = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      *outAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      *outAccess = VK_ACCESS_SHADER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
      *outAccess = VK_ACCESS_TRANSFER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
      *outAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;
#ifdef VK_KHR_fragment_shading_rate
    case VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR:
      *outStage  = VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
      *outAccess = VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
      break;
#endif
    default:
      *outStage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      *outAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
      break;
  }
}

static GPURenderPassDesc*
vk_beginDynamicRenderPass(GPUCommandBuffer              *cmdb,
                          const GPURenderPassCreateInfo *info,
                          GPUCommandBufferVk            *command,
                          GPUDeviceVk                   *device) {
  GPURenderPassDesc                          *pass;
  GPURenderPassVk                            *native;
  const GPUShadingRateAttachmentEXT          *shadingRate;
  const GPURasterizationRateMapRenderPassEXT *rateMap;
  const GPURenderPassColorAttachment         *colorAttachment;
  GPUTextureViewVk                           *colorView;
  GPUTextureViewVk                           *resolveView;
  GPUSwapchainVk                             *swapchain;
  VkRenderingAttachmentInfoKHR               *nativeAttachment;
  const GPURenderPassDepthStencilAttachment  *depthAttachment;
  GPUTextureViewVk                           *depthView;
#ifdef VK_KHR_fragment_shading_rate
  GPUTextureViewVk                           *shadingView;
#endif
  uint32_t                                    layerCount;
  uint32_t                                    i;
#ifdef VK_KHR_fragment_shading_rate
  uint32_t                                    minWidth;
  uint32_t                                    minHeight;
  uint32_t                                    maxAxis;
  uint32_t                                    minAxis;
#endif
  bool                                        hasStencil;

  if (!cmdb || !info || !command || !device || !device->dynamicRendering
      || info->colorAttachmentCount > GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS) {
    return NULL;
  }

  pass       = &command->renderPass;
  native     = &command->renderPassState;
  layerCount = 0u;
  memset(pass, 0, sizeof(*pass));
  memset(native, 0, sizeof(*native));

  if (!gpuRenderPassVRSExtensions(info, &shadingRate, &rateMap) || rateMap) {
    return NULL;
  }

  for (i = 0u; i < info->colorAttachmentCount; i++) {
    colorAttachment = &info->pColorAttachments[i];
    colorView       = colorAttachment->view ? colorAttachment->view->_priv : NULL;
    resolveView     = colorAttachment->resolveView ? colorAttachment->resolveView->_priv : NULL;
    swapchain       = colorView ? colorView->swapchain : NULL;

    if (!colorView || !colorView->view || !colorView->image || !colorView->layout || colorView->extent.width == 0u
        || colorView->extent.height == 0u
        || colorView->layerCount == 0u
        || (swapchain && (!swapchain->frameActive || colorView->imageIndex != swapchain->acquiredImageIndex))
        || (native->extent.width > 0u
            && (native->extent.width != colorView->extent.width || native->extent.height != colorView->extent.height
                || layerCount != colorView->layerCount))) {
      return NULL;
    }

    if (resolveView
        && (!resolveView->view || !resolveView->image || !resolveView->layout
            || resolveView->extent.width != colorView->extent.width
            || resolveView->extent.height != colorView->extent.height
            || resolveView->layerCount != colorView->layerCount)) {
      return NULL;
    }

    vk_transitionView(command->command,
                      colorView,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    nativeAttachment              = &native->colorAttachments[i];
    nativeAttachment->sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    nativeAttachment->imageView   = colorView->view;
    nativeAttachment->imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    nativeAttachment->loadOp      = vk__loadOp(colorAttachment->loadOp);
    nativeAttachment->storeOp     = vk__storeOp(colorAttachment->storeOp);

    if (resolveView) {
      vk_transitionView(command->command,
                        resolveView,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      nativeAttachment->resolveMode        = VK_RESOLVE_MODE_AVERAGE_BIT;
      nativeAttachment->resolveImageView   = resolveView->view;
      nativeAttachment->resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    nativeAttachment->clearValue.color = vk__clearColor(&colorAttachment->clearColor, colorAttachment->view->format);
    native->colorViews[i]              = colorView;
    native->resolveViews[i]            = resolveView;
    native->extent                     = colorView->extent;
    native->swapchain                  = swapchain;
    layerCount                         = colorView->layerCount;
  }

  if (info->pDepthStencilAttachment) {
    depthAttachment = info->pDepthStencilAttachment;
    depthView       = depthAttachment->view ? depthAttachment->view->_priv : NULL;

    if (!depthView || !depthView->view || !depthView->image || !depthView->layout || depthView->extent.width == 0u
        || depthView->extent.height == 0u
        || depthView->layerCount == 0u
        || (native->extent.width > 0u
            && (native->extent.width != depthView->extent.width || native->extent.height != depthView->extent.height
                || layerCount != depthView->layerCount))) {
      return NULL;
    }

    vk_transitionView(command->command,
                      depthView,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    if (depthAttachment->view->format != GPU_FORMAT_STENCIL8) {
      native->depthAttachment.sType                           = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
      native->depthAttachment.imageView                       = depthView->view;
      native->depthAttachment.imageLayout                     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      native->depthAttachment.loadOp                          = vk__loadOp(depthAttachment->depthLoadOp);
      native->depthAttachment.storeOp                         = vk__storeOp(depthAttachment->depthStoreOp);
      native->depthAttachment.clearValue.depthStencil.depth   = depthAttachment->clearDepth;
      native->depthAttachment.clearValue.depthStencil.stencil = depthAttachment->clearStencil;
    }

    native->depthStencilView = depthView;
    native->extent           = depthView->extent;
    layerCount               = depthView->layerCount;

    hasStencil = depthAttachment->view->format == GPU_FORMAT_STENCIL8
                 || depthAttachment->view->format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8
                 || depthAttachment->view->format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;

    if (hasStencil) {
      native->stencilAttachment.sType                           = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
      native->stencilAttachment.imageView                       = depthView->view;
      native->stencilAttachment.imageLayout                     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      native->stencilAttachment.loadOp                          = vk__loadOp(depthAttachment->stencilLoadOp);
      native->stencilAttachment.storeOp                         = vk__storeOp(depthAttachment->stencilStoreOp);
      native->stencilAttachment.clearValue.depthStencil.depth   = depthAttachment->clearDepth;
      native->stencilAttachment.clearValue.depthStencil.stencil = depthAttachment->clearStencil;
    }
  }

  if (native->extent.width == 0u || native->extent.height == 0u || layerCount == 0u) {
    return NULL;
  }
#ifdef VK_KHR_fragment_shading_rate
  if (shadingRate) {
    shadingView = shadingRate->view ? shadingRate->view->_priv : NULL;

    if (shadingRate->texelSize.width == 0u || shadingRate->texelSize.height == 0u
        || (shadingRate->texelSize.width & (shadingRate->texelSize.width - 1u)) != 0u
        || (shadingRate->texelSize.height & (shadingRate->texelSize.height - 1u)) != 0u) {
      return NULL;
    }

    maxAxis   = shadingRate->texelSize.width > shadingRate->texelSize.height ? shadingRate->texelSize.width
                                                                             : shadingRate->texelSize.height;
    minAxis   = shadingRate->texelSize.width < shadingRate->texelSize.height ? shadingRate->texelSize.width
                                                                             : shadingRate->texelSize.height;
    minWidth  = (native->extent.width - 1u) / shadingRate->texelSize.width + 1u;
    minHeight = (native->extent.height - 1u) / shadingRate->texelSize.height + 1u;

    if (!device->vrsAttachment || !shadingView || !shadingView->view || !shadingView->image || !shadingView->layout
        || shadingRate->texelSize.width < device->minVRSTexelSize.width
        || shadingRate->texelSize.height < device->minVRSTexelSize.height
        || shadingRate->texelSize.width > device->maxVRSTexelSize.width
        || shadingRate->texelSize.height > device->maxVRSTexelSize.height
        || minAxis == 0u
        || (device->maxVRSTexelAspectRatio > 0u && maxAxis > device->maxVRSTexelAspectRatio * minAxis)
        || shadingView->extent.width < minWidth
        || shadingView->extent.height < minHeight) {
      return NULL;
    }

    vk_transitionView(command->command,
                      shadingView,
                      VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR);
    native->shadingRateView                   = shadingView;
    native->shadingRateAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR;
    native->shadingRateAttachment.imageView   = shadingView->view;
    native->shadingRateAttachment.imageLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;

    native->shadingRateAttachment.shadingRateAttachmentTexelSize.width  = shadingRate->texelSize.width;
    native->shadingRateAttachment.shadingRateAttachmentTexelSize.height = shadingRate->texelSize.height;
  }
#else
  if (shadingRate) {
    return NULL;
  }
#endif
  if (info->occlusionQuerySet) {
    vk_resetQuerySet(cmdb, info->occlusionQuerySet);
  }

  native->colorCount                         = info->colorAttachmentCount;
  native->dynamic                            = true;
  native->renderingInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
  native->renderingInfo.pNext                = shadingRate ? &native->shadingRateAttachment : NULL;
  native->renderingInfo.renderArea.extent    = native->extent;
  native->renderingInfo.layerCount           = layerCount;
  native->renderingInfo.colorAttachmentCount = native->colorCount;
  native->renderingInfo.pColorAttachments    = native->colorCount > 0u ? native->colorAttachments : NULL;
  native->renderingInfo.pDepthAttachment     = native->depthAttachment.imageView ? &native->depthAttachment : NULL;
  native->renderingInfo.pStencilAttachment   = native->stencilAttachment.imageView ? &native->stencilAttachment : NULL;
  pass->_priv                                = native;
  pass->label                                = info->label;

  return pass;
}

static GPUCommandBufferVk*
vk__copyCommand(GPUTransferPassEncoder *pass) {
  return pass ? pass->_priv : NULL;
}

static GPUTransferPassEncoder*
vk_beginTransferPass(GPUCommandBuffer *cmdb, const char *label) {
  GPUCommandBufferVk     *command;
  GPUTransferPassEncoder *pass;

  command = cmdb ? cmdb->_priv : NULL;

  if (!command || !command->command) {
    return NULL;
  }

  pass = &command->copyEncoder;
  memset(pass, 0, sizeof(*pass));
  pass->_priv                   = command;
  command->copyDebugLabelActive = vk_beginDebugLabel(gpuCommandBufferDevice(cmdb),
                                                     command->command,
                                                     label);

  return pass;
}

static void
vk_copyBufferToBuffer(GPUTransferPassEncoder *pass, GPUBuffer *src, GPUBuffer *dst, const GPUBufferCopyRegion *region) {
  VkBufferCopy        copy = {0};
  GPUCommandBufferVk *command;
  GPUBufferVk        *srcVk;
  GPUBufferVk        *dstVk;

  command = vk__copyCommand(pass);
  srcVk   = src ? src->_priv : NULL;
  dstVk   = dst ? dst->_priv : NULL;

  if (!command || !srcVk || !dstVk || !region) {
    return;
  }

  copy.srcOffset = region->srcOffset;
  copy.dstOffset = region->dstOffset;
  copy.size      = region->sizeBytes;
  vkCmdCopyBuffer(command->command, srcVk->buffer, dstVk->buffer, 1u, &copy);
}

static bool
vk__copyAspect(GPUFormat format, GPUTextureAspect aspect, VkImageAspectFlags *outAspect) {
  GPUTextureAspect resolved;

  if (!outAspect || !gpuFormatResolveCopyAspect(format, aspect, &resolved)) {
    return false;
  }

  switch (resolved) {
    case GPU_TEXTURE_ASPECT_DEPTH_ONLY:
      *outAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
      return true;
    case GPU_TEXTURE_ASPECT_STENCIL_ONLY:
      *outAspect = VK_IMAGE_ASPECT_STENCIL_BIT;
      return true;
    case GPU_TEXTURE_ASPECT_ALL:
      *outAspect = VK_IMAGE_ASPECT_COLOR_BIT;
      return true;
    default:
      return false;
  }
}

static bool
vk__bufferImageCopy(GPUTexture *texture, const GPUBufferTextureCopyRegion *region, VkBufferImageCopy *outCopy) {
  GPUFormatLayout    formatLayout;
  VkImageAspectFlags aspect;
  uint32_t           rowBlocks;
  uint32_t           rowLength;

  if (!texture || !texture->_priv || !region || !outCopy
      || !vk__copyAspect(texture->format,
                         region->texture.texture.aspect,
                         &aspect)
      || !gpuFormatAspectLayout(texture->format,
                                region->texture.texture.aspect,
                                &formatLayout)
      || region->bytesPerRow % formatLayout.bytesPerBlock != 0u) {
    return false;
  }

  rowBlocks = region->bytesPerRow / formatLayout.bytesPerBlock;

  if (rowBlocks > UINT32_MAX / formatLayout.blockWidth) {
    return false;
  }

  rowLength = rowBlocks * formatLayout.blockWidth;

  memset(outCopy, 0, sizeof(*outCopy));
  outCopy->bufferOffset                    = region->bufferOffset;
  outCopy->bufferRowLength                 = rowLength;
  outCopy->bufferImageHeight               = region->rowsPerImage;
  outCopy->imageSubresource.aspectMask     = aspect;
  outCopy->imageSubresource.mipLevel       = region->texture.texture.mipLevel;
  outCopy->imageSubresource.baseArrayLayer = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                                               ? 0u
                                               : region->texture.texture.baseArrayLayer;
  outCopy->imageSubresource.layerCount     = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                                               ? 1u
                                               : region->texture.layerCount;
  outCopy->imageOffset.x                   = (int32_t)region->texture.texture.x;
  outCopy->imageOffset.y                   = (int32_t)region->texture.texture.y;
  outCopy->imageOffset.z                   = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                                               ? (int32_t)region->texture.texture.z
                                               : 0;
  outCopy->imageExtent.width               = region->texture.width;
  outCopy->imageExtent.height              = region->texture.height;
  outCopy->imageExtent.depth               = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                                               ? region->texture.depth
                                               : 1u;

  return true;
}

static void
vk_copyBufferToTexture(GPUTransferPassEncoder           *pass,
                       GPUBuffer                        *src,
                       GPUTexture                       *dst,
                       const GPUBufferTextureCopyRegion *region) {
  VkBufferImageCopy   copy;
  GPUCommandBufferVk *command;
  GPUBufferVk        *buffer;
  GPUTextureVk       *texture;

  command = vk__copyCommand(pass);
  buffer  = src ? src->_priv : NULL;
  texture = dst ? dst->_priv : NULL;

  if (!command || !buffer || !texture
      || !vk__bufferImageCopy(dst,
                              region,
                              &copy)) {
    return;
  }

  (void)vk_transitionTexture(command->command,
                             texture,
                             region->texture.texture.mipLevel,
                             1u,
                             dst->dimension == GPU_TEXTURE_DIMENSION_3D ? 0u : region->texture.texture.baseArrayLayer,
                             dst->dimension == GPU_TEXTURE_DIMENSION_3D ? 1u : region->texture.layerCount,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  vkCmdCopyBufferToImage(command->command,
                         buffer->buffer,
                         texture->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1u,
                         &copy);
}

static void
vk_copyTextureToBuffer(GPUTransferPassEncoder           *pass,
                       GPUTexture                       *src,
                       GPUBuffer                        *dst,
                       const GPUBufferTextureCopyRegion *region) {
  VkBufferImageCopy   copy;
  GPUCommandBufferVk *command;
  GPUTextureVk       *texture;
  GPUBufferVk        *buffer;

  command = vk__copyCommand(pass);
  texture = src ? src->_priv : NULL;
  buffer  = dst ? dst->_priv : NULL;

  if (!command || !texture || !buffer
      || !vk__bufferImageCopy(src,
                              region,
                              &copy)) {
    return;
  }

  (void)vk_transitionTexture(command->command,
                             texture,
                             region->texture.texture.mipLevel,
                             1u,
                             src->dimension == GPU_TEXTURE_DIMENSION_3D ? 0u : region->texture.texture.baseArrayLayer,
                             src->dimension == GPU_TEXTURE_DIMENSION_3D ? 1u : region->texture.layerCount,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  vkCmdCopyImageToBuffer(command->command,
                         texture->image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         buffer->buffer,
                         1u,
                         &copy);
}

#ifdef __APPLE__

static bool
vk__reserveScratch(GPUTransferPassEncoder *pass,
                   uint64_t                sizeBytes,
                   uint64_t                alignment,
                   VkBuffer               *outBuffer,
                   VkDeviceSize           *outOffset) {
  GPUBufferCreateInfo info = {0};
  GPUCommandBufferVk *command;
  GPUTransferChunkVk *chunk;
  GPUTransferChunkVk *candidate;
  GPUBufferVk        *bufferVk;
  uint64_t            alignedOffset;
  uint64_t            capacity;
  uint64_t            offset;

  if (!pass || !pass->_cmdb || !pass->_cmdb->_queue || !pass->_cmdb->_queue->_device || sizeBytes == 0u
      || alignment == 0u
      || (alignment & (alignment - 1u)) != 0u
      || !outBuffer
      || !outOffset) {
    return false;
  }

  *outBuffer = VK_NULL_HANDLE;
  *outOffset = 0u;

  command       = pass->_cmdb->_priv;
  chunk         = NULL;
  alignedOffset = 0u;

  if (!command) {
    return false;
  }

  for (candidate = command->transferChunks; candidate; candidate = candidate->next) {
    if (candidate->offset > UINT64_MAX - (alignment - 1u)) {
      continue;
    }

    offset = (candidate->offset + alignment - 1u) & ~(alignment - 1u);

    if (offset <= candidate->capacity && sizeBytes <= candidate->capacity - offset) {
      chunk         = candidate;
      alignedOffset = offset;
      break;
    }
  }

  if (!chunk) {
    if (sizeBytes > UINT64_MAX - (alignment - 1u)) {
      return false;
    }

    capacity = sizeBytes > GPU_VK_TRANSFER_CHUNK_SIZE ? (sizeBytes + alignment - 1u) & ~(alignment - 1u)
                                                      : GPU_VK_TRANSFER_CHUNK_SIZE;

    if (!(chunk = calloc(1, sizeof(*chunk)))) {
      return false;
    }

    info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.chain.structSize = sizeof(info);
    info.label            = "vulkan-transfer-scratch";
    info.sizeBytes        = capacity;
    info.usage            = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

    if (GPUCreateBuffer(pass->_cmdb->_queue->_device,
                        &info,
                        &chunk->buffer) != GPU_OK) {
      free(chunk);
      return false;
    }

    chunk->capacity         = capacity;
    chunk->next             = command->transferChunks;
    command->transferChunks = chunk;
    gpuDeviceRecordHotPathAlloc(pass->_cmdb->_queue->_device,
                                sizeof(*chunk) + capacity);
  }

  bufferVk = chunk->buffer->_priv;

  if (!bufferVk || !bufferVk->buffer) {
    return false;
  }

  chunk->offset = alignedOffset + sizeBytes;
  *outBuffer    = bufferVk->buffer;
  *outOffset    = alignedOffset;

  return true;
}

static bool
vk__copyDepthStencilPlane(GPUTransferPassEncoder              *pass,
                          GPUTexture                          *src,
                          GPUTexture                          *dst,
                          const GPUTextureToTextureCopyRegion *region,
                          VkImageAspectFlags                   aspect) {
  GPUFormatLayout       layout;
  VkBufferImageCopy     copy    = {0};
  VkBufferMemoryBarrier barrier = {0};
  GPUCommandBufferVk   *command;
  GPUTextureVk         *srcVk;
  GPUTextureVk         *dstVk;
  VkBuffer              scratch;
  VkDeviceSize          scratchOffset;
  uint64_t              rowBytes;
  uint64_t              rowPitch;
  uint64_t              imageBytes;
  uint64_t              scratchBytes;
  uint64_t              rowLength;
  bool                  texture3D;

  command   = vk__copyCommand(pass);
  srcVk     = src ? src->_priv : NULL;
  dstVk     = dst ? dst->_priv : NULL;
  texture3D = src && src->dimension == GPU_TEXTURE_DIMENSION_3D;

  if (!command || !srcVk || !dstVk || !region
      || !gpuFormatAspectLayout(src->format,
                                region->src.aspect,
                                &layout)
      || layout.blockWidth != 1u
      || layout.blockHeight != 1u
      || region->width > UINT64_MAX / layout.bytesPerBlock) {
    return false;
  }

  rowBytes = (uint64_t)region->width * layout.bytesPerBlock;

  if (rowBytes > UINT64_MAX - 255u) {
    return false;
  }

  rowPitch  = (rowBytes + 255u) & ~UINT64_C(255);
  rowLength = rowPitch / layout.bytesPerBlock;

  if (rowLength > UINT32_MAX) {
    return false;
  }

  if (region->height > UINT64_MAX / rowPitch) {
    return false;
  }

  imageBytes = rowPitch * region->height;

  if (region->depth > UINT64_MAX / imageBytes) {
    return false;
  }

  scratchBytes = imageBytes * region->depth;

  if (region->layerCount > UINT64_MAX / scratchBytes) {
    return false;
  }

  scratchBytes *= region->layerCount;

  if (!vk__reserveScratch(pass,
                          scratchBytes,
                          256u,
                          &scratch,
                          &scratchOffset)) {
    return false;
  }

  (void)vk_transitionTexture(command->command,
                             srcVk,
                             region->src.mipLevel,
                             1u,
                             texture3D ? 0u : region->src.baseArrayLayer,
                             texture3D ? 1u : region->layerCount,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  (void)vk_transitionTexture(command->command,
                             dstVk,
                             region->dst.mipLevel,
                             1u,
                             texture3D ? 0u : region->dst.baseArrayLayer,
                             texture3D ? 1u : region->layerCount,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  copy.bufferOffset                    = scratchOffset;
  copy.bufferRowLength                 = (uint32_t)rowLength;
  copy.bufferImageHeight               = region->height;
  copy.imageSubresource.aspectMask     = aspect;
  copy.imageSubresource.mipLevel       = region->src.mipLevel;
  copy.imageSubresource.baseArrayLayer = texture3D ? 0u : region->src.baseArrayLayer;
  copy.imageSubresource.layerCount     = texture3D ? 1u : region->layerCount;
  copy.imageExtent.width               = region->width;
  copy.imageExtent.height              = region->height;
  copy.imageExtent.depth               = texture3D ? region->depth : 1u;
  vkCmdCopyImageToBuffer(command->command,
                         srcVk->image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         scratch,
                         1u,
                         &copy);

  barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer              = scratch;
  barrier.offset              = scratchOffset;
  barrier.size                = scratchBytes;
  vk_pipelineBarrier(srcVk->gpuDevice,
                     command->command,
                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                     1u,
                     &barrier,
                     0u,
                     NULL);

  copy.imageSubresource.mipLevel       = region->dst.mipLevel;
  copy.imageSubresource.baseArrayLayer = texture3D ? 0u : region->dst.baseArrayLayer;
  vkCmdCopyBufferToImage(command->command,
                         scratch,
                         dstVk->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1u,
                         &copy);

  return true;
}
#endif

static void
vk_copyTextureToTexture(GPUTransferPassEncoder              *pass,
                        GPUTexture                          *src,
                        GPUTexture                          *dst,
                        const GPUTextureToTextureCopyRegion *region) {
  VkImageCopy         copy = {0};
  GPUCommandBufferVk *command;
  GPUTextureVk       *srcVk;
  GPUTextureVk       *dstVk;
  VkImageAspectFlags  srcAspect;
  VkImageAspectFlags  dstAspect;
  bool                texture3D;

  command   = vk__copyCommand(pass);
  srcVk     = src ? src->_priv : NULL;
  dstVk     = dst ? dst->_priv : NULL;
  texture3D = src && src->dimension == GPU_TEXTURE_DIMENSION_3D;

  if (!command || !srcVk || !dstVk || !region
      || !vk__copyAspect(src->format,
                         region->src.aspect,
                         &srcAspect)
      || !vk__copyAspect(dst->format,
                         region->dst.aspect,
                         &dstAspect)
      || srcAspect != dstAspect) {
    return;
  }

#ifdef __APPLE__
  if (src->format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 || src->format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8) {
    (void)vk__copyDepthStencilPlane(pass,
                                    src,
                                    dst,
                                    region,
                                    srcAspect);
    return;
  }
#endif

  (void)vk_transitionTexture(command->command,
                             srcVk,
                             region->src.mipLevel,
                             1u,
                             texture3D ? 0u : region->src.baseArrayLayer,
                             texture3D ? 1u : region->layerCount,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  (void)vk_transitionTexture(command->command,
                             dstVk,
                             region->dst.mipLevel,
                             1u,
                             texture3D ? 0u : region->dst.baseArrayLayer,
                             texture3D ? 1u : region->layerCount,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copy.srcSubresource.aspectMask     = srcAspect;
  copy.srcSubresource.mipLevel       = region->src.mipLevel;
  copy.srcSubresource.baseArrayLayer = texture3D ? 0u : region->src.baseArrayLayer;
  copy.srcSubresource.layerCount     = texture3D ? 1u : region->layerCount;
  copy.srcOffset.x                   = (int32_t)region->src.x;
  copy.srcOffset.y                   = (int32_t)region->src.y;
  copy.srcOffset.z                   = texture3D ? (int32_t)region->src.z : 0;
  copy.dstSubresource.aspectMask     = dstAspect;
  copy.dstSubresource.mipLevel       = region->dst.mipLevel;
  copy.dstSubresource.baseArrayLayer = texture3D ? 0u : region->dst.baseArrayLayer;
  copy.dstSubresource.layerCount     = texture3D ? 1u : region->layerCount;
  copy.dstOffset.x                   = (int32_t)region->dst.x;
  copy.dstOffset.y                   = (int32_t)region->dst.y;
  copy.dstOffset.z                   = texture3D ? (int32_t)region->dst.z : 0;
  copy.extent.width                  = region->width;
  copy.extent.height                 = region->height;
  copy.extent.depth                  = texture3D ? region->depth : 1u;
  vkCmdCopyImage(command->command,
                 srcVk->image,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 dstVk->image,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 1u,
                 &copy);
}

static void
vk_blitTexture(GPUCommandBuffer *cmdb, const GPUTextureBlitInfo *info) {
  VkImageBlit         blit = {0};
  GPUCommandBufferVk *command;
  GPUTextureVk       *src;
  GPUTextureVk       *dst;

  command = cmdb ? cmdb->_priv : NULL;
  src     = info && info->src ? info->src->_priv : NULL;
  dst     = info && info->dst ? info->dst->_priv : NULL;

  if (!command || !command->command || !src || !dst) {
    return;
  }

  if ((info->src->usage & GPU_TEXTURE_USAGE_COPY_SRC) == 0u || (info->dst->usage & GPU_TEXTURE_USAGE_COPY_DST) == 0u
      || !src->blitSrc
      || !dst->blitDst
      || (info->filter == GPU_FILTER_LINEAR && !src->linearBlit)) {
    vk_blitTextureRenderFallback(cmdb, info);
    return;
  }

  if (!vk_transitionTexture(command->command,
                            src,
                            info->srcRegion.texture.mipLevel,
                            1u,
                            info->srcRegion.texture.baseArrayLayer,
                            info->srcRegion.layerCount,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
      || !vk_transitionTexture(command->command,
                               dst,
                               info->dstRegion.texture.mipLevel,
                               1u,
                               info->dstRegion.texture.baseArrayLayer,
                               info->dstRegion.layerCount,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
    return;
  }

  blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.srcSubresource.mipLevel       = info->srcRegion.texture.mipLevel;
  blit.srcSubresource.baseArrayLayer = info->srcRegion.texture.baseArrayLayer;
  blit.srcSubresource.layerCount     = info->srcRegion.layerCount;
  blit.srcOffsets[0].x               = (int32_t)info->srcRegion.texture.x;
  blit.srcOffsets[0].y               = (int32_t)info->srcRegion.texture.y;
  blit.srcOffsets[0].z               = 0;
  blit.srcOffsets[1].x               = (int32_t)(info->srcRegion.texture.x + info->srcRegion.width);
  blit.srcOffsets[1].y               = (int32_t)(info->srcRegion.texture.y + info->srcRegion.height);
  blit.srcOffsets[1].z               = 1;
  blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.dstSubresource.mipLevel       = info->dstRegion.texture.mipLevel;
  blit.dstSubresource.baseArrayLayer = info->dstRegion.texture.baseArrayLayer;
  blit.dstSubresource.layerCount     = info->dstRegion.layerCount;
  blit.dstOffsets[0].x               = (int32_t)info->dstRegion.texture.x;
  blit.dstOffsets[0].y               = (int32_t)info->dstRegion.texture.y;
  blit.dstOffsets[0].z               = 0;
  blit.dstOffsets[1].x               = (int32_t)(info->dstRegion.texture.x + info->dstRegion.width);
  blit.dstOffsets[1].y               = (int32_t)(info->dstRegion.texture.y + info->dstRegion.height);
  blit.dstOffsets[1].z               = 1;
  vkCmdBlitImage(command->command,
                 src->image,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 dst->image,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 1u,
                 &blit,
                 info->filter == GPU_FILTER_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
}

#ifdef VK_KHR_copy_memory_indirect

static bool
vk__indirectCopyState(GPUTransferPassEncoder *pass,
                      GPUCommandBufferVk    **outCommand,
                      GPUDeviceVk           **outDevice,
                      VkQueueFlags           *outQueueFlags) {
  GPUCommandBufferVk *command;
  GPUAdapterVk       *adapter;
  GPUDevice          *device;
  GPUDeviceVk        *deviceVk;

  command  = vk__copyCommand(pass);
  device   = pass && pass->_cmdb ? gpuCommandBufferDevice(pass->_cmdb) : NULL;
  deviceVk = device ? device->_priv : NULL;
  adapter  = device && device->adapter ? device->adapter->_priv : NULL;

  if (!command || !command->owner || !deviceVk || !adapter || command->owner->familyIndex >= adapter->nQueFamilies
      || (adapter->queueFamilyProps[command->owner->familyIndex].queueFlags & adapter->indirectCopyQueues) == 0u) {
    return false;
  }

  *outCommand = command;
  *outDevice  = deviceVk;
  if (outQueueFlags) {
    *outQueueFlags = adapter->queueFamilyProps[command->owner->familyIndex].queueFlags;
  }

  return true;
}

static void
vk_copyMemoryIndirect(GPUTransferPassEncoder *pass, const GPUIndirectMemoryCopyInfoEXT *info) {
  VkCopyMemoryIndirectInfoKHR native = {0};
  GPUCommandBufferVk         *command;
  GPUDeviceVk                *device;

  if (!info
      || !vk__indirectCopyState(pass,
                                &command,
                                &device,
                                NULL)
      || !device->indirectMemoryCopy
      || !device->copyMemoryIndirect) {
    return;
  }

  native.sType                    = VK_STRUCTURE_TYPE_COPY_MEMORY_INDIRECT_INFO_KHR;
  native.srcCopyFlags             = (VkAddressCopyFlagsKHR)info->srcFlags;
  native.dstCopyFlags             = (VkAddressCopyFlagsKHR)info->dstFlags;
  native.copyCount                = info->commandCount;
  native.copyAddressRange.address = info->commands.buffer->_gpuAddress + info->commands.offset;
  native.copyAddressRange.size    = info->commands.sizeBytes;
  native.copyAddressRange.stride  = info->commands.strideBytes;
  device->copyMemoryIndirect(command->command, &native);
}

static void
vk_copyMemoryToTextureIndirect(GPUTransferPassEncoder *pass, const GPUIndirectMemoryToTextureCopyInfoEXT *info) {
  VkCopyMemoryToImageIndirectInfoKHR      native = {0};
  GPUCommandBufferVk                     *command;
  GPUDeviceVk                            *device;
  GPUTextureVk                           *texture;
  const GPUIndirectTextureSubresourceEXT *subresource;
  VkQueueFlags                            queueFlags;
  uint32_t                                i;

  texture = info && info->dst ? info->dst->_priv : NULL;

  if (!texture || !texture->indirectCopyDst
      || !vk__indirectCopyState(pass,
                                &command,
                                &device,
                                &queueFlags)
      || !device->indirectMemoryToTextureCopy
      || !device->copyMemoryToImageIndirect) {
    return;
  }

  for (i = 0u; i < info->commandCount; i++) {
    subresource = &info->pTextureSubresources[i];

    if ((subresource->aspectMask
         & (GPU_INDIRECT_TEXTURE_ASPECT_DEPTH_BIT_EXT | GPU_INDIRECT_TEXTURE_ASPECT_STENCIL_BIT_EXT)) != 0u
        && (queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0u) {
      return;
    }

    (void)vk_transitionTextureIndirectCopy(command->command,
                                           texture,
                                           subresource->mipLevel,
                                           1u,
                                           info->dst->dimension == GPU_TEXTURE_DIMENSION_3D
                                             ? 0u
                                             : subresource->baseArrayLayer,
                                           info->dst->dimension == GPU_TEXTURE_DIMENSION_3D
                                             ? 1u
                                             : subresource->layerCount);
  }

  native.sType                    = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INDIRECT_INFO_KHR;
  native.srcCopyFlags             = (VkAddressCopyFlagsKHR)info->srcFlags;
  native.copyCount                = info->commandCount;
  native.copyAddressRange.address = info->commands.buffer->_gpuAddress + info->commands.offset;
  native.copyAddressRange.size    = info->commands.sizeBytes;
  native.copyAddressRange.stride  = info->commands.strideBytes;
  native.dstImage                 = texture->image;
  native.dstImageLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  native.pImageSubresources       = (const VkImageSubresourceLayers *)info->pTextureSubresources;
  device->copyMemoryToImageIndirect(command->command, &native);
  texture->indirectCopyPending = true;
}
#endif

static void
vk_endTransferPass(GPUTransferPassEncoder *pass) {
  GPUCommandBufferVk *command;

  command = vk__copyCommand(pass);

  if (command && command->copyDebugLabelActive) {
    vk_endDebugLabel(gpuCommandBufferDevice(pass->_cmdb), command->command);
    command->copyDebugLabelActive = false;
  }
}

GPU_HIDE
void
vk_invalidateClassicFramebuffers(GPUDeviceVk *device, VkImageView view) {
  GPUClassicFramebufferVk **link;
  GPUClassicFramebufferVk  *entry;

  if (!device || !device->classicRenderLockInitialized || !view) {
    return;
  }

  vk__lockClassicRenderTargets(device);
  link = &device->classicFramebuffers;

  while ((entry = *link)) {
    bool matches;

    matches = entry->depthStencilView == view;

    for (uint32_t i = 0u; !matches && i < entry->colorCount; i++) {
      matches = entry->colorViews[i] == view || entry->resolveViews[i] == view;
    }

    if (matches) {
      *link = entry->next;
      vkDestroyFramebuffer(device->device, entry->framebuffer, NULL);
      free(entry);
      continue;
    }

    link = &entry->next;
  }

  vk__unlockClassicRenderTargets(device);
}

GPU_HIDE
void
vk_destroyClassicRenderTargets(GPUDeviceVk *device) {
  GPUClassicFramebufferVk *framebuffer;
  GPUClassicRenderPassVk  *pass;

  if (!device) {
    return;
  }

  while ((framebuffer = device->classicFramebuffers)) {
    device->classicFramebuffers = framebuffer->next;
    vkDestroyFramebuffer(device->device, framebuffer->framebuffer, NULL);
    free(framebuffer);
  }
  while ((pass = device->classicRenderPasses)) {
    device->classicRenderPasses = pass->next;
    vkDestroyRenderPass(device->device, pass->renderPass, NULL);
    free(pass);
  }
}

GPU_HIDE
void
vk_transitionView(VkCommandBuffer command, GPUTextureViewVk *view, VkImageLayout nextLayout) {
  VkImageMemoryBarrier barrier = {0};
  GPUDeviceVk         *device;
  VkPipelineStageFlags srcStage;
  VkPipelineStageFlags dstStage;
  VkAccessFlags        srcAccess;
  VkAccessFlags        dstAccess;

  if (!command || !view || !view->image) {
    return;
  }

  if (view->texture) {
    (void)vk_transitionTexture(command,
                               view->texture,
                               view->baseMip,
                               view->mipCount,
                               view->baseLayer,
                               view->layerCount,
                               nextLayout);
    return;
  }

  if (!view->layout || *view->layout == nextLayout) {
    return;
  }

  device = view->swapchain && view->swapchain->gpuDevice ? view->swapchain->gpuDevice->_priv : NULL;
  vk__layoutAccess(*view->layout,
                   &srcStage,
                   &srcAccess);
  vk__layoutAccess(nextLayout,
                   &dstStage,
                   &dstAccess);
  barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask                   = srcAccess;
  barrier.dstAccessMask                   = dstAccess;
  barrier.oldLayout                       = *view->layout;
  barrier.newLayout                       = nextLayout;
  barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
  barrier.image                           = view->image;
  barrier.subresourceRange.aspectMask     = view->aspect;
  barrier.subresourceRange.baseMipLevel   = view->baseMip;
  barrier.subresourceRange.levelCount     = view->mipCount;
  barrier.subresourceRange.baseArrayLayer = view->baseLayer;
  barrier.subresourceRange.layerCount     = view->layerCount;
  vk_pipelineBarrier(device,
                     command,
                     srcStage,
                     dstStage,
                     0u,
                     NULL,
                     1u,
                     &barrier);
  *view->layout = nextLayout;
}

GPU_HIDE
VkPipelineStageFlags
vk_barrierStages(const GPUDeviceVk *device, GPUPipelineStageMask stages) {
  VkPipelineStageFlags result;

  result = 0u;

  if ((stages & GPU_STAGE_TOP) != 0u) {
    result |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  }

  if ((stages & GPU_STAGE_VERTEX) != 0u) {
    result |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
#ifdef VK_EXT_mesh_shader
    if (device && device->taskShader) {
      result |= VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT;
    }

    if (device && device->meshShader) {
      result |= VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT;
    }
#endif
  }

  if ((stages & GPU_STAGE_FRAGMENT) != 0u) {
    result |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
              | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  }

  if ((stages & GPU_STAGE_COMPUTE) != 0u) {
    result |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  }

  if ((stages & GPU_STAGE_TRANSFER) != 0u) {
    result |= VK_PIPELINE_STAGE_TRANSFER_BIT;
  }

  if ((stages & GPU_STAGE_BOTTOM) != 0u) {
    result |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  }

  return result;
}

GPU_HIDE
VkAccessFlags
vk_barrierAccess(GPUAccessMask access) {
  VkAccessFlags result;

  result = 0u;

  if ((access & GPU_ACCESS_SHADER_READ) != 0u) {
    result |= VK_ACCESS_SHADER_READ_BIT;
  }

  if ((access & GPU_ACCESS_SHADER_WRITE) != 0u) {
    result |= VK_ACCESS_SHADER_WRITE_BIT;
  }

  if ((access & GPU_ACCESS_COLOR_READ) != 0u) {
    result |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
  }

  if ((access & GPU_ACCESS_COLOR_WRITE) != 0u) {
    result |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  }

  if ((access & GPU_ACCESS_DEPTH_READ) != 0u) {
    result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  }

  if ((access & GPU_ACCESS_DEPTH_WRITE) != 0u) {
    result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  }

  if ((access & GPU_ACCESS_TRANSFER_READ) != 0u) {
    result |= VK_ACCESS_TRANSFER_READ_BIT;
  }

  if ((access & GPU_ACCESS_TRANSFER_WRITE) != 0u) {
    result |= VK_ACCESS_TRANSFER_WRITE_BIT;
  }

  if ((access & GPU_ACCESS_INDIRECT_READ) != 0u) {
    result |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
  }

  return result;
}

GPU_HIDE
VkAccessFlags
vk_bufferBarrierAccess(const GPUBuffer *buffer, GPUAccessMask access, GPUPipelineStageMask stages) {
  VkAccessFlags result;

  result = vk_barrierAccess(access);

  if ((access & GPU_ACCESS_SHADER_READ) != 0u && (stages & GPU_STAGE_VERTEX) != 0u) {
    if (gpuBufferHasUsage(buffer,
                          GPU_BUFFER_USAGE_VERTEX)) {
      result |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    }

    if (gpuBufferHasUsage(buffer,
                          GPU_BUFFER_USAGE_INDEX)) {
      result |= VK_ACCESS_INDEX_READ_BIT;
    }
  }

  return result;
}

GPU_HIDE
VkImageLayout
vk_textureBarrierLayout(const GPUTexture *texture, GPUAccessMask access, bool source) {
  uint32_t categoryCount;

  if (access == GPU_ACCESS_NONE) {
    return source ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
  }

  categoryCount = (access & (GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE)) != 0u;
  categoryCount += (access & (GPU_ACCESS_COLOR_READ | GPU_ACCESS_COLOR_WRITE)) != 0u;
  categoryCount += (access & (GPU_ACCESS_DEPTH_READ | GPU_ACCESS_DEPTH_WRITE)) != 0u;
  categoryCount += (access & (GPU_ACCESS_TRANSFER_READ | GPU_ACCESS_TRANSFER_WRITE)) != 0u;

  if (categoryCount != 1u) {
    return VK_IMAGE_LAYOUT_GENERAL;
  }

  if ((access & GPU_ACCESS_SHADER_WRITE) != 0u
      || ((access & GPU_ACCESS_SHADER_READ) != 0u && (texture->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u)) {
    return VK_IMAGE_LAYOUT_GENERAL;
  }

  if ((access & GPU_ACCESS_SHADER_READ) != 0u) {
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }

  if ((access & (GPU_ACCESS_COLOR_READ | GPU_ACCESS_COLOR_WRITE)) != 0u) {
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }

  if ((access & (GPU_ACCESS_DEPTH_READ | GPU_ACCESS_DEPTH_WRITE)) != 0u) {
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }

  if ((access & GPU_ACCESS_TRANSFER_READ) != 0u && (access & GPU_ACCESS_TRANSFER_WRITE) == 0u) {
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  }

  if ((access & GPU_ACCESS_TRANSFER_WRITE) != 0u && (access & GPU_ACCESS_TRANSFER_READ) == 0u) {
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  }

  return VK_IMAGE_LAYOUT_GENERAL;
}

GPU_HIDE
void
vk_encodeBarriers(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers) {
  GPUCommandBufferVk  *command;
  GPUDevice           *gpuDevice;
  GPUDeviceVk         *device;
  VkPipelineStageFlags srcStages;
  VkPipelineStageFlags dstStages;
  uint32_t             bufferOffset;
  uint32_t             textureOffset;

  command   = cmdb ? cmdb->_priv : NULL;
  gpuDevice = cmdb && cmdb->_queue ? cmdb->_queue->_device : NULL;
  device    = gpuDevice ? gpuDevice->_priv : NULL;

  if (!command || !command->command || !device || !barriers) {
    return;
  }

  srcStages = vk_barrierStages(device, barriers->srcStages);
  dstStages = vk_barrierStages(device, barriers->dstStages);

  for (uint32_t i = 0u; i < barriers->bufferBarrierCount; i++) {
    const GPUBufferBarrier *barrier = &barriers->pBufferBarriers[i];
    bool                    addressedIndirect;

    addressedIndirect = gpuBufferHasUsage(barrier->buffer,
                                          GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT)
                        && (device->indirectMemoryCopy || device->indirectMemoryToTextureCopy);

    if ((barrier->srcAccess & GPU_ACCESS_INDIRECT_READ) != 0u) {
      srcStages |= addressedIndirect ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    }

    if ((barrier->dstAccess & GPU_ACCESS_INDIRECT_READ) != 0u) {
      dstStages |= addressedIndirect ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    }

    if (addressedIndirect && (barrier->srcAccess & (GPU_ACCESS_TRANSFER_READ | GPU_ACCESS_TRANSFER_WRITE)) != 0u) {
      srcStages |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    if (addressedIndirect && (barrier->dstAccess & (GPU_ACCESS_TRANSFER_READ | GPU_ACCESS_TRANSFER_WRITE)) != 0u) {
      dstStages |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
  }

  if (barriers->aliasingBarrierCount > 0u) {
    VkMemoryBarrier native = {0};

    native.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    native.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    native.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(command->command,
                         srcStages,
                         dstStages,
                         0u,
                         1u,
                         &native,
                         0u,
                         NULL,
                         0u,
                         NULL);

    for (uint32_t i = 0u; i < barriers->aliasingBarrierCount; i++) {
      GPUTexture *after;

      after = barriers->pAliasingBarriers[i].afterTexture;

      if (after && after->_priv) {
        GPUTextureVk *texture = after->_priv;

        vk_setTextureLayout(texture,
                            0u,
                            texture->mipLevelCount,
                            0u,
                            texture->arrayLayerCount,
                            VK_IMAGE_LAYOUT_UNDEFINED);
      }
    }
  }

  bufferOffset  = 0u;
  textureOffset = 0u;

  while (bufferOffset < barriers->bufferBarrierCount || textureOffset < barriers->textureBarrierCount) {
    VkBufferMemoryBarrier nativeBarriers[GPU_VK_BARRIER_CHUNK_SIZE];
    VkImageMemoryBarrier  nativeImages[GPU_VK_BARRIER_CHUNK_SIZE];
    uint32_t              nativeBarrierCount;
    uint32_t              nativeImageCount;
    uint32_t              bufferChunkCount;
    uint32_t              textureChunkCount;

    bufferChunkCount = barriers->bufferBarrierCount - bufferOffset;

    if (bufferChunkCount > GPU_VK_BARRIER_CHUNK_SIZE) {
      bufferChunkCount = GPU_VK_BARRIER_CHUNK_SIZE;
    }

    textureChunkCount = barriers->textureBarrierCount - textureOffset;

    if (textureChunkCount > GPU_VK_BARRIER_CHUNK_SIZE) {
      textureChunkCount = GPU_VK_BARRIER_CHUNK_SIZE;
    }

    nativeBarrierCount = 0u;

    for (uint32_t i = 0u; i < bufferChunkCount; i++) {
      const GPUBufferBarrier *barrier;
      GPUBufferVk            *buffer;
      VkBufferMemoryBarrier  *native;

      barrier = &barriers->pBufferBarriers[bufferOffset + i];
      buffer  = barrier->buffer ? barrier->buffer->_priv : NULL;

      if (!buffer || !buffer->buffer || buffer->device != device->device) {
        continue;
      }

      native = &nativeBarriers[nativeBarrierCount++];
      memset(native, 0, sizeof(*native));
      native->sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      native->srcAccessMask       = vk_bufferBarrierAccess(barrier->buffer,
                                                           barrier->srcAccess,
                                                           barriers->srcStages);
      native->dstAccessMask       = vk_bufferBarrierAccess(barrier->buffer,
                                                           barrier->dstAccess,
                                                           barriers->dstStages);
      native->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      native->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      native->buffer              = buffer->buffer;
      native->offset              = barrier->offset;
      native->size                = barrier->sizeBytes;
    }

    nativeImageCount = 0u;

    for (uint32_t i = 0u; i < textureChunkCount; i++) {
      const GPUTextureBarrier *barrier;
      GPUTextureVk            *texture;
      VkImageMemoryBarrier    *native;
      VkImageLayout            newLayout;

      barrier = &barriers->pTextureBarriers[textureOffset + i];

      if (!barrier->texture->_ownsNative) {
        gpuDeviceRecordValidationError(gpuDevice,
                                       "Vulkan barriers do not support swapchain textures");
        continue;
      }

      texture = barrier->texture->_priv;

      if (!texture || !texture->image || texture->device != device->device) {
        gpuDeviceRecordValidationError(gpuDevice,
                                       "Vulkan texture barrier has no compatible native image");
        continue;
      }

      newLayout = vk_textureBarrierLayout(barrier->texture,
                                          barrier->dstAccess,
                                          false);

      if (!texture->layoutUniform) {
        if (nativeBarrierCount > 0u || nativeImageCount > 0u) {
          vk_pipelineBarrier(device,
                             command->command,
                             srcStages,
                             dstStages,
                             nativeBarrierCount,
                             nativeBarriers,
                             nativeImageCount,
                             nativeImages);
          nativeBarrierCount = 0u;
          nativeImageCount   = 0u;
        }

        if (!vk_transitionTextureBarrier(command->command,
                                         texture,
                                         barrier->baseMip,
                                         barrier->mipCount,
                                         barrier->baseLayer,
                                         barrier->layerCount,
                                         newLayout,
                                         srcStages,
                                         dstStages,
                                         vk_barrierAccess(barrier->srcAccess),
                                         vk_barrierAccess(barrier->dstAccess))) {
          gpuDeviceRecordValidationError(gpuDevice,
                                         "Vulkan texture barrier range transition failed");
        }

        continue;
      }

      native = &nativeImages[nativeImageCount++];
      memset(native, 0, sizeof(*native));
      native->sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      native->srcAccessMask                   = vk_barrierAccess(barrier->srcAccess);
      native->dstAccessMask                   = vk_barrierAccess(barrier->dstAccess);
      native->oldLayout                       = texture->layout;
      native->newLayout                       = newLayout;
      native->srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
      native->dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
      native->image                           = texture->image;
      native->subresourceRange.aspectMask     = texture->aspect;
      native->subresourceRange.baseMipLevel   = barrier->baseMip;
      native->subresourceRange.levelCount     = barrier->mipCount;
      native->subresourceRange.baseArrayLayer = barrier->baseLayer;
      native->subresourceRange.layerCount     = barrier->layerCount;
      vk_setTextureLayout(texture,
                          barrier->baseMip,
                          barrier->mipCount,
                          barrier->baseLayer,
                          barrier->layerCount,
                          native->newLayout);
    }

    if (nativeBarrierCount > 0u || nativeImageCount > 0u) {
      vk_pipelineBarrier(device,
                         command->command,
                         srcStages,
                         dstStages,
                         nativeBarrierCount,
                         nativeBarriers,
                         nativeImageCount,
                         nativeImages);
    }

    bufferOffset += bufferChunkCount;
    textureOffset += textureChunkCount;
  }
}

GPU_HIDE
GPURenderPassDesc*
vk_beginRenderPass(GPUCommandBuffer *cmdb, const GPURenderPassCreateInfo *info) {
  VkFormat                                    colorFormats[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS]  = {0};
  VkImageView                                 colorViews[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS]    = {0};
  VkImageView                                 resolveViews[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS]  = {0};
  GPULoadOp                                   colorLoadOps[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS]  = {0};
  GPUStoreOp                                  colorStoreOps[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS] = {0};
  GPUClassicRenderTargetInfoVk                target                                                  = {0};
  const GPURenderPassDepthStencilAttachment  *depthStencil;
  GPUCommandBufferVk                         *command;
  GPUTextureViewVk                           *depthStencilView;
  GPUSwapchainVk                             *swapchain;
  GPURenderPassDesc                          *pass;
  GPURenderPassVk                            *native;
  GPUDeviceVk                                *device;
  const GPUShadingRateAttachmentEXT          *shadingRate;
  const GPURasterizationRateMapRenderPassEXT *rateMap;
  const GPURenderPassColorAttachment         *colorAttachment;
  GPUTextureViewVk                           *colorView;
  GPUTextureViewVk                           *resolveView;
  GPUSwapchainVk                             *attachmentSwapchain;
  const GPURenderPassColorAttachment         *singleColor;
  GPUTextureViewVk                           *singleView;
  uint32_t                                    attachmentCount;
  uint32_t                                    sampleCount;
  uint32_t                                    layerCount;
  uint32_t                                    colorIndex;
  uint32_t                                    attachmentSampleCount;
  uint32_t                                    depthSampleCount;
  uint32_t                                    resolveIndex;

  device = cmdb && cmdb->_queue && cmdb->_queue->_device ? cmdb->_queue->_device->_priv : NULL;

  if (device && device->dynamicRendering) {
    return vk_beginDynamicRenderPass(cmdb,
                                     info,
                                     cmdb->_priv,
                                     device);
  }

  if (!gpuRenderPassVRSExtensions(info, &shadingRate, &rateMap) || shadingRate || rateMap) {
    return NULL;
  }

  command = cmdb ? cmdb->_priv : NULL;

  if (!cmdb || !info || !command || !device || info->colorAttachmentCount > GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS
      || (info->colorAttachmentCount > 0u && !info->pColorAttachments)
      || (info->colorAttachmentCount == 0u && !info->pDepthStencilAttachment)) {
    return NULL;
  }

  depthStencil     = info->pDepthStencilAttachment;
  depthStencilView = depthStencil && depthStencil->view ? depthStencil->view->_priv : NULL;

  if (depthStencil && !depthStencilView) {
    return NULL;
  }

  pass   = &command->renderPass;
  native = &command->renderPassState;
  memset(pass, 0, sizeof(*pass));
  memset(native, 0, sizeof(*native));

  swapchain   = NULL;
  sampleCount = 0u;
  layerCount  = 0u;

  for (colorIndex = 0u; colorIndex < info->colorAttachmentCount; colorIndex++) {
    colorAttachment       = &info->pColorAttachments[colorIndex];
    colorView             = colorAttachment->view ? colorAttachment->view->_priv : NULL;
    resolveView           = colorAttachment->resolveView ? colorAttachment->resolveView->_priv : NULL;
    attachmentSampleCount =
        colorAttachment->view && colorAttachment->view->_texture ? colorAttachment->view->_texture->sampleCount : 0u;

    if (!colorView || !colorView->view || !colorView->image || !colorView->layout || colorView->extent.width == 0u
        || colorView->extent.height == 0u
        || colorView->layerCount == 0u
        || attachmentSampleCount == 0u
        || (sampleCount != 0u && sampleCount != attachmentSampleCount)
        || (native->extent.width != 0u
            && (native->extent.width != colorView->extent.width || native->extent.height != colorView->extent.height
                || layerCount != colorView->layerCount))
        || (resolveView && attachmentSampleCount == 1u)
        || (!resolveView && attachmentSampleCount > 1u)
        || (colorView->swapchain && resolveView)) {
      return NULL;
    }

    attachmentSwapchain = resolveView && resolveView->swapchain ? resolveView->swapchain : colorView->swapchain;

    if ((swapchain && attachmentSwapchain && attachmentSwapchain != swapchain)
        || (attachmentSwapchain
            && (!attachmentSwapchain->frameActive
                || (colorView->swapchain && colorView->imageIndex != attachmentSwapchain->acquiredImageIndex)
                || (resolveView && resolveView->swapchain
                    && resolveView->imageIndex != attachmentSwapchain->acquiredImageIndex)))) {
      return NULL;
    }

    if (attachmentSwapchain) {
      swapchain = attachmentSwapchain;
    }

    if (!colorView->swapchain) {
      if (!colorView->texture) {
        return NULL;
      }

      vk_setTextureLayout(colorView->texture,
                          colorView->baseMip,
                          colorView->mipCount,
                          colorView->baseLayer,
                          colorView->layerCount,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    if (resolveView) {
      if (!resolveView->view || !resolveView->image || !resolveView->layout
          || (!resolveView->swapchain && !resolveView->texture)
          || resolveView->extent.width != colorView->extent.width
          || resolveView->extent.height != colorView->extent.height
          || resolveView->layerCount != colorView->layerCount
          || !colorAttachment->resolveView->_texture
          || colorAttachment->resolveView->_texture->sampleCount != 1u
          || colorAttachment->resolveView->format != colorAttachment->view->format) {
        return NULL;
      }

      if (!resolveView->swapchain) {
        vk_setTextureLayout(resolveView->texture,
                            resolveView->baseMip,
                            resolveView->mipCount,
                            resolveView->baseLayer,
                            resolveView->layerCount,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      }

      target.resolveMask |= 1u << colorIndex;
      native->resolveViews[colorIndex] = resolveView;
      resolveViews[colorIndex]         = resolveView->view;
    }

    if (attachmentSwapchain) {
      target.presentMask |= 1u << colorIndex;
    }

    if (!vk_formatFromGPU(colorAttachment->view->format,
                          &colorFormats[colorIndex])) {
      return NULL;
    }

    colorViews[colorIndex]                = colorView->view;
    colorLoadOps[colorIndex]              = colorAttachment->loadOp;
    colorStoreOps[colorIndex]             = colorAttachment->storeOp;
    native->colorViews[colorIndex]        = colorView;
    native->clearValues[colorIndex].color = vk__clearColor(&colorAttachment->clearColor, colorAttachment->view->format);
    native->extent                        = colorView->extent;
    sampleCount                           = attachmentSampleCount;
    layerCount                            = colorView->layerCount;
  }

  native->swapchain        = swapchain;
  native->depthStencilView = depthStencilView;

  if (depthStencilView) {
    depthSampleCount =
        depthStencil->view && depthStencil->view->_texture ? depthStencil->view->_texture->sampleCount : 0u;

    if (!depthStencilView->view || !depthStencilView->image || !depthStencilView->layout || !depthStencilView->texture
        || depthStencilView->extent.width == 0u
        || depthStencilView->extent.height == 0u
        || depthStencilView->layerCount == 0u
        || depthSampleCount == 0u
        || (sampleCount != 0u && sampleCount != depthSampleCount)
        || (native->extent.width != 0u
            && (native->extent.width != depthStencilView->extent.width
                || native->extent.height != depthStencilView->extent.height
                || layerCount != depthStencilView->layerCount))
        || !vk_formatFromGPU(depthStencil->view->format,
                             &target.depthStencilFormat)) {
      return NULL;
    }

    native->extent          = depthStencilView->extent;
    sampleCount             = depthSampleCount;
    layerCount              = depthStencilView->layerCount;
    target.depthStencilView = depthStencilView->view;
    target.depthLoadOp      = depthStencil->depthLoadOp;
    target.depthStoreOp     = depthStencil->depthStoreOp;
    target.stencilLoadOp    = depthStencil->stencilLoadOp;
    target.stencilStoreOp   = depthStencil->stencilStoreOp;
    vk_transitionView(command->command,
                      depthStencilView,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  }

  if (sampleCount == 0u || layerCount == 0u || native->extent.width == 0u || native->extent.height == 0u) {
    return NULL;
  }

  target.colorFormats  = colorFormats;
  target.colorViews    = colorViews;
  target.resolveViews  = resolveViews;
  target.colorLoadOps  = colorLoadOps;
  target.colorStoreOps = colorStoreOps;
  target.extent        = native->extent;
  target.colorCount    = info->colorAttachmentCount;
  target.layerCount    = layerCount;
  target.sampleCount   = sampleCount;

  if (info->colorAttachmentCount == 1u && !depthStencilView && target.resolveMask == 0u && sampleCount == 1u) {
    singleColor = &info->pColorAttachments[0];
    singleView  = singleColor->view->_priv;

    if (singleView->swapchain) {
      native->renderPass  = singleView->swapchain->renderPasses[singleColor->loadOp][singleColor->storeOp];
      native->framebuffer = singleView->swapchain->framebuffers[singleView->imageIndex];
    } else {
      native->renderPass  = singleView->texture->renderPasses[singleColor->loadOp][singleColor->storeOp];
      native->framebuffer = singleView->framebuffer;
    }
  } else if (vk__getClassicRenderTarget(device,
                                        &target,
                                        &native->renderPass,
                                        &native->framebuffer) != GPU_OK) {
    return NULL;
  }

  if (!native->renderPass || !native->framebuffer || native->extent.width == 0u || native->extent.height == 0u) {
    return NULL;
  }

  if (info->occlusionQuerySet) {
    vk_resetQuerySet(cmdb, info->occlusionQuerySet);
  }

  attachmentCount = info->colorAttachmentCount;

  for (resolveIndex = 0u; resolveIndex < info->colorAttachmentCount; resolveIndex++) {
    if ((target.resolveMask & (1u << resolveIndex)) != 0u) {
      attachmentCount++;
    }
  }

  if (depthStencil) {
    native->clearValues[attachmentCount].depthStencil.depth   = depthStencil->clearDepth;
    native->clearValues[attachmentCount].depthStencil.stencil = depthStencil->clearStencil;
    attachmentCount++;
  }

  native->colorCount      = info->colorAttachmentCount;
  native->clearValueCount = attachmentCount;
  pass->_priv             = native;
  pass->label             = info->label;

  return pass;
}

GPU_HIDE
void
vk_destroyRenderPass(GPURenderPassDesc *pass) {
  GPU__UNUSED(pass);
}

GPU_HIDE
void
vk_initRenderPass(GPUApiRenderPass *api) {
  api->beginRenderPass      = vk_beginRenderPass;
  api->destroyRenderPass    = vk_destroyRenderPass;
  api->beginTransferPass    = vk_beginTransferPass;
  api->copyBufferToBuffer   = vk_copyBufferToBuffer;
  api->copyBufferToTexture  = vk_copyBufferToTexture;
  api->copyTextureToBuffer  = vk_copyTextureToBuffer;
  api->copyTextureToTexture = vk_copyTextureToTexture;
#ifdef VK_KHR_copy_memory_indirect
  api->copyMemoryIndirect          = vk_copyMemoryIndirect;
  api->copyMemoryToTextureIndirect = vk_copyMemoryToTextureIndirect;
#endif
  api->endTransferPass = vk_endTransferPass;
  api->blitTexture     = vk_blitTexture;
  api->encodeBarriers  = vk_encodeBarriers;
}
