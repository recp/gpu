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

#ifndef gpu_sampler_feedback_h
#define gpu_sampler_feedback_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "format.h"

typedef struct GPUAdapter               GPUAdapter;
typedef struct GPUCommandBuffer         GPUCommandBuffer;
typedef struct GPUDevice                GPUDevice;
typedef struct GPUTexture               GPUTexture;
typedef struct GPUSamplerFeedbackMapEXT GPUSamplerFeedbackMapEXT;

typedef enum GPUSamplerFeedbackTierEXT {
  GPU_SAMPLER_FEEDBACK_TIER_NONE_EXT = 0,
  GPU_SAMPLER_FEEDBACK_TIER_0_9_EXT  = 90,
  GPU_SAMPLER_FEEDBACK_TIER_1_0_EXT  = 100
} GPUSamplerFeedbackTierEXT;

typedef enum GPUSamplerFeedbackModeEXT {
  GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT         = 0,
  GPU_SAMPLER_FEEDBACK_MIP_REGION_USED_EXT = 1
} GPUSamplerFeedbackModeEXT;

typedef struct GPUSamplerFeedbackPropertiesEXT {
  GPUSamplerFeedbackTierEXT tier;
} GPUSamplerFeedbackPropertiesEXT;

typedef struct GPUSamplerFeedbackMapCreateInfoEXT {
  GPUChainedStruct          chain;
  const char               *label;
  GPUTexture               *texture;
  uint32_t                  mipRegionWidth;
  uint32_t                  mipRegionHeight;
  GPUSamplerFeedbackModeEXT mode;
} GPUSamplerFeedbackMapCreateInfoEXT;

typedef struct GPUSamplerFeedbackDecodeInfoEXT {
  uint32_t  width;
  uint32_t  height;
  uint32_t  mipLevelCount;
  uint32_t  arrayLayerCount;
  GPUFormat format;
} GPUSamplerFeedbackDecodeInfoEXT;

GPU_EXPORT
GPUResult
GPUGetSamplerFeedbackPropertiesEXT(const GPUAdapter                 *adapter,
                                   GPUSamplerFeedbackPropertiesEXT *outProperties);

GPU_EXPORT
GPUResult
GPUCreateSamplerFeedbackMapEXT(GPUDevice                                *device,
                               const GPUSamplerFeedbackMapCreateInfoEXT *info,
                               GPUSamplerFeedbackMapEXT                **outMap);

GPU_EXPORT
void
GPUDestroySamplerFeedbackMapEXT(GPUSamplerFeedbackMapEXT *map);

GPU_EXPORT
GPUResult
GPUGetSamplerFeedbackDecodeInfoEXT(const GPUSamplerFeedbackMapEXT *map,
                                   GPUSamplerFeedbackDecodeInfoEXT *outInfo);

GPU_EXPORT
GPUResult
GPUClearSamplerFeedbackEXT(GPUCommandBuffer         *cmdb,
                           GPUSamplerFeedbackMapEXT *map);

GPU_EXPORT
GPUResult
GPUDecodeSamplerFeedbackEXT(GPUCommandBuffer         *cmdb,
                            GPUSamplerFeedbackMapEXT *map,
                            GPUTexture               *decodedTexture);

GPU_EXPORT
GPUResult
GPUEncodeSamplerFeedbackEXT(GPUCommandBuffer         *cmdb,
                            GPUTexture               *decodedTexture,
                            GPUSamplerFeedbackMapEXT *map);

#ifdef __cplusplus
}
#endif
#endif /* gpu_sampler_feedback_h */
