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
#include "../../../api/multigpu_internal.h"

enum {
  DX12_SHARED_BARRIER_CHUNK_SIZE = 16u
};

static GPUResult
dx12_interopDevices(GPUDeviceInteropEXT *interop,
                    GPUDeviceDX12      **outFirst,
                    GPUDeviceDX12      **outSecond) {
  GPUDeviceDX12 *first, *second;
  bool           sameDevice;
  GPUResult      result;

  if (!interop || !interop->firstDevice || !interop->secondDevice ||
      !outFirst || !outSecond) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (gpuDeviceApi(interop->firstDevice) !=
      gpuDeviceApi(interop->secondDevice)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  first  = interop->firstDevice->_priv;
  second = interop->secondDevice->_priv;
  if (!first || !second || !first->d3dDevice || !second->d3dDevice) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = GPUAdaptersSharePhysicalDevice(interop->firstDevice->adapter,
                                           interop->secondDevice->adapter,
                                           &sameDevice);
  if (result != GPU_OK || !sameDevice) {
    return result == GPU_OK ? GPU_ERROR_UNSUPPORTED : result;
  }

  *outFirst  = first;
  *outSecond = second;
  return GPU_OK;
}

static GPUResult
dx12_nativeResult(HRESULT result) {
  if (result == E_OUTOFMEMORY) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  if (result == DXGI_ERROR_UNSUPPORTED) {
    return GPU_ERROR_UNSUPPORTED;
  }
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
dx12_openSharedHandle(GPUDeviceDX12     *first,
                      GPUDeviceDX12     *second,
                      ID3D12DeviceChild *object,
                      REFIID             interfaceId,
                      void             **outObject) {
  HANDLE  handle;
  HRESULT result;

  if (!first || !second || !object || !interfaceId || !outObject) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outObject = NULL;
  handle     = NULL;

  result = first->d3dDevice->lpVtbl->CreateSharedHandle(first->d3dDevice,
                                                         object,
                                                         NULL,
                                                         GENERIC_ALL,
                                                         NULL,
                                                         &handle);
  if (FAILED(result) || !handle) {
    return dx12_nativeResult(result);
  }

  result = second->d3dDevice->lpVtbl->OpenSharedHandle(second->d3dDevice,
                                                       handle,
                                                       interfaceId,
                                                       outObject);
  CloseHandle(handle);
  if (FAILED(result) || !*outObject) {
    *outObject = NULL;
    return dx12_nativeResult(result);
  }
  return GPU_OK;
}

static bool
dx12_resourceDescEqual(const D3D12_RESOURCE_DESC *first,
                       const D3D12_RESOURCE_DESC *second) {
  return first && second &&
         first->Dimension == second->Dimension &&
         first->Alignment == second->Alignment &&
         first->Width == second->Width &&
         first->Height == second->Height &&
         first->DepthOrArraySize == second->DepthOrArraySize &&
         first->MipLevels == second->MipLevels &&
         first->Format == second->Format &&
         first->SampleDesc.Count == second->SampleDesc.Count &&
         first->SampleDesc.Quality == second->SampleDesc.Quality &&
         first->Layout == second->Layout &&
         first->Flags == second->Flags;
}

static GPUResult
dx12_sharedRequirements(GPUDeviceInteropEXT       *interop,
                        const D3D12_RESOURCE_DESC *desc,
                        GPUMemoryRequirements     *outRequirements) {
  D3D12_RESOURCE_ALLOCATION_INFO firstInfo, secondInfo;
  GPUDeviceDX12                 *first, *second;
  uint64_t                       compatibility;
  GPUResult                      result;

  result = dx12_interopDevices(interop, &first, &second);
  if (result != GPU_OK || !desc || !outRequirements) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }

  first->d3dDevice->lpVtbl->GetResourceAllocationInfo(first->d3dDevice,
                                                       &firstInfo,
                                                       0u,
                                                       1u,
                                                       desc);
  second->d3dDevice->lpVtbl->GetResourceAllocationInfo(second->d3dDevice,
                                                        &secondInfo,
                                                        0u,
                                                        1u,
                                                        desc);
  if (firstInfo.SizeInBytes == UINT64_MAX ||
      secondInfo.SizeInBytes == UINT64_MAX ||
      firstInfo.SizeInBytes != secondInfo.SizeInBytes ||
      firstInfo.Alignment == 0u ||
      firstInfo.Alignment != secondInfo.Alignment) {
    return GPU_ERROR_UNSUPPORTED;
  }

  compatibility = dx12_memoryCompatibility(interop->firstDevice, desc) &
                  dx12_memoryCompatibility(interop->secondDevice, desc);
  if (compatibility == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  outRequirements->sizeBytes         = firstInfo.SizeInBytes;
  outRequirements->alignmentBytes    = firstInfo.Alignment;
  outRequirements->compatibilityMask = compatibility;
  return GPU_OK;
}

static GPUResult
dx12_createDeviceInterop(GPUDevice           *firstDevice,
                         GPUDevice           *secondDevice,
                         GPUDeviceInteropEXT *interop) {
  GPUDeviceDX12 *first, *second;

  if (!firstDevice || !secondDevice || !interop ||
      interop->firstDevice != firstDevice ||
      interop->secondDevice != secondDevice) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  return dx12_interopDevices(interop, &first, &second);
}

static void
dx12_destroyDeviceInterop(GPUDeviceInteropEXT *interop) {
  GPU__UNUSED(interop);
}

static GPUResult
dx12_getSharedBufferRequirements(
  GPUDeviceInteropEXT       *interop,
  const GPUBufferCreateInfo *firstInfo,
  const GPUBufferCreateInfo *secondInfo,
  GPUMemoryRequirements     *outRequirements
) {
  GPUBufferCreateInfo sharedInfo;
  D3D12_RESOURCE_DESC desc = {0};
  GPUResult           result;

  if (!firstInfo || !secondInfo || !outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  sharedInfo       = *firstInfo;
  sharedInfo.usage = firstInfo->usage | secondInfo->usage;
  result = dx12_bufferDesc(&sharedInfo, &desc);
  return result == GPU_OK
           ? dx12_sharedRequirements(interop, &desc, outRequirements)
           : result;
}

static GPUResult
dx12_getExternalBufferRequirements(GPUDevice                 *device,
                                    const GPUBufferCreateInfo *info,
                                    GPUMemoryRequirements     *outRequirements) {
  return dx12_getBufferMemoryRequirements(device, info, outRequirements);
}

static GPUResult
dx12_createExternalBuffer(GPUDevice                  *device,
                           const GPUBufferCreateInfo  *info,
                           GPUBuffer                 **outBuffer,
                           GPUExternalMemoryExport    *outExport) {
  GPUDeviceDX12                 *native;
  ID3D12Resource                *resource;
  D3D12_HEAP_PROPERTIES          heap = {0};
  D3D12_RESOURCE_DESC            desc = {0};
  D3D12_RESOURCE_ALLOCATION_INFO allocationInfo;
  HANDLE                         handle;
  GPUResult                      result;
  HRESULT                        nativeResult;

  native = device ? device->_priv : NULL;
  if (!native || !native->d3dDevice || !info || !outBuffer || !outExport) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outBuffer = NULL;
  memset(outExport, 0, sizeof(*outExport));
  result = dx12_bufferDesc(info, &desc);
  if (result != GPU_OK) {
    return result;
  }

  native->d3dDevice->lpVtbl->GetResourceAllocationInfo(native->d3dDevice,
                                                        &allocationInfo,
                                                        0u,
                                                        1u,
                                                        &desc);
  if (allocationInfo.SizeInBytes == UINT64_MAX ||
      allocationInfo.Alignment == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  resource              = NULL;
  handle                = NULL;
  heap.Type             = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1u;
  heap.VisibleNodeMask  = 1u;
  nativeResult = native->d3dDevice->lpVtbl->CreateCommittedResource(
    native->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_SHARED,
    &desc,
    D3D12_RESOURCE_STATE_COMMON,
    NULL,
    &IID_ID3D12Resource,
    (void **)&resource
  );
  if (FAILED(nativeResult) || !resource) {
    return dx12_nativeResult(nativeResult);
  }
  nativeResult = native->d3dDevice->lpVtbl->CreateSharedHandle(
    native->d3dDevice,
    (ID3D12DeviceChild *)resource,
    NULL,
    GENERIC_ALL,
    NULL,
    &handle
  );
  if (FAILED(nativeResult) || !handle) {
    resource->lpVtbl->Release(resource);
    return dx12_nativeResult(nativeResult);
  }

  result = dx12_wrapBuffer(device,
                           info,
                           resource,
                           D3D12_RESOURCE_STATE_COMMON,
                           outBuffer);
  if (result != GPU_OK) {
    CloseHandle(handle);
    resource->lpVtbl->Release(resource);
    return result;
  }

  outExport->handle.win32 = handle;
  outExport->sizeBytes    = allocationInfo.SizeInBytes;
  outExport->type         = GPU_EXTERNAL_MEMORY_D3D12_RESOURCE;
  outExport->dedicated    = true;
  return GPU_OK;
}

static GPUResult
dx12_getExternalTextureRequirements(
  GPUDevice                  *device,
  const GPUTextureCreateInfo *info,
  GPUMemoryRequirements      *outRequirements
) {
  return dx12_getTextureMemoryRequirements(device, info, outRequirements);
}

static GPUResult
dx12_createExternalTexture(GPUDevice                   *device,
                            const GPUTextureCreateInfo  *info,
                            GPUTexture                 **outTexture,
                            GPUExternalMemoryExport     *outExport) {
  GPUDeviceDX12                 *native;
  ID3D12Resource                *resource;
  D3D12_HEAP_PROPERTIES          heap = {0};
  D3D12_RESOURCE_DESC            desc = {0};
  D3D12_CLEAR_VALUE              clearValue = {0};
  D3D12_RESOURCE_ALLOCATION_INFO allocationInfo;
  D3D12_RESOURCE_STATES          initialState;
  HANDLE                         handle;
  GPUResult                      result;
  HRESULT                        nativeResult;
  uint32_t                       mipLevelCount, arrayLayerCount;
  uint32_t                       planeCount, subresourceCount;

  native = device ? device->_priv : NULL;
  if (!native || !native->d3dDevice || !info || !outTexture || !outExport) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outTexture = NULL;
  memset(outExport, 0, sizeof(*outExport));
  result = dx12_textureDesc(device,
                            info,
                            &desc,
                            &clearValue,
                            &initialState,
                            &mipLevelCount,
                            &arrayLayerCount,
                            &planeCount,
                            &subresourceCount);
  if (result != GPU_OK) {
    return result;
  }
  GPU__UNUSED(initialState);

  native->d3dDevice->lpVtbl->GetResourceAllocationInfo(native->d3dDevice,
                                                        &allocationInfo,
                                                        0u,
                                                        1u,
                                                        &desc);
  if (allocationInfo.SizeInBytes == UINT64_MAX ||
      allocationInfo.Alignment == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  resource              = NULL;
  handle                = NULL;
  heap.Type             = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1u;
  heap.VisibleNodeMask  = 1u;
  nativeResult = native->d3dDevice->lpVtbl->CreateCommittedResource(
    native->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_SHARED,
    &desc,
    D3D12_RESOURCE_STATE_COMMON,
    (info->usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u
      ? &clearValue
      : NULL,
    &IID_ID3D12Resource,
    (void **)&resource
  );
  if (FAILED(nativeResult) || !resource) {
    return dx12_nativeResult(nativeResult);
  }
  nativeResult = native->d3dDevice->lpVtbl->CreateSharedHandle(
    native->d3dDevice,
    (ID3D12DeviceChild *)resource,
    NULL,
    GENERIC_ALL,
    NULL,
    &handle
  );
  if (FAILED(nativeResult) || !handle) {
    resource->lpVtbl->Release(resource);
    return dx12_nativeResult(nativeResult);
  }

  result = dx12_wrapTexture(device,
                            info,
                            resource,
                            D3D12_RESOURCE_STATE_COMMON,
                            mipLevelCount,
                            arrayLayerCount,
                            planeCount,
                            subresourceCount,
                            outTexture);
  if (result != GPU_OK) {
    CloseHandle(handle);
    resource->lpVtbl->Release(resource);
    return result;
  }

  outExport->handle.win32 = handle;
  outExport->sizeBytes    = allocationInfo.SizeInBytes;
  outExport->type         = GPU_EXTERNAL_MEMORY_D3D12_RESOURCE;
  outExport->dedicated    = true;
  return GPU_OK;
}

static GPUResult
dx12_createSharedBuffer(GPUDeviceInteropEXT       *interop,
                        const GPUBufferCreateInfo *firstInfo,
                        const GPUBufferCreateInfo *secondInfo,
                        GPUBuffer                **outFirstBuffer,
                        GPUBuffer                **outSecondBuffer) {
  GPUBufferCreateInfo   sharedInfo;
  GPUDeviceDX12        *first, *second;
  ID3D12Resource       *firstResource, *secondResource;
  D3D12_HEAP_PROPERTIES heap = {0};
  D3D12_RESOURCE_DESC   desc = {0};
  GPUResult             result;
  HRESULT               nativeResult;

  result = dx12_interopDevices(interop, &first, &second);
  if (result != GPU_OK || !firstInfo || !secondInfo ||
      !outFirstBuffer || !outSecondBuffer) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }
  *outFirstBuffer  = NULL;
  *outSecondBuffer = NULL;
  sharedInfo       = *firstInfo;
  sharedInfo.usage = firstInfo->usage | secondInfo->usage;
  result = dx12_bufferDesc(&sharedInfo, &desc);
  if (result != GPU_OK) {
    return result;
  }

  firstResource        = NULL;
  secondResource       = NULL;
  heap.Type             = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1u;
  heap.VisibleNodeMask  = 1u;
  nativeResult = first->d3dDevice->lpVtbl->CreateCommittedResource(
    first->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_SHARED,
    &desc,
    D3D12_RESOURCE_STATE_COMMON,
    NULL,
    &IID_ID3D12Resource,
    (void **)&firstResource
  );
  if (FAILED(nativeResult) || !firstResource) {
    return dx12_nativeResult(nativeResult);
  }

  result = dx12_openSharedHandle(first,
                                 second,
                                 (ID3D12DeviceChild *)firstResource,
                                 &IID_ID3D12Resource,
                                 (void **)&secondResource);
  if (result != GPU_OK) {
    firstResource->lpVtbl->Release(firstResource);
    return result;
  }

  result = dx12_wrapBuffer(interop->firstDevice,
                           firstInfo,
                           firstResource,
                           D3D12_RESOURCE_STATE_COMMON,
                           outFirstBuffer);
  if (result != GPU_OK) {
    secondResource->lpVtbl->Release(secondResource);
    firstResource->lpVtbl->Release(firstResource);
    return result;
  }
  result = dx12_wrapBuffer(interop->secondDevice,
                           secondInfo,
                           secondResource,
                           D3D12_RESOURCE_STATE_COMMON,
                           outSecondBuffer);
  if (result != GPU_OK) {
    secondResource->lpVtbl->Release(secondResource);
    GPUDestroyBuffer(*outFirstBuffer);
    *outFirstBuffer = NULL;
  }
  return result;
}

static GPUResult
dx12_sharedTextureDesc(GPUDeviceInteropEXT        *interop,
                       const GPUTextureCreateInfo *firstInfo,
                       const GPUTextureCreateInfo *secondInfo,
                       GPUTextureCreateInfo       *outSharedInfo,
                       D3D12_RESOURCE_DESC        *outDesc,
                       D3D12_CLEAR_VALUE          *outClearValue,
                       uint32_t                   *outMipLevelCount,
                       uint32_t                   *outArrayLayerCount,
                       uint32_t                   *outPlaneCount,
                       uint32_t                   *outSubresourceCount) {
  GPUDeviceDX12         *first, *second;
  D3D12_RESOURCE_DESC   secondDesc = {0};
  D3D12_CLEAR_VALUE     secondClear = {0};
  D3D12_RESOURCE_STATES initialState, secondInitialState;
  uint32_t              secondMipCount, secondLayerCount;
  uint32_t              secondPlaneCount, secondSubresourceCount;
  GPUResult             result;

  result = dx12_interopDevices(interop, &first, &second);
  if (result != GPU_OK || !firstInfo || !secondInfo || !outSharedInfo || !outDesc ||
      !outClearValue || !outMipLevelCount || !outArrayLayerCount ||
      !outPlaneCount || !outSubresourceCount) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }

  *outSharedInfo       = *firstInfo;
  outSharedInfo->usage = firstInfo->usage | secondInfo->usage;
  result = dx12_textureDesc(interop->firstDevice,
                            outSharedInfo,
                            outDesc,
                            outClearValue,
                            &initialState,
                            outMipLevelCount,
                            outArrayLayerCount,
                            outPlaneCount,
                            outSubresourceCount);
  if (result != GPU_OK) {
    return result;
  }
  result = dx12_textureDesc(interop->secondDevice,
                            outSharedInfo,
                            &secondDesc,
                            &secondClear,
                            &secondInitialState,
                            &secondMipCount,
                            &secondLayerCount,
                            &secondPlaneCount,
                            &secondSubresourceCount);
  if (result != GPU_OK || !dx12_resourceDescEqual(outDesc, &secondDesc) ||
      *outMipLevelCount != secondMipCount ||
      *outArrayLayerCount != secondLayerCount ||
      *outPlaneCount != secondPlaneCount ||
      *outSubresourceCount != secondSubresourceCount) {
    return result != GPU_OK ? result : GPU_ERROR_UNSUPPORTED;
  }
  GPU__UNUSED(initialState);
  GPU__UNUSED(secondClear);
  GPU__UNUSED(secondInitialState);
  GPU__UNUSED(first);
  GPU__UNUSED(second);
  return GPU_OK;
}

static GPUResult
dx12_getSharedTextureRequirements(
  GPUDeviceInteropEXT        *interop,
  const GPUTextureCreateInfo *firstInfo,
  const GPUTextureCreateInfo *secondInfo,
  GPUMemoryRequirements      *outRequirements
) {
  GPUTextureCreateInfo sharedInfo;
  D3D12_RESOURCE_DESC  desc = {0};
  D3D12_CLEAR_VALUE    clearValue = {0};
  uint32_t             mipLevelCount, arrayLayerCount;
  uint32_t             planeCount, subresourceCount;
  GPUResult            result;

  result = dx12_sharedTextureDesc(interop,
                                  firstInfo,
                                  secondInfo,
                                  &sharedInfo,
                                  &desc,
                                  &clearValue,
                                  &mipLevelCount,
                                  &arrayLayerCount,
                                  &planeCount,
                                  &subresourceCount);
  if (result != GPU_OK) {
    return result;
  }
  GPU__UNUSED(sharedInfo);
  GPU__UNUSED(clearValue);
  GPU__UNUSED(mipLevelCount);
  GPU__UNUSED(arrayLayerCount);
  GPU__UNUSED(planeCount);
  GPU__UNUSED(subresourceCount);
  return dx12_sharedRequirements(interop, &desc, outRequirements);
}

static GPUResult
dx12_createSharedTexture(GPUDeviceInteropEXT        *interop,
                         const GPUTextureCreateInfo *firstInfo,
                         const GPUTextureCreateInfo *secondInfo,
                         GPUTexture                **outFirstTexture,
                         GPUTexture                **outSecondTexture) {
  GPUTextureCreateInfo  sharedInfo;
  GPUDeviceDX12        *first, *second;
  ID3D12Resource       *firstResource, *secondResource;
  D3D12_HEAP_PROPERTIES heap = {0};
  D3D12_RESOURCE_DESC   desc = {0};
  D3D12_CLEAR_VALUE     clearValue = {0};
  uint32_t              mipLevelCount, arrayLayerCount;
  uint32_t              planeCount, subresourceCount;
  GPUResult             result;
  HRESULT               nativeResult;

  result = dx12_interopDevices(interop, &first, &second);
  if (result != GPU_OK || !outFirstTexture || !outSecondTexture) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }
  *outFirstTexture  = NULL;
  *outSecondTexture = NULL;
  result = dx12_sharedTextureDesc(interop,
                                  firstInfo,
                                  secondInfo,
                                  &sharedInfo,
                                  &desc,
                                  &clearValue,
                                  &mipLevelCount,
                                  &arrayLayerCount,
                                  &planeCount,
                                  &subresourceCount);
  if (result != GPU_OK) {
    return result;
  }

  firstResource        = NULL;
  secondResource       = NULL;
  heap.Type             = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1u;
  heap.VisibleNodeMask  = 1u;
  nativeResult = first->d3dDevice->lpVtbl->CreateCommittedResource(
    first->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_SHARED,
    &desc,
    D3D12_RESOURCE_STATE_COMMON,
    (sharedInfo.usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u
      ? &clearValue
      : NULL,
    &IID_ID3D12Resource,
    (void **)&firstResource
  );
  if (FAILED(nativeResult) || !firstResource) {
    return dx12_nativeResult(nativeResult);
  }

  result = dx12_openSharedHandle(first,
                                 second,
                                 (ID3D12DeviceChild *)firstResource,
                                 &IID_ID3D12Resource,
                                 (void **)&secondResource);
  if (result != GPU_OK) {
    firstResource->lpVtbl->Release(firstResource);
    return result;
  }

  result = dx12_wrapTexture(interop->firstDevice,
                            firstInfo,
                            firstResource,
                            D3D12_RESOURCE_STATE_COMMON,
                            mipLevelCount,
                            arrayLayerCount,
                            planeCount,
                            subresourceCount,
                            outFirstTexture);
  if (result != GPU_OK) {
    secondResource->lpVtbl->Release(secondResource);
    firstResource->lpVtbl->Release(firstResource);
    return result;
  }
  result = dx12_wrapTexture(interop->secondDevice,
                            secondInfo,
                            secondResource,
                            D3D12_RESOURCE_STATE_COMMON,
                            mipLevelCount,
                            arrayLayerCount,
                            planeCount,
                            subresourceCount,
                            outSecondTexture);
  if (result != GPU_OK) {
    secondResource->lpVtbl->Release(secondResource);
    GPUDestroyTexture(*outFirstTexture);
    *outFirstTexture = NULL;
  }
  return result;
}

#if GPU_BUILD_WITH_DEBUG_MARKERS
static void
dx12_setSharedFenceName(ID3D12Fence *fence, const char *label) {
  wchar_t name[256];

  if (!fence || !label || label[0] == '\0' ||
      MultiByteToWideChar(CP_UTF8,
                          MB_ERR_INVALID_CHARS,
                          label,
                          -1,
                          name,
                          (int)GPU_ARRAY_LEN(name)) <= 0) {
    return;
  }
  (void)fence->lpVtbl->SetName(fence, name);
}
#endif

static GPUResult
dx12_createSharedSemaphore(GPUDeviceInteropEXT          *interop,
                           const GPUSemaphoreCreateInfo *info,
                           GPUSemaphore                 *firstSemaphore,
                           GPUSemaphore                 *secondSemaphore) {
  GPUDeviceDX12 *first, *second;
  ID3D12Fence   *firstFence, *secondFence;
  uint64_t       initialValue;
  GPUResult      result;
  HRESULT        nativeResult;

  result = dx12_interopDevices(interop, &first, &second);
  if (result != GPU_OK || !firstSemaphore || !secondSemaphore) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }

  firstFence   = NULL;
  secondFence  = NULL;
  initialValue = info ? info->initialValue : 0u;
  nativeResult = first->d3dDevice->lpVtbl->CreateFence(
    first->d3dDevice,
    initialValue,
    D3D12_FENCE_FLAG_SHARED,
    &IID_ID3D12Fence,
    (void **)&firstFence
  );
  if (FAILED(nativeResult) || !firstFence) {
    return dx12_nativeResult(nativeResult);
  }

  result = dx12_openSharedHandle(first,
                                 second,
                                 (ID3D12DeviceChild *)firstFence,
                                 &IID_ID3D12Fence,
                                 (void **)&secondFence);
  if (result != GPU_OK) {
    firstFence->lpVtbl->Release(firstFence);
    return result;
  }

#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (info) {
    dx12_setSharedFenceName(firstFence,
                            gpuDeviceDebugLabel(interop->firstDevice,
                                                info->label));
    dx12_setSharedFenceName(secondFence,
                            gpuDeviceDebugLabel(interop->secondDevice,
                                                info->label));
  }
#endif
  firstSemaphore->_priv  = firstFence;
  secondSemaphore->_priv = secondFence;
  return GPU_OK;
}

static GPUResult
dx12_createExternalSemaphore(GPUDevice                     *device,
                              const GPUSemaphoreCreateInfo  *info,
                              GPUSemaphore                  *semaphore,
                              GPUExternalSemaphoreExport    *outExport) {
  GPUDeviceDX12 *native;
  ID3D12Fence   *fence;
  HANDLE         handle;
  HRESULT        result;

  native = device ? device->_priv : NULL;
  if (!native || !native->d3dDevice || !semaphore || !outExport) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outExport, 0, sizeof(*outExport));
  fence  = NULL;
  handle = NULL;
  result = native->d3dDevice->lpVtbl->CreateFence(
    native->d3dDevice,
    info ? info->initialValue : 0u,
    D3D12_FENCE_FLAG_SHARED,
    &IID_ID3D12Fence,
    (void **)&fence
  );
  if (FAILED(result) || !fence) {
    return dx12_nativeResult(result);
  }
  result = native->d3dDevice->lpVtbl->CreateSharedHandle(
    native->d3dDevice,
    (ID3D12DeviceChild *)fence,
    NULL,
    GENERIC_ALL,
    NULL,
    &handle
  );
  if (FAILED(result) || !handle) {
    fence->lpVtbl->Release(fence);
    return dx12_nativeResult(result);
  }

#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (info) {
    dx12_setSharedFenceName(fence,
                            gpuDeviceDebugLabel(device, info->label));
  }
#endif
  semaphore->_priv         = fence;
  outExport->handle.win32  = handle;
  outExport->type          = GPU_EXTERNAL_SEMAPHORE_D3D12_FENCE;
  return GPU_OK;
}

