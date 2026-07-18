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
#include "../../cache_file.h"
#include "../../../api/pipeline_cache_internal.h"
#include "pipeline_cache.h"

#include <errno.h>
#include <stdint.h>
#if defined(_WIN32) || defined(WIN32)
# include <direct.h>
# include <process.h>
#else
# include <sys/stat.h>
# include <unistd.h>
#endif

#ifdef VK_KHR_pipeline_binary
enum {
  VK_PIPELINE_BINARY_CACHE_MAGIC   = 0x42505047u,
  VK_PIPELINE_BINARY_CACHE_VERSION = 1u,
  VK_PIPELINE_BINARY_CACHE_MAX     = 64u
};

typedef enum VKPipelineKind {
  VK_PIPELINE_KIND_GRAPHICS,
  VK_PIPELINE_KIND_COMPUTE,
#ifdef VK_KHR_ray_tracing_pipeline
  VK_PIPELINE_KIND_RAY
#endif
} VKPipelineKind;

typedef struct VKPipelineBinaryFileHeader {
  uint64_t totalDataSize;
  uint32_t magic;
  uint32_t version;
  uint32_t globalKeySize;
  uint32_t pipelineKeySize;
  uint32_t binaryCount;
  uint32_t reserved;
  uint8_t  globalKey[VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR];
  uint8_t  pipelineKey[VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR];
} VKPipelineBinaryFileHeader;

typedef struct VKPipelineBinaryRecord {
  uint64_t dataSize;
  uint32_t keySize;
  uint32_t reserved;
  uint8_t  key[VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR];
} VKPipelineBinaryRecord;

typedef struct VKPipelineBinarySet {
  void                    *storage;
  VkPipelineBinaryKeyKHR  *keys;
  VkPipelineBinaryDataKHR *data;
  VkPipelineBinaryKHR     *handles;
  uint32_t                 count;
} VKPipelineBinarySet;

_Static_assert(sizeof(VKPipelineBinaryFileHeader) == 96u,
               "pipeline binary header layout changed");
_Static_assert(sizeof(VKPipelineBinaryRecord) == 48u,
               "pipeline binary record layout changed");
#endif

typedef struct VKPipelineCache {
  char                   *path;
#ifdef VK_KHR_pipeline_binary
  char                   *binaryPath;
  GPUDeviceVk            *deviceVk;
  VkPipelineBinaryKeyKHR globalKey;
#endif
  VkDevice                device;
  VkPipelineCache         cache;
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION        lock;
#else
  pthread_mutex_t         lock;
#endif
  bool                    used;
#ifdef VK_KHR_pipeline_binary
  bool                    internalBinary;
  bool                    diskBinary;
#endif
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

#ifdef VK_KHR_pipeline_binary
static char *
vk__appendPath(const char *path, const char *suffix) {
  size_t pathLength;
  size_t suffixLength;
  char  *result;

  pathLength   = strlen(path);
  suffixLength = strlen(suffix);
  if (pathLength > SIZE_MAX - suffixLength - 1u) {
    return NULL;
  }
  result = malloc(pathLength + suffixLength + 1u);
  if (!result) {
    return NULL;
  }
  memcpy(result, path, pathLength);
  memcpy(result + pathLength, suffix, suffixLength + 1u);
  return result;
}
#endif

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
#ifdef VK_KHR_pipeline_binary
  native->deviceVk           = deviceVk;
  /* Avoid duplicating a driver-preferred on-disk cache. */
  native->internalBinary     = deviceVk->pipelineBinary &&
                               deviceVk->pipelineBinaryInternalCache &&
                               deviceVk->pipelineBinaryPrefersInternalCache;
  native->diskBinary         = deviceVk->pipelineBinary &&
                               !deviceVk->pipelineBinaryPrefersInternalCache;
  if (native->diskBinary) {
    native->globalKey.sType =
      VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR;
    if (deviceVk->getPipelineKey(deviceVk->device,
                                 NULL,
                                 &native->globalKey) != VK_SUCCESS ||
        native->globalKey.keySize == 0u ||
        native->globalKey.keySize > VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR) {
      native->diskBinary = false;
    } else {
      native->binaryPath = vk__appendPath(native->path, ".vkpb");
      if (!native->binaryPath) {
        native->diskBinary = false;
      }
    }
  }
#endif
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
#ifdef VK_KHR_pipeline_binary
    free(native->binaryPath);
#endif
    free(native->path);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  cache->_priv = native;
  return GPU_OK;
}

