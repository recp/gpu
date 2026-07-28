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

#include "../../common.h"
#include "../cmdqueue_internal.h"
#include "../device_internal.h"
#include "../library_internal.h"
#include "../texture_internal.h"
#include "blit_internal.h"

#if !defined(_WIN32) && !defined(WIN32)
#  include <pthread.h>
#endif

enum {
  GPU_BLIT_VARIANT_FLOAT_FILTERING = 0u,
  GPU_BLIT_VARIANT_FLOAT_UNFILTERABLE,
  GPU_BLIT_VARIANT_UINT,
  GPU_BLIT_VARIANT_SINT,
  GPU_BLIT_VARIANT_COUNT
};

typedef struct GPUBlitParams {
  float srcRect[4];
  float dstRect[4];
  float invSrcSize[4];
} GPUBlitParams;

typedef struct GPUBlitVariant {
  GPUShaderLibrary   *library;
  GPUBindGroupLayout *bindGroupLayout;
  GPUPipelineLayout  *pipelineLayout;
  GPURenderPipeline  *pipelines[GPU_FORMAT_COUNT];
} GPUBlitVariant;

typedef struct GPUBlitContext {
  GPUSampler     *nearestSampler;
  GPUSampler     *linearSampler;
  GPUBlitVariant  variants[GPU_BLIT_VARIANT_COUNT];
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION lock;
#else
  pthread_mutex_t  lock;
#endif
} GPUBlitContext;

typedef struct GPUBlitView {
  struct GPUBlitView *next;
  GPUTextureView     *view;
  GPUBindGroup       *groups[GPU_BLIT_VARIANT_COUNT][2];
  uint32_t            mipLevel;
  uint32_t            arrayLayer;
} GPUBlitView;

static void
gpu_blitLock(GPUBlitContext *context) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&context->lock);
#else
  pthread_mutex_lock(&context->lock);
#endif
}

static void
gpu_blitUnlock(GPUBlitContext *context) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&context->lock);
#else
  pthread_mutex_unlock(&context->lock);
#endif
}

static uint32_t
gpu_blitMipExtent(uint32_t extent, uint32_t mipLevel) {
  extent >>= mipLevel;
  return extent > 0u ? extent : 1u;
}

static bool
gpu_blitRegionValid(const GPUTextureSubresourceRegion *region,
                    const GPUTexture                  *texture) {
  uint32_t mipWidth;
  uint32_t mipHeight;

  if (!region || !texture ||
      texture->dimension != GPU_TEXTURE_DIMENSION_2D ||
      texture->sampleCount != 1u ||
      region->texture.aspect != GPU_TEXTURE_ASPECT_ALL ||
      region->texture.z != 0u ||
      region->texture.mipLevel >= texture->mipLevelCount ||
      region->width == 0u ||
      region->height == 0u ||
      region->depth != 1u ||
      region->layerCount == 0u ||
      region->texture.baseArrayLayer >= texture->depthOrLayers ||
      region->layerCount >
        texture->depthOrLayers - region->texture.baseArrayLayer) {
    return false;
  }

  mipWidth  = gpu_blitMipExtent(texture->width,
                                region->texture.mipLevel);
  mipHeight = gpu_blitMipExtent(texture->height,
                                region->texture.mipLevel);
  return region->texture.x < mipWidth &&
         region->texture.y < mipHeight &&
         region->width <= mipWidth - region->texture.x &&
         region->height <= mipHeight - region->texture.y;
}

static bool
gpu_blitSubresourcesOverlap(const GPUTextureBlitInfo *info) {
  uint32_t srcFirst;
  uint32_t srcLast;
  uint32_t dstFirst;
  uint32_t dstLast;

  if (!info || info->src != info->dst ||
      info->srcRegion.texture.mipLevel !=
        info->dstRegion.texture.mipLevel) {
    return false;
  }

  srcFirst = info->srcRegion.texture.baseArrayLayer;
  srcLast  = srcFirst + info->srcRegion.layerCount;
  dstFirst = info->dstRegion.texture.baseArrayLayer;
  dstLast  = dstFirst + info->dstRegion.layerCount;
  return srcFirst < dstLast && dstFirst < srcLast;
}

