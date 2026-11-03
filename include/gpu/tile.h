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

#ifndef gpu_tile_h
#define gpu_tile_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "device.h"
#include "library.h"
#include "pass.h"

typedef struct GPUTilePipeline GPUTilePipeline;

typedef struct GPUTilePipelineCreateInfo {
  GPUChainedStruct  chain;
  const char       *label;
  GPUShaderLibrary *library;
  const char       *entryPoint;
  const GPUFormat  *pColorFormats;
  uint32_t          colorAttachmentCount;
  uint32_t          sampleCount;
  bool              threadgroupSizeMatchesTileSize;
  GPUPipelineLayout *layout;
} GPUTilePipelineCreateInfo;

typedef struct GPUTilePipelineProperties {
  uint32_t maxThreadsPerThreadgroup;
  uint32_t imageblockSampleLength;
  bool     threadgroupSizeMatchesTileSize;
} GPUTilePipelineProperties;

/* chain onto GPURenderPassCreateInfo; zero tile dimensions request native sizing. */
typedef struct GPUTileRenderPassEXT {
  GPUChainedStruct chain;
  uint32_t         tileWidth;
  uint32_t         tileHeight;
  uint32_t         imageblockSampleLength;
  uint32_t         threadgroupMemoryLength;
} GPUTileRenderPassEXT;

/* typed USL fragment and tile kernel entries use their source-stage layout.
 * pipelines, layouts, groups and resources outlive recorded GPU work using them.
 */
GPU_EXPORT
GPUResult
GPUCreateTilePipeline(GPUDevice                       *device,
                      const GPUTilePipelineCreateInfo *info,
                      GPUTilePipeline               **outPipeline);

GPU_EXPORT
void
GPUDestroyTilePipeline(GPUTilePipeline *pipeline);

GPU_EXPORT
GPUResult
GPUGetTilePipelineProperties(const GPUTilePipeline     *pipeline,
                             GPUTilePipelineProperties *outProperties);

/* tile and ordinary render pipelines require explicit rebinding when switching. */
GPU_EXPORT
GPUResult
GPUBindTilePipeline(GPURenderPassEncoder *pass, GPUTilePipeline *pipeline);

/* mutable-resource synchronization remains caller-owned; fragment tile dispatch
 * is not a cross-pixel or global storage barrier.
 */
GPU_EXPORT
GPUResult
GPUBindTileGroup(GPURenderPassEncoder *pass,
                 uint32_t              groupIndex,
                 GPUBindGroup         *group,
                 uint32_t              dynamicOffsetCount,
                 const uint32_t       *pDynamicOffsets);

/* a pipeline bind zeros the block; upload once before dispatch when required. */
GPU_EXPORT
GPUResult
GPUSetTilePushConstants(GPURenderPassEncoder *pass,
                        uint32_t              offset,
                        uint32_t              sizeBytes,
                        const void           *data);

GPU_EXPORT
GPUResult
GPUDispatchThreadsPerTile(GPURenderPassEncoder *pass,
                          uint32_t              width,
                          uint32_t              height);

GPU_EXPORT
GPUResult
GPUGetRenderPassTileSize(const GPURenderPassEncoder *pass,
                         uint32_t                   *outWidth,
                         uint32_t                   *outHeight);

#ifdef __cplusplus
}
#endif
#endif /* gpu_tile_h */