static void
vk__mergeStoredCache(VKPipelineCache *native) {
  VkPipelineCacheCreateInfo createInfo = {0};
  VkPipelineCache           storedCache;
  void                     *data;
  size_t                    dataSize;

  data = vk__readCache(native->path, &dataSize);
  if (!data) {
    return;
  }
  createInfo.sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  createInfo.initialDataSize = dataSize;
  createInfo.pInitialData    = data;
  storedCache                = VK_NULL_HANDLE;
  if (vkCreatePipelineCache(native->device,
                            &createInfo,
                            NULL,
                            &storedCache) == VK_SUCCESS &&
      storedCache) {
    (void)vkMergePipelineCaches(native->device,
                                native->cache,
                                1u,
                                &storedCache);
    vkDestroyPipelineCache(native->device, storedCache, NULL);
  }
  free(data);
}

static void
vk__storeCache(VKPipelineCache *native) {
  GPUCacheFileGuard guard;
  void    *data;
  char    *temporaryPath;
  FILE    *file;
  size_t   dataSize;
  VkResult result;
  bool     written;

  if (!native || !native->used) {
    return;
  }
  if (!gpuCacheFileBegin(native->path, &guard)) {
    return;
  }
  vk__mergeStoredCache(native);
  dataSize = 0u;
  result   = vkGetPipelineCacheData(native->device,
                                    native->cache,
                                    &dataSize,
                                    NULL);
  if (result != VK_SUCCESS || dataSize == 0u) {
    gpuCacheFileEnd(&guard);
    return;
  }
  data = malloc(dataSize);
  if (!data) {
    gpuCacheFileEnd(&guard);
    return;
  }
  result = vkGetPipelineCacheData(native->device,
                                  native->cache,
                                  &dataSize,
                                  data);
  if (result != VK_SUCCESS) {
    free(data);
    gpuCacheFileEnd(&guard);
    return;
  }

  temporaryPath = gpuCacheFileTemporaryPath(native->path, native);
  if (!temporaryPath) {
    gpuCacheFileEnd(&guard);
    free(data);
    return;
  }
  file    = fopen(temporaryPath, "wb");
  written = file && fwrite(data, 1u, dataSize, file) == dataSize;
  if (file && fclose(file) != 0) {
    written = false;
  }
  if (written) {
    if (!gpuCacheFileReplace(temporaryPath, native->path)) {
      remove(temporaryPath);
    }
  } else {
    remove(temporaryPath);
  }
  free(temporaryPath);
  free(data);
  gpuCacheFileEnd(&guard);
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
#ifdef VK_KHR_pipeline_binary
  free(native->binaryPath);
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

#ifdef VK_KHR_pipeline_binary
typedef union VKPipelineCreateInfoUnion {
  VkGraphicsPipelineCreateInfo graphics;
  VkComputePipelineCreateInfo  compute;
#ifdef VK_KHR_ray_tracing_pipeline
  VkRayTracingPipelineCreateInfoKHR ray;
#endif
} VKPipelineCreateInfoUnion;

static VkPipelineCreateFlags
vk__pipelineFlags(VKPipelineKind kind, const void *info) {
  switch (kind) {
    case VK_PIPELINE_KIND_GRAPHICS:
      return ((const VkGraphicsPipelineCreateInfo *)info)->flags;
    case VK_PIPELINE_KIND_COMPUTE:
      return ((const VkComputePipelineCreateInfo *)info)->flags;
#ifdef VK_KHR_ray_tracing_pipeline
    case VK_PIPELINE_KIND_RAY:
      return ((const VkRayTracingPipelineCreateInfoKHR *)info)->flags;
#endif
  }
  return 0u;
}

static const void *
vk__pipelineNext(VKPipelineKind kind, const void *info) {
  switch (kind) {
    case VK_PIPELINE_KIND_GRAPHICS:
      return ((const VkGraphicsPipelineCreateInfo *)info)->pNext;
    case VK_PIPELINE_KIND_COMPUTE:
      return ((const VkComputePipelineCreateInfo *)info)->pNext;
#ifdef VK_KHR_ray_tracing_pipeline
    case VK_PIPELINE_KIND_RAY:
      return ((const VkRayTracingPipelineCreateInfoKHR *)info)->pNext;
#endif
  }
  return NULL;
}

static VkResult
vk__createNativePipeline(GPUDeviceVk    *device,
                         VKPipelineKind  kind,
                         VkPipelineCache cache,
                         const void     *info,
                         const void     *next,
                         bool            replaceNext,
                         bool            useFlags2,
                         VkPipeline     *pipeline) {
  VKPipelineCreateInfoUnion local;
  const void               *createInfo;

  createInfo = info;
  if (replaceNext) {
    switch (kind) {
      case VK_PIPELINE_KIND_GRAPHICS:
        local.graphics       = *(const VkGraphicsPipelineCreateInfo *)info;
        local.graphics.pNext = next;
        if (useFlags2) {
          local.graphics.flags = 0u;
        }
        createInfo = &local.graphics;
        break;
      case VK_PIPELINE_KIND_COMPUTE:
        local.compute       = *(const VkComputePipelineCreateInfo *)info;
        local.compute.pNext = next;
        if (useFlags2) {
          local.compute.flags = 0u;
        }
        createInfo = &local.compute;
        break;
#ifdef VK_KHR_ray_tracing_pipeline
      case VK_PIPELINE_KIND_RAY:
        local.ray       = *(const VkRayTracingPipelineCreateInfoKHR *)info;
        local.ray.pNext = next;
        if (useFlags2) {
          local.ray.flags = 0u;
        }
        createInfo = &local.ray;
        break;
#endif
    }
  }

  switch (kind) {
    case VK_PIPELINE_KIND_GRAPHICS:
      return vkCreateGraphicsPipelines(
        device->device,
        cache,
        1u,
        (const VkGraphicsPipelineCreateInfo *)createInfo,
        NULL,
        pipeline
      );
    case VK_PIPELINE_KIND_COMPUTE:
      return vkCreateComputePipelines(
        device->device,
        cache,
        1u,
        (const VkComputePipelineCreateInfo *)createInfo,
        NULL,
        pipeline
      );
#ifdef VK_KHR_ray_tracing_pipeline
    case VK_PIPELINE_KIND_RAY:
      return device->createRayTracingPipelines(
        device->device,
        VK_NULL_HANDLE,
        cache,
        1u,
        (const VkRayTracingPipelineCreateInfoKHR *)createInfo,
        NULL,
        pipeline
      );
#endif
  }
  return VK_ERROR_INITIALIZATION_FAILED;
}

static void
vk__destroyBinarySet(GPUDeviceVk *device, VKPipelineBinarySet *set) {
  if (!set) {
    return;
  }
  if (device && set->handles) {
    for (uint32_t i = 0u; i < set->count; i++) {
      if (set->handles[i]) {
        device->destroyPipelineBinary(device->device,
                                      set->handles[i],
                                      NULL);
      }
    }
  }
  free(set->handles);
  free(set->data);
  free(set->keys);
  free(set->storage);
  memset(set, 0, sizeof(*set));
}

static bool
vk__keyValid(const VkPipelineBinaryKeyKHR *key) {
  return key && key->keySize > 0u &&
         key->keySize <= VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR;
}

static bool
vk__ensureDirectory(const char *path) {
  int result;

#if defined(_WIN32) || defined(WIN32)
  result = _mkdir(path);
#else
  result = mkdir(path, 0700);
#endif
  return result == 0 || errno == EEXIST;
}

static char *
vk__binaryFilePath(const VKPipelineCache       *native,
                   const VkPipelineBinaryKeyKHR *key) {
  static const char digits[] = "0123456789abcdef";
  size_t            directoryLength;
  size_t            nameLength;
  char             *path;

  if (!native || !native->binaryPath || !vk__keyValid(key)) {
    return NULL;
  }
  directoryLength = strlen(native->binaryPath);
  nameLength      = (size_t)key->keySize * 2u;
  if (directoryLength > SIZE_MAX - nameLength - sizeof("/.bin")) {
    return NULL;
  }
  path = malloc(directoryLength + nameLength + sizeof("/.bin"));
  if (!path) {
    return NULL;
  }
  memcpy(path, native->binaryPath, directoryLength);
  path[directoryLength++] = '/';
  for (uint32_t i = 0u; i < key->keySize; i++) {
    path[directoryLength + i * 2u] = digits[key->key[i] >> 4u];
    path[directoryLength + i * 2u + 1u] = digits[key->key[i] & 15u];
  }
  memcpy(path + directoryLength + nameLength, ".bin", sizeof(".bin"));
  return path;
}

static bool
vk__readStoredBinaries(const VKPipelineCache       *native,
                       const VkPipelineBinaryKeyKHR *pipelineKey,
                       VKPipelineBinarySet          *set) {
  VKPipelineBinaryFileHeader header;
  VKPipelineBinaryRecord     record;
  uint8_t                    *cursor;
  uint8_t                    *end;
  char                       *path;
  size_t                      fileSize;

  memset(set, 0, sizeof(*set));
  path = vk__binaryFilePath(native, pipelineKey);
  if (!path) {
    return false;
  }
  set->storage = vk__readCache(path, &fileSize);
  free(path);
  if (!set->storage || fileSize < sizeof(header)) {
    vk__destroyBinarySet(NULL, set);
    return false;
  }
  memcpy(&header, set->storage, sizeof(header));
  if (header.magic != VK_PIPELINE_BINARY_CACHE_MAGIC ||
      header.version != VK_PIPELINE_BINARY_CACHE_VERSION ||
      header.globalKeySize != native->globalKey.keySize ||
      header.pipelineKeySize != pipelineKey->keySize ||
      header.binaryCount == 0u ||
      header.binaryCount > VK_PIPELINE_BINARY_CACHE_MAX ||
      header.totalDataSize > SIZE_MAX ||
      memcmp(header.globalKey,
             native->globalKey.key,
             header.globalKeySize) != 0 ||
      memcmp(header.pipelineKey,
             pipelineKey->key,
             header.pipelineKeySize) != 0) {
    vk__destroyBinarySet(NULL, set);
    return false;
  }

  set->keys    = calloc(header.binaryCount, sizeof(*set->keys));
  set->data    = calloc(header.binaryCount, sizeof(*set->data));
  set->handles = calloc(header.binaryCount, sizeof(*set->handles));
  if (!set->keys || !set->data || !set->handles) {
    vk__destroyBinarySet(NULL, set);
    return false;
  }
  set->count = header.binaryCount;
  cursor     = (uint8_t *)set->storage + sizeof(header);
  end        = (uint8_t *)set->storage + fileSize;
  for (uint32_t i = 0u; i < set->count; i++) {
    if ((size_t)(end - cursor) < sizeof(record)) {
      vk__destroyBinarySet(NULL, set);
      return false;
    }
    memcpy(&record, cursor, sizeof(record));
    cursor += sizeof(record);
    if (record.keySize == 0u ||
        record.keySize > VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR ||
        record.dataSize == 0u || record.dataSize > SIZE_MAX ||
        (uint64_t)(end - cursor) < record.dataSize) {
      vk__destroyBinarySet(NULL, set);
      return false;
    }
    set->keys[i].sType   = VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR;
    set->keys[i].keySize = record.keySize;
    memcpy(set->keys[i].key, record.key, record.keySize);
    set->data[i].dataSize = (size_t)record.dataSize;
    set->data[i].pData    = cursor;
    cursor += (size_t)record.dataSize;
  }
  if (cursor != end ||
      (uint64_t)(fileSize - sizeof(header) -
                 (size_t)set->count * sizeof(record)) !=
        header.totalDataSize) {
    vk__destroyBinarySet(NULL, set);
    return false;
  }
  return true;
}

static bool
vk__writeStoredBinaries(const VKPipelineCache       *native,
                        const VkPipelineBinaryKeyKHR *pipelineKey,
                        const VKPipelineBinarySet    *set) {
  VKPipelineBinaryFileHeader header = {0};
  VKPipelineBinaryRecord     record = {0};
  char                       *path;
  char                       *temporaryPath;
  FILE                       *file;
  size_t                      pathLength;
  bool                        written;
  int                         processId;

  if (!native || !native->diskBinary || !vk__keyValid(pipelineKey) ||
      !set || set->count == 0u ||
      set->count > VK_PIPELINE_BINARY_CACHE_MAX ||
      !vk__ensureDirectory(native->binaryPath)) {
    return false;
  }
  for (uint32_t i = 0u; i < set->count; i++) {
    if (!vk__keyValid(&set->keys[i]) || !set->data[i].pData ||
        set->data[i].dataSize == 0u ||
        header.totalDataSize > UINT64_MAX - set->data[i].dataSize) {
      return false;
    }
    header.totalDataSize += set->data[i].dataSize;
  }
  header.magic           = VK_PIPELINE_BINARY_CACHE_MAGIC;
  header.version         = VK_PIPELINE_BINARY_CACHE_VERSION;
  header.globalKeySize   = native->globalKey.keySize;
  header.pipelineKeySize = pipelineKey->keySize;
  header.binaryCount     = set->count;
  memcpy(header.globalKey,
         native->globalKey.key,
         native->globalKey.keySize);
  memcpy(header.pipelineKey, pipelineKey->key, pipelineKey->keySize);

  path = vk__binaryFilePath(native, pipelineKey);
  if (!path) {
    return false;
  }
  pathLength = strlen(path);
#if defined(_WIN32) || defined(WIN32)
  processId = _getpid();
#else
  processId = (int)getpid();
#endif
  temporaryPath = malloc(pathLength + 64u);
  if (!temporaryPath) {
    free(path);
    return false;
  }
  snprintf(temporaryPath,
           pathLength + 64u,
           "%s.tmp.%d.%p",
           path,
           processId,
           (const void *)native);
  file    = fopen(temporaryPath, "wb");
  written = file && fwrite(&header, sizeof(header), 1u, file) == 1u;
  for (uint32_t i = 0u; written && i < set->count; i++) {
    memset(&record, 0, sizeof(record));
    record.dataSize = set->data[i].dataSize;
    record.keySize  = set->keys[i].keySize;
    memcpy(record.key, set->keys[i].key, set->keys[i].keySize);
    written = fwrite(&record, sizeof(record), 1u, file) == 1u &&
              fwrite(set->data[i].pData,
                     1u,
                     set->data[i].dataSize,
                     file) == set->data[i].dataSize;
  }
  if (file && fclose(file) != 0) {
    written = false;
  }
  if (written) {
    written = gpuCacheFileReplace(temporaryPath, path);
  }
  if (!written) {
    remove(temporaryPath);
  }
  free(temporaryPath);
  free(path);
  return written;
}

static bool
vk__getPipelineKey(GPUDeviceVk           *device,
                   const void            *info,
                   VkPipelineBinaryKeyKHR *key) {
  VkPipelineCreateInfoKHR createInfo = {0};

  memset(key, 0, sizeof(*key));
  createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_INFO_KHR;
  createInfo.pNext = (void *)info;
  key->sType       = VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR;
  return device->getPipelineKey(device->device, &createInfo, key) ==
           VK_SUCCESS &&
         vk__keyValid(key);
}

static bool
vk__createStoredBinaryHandles(GPUDeviceVk         *device,
                              VKPipelineBinarySet *set) {
  VkPipelineBinaryKeysAndDataKHR keysAndData = {0};
  VkPipelineBinaryCreateInfoKHR  createInfo  = {0};
  VkPipelineBinaryHandlesInfoKHR handlesInfo = {0};
  VkResult                       result;

  keysAndData.binaryCount         = set->count;
  keysAndData.pPipelineBinaryKeys = set->keys;
  keysAndData.pPipelineBinaryData = set->data;
  createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_CREATE_INFO_KHR;
  createInfo.pKeysAndDataInfo = &keysAndData;
  handlesInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_HANDLES_INFO_KHR;
  handlesInfo.pipelineBinaryCount = set->count;
  handlesInfo.pPipelineBinaries   = set->handles;
  result = device->createPipelineBinaries(device->device,
                                           &createInfo,
                                           NULL,
                                           &handlesInfo);
  return result == VK_SUCCESS &&
         handlesInfo.pipelineBinaryCount == set->count;
}

static bool
vk__createInternalBinaryHandles(GPUDeviceVk         *device,
                                const void          *info,
                                VKPipelineBinarySet *set) {
  VkPipelineCreateInfoKHR        pipelineInfo = {0};
  VkPipelineBinaryCreateInfoKHR  createInfo   = {0};
  VkPipelineBinaryHandlesInfoKHR handlesInfo  = {0};
  VkResult                       result;

  memset(set, 0, sizeof(*set));
  pipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_INFO_KHR;
  pipelineInfo.pNext = (void *)info;
  createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_CREATE_INFO_KHR;
  createInfo.pPipelineCreateInfo = &pipelineInfo;
  handlesInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_HANDLES_INFO_KHR;
  result = device->createPipelineBinaries(device->device,
                                           &createInfo,
                                           NULL,
                                           &handlesInfo);
  if (result != VK_SUCCESS || handlesInfo.pipelineBinaryCount == 0u ||
      handlesInfo.pipelineBinaryCount > VK_PIPELINE_BINARY_CACHE_MAX) {
    return false;
  }
  set->count   = handlesInfo.pipelineBinaryCount;
  set->handles = calloc(set->count, sizeof(*set->handles));
  if (!set->handles) {
    memset(set, 0, sizeof(*set));
    return false;
  }
  handlesInfo.pPipelineBinaries = set->handles;
  result = device->createPipelineBinaries(device->device,
                                           &createInfo,
                                           NULL,
                                           &handlesInfo);
  if (result != VK_SUCCESS || handlesInfo.pipelineBinaryCount != set->count) {
    vk__destroyBinarySet(device, set);
    return false;
  }
  return true;
}

static void
vk__releaseCapturedPipeline(GPUDeviceVk *device, VkPipeline pipeline) {
  VkReleaseCapturedPipelineDataInfoKHR info = {0};

  info.sType    = VK_STRUCTURE_TYPE_RELEASE_CAPTURED_PIPELINE_DATA_INFO_KHR;
  info.pipeline = pipeline;
  device->releaseCapturedPipelineData(device->device, &info, NULL);
}

static bool
vk__capturePipelineBinaries(GPUDeviceVk               *device,
                            VkPipeline                  pipeline,
                            VKPipelineBinarySet        *set) {
  VkPipelineBinaryCreateInfoKHR  createInfo   = {0};
  VkPipelineBinaryHandlesInfoKHR handlesInfo  = {0};
  VkPipelineBinaryDataInfoKHR    dataInfo     = {0};
  VkResult                       result;
  size_t                         totalSize;
  uint8_t                       *cursor;
  bool                           complete;

  memset(set, 0, sizeof(*set));
  createInfo.sType    = VK_STRUCTURE_TYPE_PIPELINE_BINARY_CREATE_INFO_KHR;
  createInfo.pipeline = pipeline;
  handlesInfo.sType   = VK_STRUCTURE_TYPE_PIPELINE_BINARY_HANDLES_INFO_KHR;
  result = device->createPipelineBinaries(device->device,
                                           &createInfo,
                                           NULL,
                                           &handlesInfo);
  if (result != VK_SUCCESS || handlesInfo.pipelineBinaryCount == 0u ||
      handlesInfo.pipelineBinaryCount > VK_PIPELINE_BINARY_CACHE_MAX) {
    vk__releaseCapturedPipeline(device, pipeline);
    return false;
  }
  set->count   = handlesInfo.pipelineBinaryCount;
  set->handles = calloc(set->count, sizeof(*set->handles));
  set->keys    = calloc(set->count, sizeof(*set->keys));
  set->data    = calloc(set->count, sizeof(*set->data));
  if (!set->handles || !set->keys || !set->data) {
    vk__destroyBinarySet(device, set);
    vk__releaseCapturedPipeline(device, pipeline);
    return false;
  }
  handlesInfo.pPipelineBinaries = set->handles;
  result = device->createPipelineBinaries(device->device,
                                           &createInfo,
                                           NULL,
                                           &handlesInfo);
  complete = result == VK_SUCCESS &&
             handlesInfo.pipelineBinaryCount == set->count;
  totalSize = 0u;
  for (uint32_t i = 0u; complete && i < set->count; i++) {
    dataInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_BINARY_DATA_INFO_KHR;
    dataInfo.pipelineBinary = set->handles[i];
    set->keys[i].sType      = VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR;
    result = device->getPipelineBinaryData(device->device,
                                            &dataInfo,
                                            &set->keys[i],
                                            &set->data[i].dataSize,
                                            NULL);
    complete = result == VK_SUCCESS && vk__keyValid(&set->keys[i]) &&
               set->data[i].dataSize > 0u &&
               totalSize <= SIZE_MAX - set->data[i].dataSize;
    if (complete) {
      totalSize += set->data[i].dataSize;
    }
  }
  if (complete) {
    set->storage = malloc(totalSize);
    complete     = set->storage != NULL;
  }
  cursor = set->storage;
  for (uint32_t i = 0u; complete && i < set->count; i++) {
    size_t capacity;

    dataInfo.pipelineBinary = set->handles[i];
    capacity                = set->data[i].dataSize;
    set->data[i].pData      = cursor;
    result = device->getPipelineBinaryData(device->device,
                                            &dataInfo,
                                            &set->keys[i],
                                            &capacity,
                                            cursor);
    complete = result == VK_SUCCESS && capacity > 0u &&
               capacity <= set->data[i].dataSize;
    if (complete) {
      set->data[i].dataSize = capacity;
      cursor += capacity;
    }
  }
  vk__releaseCapturedPipeline(device, pipeline);
  if (!complete) {
    vk__destroyBinarySet(device, set);
  }
  return complete;
}

static VkResult
vk__createPipelineFromBinaries(GPUDeviceVk               *device,
                               VKPipelineKind             kind,
                               const void                *info,
                               const VKPipelineBinarySet *set,
                               VkPipeline                *pipeline) {
  VkPipelineBinaryInfoKHR binaryInfo = {0};

  binaryInfo.sType   = VK_STRUCTURE_TYPE_PIPELINE_BINARY_INFO_KHR;
  binaryInfo.pNext   = vk__pipelineNext(kind, info);
  binaryInfo.binaryCount      = set->count;
  binaryInfo.pPipelineBinaries = set->handles;
  return vk__createNativePipeline(device,
                                  kind,
                                  VK_NULL_HANDLE,
                                  info,
                                  &binaryInfo,
                                  true,
                                  false,
                                  pipeline);
}

static VkResult
vk__createCapturePipeline(GPUDeviceVk    *device,
                          VKPipelineKind  kind,
                          const void     *info,
                          VkPipeline     *pipeline) {
  VkPipelineCreateFlags2CreateInfoKHR flags = {0};

  flags.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR;
  flags.pNext = vk__pipelineNext(kind, info);
  flags.flags = (VkPipelineCreateFlags2)vk__pipelineFlags(kind, info) |
                VK_PIPELINE_CREATE_2_CAPTURE_DATA_BIT_KHR;
  return vk__createNativePipeline(device,
                                  kind,
                                  VK_NULL_HANDLE,
                                  info,
                                  &flags,
                                  true,
                                  true,
                                  pipeline);
}

static void
vk__removeStoredBinaries(const VKPipelineCache       *native,
                         const VkPipelineBinaryKeyKHR *pipelineKey) {
  char *path;

  path = vk__binaryFilePath(native, pipelineKey);
  if (path) {
    remove(path);
    free(path);
  }
}

static VkResult
vk__createPipelineCached(GPUDeviceVk      *device,
                         GPUPipelineCache *cache,
                         VKPipelineKind    kind,
                         const void       *info,
                         VkPipeline       *pipeline) {
  VKPipelineCache       *native;
  VKPipelineBinarySet    binaries = {0};
  VkPipelineBinaryKeyKHR pipelineKey = {0};
  VkPipelineCache        classicCache;
  VkResult               result;
  bool                   havePipelineKey;

  if (!device || !info || !pipeline) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  *pipeline = VK_NULL_HANDLE;
  native    = vk__nativeCache(cache);
  if (!native) {
    return vk__createNativePipeline(device,
                                    kind,
                                    VK_NULL_HANDLE,
                                    info,
                                    NULL,
                                    false,
                                    false,
                                    pipeline);
  }
  classicCache = vk_lockCache(cache);
  if (native->deviceVk != device) {
    vk_unlockCache(cache);
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  havePipelineKey = native->diskBinary &&
                    vk__getPipelineKey(device,
                                       info,
                                       &pipelineKey);
  if (havePipelineKey &&
      vk__readStoredBinaries(native, &pipelineKey, &binaries)) {
    if (vk__createStoredBinaryHandles(device, &binaries)) {
      result = vk__createPipelineFromBinaries(device,
                                               kind,
                                               info,
                                               &binaries,
                                               pipeline);
      if (result == VK_SUCCESS) {
        vk__destroyBinarySet(device, &binaries);
        vk_unlockCache(cache);
        return VK_SUCCESS;
      }
      if (*pipeline) {
        vkDestroyPipeline(device->device, *pipeline, NULL);
        *pipeline = VK_NULL_HANDLE;
      }
    }
    vk__destroyBinarySet(device, &binaries);
    vk__removeStoredBinaries(native, &pipelineKey);
  }

  if (native->internalBinary &&
      vk__createInternalBinaryHandles(device, info, &binaries)) {
    result = vk__createPipelineFromBinaries(device,
                                             kind,
                                             info,
                                             &binaries,
                                             pipeline);
    vk__destroyBinarySet(device, &binaries);
    if (result == VK_SUCCESS) {
      vk_unlockCache(cache);
      return VK_SUCCESS;
    }
    if (*pipeline) {
      vkDestroyPipeline(device->device, *pipeline, NULL);
      *pipeline = VK_NULL_HANDLE;
    }
  }

  if (havePipelineKey) {
    result = vk__createCapturePipeline(device, kind, info, pipeline);
    if (result == VK_SUCCESS) {
      if (vk__capturePipelineBinaries(device, *pipeline, &binaries)) {
        vk__writeStoredBinaries(native, &pipelineKey, &binaries);
        vk__destroyBinarySet(device, &binaries);
      }
      vk_unlockCache(cache);
      return VK_SUCCESS;
    }
    if (*pipeline) {
      vkDestroyPipeline(device->device, *pipeline, NULL);
      *pipeline = VK_NULL_HANDLE;
    }
  }

  result = vk__createNativePipeline(device,
                                    kind,
                                    classicCache,
                                    info,
                                    NULL,
                                    false,
                                    false,
                                    pipeline);
  vk_unlockCache(cache);
  return result;
}
#endif

GPU_HIDE
VkResult
vk_createGraphicsPipelineCached(GPUDeviceVk                        *device,
                                GPUPipelineCache                   *cache,
                                const VkGraphicsPipelineCreateInfo *info,
                                VkPipeline                         *pipeline) {
#ifdef VK_KHR_pipeline_binary
  return vk__createPipelineCached(device,
                                  cache,
                                  VK_PIPELINE_KIND_GRAPHICS,
                                  info,
                                  pipeline);
#else
  VkPipelineCache nativeCache;
  VkResult        result;

  nativeCache = vk_lockCache(cache);
  result = vkCreateGraphicsPipelines(device->device,
                                     nativeCache,
                                     1u,
                                     info,
                                     NULL,
                                     pipeline);
  vk_unlockCache(cache);
  return result;
#endif
}

GPU_HIDE
VkResult
vk_createComputePipelineCached(GPUDeviceVk                       *device,
                               GPUPipelineCache                  *cache,
                               const VkComputePipelineCreateInfo *info,
                               VkPipeline                        *pipeline) {
#ifdef VK_KHR_pipeline_binary
  return vk__createPipelineCached(device,
                                  cache,
                                  VK_PIPELINE_KIND_COMPUTE,
                                  info,
                                  pipeline);
#else
  VkPipelineCache nativeCache;
  VkResult        result;

  nativeCache = vk_lockCache(cache);
  result = vkCreateComputePipelines(device->device,
                                    nativeCache,
                                    1u,
                                    info,
                                    NULL,
                                    pipeline);
  vk_unlockCache(cache);
  return result;
#endif
}

#ifdef VK_KHR_ray_tracing_pipeline
GPU_HIDE
VkResult
vk_createRayPipelineCached(GPUDeviceVk                            *device,
                           GPUPipelineCache                       *cache,
                           const VkRayTracingPipelineCreateInfoKHR *info,
                           VkPipeline                             *pipeline) {
#ifdef VK_KHR_pipeline_binary
  return vk__createPipelineCached(device,
                                  cache,
                                  VK_PIPELINE_KIND_RAY,
                                  info,
                                  pipeline);
#else
  VkPipelineCache nativeCache;
  VkResult        result;

  nativeCache = vk_lockCache(cache);
  result = device->createRayTracingPipelines(device->device,
                                              VK_NULL_HANDLE,
                                              nativeCache,
                                              1u,
                                              info,
                                              NULL,
                                              pipeline);
  vk_unlockCache(cache);
  return result;
#endif
}
#endif

GPU_HIDE
void
vk_initPipelineCache(GPUApiPipelineCache *api) {
  api->create  = vk_createCache;
  api->destroy = vk_destroyCache;
}