static GPUResult
dx12_encodeExternalBarriers(GPUCommandBuffer               *cmdb,
                            const GPUSharedBarrierBatchEXT *barriers,
                            bool                            acquire) {
  GPUApi   *api;
  uint32_t  bufferOffset, textureOffset;

  api = cmdb && cmdb->_queue ? gpuCommandQueueApi(cmdb->_queue) : NULL;
  if (!api || api->backend != GPU_BACKEND_DX12 || !barriers) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  bufferOffset  = 0u;
  textureOffset = 0u;
  while (bufferOffset < barriers->bufferBarrierCount ||
         textureOffset < barriers->textureBarrierCount) {
    GPUBufferBarrier  bufferBarriers[DX12_SHARED_BARRIER_CHUNK_SIZE];
    GPUTextureBarrier textureBarriers[DX12_SHARED_BARRIER_CHUNK_SIZE];
    GPUBarrierBatch   batch = {0};
    uint32_t          bufferCount, textureCount;

    bufferCount = barriers->bufferBarrierCount - bufferOffset;
    if (bufferCount > DX12_SHARED_BARRIER_CHUNK_SIZE) {
      bufferCount = DX12_SHARED_BARRIER_CHUNK_SIZE;
    }
    textureCount = barriers->textureBarrierCount - textureOffset;
    if (textureCount > DX12_SHARED_BARRIER_CHUNK_SIZE) {
      textureCount = DX12_SHARED_BARRIER_CHUNK_SIZE;
    }

    for (uint32_t i = 0u; i < bufferCount; i++) {
      const GPUSharedBufferBarrierEXT *shared;
      GPUBufferBarrier                *barrier;

      shared              = &barriers->pBufferBarriers[bufferOffset + i];
      barrier             = &bufferBarriers[i];
      barrier->buffer     = acquire
                              ? shared->destinationBuffer
                              : shared->sourceBuffer;
      barrier->srcAccess  = acquire ? GPU_ACCESS_NONE : shared->srcAccess;
      barrier->dstAccess  = acquire ? shared->dstAccess : GPU_ACCESS_NONE;
      barrier->offset     = shared->offset;
      barrier->sizeBytes  = shared->sizeBytes;
    }
    for (uint32_t i = 0u; i < textureCount; i++) {
      const GPUSharedTextureBarrierEXT *shared;
      GPUTextureBarrier                *barrier;

      shared              = &barriers->pTextureBarriers[textureOffset + i];
      barrier             = &textureBarriers[i];
      barrier->texture    = acquire
                              ? shared->destinationTexture
                              : shared->sourceTexture;
      barrier->srcAccess  = acquire ? GPU_ACCESS_NONE : shared->srcAccess;
      barrier->dstAccess  = acquire ? shared->dstAccess : GPU_ACCESS_NONE;
      barrier->baseMip    = shared->baseMip;
      barrier->mipCount   = shared->mipCount;
      barrier->baseLayer  = shared->baseLayer;
      barrier->layerCount = shared->layerCount;
    }

    batch.pBufferBarriers     = bufferCount > 0u ? bufferBarriers : NULL;
    batch.pTextureBarriers    = textureCount > 0u ? textureBarriers : NULL;
    batch.srcStages           = acquire ? GPU_STAGE_TOP : barriers->srcStages;
    batch.dstStages           = acquire ? barriers->dstStages : GPU_STAGE_BOTTOM;
    batch.bufferBarrierCount  = bufferCount;
    batch.textureBarrierCount = textureCount;
    dx12_encodeBarriers(cmdb, &batch);

    bufferOffset  += bufferCount;
    textureOffset += textureCount;
  }
  return GPU_OK;
}

