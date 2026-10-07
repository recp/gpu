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

static GPUResult
cuda_createSampler(GPUApi          *__restrict api,
                   GPUDevice       *__restrict device,
                   const GPUSamplerCreateInfo *info,
                   bool                        staticIfSupported,
                   GPUSampler                **outSampler) {
  CUDA_TEXTURE_DESC         desc;
  const GPUSamplerLODClamp *lod;
  GPUSamplerCuda           *native;
  GPUSampler               *sampler;

  GPU__UNUSED(api);
  GPU__UNUSED(device);
  GPU__UNUSED(staticIfSupported);

  if (!info || !outSampler) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outSampler = NULL;

  if (!cuda_samplerTextureDesc(&info->desc, &desc)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  lod = gpuSamplerLODClamp(info);

  if (lod) {
    desc.minMipmapLevelClamp = lod->minLOD;
    desc.maxMipmapLevelClamp = lod->maxLOD;
  }

  if (!(sampler = calloc(1, sizeof(*sampler) + sizeof(*native)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native         = (GPUSamplerCuda *)(sampler + 1);
  native->desc   = desc;
  sampler->_priv = native;
  *outSampler    = sampler;

  return GPU_OK;
}

static void
cuda_destroySampler(GPUSampler *__restrict sampler) {
  free(sampler);
}

void
cuda_initSampler(GPUApiSampler *api) {
  api->createSampler  = cuda_createSampler;
  api->destroySampler = cuda_destroySampler;
}
