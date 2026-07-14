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
#include "adapter_internal.h"
#include "surface_internal.h"

static bool
gpuIsSurfaceTypeValid(GPUSurfaceType type) {
  return type == GPU_SURFACE_WINDOWS_HWND ||
         type == GPU_SURFACE_WINDOWS_COREWINDOW ||
         type == GPU_SURFACE_APPLE_NSVIEW ||
         type == GPU_SURFACE_APPLE_UIVIEW;
}

static const GPUNativeSurfaceCreateInfo*
gpuFindNativeSurfaceCreateInfo(const GPUSurfaceCreateInfo *info) {
  const GPUChainedStruct *chain;

  chain = info ? (const GPUChainedStruct *)info->chain.pNext : NULL;
  while (chain) {
    if (chain->sType == GPU_STRUCTURE_TYPE_NATIVE_SURFACE_CREATE_INFO) {
      return (const GPUNativeSurfaceCreateInfo *)chain;
    }
    chain = (const GPUChainedStruct *)chain->pNext;
  }

  return NULL;
}

GPU_EXPORT
GPUResult
GPUCreateSurface(GPUInstance       * __restrict inst,
                 const GPUSurfaceCreateInfo * __restrict info,
                 GPUSurface ** __restrict outSurface) {
  const GPUNativeSurfaceCreateInfo *nativeInfo;
  GPUApi                           *api;

  if (!outSurface) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outSurface = NULL;

  if (!inst || !info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_SURFACE_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  nativeInfo = gpuFindNativeSurfaceCreateInfo(info);
  if (!nativeInfo ||
      (nativeInfo->chain.structSize != 0 &&
       nativeInfo->chain.structSize < sizeof(*nativeInfo)) ||
      !nativeInfo->adapter ||
      nativeInfo->adapter->inst != inst ||
      !nativeInfo->nativeHandle ||
      !gpuIsSurfaceTypeValid(nativeInfo->type) ||
      !(nativeInfo->scale > 0.0f)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuInstanceApi(inst))) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (!api->surface.createSurface) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outSurface = api->surface.createSurface(api,
                                           inst,
                                           nativeInfo->adapter,
                                           nativeInfo->nativeHandle,
                                           nativeInfo->type,
                                           nativeInfo->scale);
  if (!*outSurface) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  (*outSurface)->inst = inst;

  return GPU_OK;
}

GPU_EXPORT
GPUSurface*
GPUCreateSurfaceFromNative(GPUInstance       * __restrict inst,
                           GPUAdapter        * __restrict adapter,
                           void              * __restrict nativeHandle,
                           GPUSurfaceType                 type,
                           float                          scale) {
  GPUNativeSurfaceCreateInfo nativeInfo = {0};
  GPUSurfaceCreateInfo       info = {0};
  GPUSurface                *surface;

  nativeInfo.chain.sType = GPU_STRUCTURE_TYPE_NATIVE_SURFACE_CREATE_INFO;
  nativeInfo.chain.structSize = sizeof(nativeInfo);
  nativeInfo.adapter = adapter;
  nativeInfo.nativeHandle = nativeHandle;
  nativeInfo.type = type;
  nativeInfo.scale = scale;

  info.chain.sType = GPU_STRUCTURE_TYPE_SURFACE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.chain.pNext = &nativeInfo;

  surface = NULL;
  if (GPUCreateSurface(inst, &info, &surface) != GPU_OK) {
    return NULL;
  }

  return surface;
}

GPU_EXPORT
void
GPUDestroySurface(GPUSurface * __restrict surface) {
  GPUApi *api;

  if (!surface) {
    return;
  }

  if (!(api = gpuSurfaceApi(surface))) {
    return;
  }

  if (api->surface.destroySurface) {
    api->surface.destroySurface(surface);
  }
}

GPU_EXPORT
GPUResult
GPUGetSurfaceCapabilities(const GPUAdapter * __restrict adapter,
                          const GPUSurface * __restrict surface,
                          GPUSurfaceCapabilities * __restrict outCaps) {
  GPUApi    *api;
  GPUResult  result;

  if (!adapter || !surface || !outCaps || adapter->inst != surface->inst) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outCaps, 0, sizeof(*outCaps));
  if (!(api = gpuSurfaceApi(surface))) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (!api->surface.getCapabilities) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->surface.getCapabilities(adapter,
                                        (GPUSurface *)surface,
                                        outCaps);
  if (result != GPU_OK) {
    memset(outCaps, 0, sizeof(*outCaps));
    return result;
  }
  if (outCaps->minImageCount == 0u ||
      outCaps->maxImageCount < outCaps->minImageCount ||
      outCaps->formatCount == 0u || !outCaps->pFormats) {
    memset(outCaps, 0, sizeof(*outCaps));
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return GPU_OK;
}
