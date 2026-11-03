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

#ifndef gpu_tile_internal_h
#define gpu_tile_internal_h

#include "../common.h"

struct GPUTilePipeline {
  GPUApi                    *api;
  GPUDevice                 *device;
  GPUPipelineLayout         *layout;
  void                      *native;
  GPUTilePipelineProperties  properties;
  GPUFormat                  colorFormats[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  uint32_t                   colorCount;
  uint32_t                   sampleCount;
  uint32_t                   requiredGroupMask;
  uint32_t                   pushConstantSize;
  GPUShaderStageFlags        sourceStage;
  bool                       nonSquareDispatch;
};

GPU_HIDE
bool
tileRenderPassExtension(const GPURenderPassCreateInfo *info,
                        const GPUDevice              *device,
                        const GPUTileRenderPassEXT   **outTile);

#endif /* gpu_tile_internal_h */