static GPUResult
dx12_encodeSharedBarriers(GPUDeviceInteropEXT           *interop,
                          GPUCommandBuffer               *cmdb,
                          const GPUSharedBarrierBatchEXT *barriers,
                          bool                            acquire) {
  GPUDeviceDX12 *first, *second;
  GPUResult      result;

  result = dx12_interopDevices(interop, &first, &second);
  GPU__UNUSED(first);
  GPU__UNUSED(second);
  return result == GPU_OK
           ? dx12_encodeExternalBarriers(cmdb, barriers, acquire)
           : result;
}

static GPUResult
dx12_encodeExternalRelease(GPUCommandBuffer               *cmdb,
                           const GPUSharedBarrierBatchEXT *barriers) {
  return dx12_encodeExternalBarriers(cmdb, barriers, false);
}

static GPUResult
dx12_encodeExternalAcquire(GPUCommandBuffer               *cmdb,
                           const GPUSharedBarrierBatchEXT *barriers) {
  return dx12_encodeExternalBarriers(cmdb, barriers, true);
}

static GPUResult
dx12_encodeSharedRelease(GPUDeviceInteropEXT           *interop,
                         GPUCommandBuffer               *cmdb,
                         const GPUSharedBarrierBatchEXT *barriers) {
  return dx12_encodeSharedBarriers(interop, cmdb, barriers, false);
}

