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

#ifndef gpu_vrs_internal_h
#define gpu_vrs_internal_h

#include "common.h"

struct GPURasterizationRateMapEXT {
  void      *_priv;
  GPUDevice *device;
  uint32_t   layerCount;
};

static inline bool
gpuRenderPassVRSExtensions(
  const GPURenderPassCreateInfo               *info,
  const GPUShadingRateAttachmentEXT          **outAttachment,
  const GPURasterizationRateMapRenderPassEXT **outRateMap) {
  const GPUChainedStruct *chain;

  if (!info || !outAttachment || !outRateMap) {
    return false;
  }

  *outAttachment = NULL;
  *outRateMap     = NULL;
  chain           = info->chain.pNext;
  while (chain) {
    switch (chain->sType) {
      case GPU_STRUCTURE_TYPE_SHADING_RATE_ATTACHMENT_EXT:
        if (*outAttachment ||
            (chain->structSize != 0u &&
             chain->structSize < sizeof(GPUShadingRateAttachmentEXT))) {
          return false;
        }
        *outAttachment = (const GPUShadingRateAttachmentEXT *)chain;
        break;
      case GPU_STRUCTURE_TYPE_RASTERIZATION_RATE_MAP_RENDER_PASS_EXT:
        if (*outRateMap ||
            (chain->structSize != 0u &&
             chain->structSize <
               sizeof(GPURasterizationRateMapRenderPassEXT))) {
          return false;
        }
        *outRateMap =
          (const GPURasterizationRateMapRenderPassEXT *)chain;
        break;
      default:
        return false;
    }
    chain = chain->pNext;
  }

  return !*outAttachment || !*outRateMap;
}

#endif /* gpu_vrs_internal_h */