static bool
gpu_blitInfoValid(GPUCommandBuffer         *cmdb,
                  const GPUTextureBlitInfo *info,
                  GPUFormatCapabilities    *outSrcCaps) {
  GPUDevice            *device;
  GPUFormatNumericType  srcType;
  GPUFormatNumericType  dstType;

  device = gpuCommandBufferDevice(cmdb);
  if (!cmdb || !device || cmdb->_submitted || cmdb->_activeEncoder ||
      !cmdb->_queue ||
      (cmdb->_queue->bits & GPU_QUEUE_GRAPHICS_BIT) == 0u ||
      !info || !info->src || !info->dst ||
      info->src->device != device || info->dst->device != device ||
      info->filter > GPU_FILTER_LINEAR ||
      info->src->depthOrLayers != 1u ||
      info->dst->depthOrLayers != 1u ||
      (info->src->usage &
       (GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_SRC)) !=
        (GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_SRC) ||
      (info->dst->usage &
       (GPU_TEXTURE_USAGE_COLOR_TARGET | GPU_TEXTURE_USAGE_COPY_DST)) !=
        (GPU_TEXTURE_USAGE_COLOR_TARGET | GPU_TEXTURE_USAGE_COPY_DST) ||
      info->srcRegion.layerCount != info->dstRegion.layerCount ||
      !gpu_blitRegionValid(&info->srcRegion, info->src) ||
      !gpu_blitRegionValid(&info->dstRegion, info->dst) ||
      gpu_blitSubresourcesOverlap(info)) {
    return false;
  }

  srcType = gpuFormatNumericType(info->src->format);
  dstType = gpuFormatNumericType(info->dst->format);
  if (srcType != dstType ||
      (info->filter == GPU_FILTER_LINEAR &&
       srcType != GPU_FORMAT_NUMERIC_FLOAT) ||
      GPUGetFormatCapabilities(device->adapter,
                               info->src->format,
                               outSrcCaps) != GPU_OK ||
      !outSrcCaps->sampled ||
      (info->filter == GPU_FILTER_LINEAR && !outSrcCaps->filterable)) {
    return false;
  }

  return true;
}

static const GPUBlitShaderData *
gpu_blitShaderData(const GPUBlitShaderSet *shaders, uint32_t variant) {
  if (!shaders) {
    return NULL;
  }

  switch (variant) {
    case GPU_BLIT_VARIANT_FLOAT_FILTERING:
      return &shaders->filteringFloat;
    case GPU_BLIT_VARIANT_FLOAT_UNFILTERABLE:
      return &shaders->unfilterableFloat;
    case GPU_BLIT_VARIANT_UINT:
      return &shaders->unsignedInteger;
    case GPU_BLIT_VARIANT_SINT:
      return &shaders->signedInteger;
    default:
      return NULL;
  }
}

static bool
gpu_blitEnsureSamplers(GPUDevice *device, GPUBlitContext *context) {
  GPUSamplerCreateInfo info = {0};

  if (context->nearestSampler && context->linearSampler) {
    return true;
  }

  info.label                 = "gpu-blit-nearest";
  info.desc.minFilter        = GPU_FILTER_NEAREST;
  info.desc.magFilter        = GPU_FILTER_NEAREST;
  info.desc.mipFilter        = GPU_MIP_FILTER_NEAREST;
  info.desc.addressU         = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.desc.addressV         = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.desc.addressW         = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.desc.maxAnisotropy    = 1u;
  if (GPUCreateSampler(device,
                       &info,
                       false,
                       &context->nearestSampler) != GPU_OK) {
    return false;
  }

  info.label          = "gpu-blit-linear";
  info.desc.minFilter = GPU_FILTER_LINEAR;
  info.desc.magFilter = GPU_FILTER_LINEAR;
  if (GPUCreateSampler(device,
                       &info,
                       false,
                       &context->linearSampler) != GPU_OK) {
    GPUDestroySampler(context->nearestSampler);
    context->nearestSampler = NULL;
    return false;
  }
  return true;
}

static GPUTextureSampleType
gpu_blitSampleType(uint32_t variant) {
  switch (variant) {
    case GPU_BLIT_VARIANT_FLOAT_UNFILTERABLE:
      return GPU_TEXTURE_SAMPLE_TYPE_UNFILTERABLE_FLOAT;
    case GPU_BLIT_VARIANT_UINT:
      return GPU_TEXTURE_SAMPLE_TYPE_UINT;
    case GPU_BLIT_VARIANT_SINT:
      return GPU_TEXTURE_SAMPLE_TYPE_SINT;
    default:
      return GPU_TEXTURE_SAMPLE_TYPE_FLOAT;
  }
}

