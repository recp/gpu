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
#include "../../../api/pipeline_cache_internal.h"
#include "pipeline_cache.h"

#define DX12_PIPELINE_KEY_VERSION 1u

typedef struct DX12PipelineCache {
  ID3D12PipelineLibrary  *library;
  ID3D12PipelineLibrary1 *library1;
  char                   *path;
  SRWLOCK                 lock;
  bool                    dirty;
} DX12PipelineCache;

static DX12PipelineCache *
dx12__nativeCache(GPUPipelineCache *cache) {
  return cache ? cache->_priv : NULL;
}

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

static void
dx12__keyName(const DX12PipelineKey *key,
              wchar_t                kind,
              wchar_t                name[39]) {
  static const wchar_t hex[] = L"0123456789abcdef";
  uint32_t             cursor;

  name[0] = L'g';
  name[1] = L'p';
  name[2] = L'u';
  name[3] = L'-';
  name[4] = kind;
  name[5] = L'-';
  cursor  = 6u;
  for (uint32_t valueIndex = 0u; valueIndex < 2u; valueIndex++) {
    for (uint32_t nibble = 0u; nibble < 16u; nibble++) {
      uint32_t shift;

      shift          = 60u - nibble * 4u;
      name[cursor++] = hex[(key->value[valueIndex] >> shift) & 0xfu];
    }
  }
  name[cursor] = L'\0';
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
  DX12PipelineCache  *native;
  GPUDeviceDX12      *deviceDX12;
  ID3D12Device1      *device1;
  void               *initialData;
  size_t              initialSize;
  HRESULT             result;

  deviceDX12 = device ? device->_priv : NULL;
  if (!deviceDX12 || !deviceDX12->d3dDevice || !info ||
      !info->cachePath || !cache) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  device1 = NULL;
  result  = deviceDX12->d3dDevice->lpVtbl->QueryInterface(
    deviceDX12->d3dDevice,
    &IID_ID3D12Device1,
    (void **)&device1
  );
  if (FAILED(result) || !device1) {
    return GPU_ERROR_UNSUPPORTED;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    device1->lpVtbl->Release(device1);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->path = dx12__copyPath(info->cachePath);
  if (!native->path) {
    device1->lpVtbl->Release(device1);
    free(native);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  InitializeSRWLock(&native->lock);

  initialData = dx12__readCache(native->path, &initialSize);
  result      = device1->lpVtbl->CreatePipelineLibrary(
    device1,
    initialData,
    initialSize,
    &IID_ID3D12PipelineLibrary,
    (void **)&native->library
  );
  if (FAILED(result) && initialData) {
    result = device1->lpVtbl->CreatePipelineLibrary(
      device1,
      NULL,
      0u,
      &IID_ID3D12PipelineLibrary,
      (void **)&native->library
    );
  }
  free(initialData);
  device1->lpVtbl->Release(device1);
  if (FAILED(result) || !native->library) {
    free(native->path);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  (void)native->library->lpVtbl->QueryInterface(
    native->library,
    &IID_ID3D12PipelineLibrary1,
    (void **)&native->library1
  );

  cache->_priv = native;
  return GPU_OK;
}

static void
dx12__storeCache(DX12PipelineCache *native) {
  void   *data;
  char   *temporaryPath;
  FILE   *file;
  SIZE_T  dataSize;
  size_t  pathLength;
  HRESULT result;
  bool    written;

  if (!native || !native->dirty) {
    return;
  }

  AcquireSRWLockExclusive(&native->lock);
  dataSize = native->library->lpVtbl->GetSerializedSize(native->library);
  data     = dataSize > 0u ? malloc(dataSize) : NULL;
  result   = data
               ? native->library->lpVtbl->Serialize(native->library,
                                                     data,
                                                     dataSize)
               : E_OUTOFMEMORY;
  ReleaseSRWLockExclusive(&native->lock);
#if GPU_BUILD_WITH_VALIDATION
  if (dataSize == 0u || FAILED(result)) {
    fprintf(stderr,
            "GPU Direct3D 12 pipeline cache serialize failed: "
            "size=%llu result=0x%08lx\n",
            (unsigned long long)dataSize,
            (unsigned long)result);
  }
#endif
  if (FAILED(result)) {
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
    if (!MoveFileExA(temporaryPath,
                     native->path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      remove(temporaryPath);
    }
  } else {
    remove(temporaryPath);
  }
  free(temporaryPath);
  free(data);
}

static void
dx12_destroyCache(GPUPipelineCache *cache) {
  DX12PipelineCache *native;

  native = dx12__nativeCache(cache);
  if (!native) {
    return;
  }
  dx12__storeCache(native);
  if (native->library1) {
    native->library1->lpVtbl->Release(native->library1);
  }
  native->library->lpVtbl->Release(native->library);
  free(native->path);
  free(native);
  cache->_priv = NULL;
}

#define DX12_KEY_WRITE(KEY, VALUE) \
  dx12_keyWrite((KEY), &(VALUE), sizeof(VALUE))

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
                  DX12PipelineKey                          *key) {
  dx12_keyInit(key);
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
  dx12_keyInit(key);
  dx12_keyWrite(key, rootKey, sizeof(*rootKey));
  dx12_keyWrite(key, desc->CS.pShaderBytecode, desc->CS.BytecodeLength);
  DX12_KEY_WRITE(key, desc->NodeMask);
  DX12_KEY_WRITE(key, desc->Flags);
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
  dx12__graphicsKey(&desc, info, rootKey, key);
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
  DX12PipelineCache *native;
  DX12PipelineKey    key;
  wchar_t            name[39];
  HRESULT            result;

  *outState = NULL;
  native    = dx12__nativeCache(cache);
  if (!native) {
    result = device->d3dDevice->lpVtbl->CreateGraphicsPipelineState(
      device->d3dDevice,
      desc,
      &IID_ID3D12PipelineState,
      (void **)outState
    );
    return SUCCEEDED(result) ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
  }

  dx12__graphicsKey(desc, info, rootKey, &key);
  dx12__keyName(&key, L'r', name);
  AcquireSRWLockExclusive(&native->lock);
  result = native->library->lpVtbl->LoadGraphicsPipeline(
    native->library,
    name,
    desc,
    &IID_ID3D12PipelineState,
    (void **)outState
  );
  ReleaseSRWLockExclusive(&native->lock);
  if (SUCCEEDED(result) && *outState) {
    return GPU_OK;
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
  AcquireSRWLockExclusive(&native->lock);
  result = native->library->lpVtbl->StorePipeline(native->library,
                                                   name,
                                                   *outState);
  native->dirty |= SUCCEEDED(result);
#if GPU_BUILD_WITH_VALIDATION
  if (FAILED(result)) {
    fprintf(stderr,
            "GPU Direct3D 12 render pipeline cache store failed: 0x%08lx\n",
            (unsigned long)result);
  }
#endif
  ReleaseSRWLockExclusive(&native->lock);
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_createComputePSO(GPUPipelineCache                         *cache,
                      GPUDeviceDX12                           *device,
                      const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
                      const DX12PipelineKey                   *rootKey,
                      ID3D12PipelineState                    **outState) {
  DX12PipelineCache *native;
  DX12PipelineKey    key;
  wchar_t            name[39];
  HRESULT            result;

  *outState = NULL;
  native    = dx12__nativeCache(cache);
  if (!native) {
    result = device->d3dDevice->lpVtbl->CreateComputePipelineState(
      device->d3dDevice,
      desc,
      &IID_ID3D12PipelineState,
      (void **)outState
    );
    return SUCCEEDED(result) ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
  }

  dx12__computeKey(desc, rootKey, &key);
  dx12__keyName(&key, L'c', name);
  AcquireSRWLockExclusive(&native->lock);
  result = native->library->lpVtbl->LoadComputePipeline(
    native->library,
    name,
    desc,
    &IID_ID3D12PipelineState,
    (void **)outState
  );
  ReleaseSRWLockExclusive(&native->lock);
  if (SUCCEEDED(result) && *outState) {
    return GPU_OK;
  }

  result = device->d3dDevice->lpVtbl->CreateComputePipelineState(
    device->d3dDevice,
    desc,
    &IID_ID3D12PipelineState,
    (void **)outState
  );
  if (FAILED(result) || !*outState) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  AcquireSRWLockExclusive(&native->lock);
  result = native->library->lpVtbl->StorePipeline(native->library,
                                                   name,
                                                   *outState);
  native->dirty |= SUCCEEDED(result);
#if GPU_BUILD_WITH_VALIDATION
  if (FAILED(result)) {
    fprintf(stderr,
            "GPU Direct3D 12 compute pipeline cache store failed: 0x%08lx\n",
            (unsigned long)result);
  }
#endif
  ReleaseSRWLockExclusive(&native->lock);
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_createMeshPSO(GPUPipelineCache                        *cache,
                   GPUDeviceDX12                          *device,
                   const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                   const GPURenderPipelineCreateInfo      *info,
                   const DX12PipelineKey                  *rootKey,
                   const DX12ShaderCode                   *taskCode,
                   const DX12ShaderCode                   *meshCode,
                   const DX12ShaderCode                   *fragmentCode,
                   ID3D12PipelineState                   **outState) {
  DX12PipelineCache *native;
  DX12PipelineKey    key;
  wchar_t            name[39];
  HRESULT            result;

  if (!device || !device->d3dDevice2 || !desc || !info || !rootKey ||
      !meshCode || !fragmentCode || !outState) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outState = NULL;
  native    = dx12__nativeCache(cache);
  if (native && native->library1) {
    dx12__meshKey(info,
                  rootKey,
                  taskCode,
                  meshCode,
                  fragmentCode,
                  &key);
    dx12__keyName(&key, L'm', name);
    AcquireSRWLockExclusive(&native->lock);
    result = native->library1->lpVtbl->LoadPipeline(
      native->library1,
      name,
      desc,
      &IID_ID3D12PipelineState,
      (void **)outState
    );
    ReleaseSRWLockExclusive(&native->lock);
    if (SUCCEEDED(result) && *outState) {
      return GPU_OK;
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
  if (!native || !native->library1) {
    return GPU_OK;
  }

  AcquireSRWLockExclusive(&native->lock);
  result = native->library->lpVtbl->StorePipeline(native->library,
                                                   name,
                                                   *outState);
  native->dirty |= SUCCEEDED(result);
#if GPU_BUILD_WITH_VALIDATION
  if (FAILED(result)) {
    fprintf(stderr,
            "GPU Direct3D 12 mesh pipeline cache store failed: 0x%08lx\n",
            (unsigned long)result);
  }
#endif
  ReleaseSRWLockExclusive(&native->lock);
  return GPU_OK;
}

GPU_HIDE
void
dx12_initPipelineCache(GPUApiPipelineCache *api) {
  api->create  = dx12_createCache;
  api->destroy = dx12_destroyCache;
}
