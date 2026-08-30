/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"
#include "../../../api/buffer_internal.h"
#include "../../../api/multigpu_internal.h"

#if defined(__linux__)
#  include <unistd.h>
#endif

typedef struct GPUDeviceInteropCuda {
  GPUDevice     *graphicsDevice;
  GPUDevice     *cudaDevice;
  GPUApi        *graphicsApi;
  GPUDeviceCuda *cuda;
  bool           cudaFirst;
} GPUDeviceInteropCuda;

static bool
cuda__bufferUsageSupported(GPUBufferUsageFlags usage) {
  const GPUBufferUsageFlags allowed =
    (GPUBufferUsageFlags)((uint32_t)GPU_BUFFER_USAGE_STORAGE |
                          (uint32_t)GPU_BUFFER_USAGE_COPY_SRC |
                          (uint32_t)GPU_BUFFER_USAGE_COPY_DST |
                          (uint32_t)GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT);

  return usage != 0u && (usage & ~allowed) == 0u;
}

static bool
cuda__textureUsageSupported(GPUTextureUsageFlags usage) {
  const GPUTextureUsageFlags allowed =
    GPU_TEXTURE_USAGE_SAMPLED |
    GPU_TEXTURE_USAGE_STORAGE |
    GPU_TEXTURE_USAGE_COPY_SRC |
    GPU_TEXTURE_USAGE_COPY_DST;

  return usage != 0u && (usage & ~allowed) == 0u;
}

static bool
cuda__sharedTextureSupported(const GPUDeviceInteropCuda *native,
                             const GPUTextureCreateInfo *info) {
  if (!native || !info || !cuda__textureUsageSupported(info->usage)) {
    return false;
  }
#if defined(_WIN32) || defined(WIN32)
  if (native->graphicsApi->backend == GPU_BACKEND_VULKAN &&
      info->format == GPU_FORMAT_DEPTH16_UNORM) {
    return false;
  }
  if (native->graphicsApi->backend == GPU_BACKEND_VULKAN &&
      info->dimension == GPU_TEXTURE_DIMENSION_2D &&
      info->depthOrLayers > 1u &&
      (info->mipLevelCount ? info->mipLevelCount : 1u) > 1u) {
    return false;
  }
#endif
  return true;
}

static GPUResult
cuda__interopDevices(GPUDeviceInteropEXT   *interop,
                     GPUDeviceInteropCuda **outNative) {
  GPUDeviceInteropCuda *native;

  native = interop ? interop->_priv : NULL;
  if (!native || !native->graphicsDevice || !native->cudaDevice ||
      !native->graphicsApi || !native->cuda || !outNative) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outNative = native;
  return GPU_OK;
}