static bool
gpu_blitEnsureVariant(GPUDevice             *device,
                      GPUBlitContext        *context,
                      const GPUBlitShaderSet *shaders,
                      uint32_t               variantIndex) {
  GPUBindGroupLayoutCreateInfo bindGroupInfo = {0};
  GPUPipelineLayoutCreateInfo  pipelineInfo = {0};
  GPUBindGroupLayoutEntry      entries[2] = {0};
  GPUBindGroupLayout          *layouts[1];
  GPUBlitVariant              *variant;
  GPUShaderLibrary            *library;
  GPUApi                      *api;
  const GPUBlitShaderData      *shader;

  variant = &context->variants[variantIndex];
  if (variant->library && variant->bindGroupLayout &&
      variant->pipelineLayout) {
    return true;
  }

  api    = gpuDeviceApi(device);
  shader = gpu_blitShaderData(shaders, variantIndex);
  if (!api || !shader || !shader->data || shader->size == 0u ||
      (shader->binary && !api->library.newLibraryWithBinary) ||
      (!shader->binary && !api->library.newLibraryWithSource)) {
    return false;
  }

  library = shader->binary
              ? api->library.newLibraryWithBinary(device,
                                                  shader->data,
                                                  shader->size)
              : api->library.newLibraryWithSource(device,
                                                  shader->data,
                                                  shader->size);
  if (!library) {
    return false;
  }
  library->_api    = api;
  library->_device = device;

  entries[0].binding                       = 0u;
  entries[0].arrayCount                    = 1u;
  entries[0].bindingType                   = GPU_BINDING_SAMPLED_TEXTURE;
  entries[0].visibility                    = GPU_SHADER_STAGE_FRAGMENT_BIT;
  entries[0].sampledTexture.viewType       = GPU_TEXTURE_VIEW_2D;
  entries[0].sampledTexture.sampleType     =
    gpu_blitSampleType(variantIndex);
  entries[0].sampledTexture.multisampled   = false;
  entries[1].binding                       = 1u;
  entries[1].arrayCount                    = 1u;
  entries[1].bindingType                   = GPU_BINDING_SAMPLER;
  entries[1].visibility                    = GPU_SHADER_STAGE_FRAGMENT_BIT;
  entries[1].sampler.type =
    variantIndex == GPU_BLIT_VARIANT_FLOAT_FILTERING
      ? GPU_SAMPLER_BINDING_FILTERING
      : GPU_SAMPLER_BINDING_NON_FILTERING;

  bindGroupInfo.label      = "gpu-blit-bind-group-layout";
  bindGroupInfo.pEntries   = entries;
  bindGroupInfo.entryCount = (uint32_t)GPU_ARRAY_LEN(entries);
  if (GPUCreateBindGroupLayout(device,
                               &bindGroupInfo,
                               &variant->bindGroupLayout) != GPU_OK) {
    GPUDestroyShaderLibrary(library);
    return false;
  }

  layouts[0]                         = variant->bindGroupLayout;
  pipelineInfo.label                 = "gpu-blit-pipeline-layout";
  pipelineInfo.ppBindGroupLayouts    = layouts;
  pipelineInfo.bindGroupLayoutCount  = 1u;
  pipelineInfo.pushConstantSizeBytes = sizeof(GPUBlitParams);
  pipelineInfo.pushConstantStages    = GPU_SHADER_STAGE_FRAGMENT_BIT;
  if (GPUCreatePipelineLayout(device,
                              &pipelineInfo,
                              &variant->pipelineLayout) != GPU_OK) {
    GPUDestroyBindGroupLayout(variant->bindGroupLayout);
    variant->bindGroupLayout = NULL;
    GPUDestroyShaderLibrary(library);
    return false;
  }

  variant->library = library;
  return true;
}

