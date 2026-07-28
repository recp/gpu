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
#include "../impl.h"
#include "../../cache_file.h"
#include "../../../api/pipeline_cache_internal.h"
#include "pipeline_cache.h"

#define DX12_PIPELINE_KEY_VERSION   1u
#define DX12_PIPELINE_CACHE_MAGIC   0x43584447u
#define DX12_PIPELINE_CACHE_VERSION 2u
#define DX12_PIPELINE_NAME_SIZE     34u

#if defined(__ID3D12Device1_INTERFACE_DEFINED__) && \
    defined(__ID3D12PipelineLibrary_INTERFACE_DEFINED__)
#  define DX12_HAS_PIPELINE_LIBRARY 1
#else
#  define DX12_HAS_PIPELINE_LIBRARY 0
#endif

typedef struct DX12PipelineCacheEntry {
  struct DX12PipelineCacheEntry *next;
  DX12PipelineKey                key;
  size_t                         dataSize;
  uint8_t                        data[];
} DX12PipelineCacheEntry;

typedef struct DX12PipelineCacheHeader {
  uint64_t entryCount;
  uint64_t librarySize;
  uint32_t magic;
  uint32_t version;
} DX12PipelineCacheHeader;

typedef struct DX12PipelineCacheRecord {
  uint64_t key[2];
  uint64_t dataSize;
} DX12PipelineCacheRecord;

typedef struct DX12PipelineCache {
  DX12PipelineCacheEntry *entries;
  char                   *path;
#if DX12_HAS_PIPELINE_LIBRARY
  void                   *libraryBacking;
  ID3D12PipelineLibrary  *library;
#  if defined(__ID3D12PipelineLibrary1_INTERFACE_DEFINED__)
  ID3D12PipelineLibrary1 *library1;
#  endif
#endif
  size_t                  entryCount;
  SRWLOCK                 lock;
  bool                    dirty;
} DX12PipelineCache;

static DX12PipelineCache *
dx12__nativeCache(GPUPipelineCache *cache) {
  return cache ? cache->_priv : NULL;
}

static void
dx12__mergeStoredCache(DX12PipelineCache *native);

GPU_HIDE
void
dx12_keyInit(DX12PipelineKey *key) {
  uint32_t version;

  key->value[0] = 14695981039346656037ull;
  key->value[1] = 7809847782465536322ull;
  version       = DX12_PIPELINE_KEY_VERSION;
  dx12_keyWrite(key, &version, sizeof(version));
}

GPU_HIDE
void
dx12_keyWrite(DX12PipelineKey *key, const void *data, size_t size) {
  const uint8_t *bytes;

  if (!key || (!data && size > 0u)) {
    return;
  }
  bytes = data;
  for (size_t i = 0u; i < size; i++) {
    key->value[0] ^= bytes[i];
    key->value[0] *= 1099511628211ull;
    key->value[1] ^= bytes[i];
    key->value[1] *= 14029467366897019727ull;
  }
}

