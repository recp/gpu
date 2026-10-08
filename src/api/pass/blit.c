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
  GPU_BLIT_VARIANT_FLOAT_FILTERING_ARRAY,
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

typedef struct BlitVariant {
  GPUShaderLibrary   *library;
  GPUBindGroupLayout *bindGroupLayout;
  GPUPipelineLayout  *pipelineLayout;
  GPURenderPipeline  *pipelines[GPU_FORMAT_COUNT];
} BlitVariant;

typedef struct BlitContext {
  GPUSampler    *nearestSampler;
  GPUSampler    *linearSampler;
  BlitVariant    variants[GPU_BLIT_VARIANT_COUNT];
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION lock;
#else
  pthread_mutex_t lock;
#endif
} BlitContext;

typedef struct BlitView {
  struct BlitView    *next;
  GPUTextureView     *view;
  GPUBindGroup       *groups[GPU_BLIT_VARIANT_COUNT][2];
  uint32_t            mipLevel;
  uint32_t            arrayLayer;
} BlitView;

static void
blitLock(BlitContext    *context) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&context->lock);
#else
  pthread_mutex_lock(&context->lock);
#endif
}

static void
blitUnlock(BlitContext    *context) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&context->lock);
#else
  pthread_mutex_unlock(&context->lock);
#endif
}

static uint32_t
blitMipExtent(uint32_t extent, uint32_t mipLevel) {
  extent >>= mipLevel;
  return extent > 0u ? extent : 1u;
}

static bool
blitRegionValid(const GPUTextureSubresourceRegion *region,
                const GPUTexture                  *texture) {
  uint32_t mipWidth;
  uint32_t mipHeight;

  if (!region || !texture
      || texture->dimension != GPU_TEXTURE_DIMENSION_2D
      || texture->sampleCount != 1u
      || region->texture.aspect != GPU_TEXTURE_ASPECT_ALL
      || region->texture.z != 0u
      || region->texture.mipLevel >= texture->mipLevelCount
      || region->width == 0u
      || region->height == 0u
      || region->depth != 1u
      || region->layerCount == 0u
      || region->texture.baseArrayLayer >= texture->depthOrLayers
      || region->layerCount > texture->depthOrLayers - region->texture.baseArrayLayer) {
    return false;
  }

  mipWidth  = blitMipExtent(texture->width,
                            region->texture.mipLevel);
  mipHeight = blitMipExtent(texture->height,
                            region->texture.mipLevel);

  return region->texture.x < mipWidth
         && region->texture.y < mipHeight
         && region->width <= mipWidth - region->texture.x
         && region->height <= mipHeight - region->texture.y;
}

static bool
blitSubresourcesOverlap(const GPUTextureBlitInfo *info) {
  uint32_t srcFirst;
  uint32_t srcLast;
  uint32_t dstFirst;
  uint32_t dstLast;

  if (!info || info->src != info->dst
      || info->srcRegion.texture.mipLevel != info->dstRegion.texture.mipLevel) {
    return false;
  }

  srcFirst = info->srcRegion.texture.baseArrayLayer;
  srcLast  = srcFirst + info->srcRegion.layerCount;
  dstFirst = info->dstRegion.texture.baseArrayLayer;
  dstLast  = dstFirst + info->dstRegion.layerCount;

  return srcFirst < dstLast && dstFirst < srcLast;
}

static bool
blitInfoValid(GPUCommandBuffer         *cmdb,
              const GPUTextureBlitInfo *info,
              GPUFormatCapabilities    *outSrcCaps) {
  GPUDevice           *device;
  FormatNumericType    srcType;
  FormatNumericType    dstType;

  device = commandBufferDevice(cmdb);

  if (!cmdb || !device || cmdb->_submitted || cmdb->_activeEncoder
      || !cmdb->_queue
      || (cmdb->_queue->bits & GPU_QUEUE_GRAPHICS_BIT) == 0u
      || !info || !info->src || !info->dst
      || info->src->device != device || info->dst->device != device
      || info->filter > GPU_FILTER_LINEAR
      || info->src->depthOrLayers != 1u
      || info->dst->depthOrLayers != 1u
      || (info->src->usage &
       (GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_SRC)) !=
        (GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_SRC)
      || (info->dst->usage &
       (GPU_TEXTURE_USAGE_COLOR_TARGET | GPU_TEXTURE_USAGE_COPY_DST)) !=
        (GPU_TEXTURE_USAGE_COLOR_TARGET | GPU_TEXTURE_USAGE_COPY_DST)
      || info->srcRegion.layerCount != info->dstRegion.layerCount
      || !blitRegionValid(&info->srcRegion, info->src)
      || !blitRegionValid(&info->dstRegion, info->dst)
      || blitSubresourcesOverlap(info)) {
    return false;
  }

  srcType = formatNumericType(info->src->format);
  dstType = formatNumericType(info->dst->format);

  if (srcType != dstType
      || (info->filter == GPU_FILTER_LINEAR
          && srcType != GPU_FORMAT_NUMERIC_FLOAT)
      || GPUGetFormatCapabilities(device->adapter,
                                  info->src->format,
                                  outSrcCaps) != GPU_OK
      || !outSrcCaps->sampled
      || (info->filter == GPU_FILTER_LINEAR && !outSrcCaps->filterable)) {
    return false;
  }

  return true;
}