static bool
gpu_blitEnsurePipeline(GPUDevice      *device,
                       GPUBlitVariant *variant,
                       GPUFormat       format) {
  GPURenderPipelineCreateInfo info = {0};
  GPUColorTargetState         color = {0};

  if (variant->pipelines[format]) {
    return true;
  }

  color.format                = format;
  info.label                  = "gpu-blit-pipeline";
  info.layout                 = variant->pipelineLayout;
  info.library                = variant->library;
  info.vertexEntry            = "gpu_blit_vs";
  info.fragmentEntry          = "gpu_blit_fs";
  info.pColorTargets          = &color;
  info.colorTargetCount       = 1u;
  info.primitiveTopology      = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode               = GPU_CULL_MODE_NONE;
  info.frontFace              = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  return GPUCreateRenderPipeline(device,
                                 &info,
                                 &variant->pipelines[format]) == GPU_OK;
}

static GPUBlitView *
gpu_blitEnsureView(GPUTexture *texture,
                   uint32_t    mipLevel,
                   uint32_t    arrayLayer) {
  GPUTextureViewCreateInfo info = {0};
  GPUBlitView             *entry;

  for (entry = texture->_blitViews; entry; entry = entry->next) {
    if (entry->mipLevel == mipLevel &&
        entry->arrayLayer == arrayLayer) {
      return entry;
    }
  }

  entry = calloc(1, sizeof(*entry));
  if (!entry) {
    return NULL;
  }

  info.label           = "gpu-blit-view";
  info.viewType        = GPU_TEXTURE_VIEW_2D;
  info.format          = texture->format;
  info.baseMipLevel    = mipLevel;
  info.mipLevelCount   = 1u;
  info.baseArrayLayer  = arrayLayer;
  info.arrayLayerCount = 1u;
  if (GPUCreateTextureView(texture, &info, &entry->view) != GPU_OK) {
    free(entry);
    return NULL;
  }

  entry->mipLevel   = mipLevel;
  entry->arrayLayer = arrayLayer;
  entry->next       = texture->_blitViews;
  texture->_blitViews = entry;
  return entry;
}

static GPUBindGroup *
gpu_blitEnsureGroup(GPUDevice       *device,
                    GPUBlitContext  *context,
                    GPUBlitVariant  *variant,
                    GPUBlitView     *view,
                    uint32_t         variantIndex,
                    GPUFilter        filter) {
  GPUBindGroupCreateInfo info = {0};
  GPUBindGroupEntry      entries[2] = {0};
  uint32_t               filterIndex;

  filterIndex = filter == GPU_FILTER_LINEAR ? 1u : 0u;
  if (view->groups[variantIndex][filterIndex]) {
    return view->groups[variantIndex][filterIndex];
  }

  entries[0].binding     = 0u;
  entries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  entries[0].textureView = view->view;
  entries[1].binding     = 1u;
  entries[1].bindingType = GPU_BINDING_SAMPLER;
  entries[1].sampler     = filter == GPU_FILTER_LINEAR
                             ? context->linearSampler
                             : context->nearestSampler;
  info.label      = "gpu-blit-bind-group";
  info.layout     = variant->bindGroupLayout;
  info.pEntries   = entries;
  info.entryCount = (uint32_t)GPU_ARRAY_LEN(entries);
  if (GPUCreateBindGroup(device,
                         &info,
                         &view->groups[variantIndex][filterIndex]) != GPU_OK) {
    return NULL;
  }
  return view->groups[variantIndex][filterIndex];
}

static uint32_t
gpu_blitVariantIndex(const GPUTextureBlitInfo *info,
                     const GPUFormatCapabilities *srcCaps) {
  switch (gpuFormatNumericType(info->src->format)) {
    case GPU_FORMAT_NUMERIC_UINT:
      return GPU_BLIT_VARIANT_UINT;
    case GPU_FORMAT_NUMERIC_SINT:
      return GPU_BLIT_VARIANT_SINT;
    default:
      return srcCaps->filterable
               ? GPU_BLIT_VARIANT_FLOAT_FILTERING
               : GPU_BLIT_VARIANT_FLOAT_UNFILTERABLE;
  }
}

