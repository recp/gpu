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

GPU_EXPORT
GPUResult
GPUCreateTexture(GPUDevice                  * __restrict device,
                 const GPUTextureCreateInfo * __restrict info,
                 GPUTexture                ** __restrict outTexture) {
  GPUApi *api;

  if (!outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outTexture = NULL;

  if (!device || !info ||
      info->format == GPU_FORMAT_UNDEFINED ||
      info->width == 0 ||
      info->height == 0 ||
      info->depthOrLayers == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuActiveGPUApi()) || !api->texture.create) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return api->texture.create(device, info, outTexture);
}

GPU_EXPORT
void
GPUDestroyTexture(GPUTexture * __restrict texture) {
  GPUApi *api;

  if (!texture) {
    return;
  }

  if (!(api = gpuActiveGPUApi()) || !api->texture.destroy) {
    return;
  }

  api->texture.destroy(texture);
}

GPU_EXPORT
GPUResult
GPUCreateTextureView(GPUTexture                     * __restrict texture,
                     const GPUTextureViewCreateInfo * __restrict info,
                     GPUTextureView                ** __restrict outView) {
  GPUApi *api;

  if (!outView) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outView = NULL;

  if (!texture || !info ||
      info->format == GPU_FORMAT_UNDEFINED ||
      info->mipLevelCount == 0 ||
      info->arrayLayerCount == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuActiveGPUApi()) || !api->texture.createView) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return api->texture.createView(texture, info, outView);
}

GPU_EXPORT
void
GPUDestroyTextureView(GPUTextureView * __restrict view) {
  GPUApi *api;

  if (!view) {
    return;
  }

  if (!(api = gpuActiveGPUApi()) || !api->texture.destroyView) {
    return;
  }

  api->texture.destroyView(view);
}

GPU_EXPORT
GPUResult
GPUQueueWriteTexture(GPUCommandQueue             * __restrict queue,
                     GPUTexture                  * __restrict texture,
                     const GPUTextureWriteRegion * __restrict region,
                     const void                  * __restrict data,
                     uint64_t                                 sizeBytes) {
  GPUApi *api;

  if (!queue || !texture || !region || !data ||
      region->width == 0 ||
      region->height == 0 ||
      region->depth == 0 ||
      region->layerCount == 0 ||
      region->bytesPerRow == 0 ||
      sizeBytes == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuActiveGPUApi()) || !api->texture.write) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return api->texture.write(queue, texture, region, data, sizeBytes);
}
