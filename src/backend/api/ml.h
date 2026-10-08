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

#ifndef gpu_api_ml_h
#define gpu_api_ml_h

#include <gpu/ml.h>

typedef struct GPUMLApi {
  GPUResult (*createModel)(GPUMLModelEXT *model);
  void      (*destroyModel)(GPUMLModelEXT *model);
  GPUResult (*createPipeline)(GPUMLPipelineEXT *pipeline);
  GPUResult (*createBindings)(GPUMLBindingsEXT *bindings);
  void      (*destroyBindings)(GPUMLBindingsEXT *bindings);
  GPUResult (*encode)(GPUCommandBuffer *cmdb, GPUMLBindingsEXT *bindings);
} GPUMLApi;

#endif /* gpu_api_ml_h */