GPU_HIDE
GPUResult
gpuInitBlitDevice(GPUDevice *device) {
  GPUBlitContext *context;

  if (!device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  context = calloc(1, sizeof(*context));
  if (!context) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&context->lock);
#else
  if (pthread_mutex_init(&context->lock, NULL) != 0) {
    free(context);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif
  device->_blitContext = context;
  return GPU_OK;
}

GPU_HIDE
void
gpuDestroyBlitDevice(GPUDevice *device) {
  GPUBlitContext *context;

  context = device ? device->_blitContext : NULL;
  if (!context) {
    return;
  }

  for (uint32_t variantIndex = 0u;
       variantIndex < GPU_BLIT_VARIANT_COUNT;
       variantIndex++) {
    GPUBlitVariant *variant = &context->variants[variantIndex];

    for (uint32_t format = 0u; format < GPU_FORMAT_COUNT; format++) {
      GPUDestroyRenderPipeline(variant->pipelines[format]);
    }
    GPUDestroyPipelineLayout(variant->pipelineLayout);
    GPUDestroyBindGroupLayout(variant->bindGroupLayout);
    GPUDestroyShaderLibrary(variant->library);
  }
  GPUDestroySampler(context->linearSampler);
  GPUDestroySampler(context->nearestSampler);
#if defined(_WIN32) || defined(WIN32)
  DeleteCriticalSection(&context->lock);
#else
  pthread_mutex_destroy(&context->lock);
#endif
  free(context);
  device->_blitContext = NULL;
}

GPU_HIDE
void
gpuDestroyTextureBlitViews(GPUTexture *texture) {
  GPUBlitContext *context;
  GPUBlitView    *entry;
  GPUBlitView    *next;

  context = texture && texture->device
              ? texture->device->_blitContext
              : NULL;
  if (!texture || !context) {
    return;
  }

  gpu_blitLock(context);
  for (entry = texture->_blitViews; entry; entry = next) {
    next = entry->next;
    for (uint32_t variant = 0u;
         variant < GPU_BLIT_VARIANT_COUNT;
         variant++) {
      GPUDestroyBindGroup(entry->groups[variant][1]);
      GPUDestroyBindGroup(entry->groups[variant][0]);
    }
    GPUDestroyTextureView(entry->view);
    free(entry);
  }
  texture->_blitViews = NULL;
  gpu_blitUnlock(context);
}

GPU_HIDE
void
gpuBlitTextureRenderFallback(GPUCommandBuffer         *cmdb,
                             const GPUTextureBlitInfo *info,
                             const GPUBlitShaderSet   *shaders) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      renderInfo = {0};
  GPUFormatCapabilities        srcCaps;
  GPUBlitContext              *context;
  GPUBlitVariant              *variant;
  GPUDevice                   *device;
  uint32_t                     variantIndex;

  device  = gpuCommandBufferDevice(cmdb);
  context = device ? device->_blitContext : NULL;
  if (!context ||
      GPUGetFormatCapabilities(device->adapter,
                               info->src->format,
                               &srcCaps) != GPU_OK) {
    return;
  }

  variantIndex = gpu_blitVariantIndex(info, &srcCaps);
  gpu_blitLock(context);
  if (!gpu_blitEnsureSamplers(device, context) ||
      !gpu_blitEnsureVariant(device, context, shaders, variantIndex)) {
    gpu_blitUnlock(context);
    return;
  }
  variant = &context->variants[variantIndex];
  if (!gpu_blitEnsurePipeline(device, variant, info->dst->format)) {
    gpu_blitUnlock(context);
    return;
  }

  for (uint32_t layer = 0u; layer < info->srcRegion.layerCount; layer++) {
    GPURenderPassEncoder *pass;
    GPUBlitView          *srcView;
    GPUBlitView          *dstView;
    GPUBindGroup         *group;
    GPUBlitParams         params = {0};
    GPUViewport           viewport;
    GPUScissorRect        scissor;
    uint32_t              srcMipWidth;
    uint32_t              srcMipHeight;
    uint32_t              dstMipWidth;
    uint32_t              dstMipHeight;

    srcView = gpu_blitEnsureView(
      info->src,
      info->srcRegion.texture.mipLevel,
      info->srcRegion.texture.baseArrayLayer + layer
    );
    dstView = gpu_blitEnsureView(
      info->dst,
      info->dstRegion.texture.mipLevel,
      info->dstRegion.texture.baseArrayLayer + layer
    );
    group = srcView
              ? gpu_blitEnsureGroup(device,
                                    context,
                                    variant,
                                    srcView,
                                    variantIndex,
                                    info->filter)
              : NULL;
    if (!srcView || !dstView || !group) {
      gpu_blitUnlock(context);
      return;
    }

    srcMipWidth = gpu_blitMipExtent(info->src->width,
                                    info->srcRegion.texture.mipLevel);
    srcMipHeight = gpu_blitMipExtent(info->src->height,
                                     info->srcRegion.texture.mipLevel);
    dstMipWidth = gpu_blitMipExtent(info->dst->width,
                                    info->dstRegion.texture.mipLevel);
    dstMipHeight = gpu_blitMipExtent(info->dst->height,
                                     info->dstRegion.texture.mipLevel);

    color.view    = dstView->view;
    color.loadOp  =
      info->dstRegion.texture.x == 0u &&
      info->dstRegion.texture.y == 0u &&
      info->dstRegion.width == dstMipWidth &&
      info->dstRegion.height == dstMipHeight
        ? GPU_LOAD_OP_DONT_CARE
        : GPU_LOAD_OP_LOAD;
    color.storeOp = GPU_STORE_OP_STORE;
    renderInfo.label                = "gpu-blit-render-fallback";
    renderInfo.pColorAttachments    = &color;
    renderInfo.colorAttachmentCount = 1u;

    params.srcRect[0]    = (float)info->srcRegion.texture.x;
    params.srcRect[1]    = (float)info->srcRegion.texture.y;
    params.srcRect[2]    = (float)info->srcRegion.width;
    params.srcRect[3]    = (float)info->srcRegion.height;
    params.dstRect[0]    = (float)info->dstRegion.texture.x;
    params.dstRect[1]    = (float)info->dstRegion.texture.y;
    params.dstRect[2]    = 1.0f / (float)info->dstRegion.width;
    params.dstRect[3]    = 1.0f / (float)info->dstRegion.height;
    params.invSrcSize[0] = 1.0f / (float)srcMipWidth;
    params.invSrcSize[1] = 1.0f / (float)srcMipHeight;

    viewport.x        = (float)info->dstRegion.texture.x;
    viewport.y        = (float)info->dstRegion.texture.y;
    viewport.width    = (float)info->dstRegion.width;
    viewport.height   = (float)info->dstRegion.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    scissor.x         = info->dstRegion.texture.x;
    scissor.y         = info->dstRegion.texture.y;
    scissor.width     = info->dstRegion.width;
    scissor.height    = info->dstRegion.height;

    gpu_blitUnlock(context);
    pass = GPUBeginRenderPass(cmdb, &renderInfo);
    if (!pass) {
      return;
    }
    GPUBindRenderPipeline(pass, variant->pipelines[info->dst->format]);
    GPUBindRenderGroup(pass, 0u, group, 0u, NULL);
    GPUSetViewport(pass, &viewport);
    GPUSetScissor(pass, &scissor);
    GPUSetRenderPushConstants(pass, 0u, sizeof(params), &params);
    GPUDraw(pass, 3u, 1u, 0u, 0u);
    GPUEndRenderPass(pass);
    gpu_blitLock(context);
  }
  gpu_blitUnlock(context);
}

GPU_EXPORT
void
GPUBlit(GPUCommandBuffer         *cmdb,
        const GPUTextureBlitInfo *info) {
  GPUFormatCapabilities srcCaps;
  GPUApi               *api;

  if (!gpu_blitInfoValid(cmdb, info, &srcCaps)) {
    return;
  }

  if (info->src->format == info->dst->format &&
      info->srcRegion.width == info->dstRegion.width &&
      info->srcRegion.height == info->dstRegion.height) {
    GPUTextureToTextureCopyRegion region = {0};
    GPUTransferPassEncoder       *pass;

    region.src        = info->srcRegion.texture;
    region.dst        = info->dstRegion.texture;
    region.width      = info->srcRegion.width;
    region.height     = info->srcRegion.height;
    region.depth      = 1u;
    region.layerCount = info->srcRegion.layerCount;
    pass = GPUBeginTransferPass(cmdb, "gpu-blit-native-copy");
    if (!pass) {
      return;
    }
    GPUCopyTextureToTexture(pass, info->src, info->dst, &region);
    GPUEndTransferPass(pass);
    return;
  }

  api = gpuCommandBufferApi(cmdb);
  if (api && api->renderPass.blitTexture) {
    api->renderPass.blitTexture(cmdb, info);
  }
}