static void *
dx12__readCache(const char *path, size_t *outSize) {
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

static void
dx12__freeEntries(DX12PipelineCache *native) {
  DX12PipelineCacheEntry *entry;

  entry = native ? native->entries : NULL;
  while (entry) {
    DX12PipelineCacheEntry *next;

    next = entry->next;
    free(entry);
    entry = next;
  }
  if (native) {
    native->entries    = NULL;
    native->entryCount = 0u;
  }
}

static bool
dx12__loadCache(DX12PipelineCache *native,
                const void        *data,
                size_t             dataSize,
                const void       **outLibraryData,
                size_t            *outLibrarySize) {
  DX12PipelineCacheEntry **tail;
  const uint8_t           *bytes;
  DX12PipelineCacheHeader  header;
  size_t                   cursor;

  if (outLibraryData) {
    *outLibraryData = NULL;
  }
  if (outLibrarySize) {
    *outLibrarySize = 0u;
  }
  if (!native || !data || dataSize < sizeof(header)) {
    return false;
  }

  bytes = data;
  memcpy(&header, bytes, sizeof(header));
  if (header.magic != DX12_PIPELINE_CACHE_MAGIC ||
      header.version != DX12_PIPELINE_CACHE_VERSION ||
      header.entryCount > (uint64_t)SIZE_MAX) {
    return false;
  }

  cursor = sizeof(header);
  if (header.librarySize > (uint64_t)SIZE_MAX ||
      (size_t)header.librarySize > dataSize - cursor) {
    goto invalid;
  }
  if (outLibraryData && header.librarySize > 0u) {
    *outLibraryData = bytes + cursor;
  }
  if (outLibrarySize) {
    *outLibrarySize = (size_t)header.librarySize;
  }
  cursor += (size_t)header.librarySize;
  if ((size_t)header.entryCount >
      (dataSize - cursor) / sizeof(DX12PipelineCacheRecord)) {
    goto invalid;
  }
  tail   = &native->entries;
  for (uint64_t i = 0u; i < header.entryCount; i++) {
    DX12PipelineCacheEntry *entry;
    DX12PipelineCacheRecord record;

    if (sizeof(record) > dataSize - cursor) {
      goto invalid;
    }
    memcpy(&record, bytes + cursor, sizeof(record));
    cursor += sizeof(record);
    if (record.dataSize == 0u || record.dataSize > (uint64_t)SIZE_MAX ||
        (size_t)record.dataSize > dataSize - cursor ||
        (size_t)record.dataSize > SIZE_MAX - sizeof(*entry)) {
      goto invalid;
    }

    entry = malloc(sizeof(*entry) + (size_t)record.dataSize);
    if (!entry) {
      goto invalid;
    }
    entry->next         = NULL;
    entry->key.value[0] = record.key[0];
    entry->key.value[1] = record.key[1];
    entry->dataSize     = (size_t)record.dataSize;
    memcpy(entry->data, bytes + cursor, entry->dataSize);
    cursor += entry->dataSize;
    *tail = entry;
    tail  = &entry->next;
    native->entryCount++;
  }
  if (cursor != dataSize) {
    goto invalid;
  }
  return true;

invalid:
  if (outLibraryData) {
    *outLibraryData = NULL;
  }
  if (outLibrarySize) {
    *outLibrarySize = 0u;
  }
  dx12__freeEntries(native);
  return false;
}

#if DX12_HAS_PIPELINE_LIBRARY
static void
dx12__createPipelineLibrary(DX12PipelineCache *native,
                            ID3D12Device       *device,
                            void               *data,
                            size_t              dataSize) {
  D3D12_FEATURE_DATA_SHADER_CACHE support = {0};
  ID3D12Device1                  *device1;
  HRESULT                         result;

  device1 = NULL;
  if (!native || !device ||
      FAILED(device->lpVtbl->CheckFeatureSupport(device,
                                                 D3D12_FEATURE_SHADER_CACHE,
                                                 &support,
                                                 sizeof(support))) ||
      !(support.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_LIBRARY) ||
      FAILED(device->lpVtbl->QueryInterface(device,
                                            &IID_ID3D12Device1,
                                            (void **)&device1)) ||
      !device1) {
    free(data);
    return;
  }

  result = device1->lpVtbl->CreatePipelineLibrary(
    device1,
    data,
    dataSize,
    &IID_ID3D12PipelineLibrary,
    (void **)&native->library
  );
  if (SUCCEEDED(result) && native->library) {
    native->libraryBacking = data;
  }
  if ((FAILED(result) || !native->library) && dataSize > 0u) {
    if (native->library) {
      native->library->lpVtbl->Release(native->library);
    }
    native->library = NULL;
    free(data);
    result = device1->lpVtbl->CreatePipelineLibrary(
      device1,
      NULL,
      0u,
      &IID_ID3D12PipelineLibrary,
      (void **)&native->library
    );
    if (SUCCEEDED(result) && native->library) {
      native->dirty = true;
    }
  } else if (FAILED(result) || !native->library) {
    free(data);
  }
  if (FAILED(result) && native->library) {
    native->library->lpVtbl->Release(native->library);
    native->library = NULL;
  }
  device1->lpVtbl->Release(device1);

#  if defined(__ID3D12PipelineLibrary1_INTERFACE_DEFINED__)
  if (SUCCEEDED(result) && native->library) {
    (void)native->library->lpVtbl->QueryInterface(
      native->library,
      &IID_ID3D12PipelineLibrary1,
      (void **)&native->library1
    );
  }
#  endif
}

static void
dx12__pipelineName(const DX12PipelineKey *key,
                   WCHAR                  name[DX12_PIPELINE_NAME_SIZE]) {
  static const WCHAR hex[] = L"0123456789abcdef";
  uint32_t           cursor;

  name[0] = L'g';
  cursor  = 1u;
  for (uint32_t word = 0u; word < 2u; word++) {
    for (uint32_t shift = 64u; shift > 0u; shift -= 4u) {
      name[cursor++] = hex[(key->value[word] >> (shift - 4u)) & 0xfu];
    }
  }
  name[cursor] = L'\0';
}

static HRESULT
dx12__loadGraphicsLibrary(DX12PipelineCache                         *native,
                          const DX12PipelineKey                     *key,
                          const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
                          ID3D12PipelineState                      **outState) {
  WCHAR   name[DX12_PIPELINE_NAME_SIZE];
  HRESULT result;

  if (!native || !native->library) {
    return E_NOINTERFACE;
  }
  dx12__pipelineName(key, name);
  AcquireSRWLockExclusive(&native->lock);
  result = native->library->lpVtbl->LoadGraphicsPipeline(
    native->library,
    name,
    desc,
    &IID_ID3D12PipelineState,
    (void **)outState
  );
  ReleaseSRWLockExclusive(&native->lock);
  return result;
}

static HRESULT
dx12__loadComputeLibrary(DX12PipelineCache                        *native,
                         const DX12PipelineKey                    *key,
                         const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
                         ID3D12PipelineState                     **outState) {
  WCHAR   name[DX12_PIPELINE_NAME_SIZE];
  HRESULT result;

  if (!native || !native->library) {
    return E_NOINTERFACE;
  }
  dx12__pipelineName(key, name);
  AcquireSRWLockExclusive(&native->lock);
  result = native->library->lpVtbl->LoadComputePipeline(
    native->library,
    name,
    desc,
    &IID_ID3D12PipelineState,
    (void **)outState
  );
  ReleaseSRWLockExclusive(&native->lock);
  return result;
}

#  if defined(__ID3D12PipelineLibrary1_INTERFACE_DEFINED__)
static HRESULT
dx12__loadMeshLibrary(DX12PipelineCache                       *native,
                      const DX12PipelineKey                   *key,
                      const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                      ID3D12PipelineState                    **outState) {
  WCHAR   name[DX12_PIPELINE_NAME_SIZE];
  HRESULT result;

  if (!native || !native->library1) {
    return E_NOINTERFACE;
  }
  dx12__pipelineName(key, name);
  AcquireSRWLockExclusive(&native->lock);
  result = native->library1->lpVtbl->LoadPipeline(
    native->library1,
    name,
    desc,
    &IID_ID3D12PipelineState,
    (void **)outState
  );
  ReleaseSRWLockExclusive(&native->lock);
  return result;
}
#  endif

static void
dx12__storeLibrary(DX12PipelineCache     *native,
                   const DX12PipelineKey *key,
                   ID3D12PipelineState    *state) {
  WCHAR   name[DX12_PIPELINE_NAME_SIZE];
  HRESULT result;

  if (!native || !native->library || !state) {
    return;
  }
  dx12__pipelineName(key, name);
  AcquireSRWLockExclusive(&native->lock);
  result = native->library->lpVtbl->StorePipeline(native->library,
                                                   name,
                                                   state);
  if (SUCCEEDED(result)) {
    native->dirty = true;
  }
  ReleaseSRWLockExclusive(&native->lock);
}
#endif

static char *
dx12__copyPath(const char *path) {
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
dx12_createCache(GPUDevice                        *device,
                 const GPUPipelineCacheCreateInfo *info,
                 GPUPipelineCache                 *cache) {
  DX12PipelineCache *native;
  GPUDeviceDX12     *deviceDX12;
  const void        *libraryData;
#if DX12_HAS_PIPELINE_LIBRARY
  void              *libraryBacking;
#endif
  void              *initialData;
  size_t             librarySize;
  size_t             initialSize;

  deviceDX12 = device ? device->_priv : NULL;
  if (!deviceDX12 || !deviceDX12->d3dDevice || !info ||
      !info->cachePath || !cache) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->path = dx12__copyPath(info->cachePath);
  if (!native->path) {
    free(native);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  InitializeSRWLock(&native->lock);

  initialData = dx12__readCache(native->path, &initialSize);
  libraryData = NULL;
  librarySize = 0u;
  (void)dx12__loadCache(native,
                        initialData,
                        initialSize,
                        &libraryData,
                        &librarySize);
#if DX12_HAS_PIPELINE_LIBRARY
  libraryBacking = NULL;
  if (librarySize > 0u) {
    libraryBacking = malloc(librarySize);
    if (libraryBacking) {
      memcpy(libraryBacking, libraryData, librarySize);
    } else {
      librarySize = 0u;
    }
  }
  dx12__createPipelineLibrary(native,
                              deviceDX12->d3dDevice,
                              libraryBacking,
                              librarySize);
#else
  GPU__UNUSED(libraryData);
  GPU__UNUSED(librarySize);
#endif
  free(initialData);

  cache->_priv = native;
  return GPU_OK;
}

static void
dx12__storeCache(DX12PipelineCache *native) {
  GPUCacheFileGuard        guard;
  DX12PipelineCacheEntry *entry;
  uint8_t                *data;
  uint8_t                *libraryData;
  char                   *temporaryPath;
  FILE                   *file;
  size_t                  dataSize;
  size_t                  librarySize;
  size_t                  cursor;
  uint64_t                entryCount;
  bool                    valid;
  bool                    written;

  if (!native || !native->dirty) {
    return;
  }
  if (!gpuCacheFileBegin(native->path, &guard)) {
    return;
  }
  dx12__mergeStoredCache(native);

  AcquireSRWLockExclusive(&native->lock);
  libraryData = NULL;
  librarySize = 0u;
#if DX12_HAS_PIPELINE_LIBRARY
  if (native->library) {
    SIZE_T serializedSize;

    serializedSize = native->library->lpVtbl->GetSerializedSize(native->library);
    if (serializedSize > 0u) {
      libraryData = malloc((size_t)serializedSize);
      if (libraryData &&
          SUCCEEDED(native->library->lpVtbl->Serialize(native->library,
                                                       libraryData,
                                                       serializedSize))) {
        librarySize = (size_t)serializedSize;
      } else {
        free(libraryData);
        libraryData = NULL;
      }
    }
  }
#endif
  valid      = librarySize <= SIZE_MAX - sizeof(DX12PipelineCacheHeader);
  dataSize   = valid
             ? sizeof(DX12PipelineCacheHeader) + librarySize
             : 0u;
  entryCount = 0u;
  for (entry = native->entries; valid && entry; entry = entry->next) {
    if (entry->dataSize > SIZE_MAX - sizeof(DX12PipelineCacheRecord) ||
        sizeof(DX12PipelineCacheRecord) + entry->dataSize >
          SIZE_MAX - dataSize) {
      valid = false;
      break;
    }
    dataSize += sizeof(DX12PipelineCacheRecord) + entry->dataSize;
    entryCount++;
  }
  data = valid ? malloc(dataSize) : NULL;
  if (!data) {
    valid = false;
  }
  if (valid) {
    DX12PipelineCacheHeader header;

    header.entryCount  = entryCount;
    header.librarySize = librarySize;
    header.magic       = DX12_PIPELINE_CACHE_MAGIC;
    header.version     = DX12_PIPELINE_CACHE_VERSION;
    memcpy(data, &header, sizeof(header));
    cursor = sizeof(header);
    if (librarySize > 0u) {
      memcpy(data + cursor, libraryData, librarySize);
      cursor += librarySize;
    }
    for (entry = native->entries; entry; entry = entry->next) {
      DX12PipelineCacheRecord record;

      record.key[0]   = entry->key.value[0];
      record.key[1]   = entry->key.value[1];
      record.dataSize = entry->dataSize;
      memcpy(data + cursor, &record, sizeof(record));
      cursor += sizeof(record);
      memcpy(data + cursor, entry->data, entry->dataSize);
      cursor += entry->dataSize;
    }
    valid = cursor == dataSize;
  }
  ReleaseSRWLockExclusive(&native->lock);
  free(libraryData);
  if (!valid) {
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
dx12_destroyCache(GPUPipelineCache *cache) {
  DX12PipelineCache *native;

  native = dx12__nativeCache(cache);
  if (!native) {
    return;
  }
  dx12__storeCache(native);
#if DX12_HAS_PIPELINE_LIBRARY
#  if defined(__ID3D12PipelineLibrary1_INTERFACE_DEFINED__)
  if (native->library1) {
    native->library1->lpVtbl->Release(native->library1);
  }
#  endif
  if (native->library) {
    native->library->lpVtbl->Release(native->library);
  }
  free(native->libraryBacking);
#endif
  dx12__freeEntries(native);
  free(native->path);
  free(native);
  cache->_priv = NULL;
}

#define DX12_KEY_WRITE(KEY, VALUE) \
  dx12_keyWrite((KEY), &(VALUE), sizeof(VALUE))

enum {
  DX12_PIPELINE_KIND_RENDER  = 1u,
  DX12_PIPELINE_KIND_COMPUTE = 2u,
  DX12_PIPELINE_KIND_MESH    = 3u
};

static void
dx12__depthKey(DX12PipelineKey *key, const GPUDepthStencilState *state) {
  GPUDepthStencilState        empty = {0};
  const GPUDepthStencilState *value;

  value = state ? state : &empty;
  DX12_KEY_WRITE(key, value->depthTestEnable);
  DX12_KEY_WRITE(key, value->depthWriteEnable);
  DX12_KEY_WRITE(key, value->depthCompare);
  DX12_KEY_WRITE(key, value->stencilTestEnable);
  DX12_KEY_WRITE(key, value->front.compare);
  DX12_KEY_WRITE(key, value->front.failOp);
  DX12_KEY_WRITE(key, value->front.depthFailOp);
  DX12_KEY_WRITE(key, value->front.passOp);
  DX12_KEY_WRITE(key, value->back.compare);
  DX12_KEY_WRITE(key, value->back.failOp);
  DX12_KEY_WRITE(key, value->back.depthFailOp);
  DX12_KEY_WRITE(key, value->back.passOp);
  DX12_KEY_WRITE(key, value->stencilReadMask);
  DX12_KEY_WRITE(key, value->stencilWriteMask);
}

static void
dx12__graphicsKey(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
                  const GPURenderPipelineCreateInfo       *info,
                  const DX12PipelineKey                    *rootKey,
                  uint32_t                                  kind,
                  DX12PipelineKey                          *key) {
  dx12_keyInit(key);
  DX12_KEY_WRITE(key, kind);
  dx12_keyWrite(key, rootKey, sizeof(*rootKey));
  dx12_keyWrite(key, desc->VS.pShaderBytecode, desc->VS.BytecodeLength);
  dx12_keyWrite(key, desc->PS.pShaderBytecode, desc->PS.BytecodeLength);

  DX12_KEY_WRITE(key, info->vertex.bufferLayoutCount);
  for (uint32_t i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    const GPUVertexBufferLayout *layout;

    layout = &info->vertex.pBufferLayouts[i];
    DX12_KEY_WRITE(key, layout->strideBytes);
    DX12_KEY_WRITE(key, layout->stepMode);
    DX12_KEY_WRITE(key, layout->attributeCount);
    for (uint32_t j = 0u; j < layout->attributeCount; j++) {
      const GPUVertexAttribute *attribute;

      attribute = &layout->pAttributes[j];
      DX12_KEY_WRITE(key, attribute->format);
      DX12_KEY_WRITE(key, attribute->offset);
      DX12_KEY_WRITE(key, attribute->shaderLocation);
    }
  }

  DX12_KEY_WRITE(key, info->colorTargetCount);
  for (uint32_t i = 0u; i < info->colorTargetCount; i++) {
    const GPUColorTargetState *target;

    target = &info->pColorTargets[i];
    DX12_KEY_WRITE(key, target->format);
    DX12_KEY_WRITE(key, target->blend.enabled);
    DX12_KEY_WRITE(key, target->blend.color.srcFactor);
    DX12_KEY_WRITE(key, target->blend.color.dstFactor);
    DX12_KEY_WRITE(key, target->blend.color.op);
    DX12_KEY_WRITE(key, target->blend.alpha.srcFactor);
    DX12_KEY_WRITE(key, target->blend.alpha.dstFactor);
    DX12_KEY_WRITE(key, target->blend.alpha.op);
    DX12_KEY_WRITE(key, target->blend.writeMask);
  }
  DX12_KEY_WRITE(key, info->depthStencilFormat);
  dx12__depthKey(key, info->pDepthStencilState);
  DX12_KEY_WRITE(key, info->primitiveTopology);
  DX12_KEY_WRITE(key, info->cullMode);
  DX12_KEY_WRITE(key, info->frontFace);
  DX12_KEY_WRITE(key, info->multisample.sampleCount);
  DX12_KEY_WRITE(key, info->multisample.sampleMask);
  DX12_KEY_WRITE(key, info->multisample.alphaToCoverageEnable);
}

static void
dx12__computeKey(const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
                 const DX12PipelineKey                   *rootKey,
                 DX12PipelineKey                         *key) {
  uint32_t kind;

  kind = DX12_PIPELINE_KIND_COMPUTE;
  dx12_keyInit(key);
  DX12_KEY_WRITE(key, kind);
  dx12_keyWrite(key, rootKey, sizeof(*rootKey));
  dx12_keyWrite(key, desc->CS.pShaderBytecode, desc->CS.BytecodeLength);
  DX12_KEY_WRITE(key, desc->NodeMask);
  DX12_KEY_WRITE(key, desc->Flags);
}

static DX12PipelineCacheEntry **
dx12__findEntry(DX12PipelineCache     *native,
                const DX12PipelineKey *key) {
  DX12PipelineCacheEntry **link;

  for (link = &native->entries; *link; link = &(*link)->next) {
    if ((*link)->key.value[0] == key->value[0] &&
        (*link)->key.value[1] == key->value[1]) {
      return link;
    }
  }
  return link;
}

static void
dx12__mergeStoredCache(DX12PipelineCache *native) {
  DX12PipelineCacheEntry *entry;
  DX12PipelineCache       stored = {0};
  void                   *data;
  size_t                  dataSize;

  data = dx12__readCache(native->path, &dataSize);
  if (!dx12__loadCache(&stored, data, dataSize, NULL, NULL)) {
    free(data);
    return;
  }
  free(data);

  AcquireSRWLockExclusive(&native->lock);
  entry          = stored.entries;
  stored.entries = NULL;
  while (entry) {
    DX12PipelineCacheEntry **link;
    DX12PipelineCacheEntry  *next;

    next = entry->next;
    link = dx12__findEntry(native, &entry->key);
    if (*link) {
      free(entry);
    } else {
      entry->next = NULL;
      *link       = entry;
      native->entryCount++;
    }
    entry = next;
  }
  ReleaseSRWLockExclusive(&native->lock);
}

static void
dx12__discardEntry(DX12PipelineCache     *native,
                   const DX12PipelineKey *key) {
  DX12PipelineCacheEntry **link;
  DX12PipelineCacheEntry  *entry;

  AcquireSRWLockExclusive(&native->lock);
  link  = dx12__findEntry(native, key);
  entry = *link;
  if (entry) {
    *link = entry->next;
    native->entryCount--;
    native->dirty = true;
    free(entry);
  }
  ReleaseSRWLockExclusive(&native->lock);
}

static void
dx12__storeBlob(DX12PipelineCache     *native,
                const DX12PipelineKey *key,
                ID3D12PipelineState   *state) {
  DX12PipelineCacheEntry **link;
  DX12PipelineCacheEntry  *entry;
  ID3DBlob                *blob;
  const void              *data;
  size_t                   dataSize;
  HRESULT                  result;

  blob   = NULL;
  result = state->lpVtbl->GetCachedBlob(state, &blob);
  if (FAILED(result) || !blob) {
    return;
  }
  data     = blob->lpVtbl->GetBufferPointer(blob);
  dataSize = blob->lpVtbl->GetBufferSize(blob);
  if (!data || dataSize == 0u || dataSize > SIZE_MAX - sizeof(*entry)) {
    blob->lpVtbl->Release(blob);
    return;
  }

  entry = malloc(sizeof(*entry) + dataSize);
  if (!entry) {
    blob->lpVtbl->Release(blob);
    return;
  }
  entry->next     = NULL;
  entry->key      = *key;
  entry->dataSize = dataSize;
  memcpy(entry->data, data, dataSize);
  blob->lpVtbl->Release(blob);

  AcquireSRWLockExclusive(&native->lock);
  link = dx12__findEntry(native, key);
  if (*link) {
    free(entry);
  } else {
    *link = entry;
    native->entryCount++;
    native->dirty = true;
  }
  ReleaseSRWLockExclusive(&native->lock);
}

static void
dx12__meshKey(const GPURenderPipelineCreateInfo *info,
              const DX12PipelineKey             *rootKey,
              const DX12ShaderCode              *taskCode,
              const DX12ShaderCode              *meshCode,
              const DX12ShaderCode              *fragmentCode,
              DX12PipelineKey                   *key) {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {0};

  desc.PS.pShaderBytecode = fragmentCode->data;
  desc.PS.BytecodeLength  = fragmentCode->size;
  dx12__graphicsKey(&desc,
                    info,
                    rootKey,
                    DX12_PIPELINE_KIND_MESH,
                    key);
  if (taskCode) {
    dx12_keyWrite(key, taskCode->data, taskCode->size);
  }
  dx12_keyWrite(key, meshCode->data, meshCode->size);
}

GPU_HIDE
GPUResult
dx12_createGraphicsPSO(GPUPipelineCache                          *cache,
                       GPUDeviceDX12                            *device,
                       const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
                       const GPURenderPipelineCreateInfo        *info,
                       const DX12PipelineKey                    *rootKey,
                       ID3D12PipelineState                     **outState) {
  DX12PipelineCacheEntry              *entry;
  DX12PipelineCache                   *native;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC   descCopy;
  DX12PipelineKey                      key;
  HRESULT                              result;

  *outState = NULL;
  native    = dx12__nativeCache(cache);
  if (native) {
    dx12__graphicsKey(desc,
                      info,
                      rootKey,
                      DX12_PIPELINE_KIND_RENDER,
                      &key);
#if DX12_HAS_PIPELINE_LIBRARY
    result = dx12__loadGraphicsLibrary(native, &key, desc, outState);
    if (SUCCEEDED(result) && *outState) {
      return GPU_OK;
    }
    if (*outState) {
      (*outState)->lpVtbl->Release(*outState);
      *outState = NULL;
    }
#endif
    AcquireSRWLockShared(&native->lock);
    entry = *dx12__findEntry(native, &key);
    if (entry) {
      descCopy           = *desc;
      descCopy.CachedPSO.pCachedBlob           = entry->data;
      descCopy.CachedPSO.CachedBlobSizeInBytes = entry->dataSize;
      result = device->d3dDevice->lpVtbl->CreateGraphicsPipelineState(
        device->d3dDevice,
        &descCopy,
        &IID_ID3D12PipelineState,
        (void **)outState
      );
      ReleaseSRWLockShared(&native->lock);
      if (SUCCEEDED(result) && *outState) {
#if DX12_HAS_PIPELINE_LIBRARY
        dx12__storeLibrary(native, &key, *outState);
#endif
        return GPU_OK;
      }
      if (*outState) {
        (*outState)->lpVtbl->Release(*outState);
        *outState = NULL;
      }
      dx12__discardEntry(native, &key);
#if GPU_BUILD_WITH_VALIDATION
      fprintf(stderr,
              "GPU Direct3D 12 cached render pipeline rejected: 0x%08lx\n",
              (unsigned long)result);
#endif
    } else {
      ReleaseSRWLockShared(&native->lock);
    }
  }

  result = device->d3dDevice->lpVtbl->CreateGraphicsPipelineState(
    device->d3dDevice,
    desc,
    &IID_ID3D12PipelineState,
    (void **)outState
  );
  if (FAILED(result) || !*outState) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (native) {
#if DX12_HAS_PIPELINE_LIBRARY
    dx12__storeLibrary(native, &key, *outState);
#endif
    dx12__storeBlob(native, &key, *outState);
  }
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_createComputePSO(GPUPipelineCache                         *cache,
                      GPUDeviceDX12                           *device,
                      const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
                      const DX12PipelineKey                   *rootKey,
                      ID3D12PipelineState                    **outState) {
  DX12PipelineCacheEntry             *entry;
  DX12PipelineCache                  *native;
  D3D12_COMPUTE_PIPELINE_STATE_DESC   descCopy;
  DX12PipelineKey                     key;
  HRESULT                             result;

  *outState = NULL;
  native    = dx12__nativeCache(cache);
  if (native) {
    dx12__computeKey(desc, rootKey, &key);
#if DX12_HAS_PIPELINE_LIBRARY
    result = dx12__loadComputeLibrary(native, &key, desc, outState);
    if (SUCCEEDED(result) && *outState) {
      return GPU_OK;
    }
    if (*outState) {
      (*outState)->lpVtbl->Release(*outState);
      *outState = NULL;
    }
#endif
    AcquireSRWLockShared(&native->lock);
    entry = *dx12__findEntry(native, &key);
    if (entry) {
      descCopy           = *desc;
      descCopy.CachedPSO.pCachedBlob           = entry->data;
      descCopy.CachedPSO.CachedBlobSizeInBytes = entry->dataSize;
      result = device->d3dDevice->lpVtbl->CreateComputePipelineState(
        device->d3dDevice,
        &descCopy,
        &IID_ID3D12PipelineState,
        (void **)outState
      );
      ReleaseSRWLockShared(&native->lock);
      if (SUCCEEDED(result) && *outState) {
#if DX12_HAS_PIPELINE_LIBRARY
        dx12__storeLibrary(native, &key, *outState);
#endif
        return GPU_OK;
      }
      if (*outState) {
        (*outState)->lpVtbl->Release(*outState);
        *outState = NULL;
      }
      dx12__discardEntry(native, &key);
#if GPU_BUILD_WITH_VALIDATION
      fprintf(stderr,
              "GPU Direct3D 12 cached compute pipeline rejected: 0x%08lx\n",
              (unsigned long)result);
#endif
    } else {
      ReleaseSRWLockShared(&native->lock);
    }
  }

  result = device->d3dDevice->lpVtbl->CreateComputePipelineState(
    device->d3dDevice,
    desc,
    &IID_ID3D12PipelineState,
    (void **)outState
  );
  if (FAILED(result) || !*outState) {
#if GPU_BUILD_WITH_VALIDATION
    fprintf(stderr,
            "GPU Direct3D 12 compute pipeline create failed: 0x%08lx\n",
            (unsigned long)result);
#endif
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (native) {
#if DX12_HAS_PIPELINE_LIBRARY
    dx12__storeLibrary(native, &key, *outState);
#endif
    dx12__storeBlob(native, &key, *outState);
  }
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_createMeshPSO(GPUPipelineCache                        *cache,
                   GPUDeviceDX12                          *device,
                   const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                   D3D12_CACHED_PIPELINE_STATE            *cachedPSO,
                   const GPURenderPipelineCreateInfo      *info,
                   const DX12PipelineKey                  *rootKey,
                   const DX12ShaderCode                   *taskCode,
                   const DX12ShaderCode                   *meshCode,
                   const DX12ShaderCode                   *fragmentCode,
                   ID3D12PipelineState                   **outState) {
  DX12PipelineCacheEntry *entry;
  DX12PipelineCache      *native;
  DX12PipelineKey         key;
  HRESULT                 result;

  if (!device || !device->d3dDevice2 || !desc || !cachedPSO || !info ||
      !rootKey || !meshCode || !fragmentCode || !outState) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outState = NULL;
  native    = dx12__nativeCache(cache);
  if (native) {
    dx12__meshKey(info,
                  rootKey,
                  taskCode,
                  meshCode,
                  fragmentCode,
                  &key);
#if DX12_HAS_PIPELINE_LIBRARY && \
    defined(__ID3D12PipelineLibrary1_INTERFACE_DEFINED__)
    result = dx12__loadMeshLibrary(native, &key, desc, outState);
    if (SUCCEEDED(result) && *outState) {
      return GPU_OK;
    }
    if (*outState) {
      (*outState)->lpVtbl->Release(*outState);
      *outState = NULL;
    }
#endif
    AcquireSRWLockShared(&native->lock);
    entry = *dx12__findEntry(native, &key);
    if (entry) {
      cachedPSO->pCachedBlob           = entry->data;
      cachedPSO->CachedBlobSizeInBytes = entry->dataSize;
      result = device->d3dDevice2->lpVtbl->CreatePipelineState(
        device->d3dDevice2,
        desc,
        &IID_ID3D12PipelineState,
        (void **)outState
      );
      cachedPSO->pCachedBlob           = NULL;
      cachedPSO->CachedBlobSizeInBytes = 0u;
      ReleaseSRWLockShared(&native->lock);
      if (SUCCEEDED(result) && *outState) {
#if DX12_HAS_PIPELINE_LIBRARY
        dx12__storeLibrary(native, &key, *outState);
#endif
        return GPU_OK;
      }
      if (*outState) {
        (*outState)->lpVtbl->Release(*outState);
        *outState = NULL;
      }
      dx12__discardEntry(native, &key);
#if GPU_BUILD_WITH_VALIDATION
      fprintf(stderr,
              "GPU Direct3D 12 cached mesh pipeline rejected: 0x%08lx\n",
              (unsigned long)result);
#endif
    } else {
      ReleaseSRWLockShared(&native->lock);
    }
  }

  result = device->d3dDevice2->lpVtbl->CreatePipelineState(
    device->d3dDevice2,
    desc,
    &IID_ID3D12PipelineState,
    (void **)outState
  );
  if (FAILED(result) || !*outState) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (native) {
#if DX12_HAS_PIPELINE_LIBRARY
    dx12__storeLibrary(native, &key, *outState);
#endif
    dx12__storeBlob(native, &key, *outState);
  }
  return GPU_OK;
}

GPU_HIDE
void
dx12_initPipelineCache(GPUApiPipelineCache *api) {
  api->create  = dx12_createCache;
  api->destroy = dx12_destroyCache;
}
