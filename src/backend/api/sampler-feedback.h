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

#ifndef gpu_api_sampler_feedback_h
#define gpu_api_sampler_feedback_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/sampler-feedback.h>

typedef struct GPUApiSamplerFeedback {
  void
  (*getProperties)(const GPUAdapter                 *adapter,
                   GPUSamplerFeedbackPropertiesEXT *outProperties);

  GPUResult
  (*create)(GPUDevice                                *device,
            const GPUSamplerFeedbackMapCreateInfoEXT *info,
            GPUSamplerFeedbackMapEXT                 *map);

  void
  (*destroy)(GPUSamplerFeedbackMapEXT *map);

  GPUResult
  (*clear)(GPUCommandBuffer *cmdb, GPUSamplerFeedbackMapEXT *map);

  GPUResult
  (*decode)(GPUCommandBuffer         *cmdb,
            GPUSamplerFeedbackMapEXT *map,
            GPUTexture               *decodedTexture);

  GPUResult
  (*encode)(GPUCommandBuffer         *cmdb,
            GPUTexture               *decodedTexture,
            GPUSamplerFeedbackMapEXT *map);
} GPUApiSamplerFeedback;

#ifdef __cplusplus
}
#endif
#endif /* gpu_api_sampler_feedback_h */
