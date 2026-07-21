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

#ifndef gpu_sampler_internal_h
#define gpu_sampler_internal_h

#include "../common.h"
#include "device_internal.h"

struct GPUSampler {
  GPUDevice *device;
  void      *_priv;
  uint64_t   _gpuResourceID;
  GPUSamplerDesc desc;
};

static inline GPUApi *
gpuSamplerApi(const GPUSampler *sampler) {
  return sampler ? gpuDeviceApi(sampler->device) : NULL;
}

#endif /* gpu_sampler_internal_h */