static const BlitShaderData*
blitShaderData(const BlitShaderSet    *shaders, uint32_t variant) {
  if (!shaders) {
    return NULL;
  }

  switch (variant) {
    case GPU_BLIT_VARIANT_FLOAT_FILTERING:
      return &shaders->filteringFloat;
    case GPU_BLIT_VARIANT_FLOAT_FILTERING_ARRAY:
      return &shaders->filteringFloatArray;
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
blitEnsureSamplers(GPUDevice *device, BlitContext    *context) {
  GPUSamplerCreateInfo info = {0};

  if (context->nearestSampler && context->linearSampler) {
    return true;
  }

  info.label              = "gpu-blit-nearest";
  info.desc.minFilter     = GPU_FILTER_NEAREST;
  info.desc.magFilter     = GPU_FILTER_NEAREST;
  info.desc.mipFilter     = GPU_MIP_FILTER_NEAREST;
  info.desc.addressU      = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.desc.addressV      = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.desc.addressW      = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.desc.maxAnisotropy = 1u;

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
blitSampleType(uint32_t variant) {
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
blitEnsureVariant(GPUDevice              *device,
                  BlitContext            *context,
                  const BlitShaderSet    *shaders,
                  uint32_t                variantIndex) {
  GPUBindGroupLayoutCreateInfo bindGroupInfo = {0};
  GPUPipelineLayoutCreateInfo  pipelineInfo  = {0};
  GPUBindGroupLayoutEntry      entries[2]    = {0};
  GPUBindGroupLayout          *layouts[1];
  BlitVariant                 *variant;
  GPUShaderLibrary            *library;
  Api                         *api;
  const BlitShaderData        *shader;

  variant = &context->variants[variantIndex];

  if (variant->library && variant->bindGroupLayout
      && variant->pipelineLayout) {
    return true;
  }

  api    = deviceApi(device);
  shader = blitShaderData(shaders, variantIndex);

  if (!api || !shader || !shader->data || shader->size == 0u
      || (shader->binary && !api->library.newLibraryWithBinary)
      || (!shader->binary && !api->library.newLibraryWithSource)) {
    return false;
  }

  library = shader->binary
              ? api->library.newLibraryWithBinary(device,
                                                  shader->data,
                                                  shader->size)
              : api->library.newLibraryWithSource(device,
                                                  shader->data,
                                                  shader->size,
                                                  GPU_SHADER_SOURCE_COMPILE_NONE);

  if (!library) {
    return false;
  }

  library->_api    = api;
  library->_device = device;

  entries[0].binding                     = 0u;
  entries[0].arrayCount                  = 1u;
  entries[0].bindingType                 = GPU_BINDING_SAMPLED_TEXTURE;
  entries[0].visibility                  = GPU_SHADER_STAGE_FRAGMENT_BIT;
  entries[0].sampledTexture.viewType     = variantIndex == GPU_BLIT_VARIANT_FLOAT_FILTERING_ARRAY
                                             ? GPU_TEXTURE_VIEW_2D_ARRAY
                                             : GPU_TEXTURE_VIEW_2D;
  entries[0].sampledTexture.sampleType   = blitSampleType(variantIndex);
  entries[0].sampledTexture.multisampled = false;
  entries[1].binding                     = 1u;
  entries[1].arrayCount                  = 1u;
  entries[1].bindingType                 = GPU_BINDING_SAMPLER;
  entries[1].visibility                  = GPU_SHADER_STAGE_FRAGMENT_BIT;
  entries[1].sampler.type                = variantIndex == GPU_BLIT_VARIANT_FLOAT_FILTERING
                                           || variantIndex == GPU_BLIT_VARIANT_FLOAT_FILTERING_ARRAY
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
blitEnsurePipeline(GPUDevice      *device,
                   BlitVariant    *variant,
                   GPUFormat       format) {
  GPURenderPipelineCreateInfo info  = {0};
  GPUColorTargetState         color = {0};

  if (variant->pipelines[format]) {
    return true;
  }

  color.format                 = format;
  info.label                   = "gpu-blit-pipeline";
  info.layout                  = variant->pipelineLayout;
  info.library                 = variant->library;
  info.vertexEntry             = "gpu_blit_vs";
  info.fragmentEntry           = "gpu_blit_fs";
  info.pColorTargets           = &color;
  info.colorTargetCount        = 1u;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;

  return GPUCreateRenderPipeline(device,
                                 &info,
                                 &variant->pipelines[format]) == GPU_OK;
}

static BlitView*
blitEnsureView(GPUTexture *texture,
               uint32_t    mipLevel,
               uint32_t    arrayLayer) {
  GPUTextureViewCreateInfo info = {0};
  BlitView                *entry;

  for (entry = texture->_blitViews; entry; entry = entry->next) {
    if (entry->mipLevel == mipLevel
        && entry->arrayLayer == arrayLayer) {
      return entry;
    }
  }

  if (!(entry = calloc(1, sizeof(*entry)))) {
    return NULL;
  }

  info.label           = "gpu-blit-view";
  info.viewType        = texture->depthOrLayers > 1u
                           ? GPU_TEXTURE_VIEW_2D_ARRAY
                           : GPU_TEXTURE_VIEW_2D;
  info.format          = texture->format;
  info.baseMipLevel    = mipLevel;
  info.mipLevelCount   = 1u;
  info.baseArrayLayer  = arrayLayer;
  info.arrayLayerCount = 1u;

  if (GPUCreateTextureView(texture, &info, &entry->view) != GPU_OK) {
    free(entry);
    return NULL;
  }

  entry->mipLevel     = mipLevel;
  entry->arrayLayer   = arrayLayer;
  entry->next         = texture->_blitViews;
  texture->_blitViews = entry;

  return entry;
}

static GPUBindGroup*
blitEnsureGroup(GPUDevice      *device,
                BlitContext    *context,
                BlitVariant    *variant,
                BlitView       *view,
                uint32_t        variantIndex,
                GPUFilter       filter) {
  GPUBindGroupCreateInfo info       = {0};
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
  info.label             = "gpu-blit-bind-group";
  info.layout            = variant->bindGroupLayout;
  info.pEntries          = entries;
  info.entryCount        = (uint32_t)GPU_ARRAY_LEN(entries);

  if (GPUCreateBindGroup(device,
                         &info,
                         &view->groups[variantIndex][filterIndex]) != GPU_OK) {
    return NULL;
  }

  return view->groups[variantIndex][filterIndex];
}

static uint32_t
blitVariantIndex(const GPUTextureBlitInfo    *info,
                 const GPUFormatCapabilities *srcCaps) {
  if (info->src->depthOrLayers > 1u) {
    return GPU_BLIT_VARIANT_FLOAT_FILTERING_ARRAY;
  }

  switch (formatNumericType(info->src->format)) {
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

static bool
generateMipmapsValid(GPUCommandBuffer *cmdb, GPUTexture *texture) {
  GPUFormatCapabilities caps;
  GPUDevice            *device;

  device = commandBufferDevice(cmdb);

  return cmdb && device && !cmdb->_submitted && !cmdb->_activeEncoder
         && cmdb->_queue
         && (cmdb->_queue->bits & GPU_QUEUE_GRAPHICS_BIT) != 0u
         && texture && texture->device == device
         && texture->dimension == GPU_TEXTURE_DIMENSION_2D
         && texture->mipLevelCount > 1u
         && texture->sampleCount == 1u
         && (texture->usage &
          (GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COLOR_TARGET)) ==
           (GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COLOR_TARGET)
         && formatNumericType(texture->format) == GPU_FORMAT_NUMERIC_FLOAT
         && GPUGetFormatCapabilities(device->adapter,
                                     texture->format,
                                     &caps) == GPU_OK
         && caps.sampled && caps.filterable && caps.colorAttachment;
}

GPU_HIDE
GPUResult
initBlitDevice(GPUDevice *device) {
  BlitContext    *context;

  if (!device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(context = calloc(1, sizeof(*context)))) {
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
destroyBlitDevice(GPUDevice *device) {
  BlitContext    *context;
  BlitVariant    *variant;
  uint32_t        variantIndex;
  uint32_t        format;

  context = device ? device->_blitContext : NULL;

  if (!context) {
    return;
  }

  for (variantIndex = 0u; variantIndex < GPU_BLIT_VARIANT_COUNT; variantIndex++) {
    variant = &context->variants[variantIndex];

    for (format = 0u; format < GPU_FORMAT_COUNT; format++) {
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
destroyTextureBlitViews(GPUTexture *texture) {
  BlitContext    *context;
  BlitView       *entry;
  BlitView       *next;
  uint32_t        variant;

  context = texture && texture->device
              ? texture->device->_blitContext
              : NULL;

  if (!texture || !context) {
    return;
  }

  blitLock(context);

  for (entry = texture->_blitViews; entry; entry = next) {
    next = entry->next;

    for (variant = 0u; variant < GPU_BLIT_VARIANT_COUNT; variant++) {
      GPUDestroyBindGroup(entry->groups[variant][1]);
      GPUDestroyBindGroup(entry->groups[variant][0]);
    }

    GPUDestroyTextureView(entry->view);
    free(entry);
  }

  texture->_blitViews = NULL;
  blitUnlock(context);
}

GPU_HIDE
void
blitTextureRenderFallback(GPUCommandBuffer         *cmdb,
                          const GPUTextureBlitInfo *info,
                          const BlitShaderSet      *shaders) {
  GPURenderPassColorAttachment color      = {0};
  GPURenderPassCreateInfo      renderInfo = {0};
  GPUFormatCapabilities        srcCaps;
  BlitContext                 *context;
  BlitVariant                 *variant;
  GPUDevice                   *device;
  GPURenderPassEncoder        *pass;
  BlitView                    *srcView;
  BlitView                    *dstView;
  GPUBindGroup                *group;
  uint32_t                     variantIndex;
  uint32_t                     layer;
  uint32_t                     srcMipWidth;
  uint32_t                     srcMipHeight;
  uint32_t                     dstMipWidth;
  uint32_t                     dstMipHeight;

  device  = commandBufferDevice(cmdb);
  context = device ? device->_blitContext : NULL;

  if (!context
      || GPUGetFormatCapabilities(device->adapter,
                                  info->src->format,
                                  &srcCaps) != GPU_OK) {
    return;
  }

  variantIndex = blitVariantIndex(info, &srcCaps);
  blitLock(context);

  if (!blitEnsureSamplers(device, context)
      || !blitEnsureVariant(device, context, shaders, variantIndex)) {
    blitUnlock(context);
    return;
  }

  variant = &context->variants[variantIndex];

  if (!blitEnsurePipeline(device, variant, info->dst->format)) {
    blitUnlock(context);
    return;
  }

  for (layer = 0u; layer < info->srcRegion.layerCount; layer++) {
    GPUBlitParams  params = {0};
    GPUViewport    viewport;
    GPUScissorRect scissor;

    srcView = blitEnsureView(info->src,
                             info->srcRegion.texture.mipLevel,
                             info->srcRegion.texture.baseArrayLayer + layer);
    dstView = blitEnsureView(info->dst,
                             info->dstRegion.texture.mipLevel,
                             info->dstRegion.texture.baseArrayLayer + layer);
    group   = srcView
              ? blitEnsureGroup(device,
                                context,
                                variant,
                                srcView,
                                variantIndex,
                                info->filter)
              : NULL;

    if (!srcView || !dstView || !group) {
      blitUnlock(context);
      return;
    }

    srcMipWidth  = blitMipExtent(info->src->width,
                                 info->srcRegion.texture.mipLevel);
    srcMipHeight = blitMipExtent(info->src->height,
                                 info->srcRegion.texture.mipLevel);
    dstMipWidth  = blitMipExtent(info->dst->width,
                                 info->dstRegion.texture.mipLevel);
    dstMipHeight = blitMipExtent(info->dst->height,
                                 info->dstRegion.texture.mipLevel);

    color.view                      = dstView->view;
    color.loadOp                    = info->dstRegion.texture.x == 0u
                                      && info->dstRegion.texture.y == 0u
                                      && info->dstRegion.width == dstMipWidth
                                      && info->dstRegion.height == dstMipHeight
                                        ? GPU_LOAD_OP_DONT_CARE
                                        : GPU_LOAD_OP_LOAD;
    color.storeOp                   = GPU_STORE_OP_STORE;
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
    params.invSrcSize[2] = info->filter == GPU_FILTER_LINEAR ? 1.0f : 0.0f;

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

    blitUnlock(context);

    if (!(pass = GPUBeginRenderPass(cmdb, &renderInfo))) {
      return;
    }

    GPUBindRenderPipeline(pass, variant->pipelines[info->dst->format]);
    GPUBindRenderGroup(pass, 0u, group, 0u, NULL);
    GPUSetViewport(pass, &viewport);
    GPUSetScissor(pass, &scissor);
    GPUSetRenderPushConstants(pass, 0u, sizeof(params), &params);
    GPUDraw(pass, 3u, 1u, 0u, 0u);
    GPUEndRenderPass(pass);
    blitLock(context);
  }

  blitUnlock(context);
}

GPU_HIDE
void
generateMipmapsFallback(GPUCommandBuffer *cmdb,
                        GPUTexture       *texture,
                        void            (*blitTexture)(GPUCommandBuffer         *cmdb,
                                                       const GPUTextureBlitInfo *info)) {
  GPUTextureBlitInfo info           = {0};
  GPUTextureBarrier  textureBarrier = {0};
  GPUBarrierBatch    barrierBatch   = {0};
  uint32_t           mipLevel;

  if (!cmdb || !texture || !blitTexture) {
    return;
  }

  info.src                      = texture;
  info.dst                      = texture;
  info.srcRegion.texture.aspect = GPU_TEXTURE_ASPECT_ALL;
  info.srcRegion.depth          = 1u;
  info.srcRegion.layerCount     = texture->depthOrLayers;
  info.dstRegion.texture.aspect = GPU_TEXTURE_ASPECT_ALL;
  info.dstRegion.depth          = 1u;
  info.dstRegion.layerCount     = texture->depthOrLayers;
  info.filter                   = GPU_FILTER_LINEAR;

  for (mipLevel = 1u; mipLevel < texture->mipLevelCount; mipLevel++) {
    info.srcRegion.texture.mipLevel = mipLevel - 1u;
    info.srcRegion.width            = blitMipExtent(texture->width, mipLevel - 1u);
    info.srcRegion.height           = blitMipExtent(texture->height, mipLevel - 1u);
    info.dstRegion.texture.mipLevel = mipLevel;
    info.dstRegion.width            = blitMipExtent(texture->width, mipLevel);
    info.dstRegion.height           = blitMipExtent(texture->height, mipLevel);
    blitTexture(cmdb, &info);

    if (mipLevel + 1u < texture->mipLevelCount) {
      textureBarrier.texture           = texture;
      textureBarrier.srcAccess         = GPU_ACCESS_COLOR_WRITE;
      textureBarrier.dstAccess         = GPU_ACCESS_SHADER_READ;
      textureBarrier.baseMip           = mipLevel;
      textureBarrier.mipCount          = 1u;
      textureBarrier.layerCount        = texture->depthOrLayers;
      barrierBatch.pTextureBarriers    = &textureBarrier;
      barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
      barrierBatch.dstStages           = GPU_STAGE_FRAGMENT;
      barrierBatch.textureBarrierCount = 1u;
      GPUEncodeBarriers(cmdb, &barrierBatch);
    }
  }
}

GPU_EXPORT
void
GPUBlit(GPUCommandBuffer         *cmdb,
        const GPUTextureBlitInfo *info) {
  GPUFormatCapabilities   srcCaps;
  Api                    *api;
  GPUTransferPassEncoder *pass;

  if (!blitInfoValid(cmdb, info, &srcCaps)) {
    return;
  }

  if (info->src->format == info->dst->format
      && info->srcRegion.width == info->dstRegion.width
      && info->srcRegion.height == info->dstRegion.height) {
    GPUTextureToTextureCopyRegion region = {0};

    region.src        = info->srcRegion.texture;
    region.dst        = info->dstRegion.texture;
    region.width      = info->srcRegion.width;
    region.height     = info->srcRegion.height;
    region.depth      = 1u;
    region.layerCount = info->srcRegion.layerCount;

    if (!(pass = GPUBeginTransferPass(cmdb, "gpu-blit-native-copy"))) {
      return;
    }

    GPUCopyTextureToTexture(pass, info->src, info->dst, &region);
    GPUEndTransferPass(pass);
    return;
  }

  if ((api = commandBufferApi(cmdb)) && api->renderPass.blitTexture) {
    api->renderPass.blitTexture(cmdb, info);
  }
}

GPU_EXPORT
void
GPUGenerateMipmaps(GPUCommandBuffer *cmdb, GPUTexture *texture) {
  Api    *api;

  if (!generateMipmapsValid(cmdb, texture)) {
    return;
  }

  if (!(api = commandBufferApi(cmdb))) {
    return;
  }

  if (api->renderPass.generateMipmaps) {
    api->renderPass.generateMipmaps(cmdb, texture);
  } else if (api->renderPass.blitTexture) {
    generateMipmapsFallback(cmdb,
                            texture,
                            api->renderPass.blitTexture);
  }
}