static GPUResult
cuda_createDeviceInterop(GPUDevice           *firstDevice,
                         GPUDevice           *secondDevice,
                         GPUDeviceInteropEXT *interop) {
  GPUDeviceInteropCuda *native;
  GPUDevice            *cudaDevice, *graphicsDevice;
  GPUApi               *firstApi, *secondApi, *graphicsApi;
  GPUDeviceCuda        *cuda;
  bool                  cudaFirst, sameDevice;
  GPUResult             result;

  if (!firstDevice || !secondDevice || !interop) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  firstApi  = gpuDeviceApi(firstDevice);
  secondApi = gpuDeviceApi(secondDevice);
  cudaFirst = firstApi && firstApi->backend == GPU_BACKEND_CUDA;
  if (!firstApi || !secondApi ||
      cudaFirst == (secondApi->backend == GPU_BACKEND_CUDA)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  cudaDevice     = cudaFirst ? firstDevice : secondDevice;
  graphicsDevice = cudaFirst ? secondDevice : firstDevice;
  graphicsApi    = cudaFirst ? secondApi : firstApi;
  cuda           = cuda_device(cudaDevice);
  if (!cuda || !cuda->driver ||
      !cuda->driver->importExternalMemory ||
      !cuda->driver->externalMemoryGetMappedBuffer ||
      !cuda->driver->destroyExternalMemory ||
      !cuda->driver->importExternalSemaphore ||
      !cuda->driver->destroyExternalSemaphore ||
      !cuda->driver->waitExternalSemaphoresAsync ||
      !cuda->driver->signalExternalSemaphoresAsync ||
      !graphicsApi->multigpu.getExternalBufferRequirements ||
      !graphicsApi->multigpu.createExternalBuffer ||
      !graphicsApi->multigpu.createExternalSemaphore ||
      !graphicsApi->multigpu.encodeExternalRelease ||
      !graphicsApi->multigpu.encodeExternalAcquire) {
    return GPU_ERROR_UNSUPPORTED;
  }

  sameDevice = false;
  result = GPUAdaptersSharePhysicalDevice(cudaDevice->adapter,
                                           graphicsDevice->adapter,
                                           &sameDevice);
  if (result != GPU_OK || !sameDevice) {
    return result != GPU_OK ? result : GPU_ERROR_UNSUPPORTED;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->graphicsDevice = graphicsDevice;
  native->cudaDevice     = cudaDevice;
  native->graphicsApi    = graphicsApi;
  native->cuda           = cuda;
  native->cudaFirst      = cudaFirst;
  interop->_priv         = native;
  return GPU_OK;
}

static void
cuda_destroyDeviceInterop(GPUDeviceInteropEXT *interop) {
  if (!interop) {
    return;
  }
  free(interop->_priv);
  interop->_priv = NULL;
}

static void
cuda__bufferInfos(GPUDeviceInteropCuda       *native,
                  const GPUBufferCreateInfo  *firstInfo,
                  const GPUBufferCreateInfo  *secondInfo,
                  const GPUBufferCreateInfo **outGraphicsInfo,
                  const GPUBufferCreateInfo **outCudaInfo,
                  GPUBufferCreateInfo         *outSharedInfo) {
  *outGraphicsInfo = native->cudaFirst ? secondInfo : firstInfo;
  *outCudaInfo     = native->cudaFirst ? firstInfo : secondInfo;
  *outSharedInfo   = **outGraphicsInfo;
  outSharedInfo->usage = firstInfo->usage | secondInfo->usage;
}

static GPUResult
cuda_getSharedBufferRequirements(
  GPUDeviceInteropEXT       *interop,
  const GPUBufferCreateInfo *firstInfo,
  const GPUBufferCreateInfo *secondInfo,
  GPUMemoryRequirements     *outRequirements
) {
  GPUDeviceInteropCuda      *native;
  const GPUBufferCreateInfo *graphicsInfo, *cudaInfo;
  GPUBufferCreateInfo        sharedInfo;
  GPUResult                  result;

  result = cuda__interopDevices(interop, &native);
  if (result != GPU_OK || !firstInfo || !secondInfo || !outRequirements) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }
  cuda__bufferInfos(native,
                    firstInfo,
                    secondInfo,
                    &graphicsInfo,
                    &cudaInfo,
                    &sharedInfo);
  GPU__UNUSED(graphicsInfo);
  if (!cuda__bufferUsageSupported(cudaInfo->usage)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  return native->graphicsApi->multigpu.getExternalBufferRequirements(
    native->graphicsDevice,
    &sharedInfo,
    outRequirements
  );
}

static bool
cuda__memoryDesc(const GPUExternalMemoryExport  *memory,
                 CUDAExternalMemoryHandleDesc  *outDesc) {
  if (!memory || !outDesc || memory->sizeBytes == 0u) {
    return false;
  }
  memset(outDesc, 0, sizeof(*outDesc));
  outDesc->size  = memory->sizeBytes;
  outDesc->flags = memory->dedicated
                     ? CUDA_EXTERNAL_MEMORY_DEDICATED
                     : 0u;
  switch (memory->type) {
    case GPU_EXTERNAL_MEMORY_OPAQUE_FD:
      outDesc->type      = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
      outDesc->handle.fd = memory->handle.fd;
      return memory->handle.fd >= 0;
    case GPU_EXTERNAL_MEMORY_OPAQUE_WIN32:
      outDesc->type                = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32;
      outDesc->handle.win32.handle = memory->handle.win32;
      return memory->handle.win32 != NULL;
    case GPU_EXTERNAL_MEMORY_D3D12_RESOURCE:
      outDesc->type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
      outDesc->handle.win32.handle = memory->handle.win32;
      return memory->handle.win32 != NULL;
    default:
      return false;
  }
}

static void
cuda__closeMemoryExport(const GPUExternalMemoryExport *memory,
                        bool                           imported) {
  if (!memory) {
    return;
  }
#if defined(_WIN32) || defined(WIN32)
  if ((memory->type == GPU_EXTERNAL_MEMORY_OPAQUE_WIN32 ||
       memory->type == GPU_EXTERNAL_MEMORY_D3D12_RESOURCE) &&
      memory->handle.win32) {
    CloseHandle((HANDLE)memory->handle.win32);
  }
#elif defined(__linux__)
  if (memory->type == GPU_EXTERNAL_MEMORY_OPAQUE_FD &&
      memory->handle.fd >= 0 && !imported) {
    close(memory->handle.fd);
  }
#else
  GPU__UNUSED(imported);
#endif
}

static GPUResult
cuda__importBuffer(GPUDevice                      *device,
                   const GPUBufferCreateInfo      *info,
                   const GPUExternalMemoryExport  *memory,
                   GPUBuffer                     **outBuffer) {
  GPUDeviceCuda                  *deviceNative;
  GPUBufferCuda                  *native;
  GPUBuffer                      *buffer;
  CUDAExternalMemoryHandleDesc   handleDesc;
  CUDAExternalMemoryBufferDesc   bufferDesc = {0};
  CUresult                        result;
  GPUResult                       pushResult;
  bool                            imported;

  deviceNative = cuda_device(device);
  if (!deviceNative || !info || !memory || !outBuffer ||
      info->sizeBytes > memory->sizeBytes ||
      !cuda__memoryDesc(memory, &handleDesc)) {
    cuda__closeMemoryExport(memory, false);
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outBuffer = NULL;
  buffer = calloc(1, sizeof(*buffer));
  native = calloc(1, sizeof(*native));
  if (!buffer || !native) {
    free(native);
    free(buffer);
    cuda__closeMemoryExport(memory, false);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  pushResult = cuda_push(deviceNative->driver, deviceNative->context);
  if (pushResult != GPU_OK) {
    cuda__closeMemoryExport(memory, false);
    free(native);
    free(buffer);
    return pushResult;
  }
  result = deviceNative->driver->importExternalMemory(
    &native->externalMemory,
    &handleDesc
  );
  imported = result == CUDA_SUCCESS;
  if (result == CUDA_SUCCESS) {
    bufferDesc.size = info->sizeBytes;
    result = deviceNative->driver->externalMemoryGetMappedBuffer(
      &native->address,
      native->externalMemory,
      &bufferDesc
    );
  }
  if (result != CUDA_SUCCESS && native->externalMemory) {
    (void)deviceNative->driver->destroyExternalMemory(
      native->externalMemory
    );
    native->externalMemory = NULL;
  }
  cuda_pop(deviceNative->driver);
  cuda__closeMemoryExport(memory, imported);
  if (result != CUDA_SUCCESS) {
    cuda_report(device, result, "external buffer import");
    free(native);
    free(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->driver          = deviceNative->driver;
  buffer->_priv           = native;
  buffer->device          = device;
  buffer->_gpuAddress     = native->address;
  buffer->_allocationSize = memory->sizeBytes;
  buffer->sizeBytes       = info->sizeBytes;
  buffer->usage           = info->usage;
  *outBuffer              = buffer;
  return GPU_OK;
}

static GPUResult
cuda_createSharedBuffer(GPUDeviceInteropEXT       *interop,
                        const GPUBufferCreateInfo *firstInfo,
                        const GPUBufferCreateInfo *secondInfo,
                        GPUBuffer                **outFirstBuffer,
                        GPUBuffer                **outSecondBuffer) {
  GPUDeviceInteropCuda      *native;
  const GPUBufferCreateInfo *graphicsInfo, *cudaInfo;
  GPUBufferCreateInfo        sharedInfo;
  GPUExternalMemoryExport    memory = {0};
  GPUBuffer                 *graphicsBuffer, *cudaBuffer;
  GPUResult                  result;

  result = cuda__interopDevices(interop, &native);
  if (result != GPU_OK || !firstInfo || !secondInfo ||
      !outFirstBuffer || !outSecondBuffer) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }
  *outFirstBuffer  = NULL;
  *outSecondBuffer = NULL;
  cuda__bufferInfos(native,
                    firstInfo,
                    secondInfo,
                    &graphicsInfo,
                    &cudaInfo,
                    &sharedInfo);
  if (!cuda__bufferUsageSupported(cudaInfo->usage)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  graphicsBuffer = NULL;
  cudaBuffer     = NULL;
  result = native->graphicsApi->multigpu.createExternalBuffer(
    native->graphicsDevice,
    &sharedInfo,
    &graphicsBuffer,
    &memory
  );
  if (result != GPU_OK) {
    return result;
  }
  result = cuda__importBuffer(native->cudaDevice,
                              cudaInfo,
                              &memory,
                              &cudaBuffer);
  if (result != GPU_OK) {
    GPUDestroyBuffer(graphicsBuffer);
    return result;
  }
  graphicsBuffer->usage = graphicsInfo->usage;
  if (native->cudaFirst) {
    *outFirstBuffer  = cudaBuffer;
    *outSecondBuffer = graphicsBuffer;
  } else {
    *outFirstBuffer  = graphicsBuffer;
    *outSecondBuffer = cudaBuffer;
  }
  return GPU_OK;
}

static void
cuda__textureInfos(GPUDeviceInteropCuda        *native,
                   const GPUTextureCreateInfo  *firstInfo,
                   const GPUTextureCreateInfo  *secondInfo,
                   const GPUTextureCreateInfo **outGraphicsInfo,
                   const GPUTextureCreateInfo **outCudaInfo,
                   GPUTextureCreateInfo         *outSharedInfo) {
  *outGraphicsInfo = native->cudaFirst ? secondInfo : firstInfo;
  *outCudaInfo     = native->cudaFirst ? firstInfo : secondInfo;
  *outSharedInfo   = **outGraphicsInfo;
  outSharedInfo->usage = firstInfo->usage | secondInfo->usage;
}

static GPUResult
cuda_getSharedTextureRequirements(
  GPUDeviceInteropEXT        *interop,
  const GPUTextureCreateInfo *firstInfo,
  const GPUTextureCreateInfo *secondInfo,
  GPUMemoryRequirements      *outRequirements
) {
  GPUDeviceInteropCuda       *native;
  const GPUTextureCreateInfo *graphicsInfo, *cudaInfo;
  GPUTextureCreateInfo        sharedInfo;
  GPUCudaTexturePlan          plan;
  GPUCudaFormatInfo           format;
  GPUResult                   result;

  result = cuda__interopDevices(interop, &native);
  if (result != GPU_OK || !firstInfo || !secondInfo || !outRequirements) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }
  cuda__textureInfos(native,
                     firstInfo,
                     secondInfo,
                     &graphicsInfo,
                     &cudaInfo,
                     &sharedInfo);
  GPU__UNUSED(graphicsInfo);
  if (!cuda__sharedTextureSupported(native, cudaInfo) ||
      !native->cuda->driver->externalMemoryGetMappedMipmappedArray ||
      !cuda_formatInfo(cudaInfo->format, &format) ||
      !cuda_texturePlan(cudaInfo, &format, &plan) ||
      !native->graphicsApi->multigpu.getExternalTextureRequirements) {
    return GPU_ERROR_UNSUPPORTED;
  }
  GPU__UNUSED(plan);
  return native->graphicsApi->multigpu.getExternalTextureRequirements(
    native->graphicsDevice,
    &sharedInfo,
    outRequirements
  );
}

static GPUResult
cuda__importTexture(GPUDevice                     *device,
                    const GPUTextureCreateInfo     *info,
                    GPUTextureUsageFlags            graphicsUsage,
                    const GPUExternalMemoryExport  *memory,
                    GPUTexture                    **outTexture) {
  GPUDeviceCuda                         *deviceNative;
  GPUTextureCuda                        *native;
  GPUTexture                            *texture;
  GPUCudaTexturePlan                     plan;
  GPUCudaFormatInfo                      format;
  CUDAExternalMemoryHandleDesc           handleDesc;
  CUDAExternalMemoryMipmappedArrayDesc   mipmapDesc = {0};
  CUresult                               result;
  GPUResult                              pushResult;
  const char                            *operation;
  bool                                   imported;

  deviceNative = cuda_device(device);
  if (!deviceNative || !info || !memory || !outTexture ||
      !deviceNative->driver->externalMemoryGetMappedMipmappedArray ||
      !cuda_formatInfo(info->format, &format) ||
      !cuda_texturePlan(info, &format, &plan) ||
      !cuda__memoryDesc(memory, &handleDesc)) {
    cuda__closeMemoryExport(memory, false);
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outTexture = NULL;
  texture = calloc(1, sizeof(*texture));
  native  = calloc(1, sizeof(*native));
  if (!texture || !native) {
    free(native);
    free(texture);
    cuda__closeMemoryExport(memory, false);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  if ((graphicsUsage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u) {
    plan.desc.Flags |= CUDA_ARRAY3D_COLOR_ATTACHMENT;
  }
  mipmapDesc.arrayDesc = plan.desc;
  mipmapDesc.numLevels = plan.mipLevelCount;
  pushResult = cuda_push(deviceNative->driver, deviceNative->context);
  if (pushResult != GPU_OK) {
    cuda__closeMemoryExport(memory, false);
    free(native);
    free(texture);
    return pushResult;
  }
  operation = "external texture memory import";
  result = deviceNative->driver->importExternalMemory(
    &native->externalMemory,
    &handleDesc
  );
  imported = result == CUDA_SUCCESS;
  if (result == CUDA_SUCCESS) {
    operation = "external texture mapping";
    result = deviceNative->driver->externalMemoryGetMappedMipmappedArray(
      &native->mipmap,
      native->externalMemory,
      &mipmapDesc
    );
  }
  if (result != CUDA_SUCCESS && native->mipmap) {
    (void)deviceNative->driver->mipmappedArrayDestroy(native->mipmap);
    native->mipmap = NULL;
  }
  if (result != CUDA_SUCCESS && native->externalMemory) {
    (void)deviceNative->driver->destroyExternalMemory(native->externalMemory);
    native->externalMemory = NULL;
  }
  cuda_pop(deviceNative->driver);
  cuda__closeMemoryExport(memory, imported);
  if (result != CUDA_SUCCESS) {
    cuda_report(device, result, operation);
    free(native);
    free(texture);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->driver           = deviceNative->driver;
  native->format           = format;
  native->arrayFlags       = plan.desc.Flags;
  texture->_priv           = native;
  texture->device          = device;
  texture->format          = info->format;
  texture->dimension       = info->dimension;
  texture->width           = info->width;
  texture->height          = info->height;
  texture->depthOrLayers   = info->depthOrLayers;
  texture->mipLevelCount   = plan.mipLevelCount;
  texture->sampleCount     = 1u;
  texture->usage           = info->usage;
  texture->_ownsNative     = true;
  *outTexture              = texture;
  return GPU_OK;
}

static GPUResult
cuda_createSharedTexture(GPUDeviceInteropEXT        *interop,
                         const GPUTextureCreateInfo *firstInfo,
                         const GPUTextureCreateInfo *secondInfo,
                         GPUTexture                **outFirstTexture,
                         GPUTexture                **outSecondTexture) {
  GPUDeviceInteropCuda       *native;
  const GPUTextureCreateInfo *graphicsInfo, *cudaInfo;
  GPUTextureCreateInfo        sharedInfo;
  GPUExternalMemoryExport     memory = {0};
  GPUTexture                 *graphicsTexture, *cudaTexture;
  GPUResult                   result;

  result = cuda__interopDevices(interop, &native);
  if (result != GPU_OK || !firstInfo || !secondInfo ||
      !outFirstTexture || !outSecondTexture) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }
  *outFirstTexture  = NULL;
  *outSecondTexture = NULL;
  cuda__textureInfos(native,
                     firstInfo,
                     secondInfo,
                     &graphicsInfo,
                     &cudaInfo,
                     &sharedInfo);
  if (!cuda__sharedTextureSupported(native, cudaInfo) ||
      !native->cuda->driver->externalMemoryGetMappedMipmappedArray ||
      !native->graphicsApi->multigpu.createExternalTexture) {
    return GPU_ERROR_UNSUPPORTED;
  }

  graphicsTexture = NULL;
  cudaTexture     = NULL;
  result = native->graphicsApi->multigpu.createExternalTexture(
    native->graphicsDevice,
    &sharedInfo,
    &graphicsTexture,
    &memory
  );
  if (result != GPU_OK) {
    return result;
  }
  result = cuda__importTexture(native->cudaDevice,
                               cudaInfo,
                               graphicsInfo->usage,
                               &memory,
                               &cudaTexture);
  if (result != GPU_OK) {
    GPUDestroyTexture(graphicsTexture);
    return result;
  }
  graphicsTexture->usage = graphicsInfo->usage;
  if (native->cudaFirst) {
    *outFirstTexture  = cudaTexture;
    *outSecondTexture = graphicsTexture;
  } else {
    *outFirstTexture  = graphicsTexture;
    *outSecondTexture = cudaTexture;
  }
  return GPU_OK;
}

static bool
cuda__semaphoreDesc(const GPUExternalSemaphoreExport *semaphore,
                    CUDAExternalSemaphoreHandleDesc  *outDesc) {
  if (!semaphore || !outDesc) {
    return false;
  }
  memset(outDesc, 0, sizeof(*outDesc));
  switch (semaphore->type) {
    case GPU_EXTERNAL_SEMAPHORE_OPAQUE_FD:
      outDesc->type      = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD;
      outDesc->handle.fd = semaphore->handle.fd;
      return semaphore->handle.fd >= 0;
    case GPU_EXTERNAL_SEMAPHORE_OPAQUE_WIN32:
      outDesc->type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32;
      outDesc->handle.win32.handle = semaphore->handle.win32;
      return semaphore->handle.win32 != NULL;
    case GPU_EXTERNAL_SEMAPHORE_D3D12_FENCE:
      outDesc->type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE;
      outDesc->handle.win32.handle = semaphore->handle.win32;
      return semaphore->handle.win32 != NULL;
    case GPU_EXTERNAL_SEMAPHORE_TIMELINE_FD:
      outDesc->type =
        CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_FD;
      outDesc->handle.fd = semaphore->handle.fd;
      return semaphore->handle.fd >= 0;
    case GPU_EXTERNAL_SEMAPHORE_TIMELINE_WIN32:
      outDesc->type =
        CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_WIN32;
      outDesc->handle.win32.handle = semaphore->handle.win32;
      return semaphore->handle.win32 != NULL;
    default:
      return false;
  }
}

static void
cuda__closeSemaphoreExport(const GPUExternalSemaphoreExport *semaphore,
                           bool                              imported) {
  if (!semaphore) {
    return;
  }
#if defined(_WIN32) || defined(WIN32)
  if (semaphore->type != GPU_EXTERNAL_SEMAPHORE_NONE &&
      semaphore->type != GPU_EXTERNAL_SEMAPHORE_OPAQUE_FD &&
      semaphore->type != GPU_EXTERNAL_SEMAPHORE_TIMELINE_FD &&
      semaphore->handle.win32) {
    CloseHandle((HANDLE)semaphore->handle.win32);
  }
#elif defined(__linux__)
  if ((semaphore->type == GPU_EXTERNAL_SEMAPHORE_OPAQUE_FD ||
       semaphore->type == GPU_EXTERNAL_SEMAPHORE_TIMELINE_FD) &&
      semaphore->handle.fd >= 0 && !imported) {
    close(semaphore->handle.fd);
  }
#else
  GPU__UNUSED(imported);
#endif
}

static GPUResult
cuda_createSharedSemaphore(GPUDeviceInteropEXT          *interop,
                           const GPUSemaphoreCreateInfo *info,
                           GPUSemaphore                 *firstSemaphore,
                           GPUSemaphore                 *secondSemaphore) {
  GPUDeviceInteropCuda       *native;
  GPUExternalSemaphoreExport  semaphoreExport = {0};
  CUDAExternalSemaphoreHandleDesc desc;
  GPUSemaphore               *graphicsSemaphore, *cudaSemaphore;
  GPUSemaphoreCuda           *cudaState;
  CUresult                    cudaResult;
  GPUResult                   result;
  GPUResult                   pushResult;
  bool                        imported;

  result = cuda__interopDevices(interop, &native);
  if (result != GPU_OK || !firstSemaphore || !secondSemaphore) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }
  graphicsSemaphore = native->cudaFirst ? secondSemaphore : firstSemaphore;
  cudaSemaphore     = native->cudaFirst ? firstSemaphore : secondSemaphore;
  result = native->graphicsApi->multigpu.createExternalSemaphore(
    native->graphicsDevice,
    info,
    graphicsSemaphore,
    &semaphoreExport
  );
  if (result != GPU_OK) {
    return result;
  }
  if (!cuda__semaphoreDesc(&semaphoreExport, &desc)) {
    cuda__closeSemaphoreExport(&semaphoreExport, false);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  cudaState = calloc(1, sizeof(*cudaState));
  if (!cudaState) {
    cuda__closeSemaphoreExport(&semaphoreExport, false);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  pushResult = cuda_push(native->cuda->driver, native->cuda->context);
  if (pushResult != GPU_OK) {
    cuda__closeSemaphoreExport(&semaphoreExport, false);
    free(cudaState);
    return pushResult;
  }
  cudaResult = native->cuda->driver->importExternalSemaphore(
    &cudaState->semaphore,
    &desc
  );
  imported = cudaResult == CUDA_SUCCESS;
  cuda_pop(native->cuda->driver);
  cuda__closeSemaphoreExport(&semaphoreExport, imported);
  if (cudaResult != CUDA_SUCCESS) {
    cuda_report(native->cudaDevice, cudaResult, "external semaphore import");
    free(cudaState);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  cudaState->driver     = native->cuda->driver;
  cudaSemaphore->_priv = cudaState;
  return GPU_OK;
}

static GPUResult
cuda__encodeSharedBarriers(GPUDeviceInteropEXT           *interop,
                           GPUCommandBuffer               *cmdb,
                           const GPUSharedBarrierBatchEXT *barriers,
                           bool                            acquire) {
  GPUDeviceInteropCuda *native;
  GPUDevice            *commandDevice;
  GPUResult             result;

  result = cuda__interopDevices(interop, &native);
  if (result != GPU_OK || !cmdb || !barriers) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }
  commandDevice = gpuCommandBufferDevice(cmdb);
  if (commandDevice == native->cudaDevice) {
    return GPU_OK;
  }
  if (commandDevice != native->graphicsDevice) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  return acquire
           ? native->graphicsApi->multigpu.encodeExternalAcquire(cmdb,
                                                                  barriers)
           : native->graphicsApi->multigpu.encodeExternalRelease(cmdb,
                                                                  barriers);
}

static GPUResult
cuda_encodeSharedRelease(GPUDeviceInteropEXT           *interop,
                         GPUCommandBuffer               *cmdb,
                         const GPUSharedBarrierBatchEXT *barriers) {
  return cuda__encodeSharedBarriers(interop, cmdb, barriers, false);
}

static GPUResult
cuda_encodeSharedAcquire(GPUDeviceInteropEXT           *interop,
                         GPUCommandBuffer               *cmdb,
                         const GPUSharedBarrierBatchEXT *barriers) {
  return cuda__encodeSharedBarriers(interop, cmdb, barriers, true);
}

void
cuda_initMultiGPU(GPUApiMultiGPU *api) {
  api->createInterop         = cuda_createDeviceInterop;
  api->destroyInterop        = cuda_destroyDeviceInterop;
  api->getBufferRequirements = cuda_getSharedBufferRequirements;
  api->createBuffer          = cuda_createSharedBuffer;
  api->getTextureRequirements = cuda_getSharedTextureRequirements;
  api->createTexture          = cuda_createSharedTexture;
  api->createSemaphore       = cuda_createSharedSemaphore;
  api->encodeRelease         = cuda_encodeSharedRelease;
  api->encodeAcquire         = cuda_encodeSharedAcquire;
}
