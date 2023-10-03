/*
 * Copyright (C) 2020 Recep Aslantas
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

#ifndef gpu_instance_h
#define gpu_instance_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef enum GPUFeatures {
  GPU_FEATURE_NONE = 0,
  GPU_FEATURE_SURFACE,
  GPU_FEATURE_SWAPCHAIN,
  GPU_FEATURE_DEFAULT = GPU_FEATURE_SURFACE | GPU_FEATURE_SWAPCHAIN
} GPUFeatures;

typedef struct GPUInitParams {
  GPUFeatures requiredFeatures;    /* DEFAULT */
  GPUFeatures optionalFeatures;    /* NONE    */
  bool        validation;          /* false, Vulkan validation layers */
  bool        validation_usebreak; /* false   */
} GPUInitParams;

typedef struct GPUInstance {
  void          *_priv;
  GPUInitParams *initParams; /* read-only */
  uint32_t       validationError;
} GPUInstance;

/*!
 * @brief creates GPU instance by specified params, features, options if possible
 *
 * @param[in]  params init params, NULL to default.
 */
GPU_EXPORT
GPUInstance*
GPUCreateInstance(GPUInitParams * __restrict params);

#ifdef __cplusplus
}
#endif
#endif /* gpu_instance_h */
