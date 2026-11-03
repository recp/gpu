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

#ifndef gpu_api_tile_h
#define gpu_api_tile_h

#include <gpu/tile.h>

typedef struct GPUTileApi {
  GPUResult (*createPipeline)(GPUDevice *device,
                              const GPUTilePipelineCreateInfo *info,
                              GPUTilePipeline *pipeline);
  void (*destroyPipeline)(GPUTilePipeline *pipeline);
  GPUResult (*bindPipeline)(GPURenderPassEncoder *pass, GPUTilePipeline *pipeline);
  GPUResult (*bindGroup)(GPURenderPassEncoder *pass,
                          uint32_t groupIndex,
                          GPUBindGroup *group,
                          uint32_t dynamicOffsetCount,
                          const uint32_t *dynamicOffsets);
  GPUResult (*pushConstants)(GPURenderPassEncoder *pass,
                              const void *data,
                              uint32_t sizeBytes);
  void (*dispatch)(GPURenderPassEncoder *pass, uint32_t width, uint32_t height);
  void (*getSize)(const GPURenderPassEncoder *pass, uint32_t *width, uint32_t *height);
} GPUTileApi;

#endif /* gpu_api_tile_h */
