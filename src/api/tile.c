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

#include "tile_internal.h"
#include "device_internal.h"
#include "library_internal.h"
#include "descr/descriptor_internal.h"

static GPUResult
setPartialTilePushConstants(GPURenderPassEncoder *pass,
                           uint32_t              offset,
                           uint32_t              sizeBytes,
                           const void           *data) {
  uint8_t   previous[4096];
  GPUResult result;

  memcpy(previous, pass->_pushConstants + offset, sizeBytes);
  memcpy(pass->_pushConstants + offset, data, sizeBytes);
  result = pass->_api->tile.pushConstants(pass, pass->_pushConstants,
                                         pass->_pushConstantSizeBytes);
  if (result != GPU_OK)
    memcpy(pass->_pushConstants + offset, previous, sizeBytes);
  return result;
}

GPU_HIDE
bool
tileRenderPassExtension(const GPURenderPassCreateInfo *info,
                        const GPUDevice              *device,
                        const GPUTileRenderPassEXT   **outTile) {
  const GPUChainedStruct     *chain;
  const GPUTileRenderPassEXT *tile;

  *outTile = NULL;
  if (!info)
    return false;

  for (chain = info->chain.pNext; chain; chain = chain->pNext) {
    if (chain->sType != GPU_STRUCTURE_TYPE_TILE_RENDER_PASS_EXT)
      continue;

    tile = (const GPUTileRenderPassEXT *)chain;
    if (*outTile || (chain->structSize && chain->structSize < sizeof(*tile))
        || (!GPUIsFeatureEnabled(device, GPU_FEATURE_TILE_SHADERS)
            && !GPUIsFeatureEnabled(device, GPU_FEATURE_IMAGEBLOCKS))
        || ((tile->tileWidth == 0u) != (tile->tileHeight == 0u))
        || (tile->tileWidth && tile->tileWidth != 16u && tile->tileWidth != 32u)
        || (tile->tileHeight && tile->tileHeight != 16u && tile->tileHeight != 32u)
        || (tile->tileWidth == 16u && tile->tileHeight == 32u)
        || (tile->threadgroupMemoryLength & 15u)) {
      return false;
    }

    if (tile->imageblockSampleLength
        && !GPUIsFeatureEnabled(device, GPU_FEATURE_IMAGEBLOCKS))
      return false;

    *outTile = tile;
  }

  return true;
}

GPU_EXPORT
GPUResult
GPUCreateTilePipeline(GPUDevice                       *device,
                      const GPUTilePipelineCreateInfo *info,
                      GPUTilePipeline               **outPipeline) {
  GPUShaderReflection        reflection;
  GPUFormatCapabilities      caps;
  GPUApi                    *api;
  GPUTilePipeline           *pipeline;
  GPUShaderStageFlags        stage;
  GPUResult                  result;
  uint32_t                   samples;
  uint32_t                   executionFlags;
  uint32_t                   requiredGroups = 0u;
  uint32_t                   i;

  if (!outPipeline)
    return GPU_ERROR_INVALID_ARGUMENT;

  *outPipeline = NULL;
  if (!device || !info || !info->library || !info->entryPoint
      || info->library->_device != device || info->chain.pNext
      || (info->chain.sType != GPU_STRUCTURE_TYPE_NONE
          && info->chain.sType != GPU_STRUCTURE_TYPE_TILE_PIPELINE_CREATE_INFO)
      || (info->chain.structSize && info->chain.structSize < sizeof(*info))
      || (info->layout && info->layout->_device != device)
      || info->colorAttachmentCount > GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS
      || (info->colorAttachmentCount && !info->pColorFormats))
    return GPU_ERROR_INVALID_ARGUMENT;

  api = deviceApi(device);
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_TILE_SHADERS)
      || !api || !api->tile.createPipeline)
    return GPU_ERROR_UNSUPPORTED;

  if (!shaderEntryView(info->library, info->entryPoint, &stage, &reflection, &executionFlags)
      || (stage != GPU_SHADER_STAGE_FRAGMENT_BIT
          && (stage != GPU_SHADER_STAGE_COMPUTE_BIT
              || !(executionFlags & GPUShaderEntryExecutionTileBit))))
    return GPU_ERROR_UNSUPPORTED;

  if ((reflection.resourceCount || reflection.pushConstantSizeBytes) && !info->layout)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (reflection.pushConstantSizeBytes > sizeof(((GPURenderPassEncoder *)0)->_pushConstants)
      || (reflection.pushConstantSizeBytes & 3u))
    return GPU_ERROR_UNSUPPORTED;

  if (info->layout
      && !pipelineLayoutMatchesShaderEntries(info->layout,
                                             info->library,
                                             &info->entryPoint,
                                             1u,
                                             stage,
                                             &requiredGroups))
    return GPU_ERROR_INVALID_ARGUMENT;

  if (shaderFramebufferFetchMask(info->library, info->entryPoint)
      >> info->colorAttachmentCount)
    return GPU_ERROR_INVALID_ARGUMENT;

  samples = info->sampleCount ? info->sampleCount : 1u;
  if (samples != 1u && samples != 2u && samples != 4u && samples != 8u)
    return GPU_ERROR_INVALID_ARGUMENT;

  for (i = 0u; i < info->colorAttachmentCount; i++) {
    result = GPUGetFormatCapabilities(device->adapter, info->pColorFormats[i], &caps);
    if (result != GPU_OK)
      return result;

    if (!caps.colorAttachment || !(caps.supportedSampleCounts & samples))
      return GPU_ERROR_UNSUPPORTED;
  }

  if (!(pipeline = calloc(1, sizeof(*pipeline))))
    return GPU_ERROR_OUT_OF_MEMORY;

  pipeline->api         = api;
  pipeline->device      = device;
  pipeline->colorCount  = info->colorAttachmentCount;
  pipeline->sampleCount = samples;
  pipeline->layout      = info->layout;
  pipeline->sourceStage = stage;
  pipeline->requiredGroupMask = requiredGroups;
  pipeline->pushConstantSize  = reflection.pushConstantSizeBytes;
  if (pipeline->colorCount)
    memcpy(pipeline->colorFormats, info->pColorFormats,
           pipeline->colorCount * sizeof(*pipeline->colorFormats));

  result = api->tile.createPipeline(device, info, pipeline);
  if (result != GPU_OK) {
    free(pipeline);
    return result;
  }

  *outPipeline = pipeline;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyTilePipeline(GPUTilePipeline *pipeline) {
  if (!pipeline)
    return;

  pipeline->api->tile.destroyPipeline(pipeline);
  free(pipeline);
}