static GPUResult
dx12_encodeSharedAcquire(GPUDeviceInteropEXT           *interop,
                         GPUCommandBuffer               *cmdb,
                         const GPUSharedBarrierBatchEXT *barriers) {
  return dx12_encodeSharedBarriers(interop, cmdb, barriers, true);
}

GPU_HIDE
void
dx12_initMultiGPU(GPUApiMultiGPU *api) {
  api->createInterop          = dx12_createDeviceInterop;
  api->destroyInterop         = dx12_destroyDeviceInterop;
  api->getBufferRequirements  = dx12_getSharedBufferRequirements;
  api->createBuffer           = dx12_createSharedBuffer;
  api->getTextureRequirements = dx12_getSharedTextureRequirements;
  api->createTexture          = dx12_createSharedTexture;
  api->createSemaphore        = dx12_createSharedSemaphore;
  api->encodeRelease          = dx12_encodeSharedRelease;
  api->encodeAcquire          = dx12_encodeSharedAcquire;
  api->getExternalBufferRequirements =
    dx12_getExternalBufferRequirements;
  api->createExternalBuffer    = dx12_createExternalBuffer;
  api->getExternalTextureRequirements =
    dx12_getExternalTextureRequirements;
  api->createExternalTexture   = dx12_createExternalTexture;
  api->createExternalSemaphore = dx12_createExternalSemaphore;
  api->encodeExternalRelease   = dx12_encodeExternalRelease;
  api->encodeExternalAcquire   = dx12_encodeExternalAcquire;
}
