/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

static GPUResult
cuda_createTexture(GPUDevice                  * __restrict device,
                   const GPUTextureCreateInfo * __restrict info,
                   GPUTexture                ** __restrict outTexture) {
  GPUDeviceCuda          *deviceNative;
  GPUTextureCuda         *native;
  GPUTexture             *texture;
  GPUCudaTexturePlan      plan;
  GPUCudaFormatInfo       format;
  CUresult                result;

  if (!device || !info || !outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!cuda_formatInfo(info->format, &format) ||
      !cuda_texturePlan(info, &format, &plan)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outTexture   = NULL;
  deviceNative = cuda_device(device);
  if (!deviceNative) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  texture = calloc(1, sizeof(*texture));
  native  = calloc(1, sizeof(*native));
  if (!texture || !native) {
    free(native);
    free(texture);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->driver     = deviceNative->driver;
  native->format     = format;
  native->arrayFlags = plan.desc.Flags;
  if (cuda_push(native->driver, deviceNative->context) != GPU_OK) {
    free(native);
    free(texture);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = plan.mipmapped
             ? native->driver->mipmappedArrayCreate(&native->mipmap,
                                                     &plan.desc,
                                                     plan.mipLevelCount)
             : native->driver->array3DCreate(&native->array, &plan.desc);
  cuda_pop(native->driver);
  if (result != CUDA_SUCCESS) {
    cuda_report(device, result, "texture allocation");
    free(native);
    free(texture);
    return result == CUDA_ERROR_OUT_OF_MEMORY
             ? GPU_ERROR_OUT_OF_MEMORY
             : GPU_ERROR_BACKEND_FAILURE;
  }

  texture->_priv          = native;
  texture->format         = info->format;
  texture->dimension      = info->dimension;
  texture->width          = info->width;
  texture->height         = info->height;
  texture->depthOrLayers  = info->depthOrLayers;
  texture->mipLevelCount  = plan.mipLevelCount;
  texture->sampleCount    = 1u;
  texture->usage          = info->usage;
  texture->_ownsNative    = true;
  *outTexture             = texture;
  return GPU_OK;
}

static bool
cuda__textureValid(const GPUTextureCuda *native) {
  return native && (native->array || native->mipmap);
}

static CUresult
cuda__textureLevel(GPUTextureCuda *native,
                   uint32_t        mipLevel,
                   CUarray        *outArray) {
  if (outArray) {
    *outArray = NULL;
  }
  if (!cuda__textureValid(native) || !outArray) {
    return CUDA_ERROR_INVALID_VALUE;
  }
  if (native->array) {
    if (mipLevel != 0u) {
      return CUDA_ERROR_INVALID_VALUE;
    }
    *outArray = native->array;
    return CUDA_SUCCESS;
  }
  return native->driver->mipmappedArrayGetLevel(outArray,
                                                 native->mipmap,
                                                 mipLevel);
}

static void
cuda_destroyTexture(GPUTexture * __restrict texture) {
  GPUTextureCuda *native;
  GPUDeviceCuda  *device;

  native = texture ? texture->_priv : NULL;
  device = texture ? cuda_device(texture->device) : NULL;
  if (cuda__textureValid(native) && device &&
      cuda_push(native->driver, device->context) == GPU_OK) {
    if (native->mipmap) {
      (void)native->driver->mipmappedArrayDestroy(native->mipmap);
    } else {
      (void)native->driver->arrayDestroy(native->array);
    }
    cuda_pop(native->driver);
  }
  free(native);
  free(texture);
}

static GPUResult
cuda_createTextureView(GPUTexture                     * __restrict texture,
                       const GPUTextureViewCreateInfo * __restrict info,
                       GPUTextureView                ** __restrict outView) {
  GPUTextureCuda         *textureNative;
  GPUTextureViewCuda     *native;
  GPUTextureView         *view;
  GPUDeviceCuda          *device;
  GPUCudaTextureViewPlan plan;
  CUDA_RESOURCE_DESC      desc = {0};
  GPUCudaFormatInfo       format;
  CUresult                result;

  if (!texture || !info || !outView ||
      !cuda_formatInfo(info->format, &format) ||
      !cuda_textureViewPlan(texture, info, &plan)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (plan.hasResourceView &&
      !cuda_formatResourceView(&format, &plan.desc.format)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outView      = NULL;
  textureNative = texture->_priv;
  device        = cuda_device(texture->device);
  if (!cuda__textureValid(textureNative) || !device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (memcmp(&textureNative->format, &format, sizeof(format)) != 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if ((info->viewType == GPU_TEXTURE_VIEW_CUBE ||
       info->viewType == GPU_TEXTURE_VIEW_CUBE_ARRAY) &&
      (textureNative->arrayFlags & CUDA_ARRAY3D_CUBEMAP) == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if ((texture->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u &&
      (texture->usage & GPU_TEXTURE_USAGE_SAMPLED) == 0u &&
      !plan.surfaceCompatible) {
    return GPU_ERROR_UNSUPPORTED;
  }

  view   = calloc(1, sizeof(*view));
  native = calloc(1, sizeof(*native));
  if (!view || !native) {
    free(native);
    free(view);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->driver          = textureNative->driver;
  native->cache           = native->inlineCache;
  native->cacheCapacity   = CUDA_INLINE_TEXTURE_CACHE_CAPACITY;
  native->resourceView    = plan.desc;
  native->hasResourceView = plan.hasResourceView;
#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&native->lock);
#else
  if (pthread_mutex_init(&native->lock, NULL) != 0) {
    free(native);
    free(view);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif

  if (plan.singleLevel) {
    if (cuda_push(native->driver, device->context) != GPU_OK) {
#if defined(_WIN32) || defined(WIN32)
      DeleteCriticalSection(&native->lock);
#else
      pthread_mutex_destroy(&native->lock);
#endif
      free(native);
      free(view);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    result = cuda__textureLevel(textureNative,
                                plan.mipLevel,
                                &native->array);
    if (result == CUDA_SUCCESS &&
        (texture->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u &&
        plan.surfaceCompatible) {
      desc.resType          = CU_RESOURCE_TYPE_ARRAY;
      desc.res.array.hArray = native->array;
      result = native->driver->surfObjectCreate(&native->surface, &desc);
    }
    cuda_pop(native->driver);
    if (result != CUDA_SUCCESS) {
      cuda_report(texture->device, result, "texture view creation");
#if defined(_WIN32) || defined(WIN32)
      DeleteCriticalSection(&native->lock);
#else
      pthread_mutex_destroy(&native->lock);
#endif
      free(native);
      free(view);
      return result == CUDA_ERROR_OUT_OF_MEMORY
               ? GPU_ERROR_OUT_OF_MEMORY
               : GPU_ERROR_BACKEND_FAILURE;
    }
  }

  view->_priv       = native;
  view->_ownsNative = true;
  *outView          = view;
  return GPU_OK;
}

static void
cuda_destroyTextureView(GPUTextureView * __restrict view) {
  GPUTextureViewCuda *native;
  GPUDeviceCuda      *device;

  native = view ? view->_priv : NULL;
  device = view && view->_texture
             ? cuda_device(view->_texture->device)
             : NULL;
  if (native && device &&
      cuda_push(native->driver, device->context) == GPU_OK) {
#if defined(_WIN32) || defined(WIN32)
    EnterCriticalSection(&native->lock);
#else
    pthread_mutex_lock(&native->lock);
#endif
    for (uint32_t i = 0u; i < native->cacheCount; i++) {
      (void)native->driver->texObjectDestroy(native->cache[i].texture);
    }
    if (native->surface) {
      (void)native->driver->surfObjectDestroy(native->surface);
    }
#if defined(_WIN32) || defined(WIN32)
    LeaveCriticalSection(&native->lock);
#else
    pthread_mutex_unlock(&native->lock);
#endif
    cuda_pop(native->driver);
  }
  if (native) {
#if defined(_WIN32) || defined(WIN32)
    DeleteCriticalSection(&native->lock);
#else
    pthread_mutex_destroy(&native->lock);
#endif
    if (native->cacheDynamic) {
      free(native->cache);
    }
  }
  free(native);
  free(view);
}

GPUResult
cuda_getTextureObject(GPUTextureView          *view,
                      const CUDA_TEXTURE_DESC *desc,
                      bool                     exactCoordinates,
                      CUtexObject             *outTexture) {
  GPUCudaTextureCacheEntry *cache;
  GPUTextureViewCuda       *native;
  GPUTextureCuda           *textureNative;
  GPUDeviceCuda            *device;
  CUDA_RESOURCE_DESC        resource = {0};
  CUDA_TEXTURE_DESC         effective;
  CUtexObject               texture;
  CUresult                  result;
  uint32_t                  capacity;

  if (outTexture) {
    *outTexture = 0u;
  }
  native        = view ? view->_priv : NULL;
  textureNative = view && view->_texture ? view->_texture->_priv : NULL;
  device        = view && view->_texture
                    ? cuda_device(view->_texture->device)
                    : NULL;
  if (!native || !textureNative || !device ||
      (!native->array && !textureNative->mipmap) ||
      !desc || !outTexture ||
      view->mipLevelCount == 0u ||
      (view->_texture->usage & GPU_TEXTURE_USAGE_SAMPLED) == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!cuda_formatTextureDesc(&textureNative->format, desc, &effective)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (view->viewType == GPU_TEXTURE_VIEW_CUBE ||
      view->viewType == GPU_TEXTURE_VIEW_CUBE_ARRAY) {
    if ((textureNative->arrayFlags & CUDA_ARRAY3D_CUBEMAP) == 0u) {
      return GPU_ERROR_UNSUPPORTED;
    }
    effective.flags |= CU_TRSF_SEAMLESS_CUBEMAP;
  }
  if (!native->array) {
    if ((effective.flags & CU_TRSF_NORMALIZED_COORDINATES) == 0u) {
      if (!exactCoordinates) {
        return GPU_ERROR_UNSUPPORTED;
      }
      /* Mipmapped objects require this flag; PTX s32 coordinates stay exact. */
      effective.flags |= CU_TRSF_NORMALIZED_COORDINATES;
    }
    effective.minMipmapLevelClamp = 0.0f;
    effective.maxMipmapLevelClamp = (float)(view->mipLevelCount - 1u);
  }

#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&native->lock);
#else
  pthread_mutex_lock(&native->lock);
#endif
  for (uint32_t i = 0u; i < native->cacheCount; i++) {
    if (memcmp(&native->cache[i].desc, &effective, sizeof(effective)) == 0) {
      *outTexture = native->cache[i].texture;
#if defined(_WIN32) || defined(WIN32)
      LeaveCriticalSection(&native->lock);
#else
      pthread_mutex_unlock(&native->lock);
#endif
      return GPU_OK;
    }
  }
  if (native->cacheCount == CUDA_TEXTURE_CACHE_CAPACITY) {
#if defined(_WIN32) || defined(WIN32)
    LeaveCriticalSection(&native->lock);
#else
    pthread_mutex_unlock(&native->lock);
#endif
    return GPU_ERROR_UNSUPPORTED;
  }
  if (native->cacheCount == native->cacheCapacity) {
    capacity = native->cacheCapacity * 2u;
    if (capacity > CUDA_TEXTURE_CACHE_CAPACITY) {
      capacity = CUDA_TEXTURE_CACHE_CAPACITY;
    }
    cache = malloc((size_t)capacity * sizeof(*cache));
    if (!cache) {
#if defined(_WIN32) || defined(WIN32)
      LeaveCriticalSection(&native->lock);
#else
      pthread_mutex_unlock(&native->lock);
#endif
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    memcpy(cache,
           native->cache,
           (size_t)native->cacheCount * sizeof(*cache));
    if (native->cacheDynamic) {
      free(native->cache);
    }
    native->cache         = cache;
    native->cacheCapacity = capacity;
    native->cacheDynamic  = true;
    gpuDeviceRecordHotPathAlloc(view->_texture->device,
                                (uint64_t)capacity * sizeof(*cache));
  }

  if (native->array) {
    resource.resType          = CU_RESOURCE_TYPE_ARRAY;
    resource.res.array.hArray = native->array;
  } else {
    resource.resType                    = CU_RESOURCE_TYPE_MIPMAPPED_ARRAY;
    resource.res.mipmap.hMipmappedArray = textureNative->mipmap;
  }
  if (cuda_push(native->driver, device->context) != GPU_OK) {
#if defined(_WIN32) || defined(WIN32)
    LeaveCriticalSection(&native->lock);
#else
    pthread_mutex_unlock(&native->lock);
#endif
    return GPU_ERROR_BACKEND_FAILURE;
  }
  texture = 0u;
  result  = native->driver->texObjectCreate(&texture,
                                             &resource,
                                             &effective,
                                             native->hasResourceView
                                               ? &native->resourceView
                                               : NULL);
  cuda_pop(native->driver);
  if (result == CUDA_SUCCESS) {
    native->cache[native->cacheCount].desc    = effective;
    native->cache[native->cacheCount].texture = texture;
    native->cacheCount++;
    *outTexture = texture;
  }
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&native->lock);
#else
  pthread_mutex_unlock(&native->lock);
#endif
  if (result != CUDA_SUCCESS) {
    cuda_report(view->_texture->device, result, "texture-object creation");
    return result == CUDA_ERROR_OUT_OF_MEMORY
             ? GPU_ERROR_OUT_OF_MEMORY
             : GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

static GPUResult
cuda_writeTexture(GPUQueue                    * __restrict queue,
                  GPUTexture                  * __restrict texture,
                  const GPUTextureWriteRegion * __restrict region,
                  const void                  * __restrict data,
                  uint64_t                                 sizeBytes) {
  GPUTextureCuda *native;
  GPUQueueCuda   *queueNative;
  CUDA_MEMCPY3D   copy = {0};
  CUresult        result;
  size_t          widthBytes;

  native      = texture ? texture->_priv : NULL;
  queueNative = cuda_queue(queue);
  if (!cuda__textureValid(native) || !queueNative || !region || !data ||
      sizeBytes > SIZE_MAX || region->aspect != GPU_TEXTURE_ASPECT_ALL) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (region->width > SIZE_MAX / native->format.bytesPerTexel) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  widthBytes = (size_t)region->width * native->format.bytesPerTexel;

  copy.srcMemoryType = CU_MEMORYTYPE_HOST;
  copy.srcHost       = data;
  copy.srcPitch      = region->bytesPerRow;
  copy.srcHeight     = region->rowsPerImage
                         ? region->rowsPerImage
                         : region->height;
  copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
  copy.dstZ          = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                         ? 0u
                         : region->baseArrayLayer;
  copy.WidthInBytes  = widthBytes;
  copy.Height        = texture->dimension == GPU_TEXTURE_DIMENSION_1D
                         ? 1u
                         : region->height;
  copy.Depth         = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                         ? region->depth
                         : region->layerCount;

  cuda_queueLock(queueNative);
  if (cuda_push(queueNative->driver, queueNative->context) != GPU_OK) {
    cuda_queueUnlock(queueNative);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = queueNative->driver->streamSynchronize(queueNative->stream);
  if (result == CUDA_SUCCESS) {
    result = cuda__textureLevel(native, region->mipLevel, &copy.dstArray);
  }
  if (result == CUDA_SUCCESS) {
    result = queueNative->driver->memcpy3D(&copy);
  }
  cuda_pop(queueNative->driver);
  cuda_queueUnlock(queueNative);
  if (result != CUDA_SUCCESS) {
    cuda_report(queue->_device, result, "texture upload");
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

void
cuda_initTexture(GPUApiTexture *api) {
  api->create      = cuda_createTexture;
  api->destroy     = cuda_destroyTexture;
  api->createView  = cuda_createTextureView;
  api->destroyView = cuda_destroyTextureView;
  api->write       = cuda_writeTexture;
}
