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
#include "../../../api/pipeline_cache_internal.h"
#include "pipeline_cache.h"

typedef struct VKPipelineCache {
  char            *path;
  VkDevice         device;
  VkPipelineCache  cache;
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION lock;
#else
  pthread_mutex_t  lock;
#endif
  bool             used;
} VKPipelineCache;

static VKPipelineCache *
vk__nativeCache(GPUPipelineCache *cache) {
  return cache ? cache->_priv : NULL;
}

static void *
vk__readCache(const char *path, size_t *outSize) {
  void *data;
  FILE *file;
  long  size;

  *outSize = 0u;
  file     = fopen(path, "rb");
  if (!file) {
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }

  data = malloc((size_t)size);
  if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *outSize = (size_t)size;
  return data;
}

static char *
vk__copyPath(const char *path) {
  size_t length;
  char  *copy;

  length = strlen(path) + 1u;
  copy   = malloc(length);
  if (copy) {
    memcpy(copy, path, length);
  }
  return copy;
}

static GPUResult
vk_createCache(GPUDevice                        *device,
               const GPUPipelineCacheCreateInfo *info,
               GPUPipelineCache                 *cache) {
  VkPipelineCacheCreateInfo createInfo = {0};
  VKPipelineCache          *native;
  GPUDeviceVk              *deviceVk;
  VkResult                  result;
  void                     *initialData;
  size_t                    initialSize;

  deviceVk = device ? device->_priv : NULL;
  if (!deviceVk || !deviceVk->device || !info || !info->cachePath || !cache) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->path = vk__copyPath(info->cachePath);
  if (!native->path) {
    free(native);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&native->lock);
#else
  if (pthread_mutex_init(&native->lock, NULL) != 0) {
    free(native->path);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif

  initialData                = vk__readCache(native->path, &initialSize);
  createInfo.sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  createInfo.initialDataSize = initialSize;
  createInfo.pInitialData    = initialData;
  native->device             = deviceVk->device;
  result = vkCreatePipelineCache(native->device,
                                 &createInfo,
                                 NULL,
                                 &native->cache);
  if (result != VK_SUCCESS && initialData) {
    createInfo.initialDataSize = 0u;
    createInfo.pInitialData    = NULL;
    result = vkCreatePipelineCache(native->device,
                                   &createInfo,
                                   NULL,
                                   &native->cache);
  }
  free(initialData);
  if (result != VK_SUCCESS) {
#if defined(_WIN32) || defined(WIN32)
    DeleteCriticalSection(&native->lock);
#else
    pthread_mutex_destroy(&native->lock);
#endif
    free(native->path);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  cache->_priv = native;
  return GPU_OK;
}

static bool
vk__replaceFile(const char *source, const char *destination) {
#if defined(_WIN32) || defined(WIN32)
  return MoveFileExA(source,
                     destination,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(source, destination) == 0;
#endif
}

static void
vk__storeCache(VKPipelineCache *native) {
  void    *data;
  char    *temporaryPath;
  FILE    *file;
  size_t   dataSize;
  size_t   pathLength;
  VkResult result;
  bool     written;

  if (!native || !native->used) {
    return;
  }
  dataSize = 0u;
  result   = vkGetPipelineCacheData(native->device,
                                    native->cache,
                                    &dataSize,
                                    NULL);
  if (result != VK_SUCCESS || dataSize == 0u) {
    return;
  }
  data = malloc(dataSize);
  if (!data) {
    return;
  }
  result = vkGetPipelineCacheData(native->device,
                                  native->cache,
                                  &dataSize,
                                  data);
  if (result != VK_SUCCESS) {
    free(data);
    return;
  }

  pathLength    = strlen(native->path);
  temporaryPath = malloc(pathLength + sizeof(".tmp"));
  if (!temporaryPath) {
    free(data);
    return;
  }
  memcpy(temporaryPath, native->path, pathLength);
  memcpy(temporaryPath + pathLength, ".tmp", sizeof(".tmp"));
  file    = fopen(temporaryPath, "wb");
  written = file && fwrite(data, 1u, dataSize, file) == dataSize;
  if (file && fclose(file) != 0) {
    written = false;
  }
  if (written) {
    if (!vk__replaceFile(temporaryPath, native->path)) {
      remove(temporaryPath);
    }
  } else {
    remove(temporaryPath);
  }
  free(temporaryPath);
  free(data);
}

static void
vk_destroyCache(GPUPipelineCache *cache) {
  VKPipelineCache *native;

  native = vk__nativeCache(cache);
  if (!native) {
    return;
  }
  vk__storeCache(native);
  vkDestroyPipelineCache(native->device, native->cache, NULL);
#if defined(_WIN32) || defined(WIN32)
  DeleteCriticalSection(&native->lock);
#else
  pthread_mutex_destroy(&native->lock);
#endif
  free(native->path);
  free(native);
  cache->_priv = NULL;
}

GPU_HIDE
VkPipelineCache
vk_lockCache(GPUPipelineCache *cache) {
  VKPipelineCache *native;

  native = vk__nativeCache(cache);
  if (!native) {
    return VK_NULL_HANDLE;
  }
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&native->lock);
#else
  pthread_mutex_lock(&native->lock);
#endif
  native->used = true;
  return native->cache;
}

GPU_HIDE
void
vk_unlockCache(GPUPipelineCache *cache) {
  VKPipelineCache *native;

  native = vk__nativeCache(cache);
  if (!native) {
    return;
  }
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&native->lock);
#else
  pthread_mutex_unlock(&native->lock);
#endif
}

GPU_HIDE
void
vk_initPipelineCache(GPUApiPipelineCache *api) {
  api->create  = vk_createCache;
  api->destroy = vk_destroyCache;
}
