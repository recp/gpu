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
#include "pipeline_cache.h"
#include "../../../api/tile_internal.h"
#include "../../../api/library_internal.h"

static GPUResult
mt_createTilePipeline(GPUDevice                       *device,
                       const GPUTilePipelineCreateInfo *info,
                       GPUTilePipeline                *pipeline) {
  GPUShaderFunction               *function;
  MTShaderFunction                *shader;
  GPUDeviceMT                     *native;
  MTLTileRenderPipelineDescriptor  *descriptor;
  id<MTLRenderPipelineState>        state = nil;
  NSError                         *error = nil;
  uint32_t                         i;

  native = device->_priv;
  if (!(function = shaderFunction(info->library, info->entryPoint)))
    return GPU_ERROR_BACKEND_FAILURE;

  shader = function->_priv;
#if MT_HAS_METAL4
  if (native->commandMode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      MTL4TileRenderPipelineDescriptor   *modern;
      MTL4LibraryFunctionDescriptor     *entry;
      MTL4SpecializedFunctionDescriptor *specialized;

      entry         = [MTL4LibraryFunctionDescriptor new];
      entry.library = shader->library;
      entry.name    = shader->name;
      modern        = [MTL4TileRenderPipelineDescriptor new];
      if (shader->constants) {
        specialized                    = [MTL4SpecializedFunctionDescriptor new];
        specialized.functionDescriptor = entry;
        specialized.constantValues     = shader->constants;
        modern.tileFunctionDescriptor  = specialized;
        [specialized release];
      } else {
        modern.tileFunctionDescriptor = entry;
      }
      [entry release];

      if (info->label)
        modern.label = [NSString stringWithUTF8String:info->label];

      modern.rasterSampleCount = pipeline->sampleCount;
      modern.threadgroupSizeMatchesTileSize = info->threadgroupSizeMatchesTileSize;
      for (i = 0u; i < pipeline->colorCount; i++) {
        modern.colorAttachments[i].pixelFormat = mt_format(pipeline->colorFormats[i]);
      }

      state = mt_compileRenderPipeline4(NULL, native, modern, &error);
      [modern release];
    }
  } else
#endif
  {
    if (@available(macOS 11.0, iOS 11.0, *)) {
      descriptor = [MTLTileRenderPipelineDescriptor new];
      if (info->label)
        descriptor.label = [NSString stringWithUTF8String:info->label];

      descriptor.tileFunction = shader->function;
      descriptor.rasterSampleCount = pipeline->sampleCount;
      descriptor.threadgroupSizeMatchesTileSize = info->threadgroupSizeMatchesTileSize;
      for (i = 0u; i < pipeline->colorCount; i++) {
        descriptor.colorAttachments[i].pixelFormat = mt_format(pipeline->colorFormats[i]);
      }

      state = [native->device newRenderPipelineStateWithTileDescriptor:descriptor
                                                               options:MTLPipelineOptionNone
                                                            reflection:nil
                                                                 error:&error];
      [descriptor release];
    }
  }

  destroyShaderFunction(info->library, function);
  if (!state) {
    if (error)
      fprintf(stderr, "GPU: tile pipeline: %s\n", error.localizedDescription.UTF8String);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (@available(macOS 11.0, iOS 13.0, *)) {
    if (shaderUsesImageblock(info->library, info->entryPoint))
      pipeline->properties.imageblockSampleLength = (uint32_t)state.imageblockSampleLength;
    pipeline->properties.maxThreadsPerThreadgroup = (uint32_t)state.maxTotalThreadsPerThreadgroup;
    pipeline->properties.threadgroupSizeMatchesTileSize = state.threadgroupSizeMatchesTileSize;
    pipeline->nonSquareDispatch = [native->device supportsFamily:MTLGPUFamilyApple5];
  }

  pipeline->native = state;
  return GPU_OK;
}

static void
mt_destroyTilePipeline(GPUTilePipeline *pipeline) {
  [(id<MTLRenderPipelineState>)pipeline->native release];
}

static GPUResult
mt_bindTilePipeline(GPURenderPassEncoder *pass, GPUTilePipeline *pipeline) {
  MTRenderEncoder *native;

  native = pass->_priv;
#if MT_HAS_METAL4
  if (native->modern) {
    /* native validation compares the requested dimensions, not the resolved size. */
    if (pipeline->properties.threadgroupSizeMatchesTileSize && !pass->_tileExplicitSize)
      return GPU_ERROR_UNSUPPORTED;

    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern setRenderPipelineState:(id<MTLRenderPipelineState>)pipeline->native];
      return GPU_OK;
    }
    return GPU_ERROR_UNSUPPORTED;
  }
#endif
  [native->classic setRenderPipelineState:(id<MTLRenderPipelineState>)pipeline->native];
  return GPU_OK;
}

static void
mt_dispatchTile(GPURenderPassEncoder *pass, uint32_t width, uint32_t height) {
  MTRenderEncoder *native;
  MTLSize          size;

  native = pass->_priv;
  size   = MTLSizeMake(width, height, 1u);
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *))
      [native->modern dispatchThreadsPerTile:size];
    return;
  }
#endif
  if (@available(macOS 11.0, iOS 11.0, *))
    [native->classic dispatchThreadsPerTile:size];
}

static void
mt_getTileSize(const GPURenderPassEncoder *pass, uint32_t *width, uint32_t *height) {
  MTRenderEncoder *native;

  native = pass->_priv;
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      *width  = (uint32_t)[native->modern tileWidth];
      *height = (uint32_t)[native->modern tileHeight];
    }
    return;
  }
#endif
  if (@available(macOS 11.0, iOS 11.0, *)) {
    *width  = (uint32_t)native->classic.tileWidth;
    *height = (uint32_t)native->classic.tileHeight;
  }
}

GPU_HIDE
void
mt_initTile(GPUTileApi *api) {
  api->createPipeline  = mt_createTilePipeline;
  api->destroyPipeline = mt_destroyTilePipeline;
  api->bindPipeline    = mt_bindTilePipeline;
  api->dispatch        = mt_dispatchTile;
  api->getSize         = mt_getTileSize;
}