GPU_EXPORT
GPUResult
GPUGetTilePipelineProperties(const GPUTilePipeline     *pipeline,
                             GPUTilePipelineProperties *outProperties) {
  if (!pipeline || !outProperties)
    return GPU_ERROR_INVALID_ARGUMENT;

  *outProperties = pipeline->properties;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetRenderPassTileSize(const GPURenderPassEncoder *pass,
                         uint32_t                   *outWidth,
                         uint32_t                   *outHeight) {
  if (!pass || pass->_ended || !outWidth || !outHeight)
    return GPU_ERROR_INVALID_ARGUMENT;

  *outWidth  = 0u;
  *outHeight = 0u;
  if (!pass->_tileConfigured || !pass->_api->tile.getSize)
    return GPU_ERROR_UNSUPPORTED;

  pass->_api->tile.getSize(pass, outWidth, outHeight);
  return *outWidth && *outHeight ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

GPU_EXPORT
GPUResult
GPUBindTilePipeline(GPURenderPassEncoder *pass, GPUTilePipeline *pipeline) {
  GPUResult result;
  uint32_t  width;
  uint32_t  height;
  uint32_t  i;

  if (!pass || pass->_ended || !pipeline || pipeline->device != pass->_device)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (!pass->_tileConfigured || !pass->_api->tile.bindPipeline)
    return GPU_ERROR_UNSUPPORTED;

  if (pipeline->colorCount != pass->_colorAttachmentCount
      || pipeline->properties.imageblockSampleLength > pass->_tileImageblockSampleLength)
    return GPU_ERROR_INVALID_ARGUMENT;

  for (i = 0u; i < pipeline->colorCount; i++) {
    if (pipeline->colorFormats[i] != pass->_colorAttachmentFormats[i]
        || pipeline->sampleCount != pass->_colorAttachmentSampleCounts[i])
      return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (pass->_depthStencilSampleCount
      && pipeline->sampleCount != pass->_depthStencilSampleCount)
    return GPU_ERROR_INVALID_ARGUMENT;

  result = GPUGetRenderPassTileSize(pass, &width, &height);
  if (result != GPU_OK)
    return result;

  result = pass->_api->tile.bindPipeline(pass, pipeline);
  if (result != GPU_OK)
    return result;

  /* ordinary rendering sees a layout change on the next explicit rebind. */
  pass->_pipelineLayout = NULL;
  pass->_tileLayout = pipeline->layout;
  memset(pass->_boundGroups, 0, sizeof(pass->_boundGroups));
  memset(pass->_boundGroupLayouts, 0, sizeof(pass->_boundGroupLayouts));
  memset(pass->_boundDynamicOffsetCounts, 0, sizeof(pass->_boundDynamicOffsetCounts));
  pass->_tileBoundGroupMask    = 0u;
  pass->_tileSourceStage       = pipeline->sourceStage;
  pass->_requiredBindGroupMask = pipeline->requiredGroupMask;
  pass->_pushConstantSizeBytes = pipeline->pushConstantSize;
  pass->_pushConstantsEmitted  = false;
  if (pipeline->pushConstantSize)
    memset(pass->_pushConstants, 0, pipeline->pushConstantSize);

  pass->_tileWidth       = width;
  pass->_tileHeight      = height;
  pass->_tileMaxThreads  = pipeline->properties.maxThreadsPerThreadgroup;
  pass->_tileMatchesSize = pipeline->properties.threadgroupSizeMatchesTileSize;
  pass->_tileNonSquare   = pipeline->nonSquareDispatch;
  pass->_hasTilePipeline = true;
  pass->_hasPipeline     = false;
  pass->_pipeline        = NULL;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUBindTileGroup(GPURenderPassEncoder *pass,
                 uint32_t              groupIndex,
                 GPUBindGroup         *group,
                 uint32_t              dynamicOffsetCount,
                 const uint32_t       *pDynamicOffsets) {
  GPUResult result;

  if (!pass || pass->_ended || !pass->_hasTilePipeline || !pass->_tileLayout
      || !group || groupIndex >= GPU_ENCODER_MAX_BIND_GROUPS
      || bindGroupGetDevice(group) != pass->_device
      || !validateBindGroupDynamicOffsets(pass->_tileLayout, groupIndex, group,
                                          dynamicOffsetCount, pDynamicOffsets))
    return GPU_ERROR_INVALID_ARGUMENT;

  frameStatsRecordBindRequest(pass->_stats);
  if (bindGroupShadowMatches(pass->_boundGroups[groupIndex],
                             pass->_boundDynamicOffsetCounts[groupIndex],
                             pass->_boundDynamicOffsets[groupIndex],
                             group, dynamicOffsetCount, pDynamicOffsets))
    return GPU_OK;

  result = pass->_api->tile.bindGroup(pass, groupIndex, group,
                                     dynamicOffsetCount, pDynamicOffsets);
  if (result != GPU_OK)
    return result;

  pass->_boundGroups[groupIndex]       = group;
  pass->_boundGroupLayouts[groupIndex] = bindGroupGetLayout(group);
  storeBindGroupShadow(&pass->_boundDynamicOffsetCounts[groupIndex],
                       pass->_boundDynamicOffsets[groupIndex],
                       dynamicOffsetCount, pDynamicOffsets);
  pass->_tileBoundGroupMask |= 1u << groupIndex;
  frameStatsRecordBindEmission(pass->_stats);
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUSetTilePushConstants(GPURenderPassEncoder *pass,
                        uint32_t              offset,
                        uint32_t              sizeBytes,
                        const void           *data) {
  GPUResult result;

  if (!pass || pass->_ended || !pass->_hasTilePipeline
      || offset > pass->_pushConstantSizeBytes
      || sizeBytes > pass->_pushConstantSizeBytes - offset
      || ((offset | sizeBytes) & 3u) || (sizeBytes && !data))
    return GPU_ERROR_INVALID_ARGUMENT;

  if (!sizeBytes)
    return GPU_OK;

  frameStatsRecordStateRequest(pass->_stats);
  if (pass->_pushConstantsEmitted
      && memcmp(pass->_pushConstants + offset, data, sizeBytes) == 0)
    return GPU_OK;

  if (offset == 0u && sizeBytes == pass->_pushConstantSizeBytes) {
    result = pass->_api->tile.pushConstants(pass, data, sizeBytes);
    if (result == GPU_OK)
      memcpy(pass->_pushConstants, data, sizeBytes);
  } else {
    result = setPartialTilePushConstants(pass, offset, sizeBytes, data);
  }
  if (result != GPU_OK)
    return result;

  pass->_pushConstantsEmitted = true;
  frameStatsRecordStateEmission(pass->_stats);
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUDispatchThreadsPerTile(GPURenderPassEncoder *pass,
                          uint32_t              width,
                          uint32_t              height) {
  if (!pass || pass->_ended || !pass->_hasTilePipeline || !width || !height
      || width > pass->_tileWidth || height > pass->_tileHeight
      || (uint64_t)width * height > pass->_tileMaxThreads
      || (pass->_tileMatchesSize
          && (width != pass->_tileWidth || height != pass->_tileHeight)))
    return GPU_ERROR_INVALID_ARGUMENT;

  if ((pass->_tileBoundGroupMask & pass->_requiredBindGroupMask) != pass->_requiredBindGroupMask
      || (pass->_pushConstantSizeBytes && !pass->_pushConstantsEmitted))
    return GPU_ERROR_INVALID_ARGUMENT;

  if (width != height && !pass->_tileNonSquare)
    return GPU_ERROR_UNSUPPORTED;

  pass->_api->tile.dispatch(pass, width, height);
  return GPU_OK;
}
