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

#include "../common.h"
#include "device_internal.h"
#include "sampler_internal.h"

static int
gpu_samplerDescIsValid(const GPUSamplerDesc *desc) {
  if (!desc) {
    return 0;
  }

  return (desc->minFilter == GPU_FILTER_NEAREST ||
          desc->minFilter == GPU_FILTER_LINEAR) &&
         (desc->magFilter == GPU_FILTER_NEAREST ||
          desc->magFilter == GPU_FILTER_LINEAR) &&
         (desc->mipFilter == GPU_MIP_FILTER_NEAREST ||
          desc->mipFilter == GPU_MIP_FILTER_LINEAR) &&
         (desc->addressU == GPU_ADDRESS_MODE_REPEAT ||
          desc->addressU == GPU_ADDRESS_MODE_MIRRORED_REPEAT ||
          desc->addressU == GPU_ADDRESS_MODE_CLAMP_TO_EDGE) &&
         (desc->addressV == GPU_ADDRESS_MODE_REPEAT ||
          desc->addressV == GPU_ADDRESS_MODE_MIRRORED_REPEAT ||
          desc->addressV == GPU_ADDRESS_MODE_CLAMP_TO_EDGE) &&
         (desc->addressW == GPU_ADDRESS_MODE_REPEAT ||
          desc->addressW == GPU_ADDRESS_MODE_MIRRORED_REPEAT ||
          desc->addressW == GPU_ADDRESS_MODE_CLAMP_TO_EDGE);
}

GPU_EXPORT
GPUResult
GPUCreateSampler(GPUDevice                  *__restrict device,
                 const GPUSamplerCreateInfo *__restrict info,
                 bool                                   staticIfSupported,
                 GPUSampler               **__restrict outSampler) {
  GPUApi *api;
  GPUResult result;

  if (!outSampler) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outSampler = NULL;

  if (!device || !info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!gpu_samplerDescIsValid(&info->desc)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuDeviceApi(device)))
    return GPU_ERROR_BACKEND_FAILURE;

  if (!api->sampler.createSampler) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = api->sampler.createSampler(api,
                                      device,
                                      info,
                                      staticIfSupported,
                                      outSampler);
  if (result != GPU_OK) {
    return result;
  }
  if (!*outSampler) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  (*outSampler)->device = device;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroySampler(GPUSampler *__restrict sampler) {
  GPUApi *api;

  if (!sampler) {
    return;
  }

  if (!(api = gpuSamplerApi(sampler))) {
    return;
  }

  if (api->sampler.destroySampler) {
    api->sampler.destroySampler(sampler);
  }
}

GPU_EXPORT
GPUResult
GPUCreateSamplerFromUSLStaticSampler(GPUDevice *__restrict device,
                                     const GPUUSLStaticSamplerDesc *desc,
                                     bool staticIfSupported,
                                     GPUSampler **__restrict outSampler) {
  GPUApi *api;
  GPUResult result;

  if (!outSampler) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outSampler = NULL;

  if (!device || !GPUUSLStaticSamplerDescIsValid(desc)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuDeviceApi(device))) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (api->sampler.createSamplerFromUSLStaticSampler) {
    result = api->sampler.createSamplerFromUSLStaticSampler(api,
                                                            device,
                                                            desc,
                                                            staticIfSupported,
                                                            outSampler);
    if (result != GPU_OK) {
      return result;
    }
    if (!*outSampler) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    (*outSampler)->device = device;
    return GPU_OK;
  }

  return GPU_ERROR_BACKEND_FAILURE;
}
