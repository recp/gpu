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
  CUDA_ARRAY3D_DESCRIPTOR desc = {0};
  GPUCudaFormatInfo       format;
  GPUTextureUsageFlags    allowedUsage;
  CUresult                result;

  if (!device || !info || !outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  allowedUsage = GPU_TEXTURE_USAGE_SAMPLED |
                 GPU_TEXTURE_USAGE_STORAGE |
                 GPU_TEXTURE_USAGE_COPY_SRC |
                 GPU_TEXTURE_USAGE_COPY_DST;
  if (info->dimension != GPU_TEXTURE_DIMENSION_2D ||
      !cuda_formatInfo(info->format, &format) ||
      info->depthOrLayers != 1u ||
      (info->mipLevelCount != 0u && info->mipLevelCount != 1u) ||
      (info->sampleCount != 0u && info->sampleCount != 1u) ||
      (info->usage & ~allowedUsage) != 0u ||
      ((info->usage & GPU_TEXTURE_USAGE_SAMPLED) != 0u &&
       (format.flags & GPU_CUDA_FORMAT_SAMPLED_BIT) == 0u) ||
      ((info->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u &&
       (format.flags & GPU_CUDA_FORMAT_STORAGE_BIT) == 0u)) {
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

  desc.Width       = info->width;
  desc.Height      = info->height;
  desc.Depth       = 0u;
  desc.Format      = format.arrayFormat;
  desc.NumChannels = format.channelCount;
  desc.Flags       = (info->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u
                       ? CUDA_ARRAY3D_SURFACE_LDST
                       : 0u;
  native->driver   = deviceNative->driver;
  native->format   = format;
  if (cuda_push(native->driver, deviceNative->context) != GPU_OK) {
    free(native);
    free(texture);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = native->driver->array3DCreate(&native->array, &desc);
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
  texture->mipLevelCount  = 1u;
  texture->sampleCount    = 1u;
  texture->usage          = info->usage;
  texture->_ownsNative    = true;
  *outTexture             = texture;
  return GPU_OK;
}

static void
cuda_destroyTexture(GPUTexture * __restrict texture) {
  GPUTextureCuda *native;
  GPUDeviceCuda  *device;

  native = texture ? texture->_priv : NULL;
  device = texture ? cuda_device(texture->device) : NULL;
  if (native && native->array && device &&
      cuda_push(native->driver, device->context) == GPU_OK) {
    (void)native->driver->arrayDestroy(native->array);
    cuda_pop(native->driver);
  }
  free(native);
  free(texture);
}

static GPUResult
cuda_createTextureView(GPUTexture                     * __restrict texture,
                       const GPUTextureViewCreateInfo * __restrict info,
                       GPUTextureView                ** __restrict outView) {
  GPUTextureCuda      *textureNative;
  GPUTextureViewCuda  *native;
  GPUTextureView      *view;
  GPUDeviceCuda       *device;
  CUDA_RESOURCE_DESC   desc = {0};
  GPUCudaFormatInfo    format;
  CUresult             result;

  if (!texture || !info || !outView ||
      info->viewType != GPU_TEXTURE_VIEW_2D ||
      !cuda_formatInfo(info->format, &format) ||
      info->baseMipLevel != 0u || info->mipLevelCount != 1u ||
      info->baseArrayLayer != 0u || info->arrayLayerCount != 1u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outView      = NULL;
  textureNative = texture->_priv;
  device        = cuda_device(texture->device);
  if (!textureNative || !textureNative->array || !device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (memcmp(&textureNative->format, &format, sizeof(format)) != 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  view   = calloc(1, sizeof(*view));
  native = calloc(1, sizeof(*native));
  if (!view || !native) {
    free(native);
    free(view);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->driver        = textureNative->driver;
  native->cache         = native->inlineCache;
  native->cacheCapacity = CUDA_INLINE_TEXTURE_CACHE_CAPACITY;
#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&native->lock);
#else
  if (pthread_mutex_init(&native->lock, NULL) != 0) {
    free(native);
    free(view);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif

  if ((texture->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u) {
    desc.resType          = CU_RESOURCE_TYPE_ARRAY;
    desc.res.array.hArray = textureNative->array;
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
    result = native->driver->surfObjectCreate(&native->surface, &desc);
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
  if (!native || !textureNative || !textureNative->array || !device ||
      !desc || !outTexture ||
      (view->_texture->usage & GPU_TEXTURE_USAGE_SAMPLED) == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!cuda_formatTextureDesc(&textureNative->format, desc, &effective)) {
    return GPU_ERROR_UNSUPPORTED;
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

  resource.resType          = CU_RESOURCE_TYPE_ARRAY;
  resource.res.array.hArray = textureNative->array;
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
                                             NULL);
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
  if (!native || !native->array || !queueNative || !region || !data ||
      sizeBytes > SIZE_MAX || region->aspect != GPU_TEXTURE_ASPECT_ALL ||
      region->mipLevel != 0u || region->baseArrayLayer != 0u ||
      region->layerCount != 1u || region->depth != 1u) {
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
  copy.dstArray      = native->array;
  copy.WidthInBytes  = widthBytes;
  copy.Height        = region->height;
  copy.Depth         = 1u;

  cuda_queueLock(queueNative);
  if (cuda_push(queueNative->driver, queueNative->context) != GPU_OK) {
    cuda_queueUnlock(queueNative);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = queueNative->driver->streamSynchronize(queueNative->stream);
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
