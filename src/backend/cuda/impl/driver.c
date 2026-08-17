/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

#if !defined(_WIN32) && !defined(WIN32)
#  include <dlfcn.h>
#  include <sched.h>
#endif

static GPUCUDA  cuda;
static uint32_t cudaState;

static void *
cuda__open(void) {
#if defined(_WIN32) || defined(WIN32)
  return LoadLibraryA("nvcuda.dll");
#else
  return dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
}

static void
cuda__close(void *library) {
  if (!library) {
    return;
  }
#if defined(_WIN32) || defined(WIN32)
  FreeLibrary((HMODULE)library);
#else
  dlclose(library);
#endif
}

static void *
cuda__symbol(void *library, const char *name) {
#if defined(_WIN32) || defined(WIN32)
  return (void *)GetProcAddress((HMODULE)library, name);
#else
  return dlsym(library, name);
#endif
}

static void *
cuda__symbol2(void *library, const char *preferred, const char *fallback) {
  void *symbol;

  symbol = cuda__symbol(library, preferred);
  return symbol ? symbol : cuda__symbol(library, fallback);
}

static bool
cuda__load(void) {
  void *library;
  int   driverVersion;

  library = cuda__open();
  if (!library) {
    return false;
  }

#define CUDA_LOAD_REQUIRED(field, name)                                      \
  do {                                                                       \
    cuda.field = (void *)cuda__symbol(library, name);                         \
    if (!cuda.field) {                                                        \
      goto fail;                                                              \
    }                                                                        \
  } while (0)
#define CUDA_LOAD_REQUIRED2(field, preferred, fallback)                      \
  do {                                                                       \
    cuda.field = (void *)cuda__symbol2(library, preferred, fallback);         \
    if (!cuda.field) {                                                        \
      goto fail;                                                              \
    }                                                                        \
  } while (0)
#define CUDA_LOAD_OPTIONAL(field, name)                                      \
  cuda.field = (void *)cuda__symbol(library, name)

  memset(&cuda, 0, sizeof(cuda));
  cuda.library = library;
  CUDA_LOAD_REQUIRED(init, "cuInit");
  CUDA_LOAD_REQUIRED(driverGetVersion, "cuDriverGetVersion");
  CUDA_LOAD_REQUIRED(deviceGetCount, "cuDeviceGetCount");
  CUDA_LOAD_REQUIRED(deviceGet, "cuDeviceGet");
  CUDA_LOAD_REQUIRED(deviceGetName, "cuDeviceGetName");
  CUDA_LOAD_REQUIRED2(deviceGetUuid, "cuDeviceGetUuid_v2", "cuDeviceGetUuid");
  CUDA_LOAD_OPTIONAL(deviceGetLuid, "cuDeviceGetLuid");
  CUDA_LOAD_REQUIRED(deviceGetAttribute, "cuDeviceGetAttribute");
  CUDA_LOAD_REQUIRED(primaryCtxRetain, "cuDevicePrimaryCtxRetain");
  CUDA_LOAD_REQUIRED2(primaryCtxRelease,
                      "cuDevicePrimaryCtxRelease_v2",
                      "cuDevicePrimaryCtxRelease");
  CUDA_LOAD_REQUIRED2(ctxPushCurrent, "cuCtxPushCurrent_v2", "cuCtxPushCurrent");
  CUDA_LOAD_REQUIRED2(ctxPopCurrent, "cuCtxPopCurrent_v2", "cuCtxPopCurrent");
  CUDA_LOAD_REQUIRED(streamCreate, "cuStreamCreate");
  CUDA_LOAD_REQUIRED2(streamDestroy, "cuStreamDestroy_v2", "cuStreamDestroy");
  CUDA_LOAD_REQUIRED(streamSynchronize, "cuStreamSynchronize");
  CUDA_LOAD_REQUIRED(eventCreate, "cuEventCreate");
  CUDA_LOAD_REQUIRED2(eventDestroy, "cuEventDestroy_v2", "cuEventDestroy");
  CUDA_LOAD_REQUIRED(eventRecord, "cuEventRecord");
  CUDA_LOAD_REQUIRED(eventSynchronize, "cuEventSynchronize");
  CUDA_LOAD_OPTIONAL(importExternalMemory, "cuImportExternalMemory");
  CUDA_LOAD_OPTIONAL(externalMemoryGetMappedBuffer,
                     "cuExternalMemoryGetMappedBuffer");
  CUDA_LOAD_OPTIONAL(destroyExternalMemory, "cuDestroyExternalMemory");
  CUDA_LOAD_OPTIONAL(importExternalSemaphore, "cuImportExternalSemaphore");
  CUDA_LOAD_OPTIONAL(signalExternalSemaphoresAsync,
                     "cuSignalExternalSemaphoresAsync");
  CUDA_LOAD_OPTIONAL(waitExternalSemaphoresAsync,
                     "cuWaitExternalSemaphoresAsync");
  CUDA_LOAD_OPTIONAL(destroyExternalSemaphore,
                     "cuDestroyExternalSemaphore");
  CUDA_LOAD_REQUIRED2(memAlloc, "cuMemAlloc_v2", "cuMemAlloc");
  CUDA_LOAD_REQUIRED2(memFree, "cuMemFree_v2", "cuMemFree");
  CUDA_LOAD_REQUIRED2(memcpyHtoD, "cuMemcpyHtoD_v2", "cuMemcpyHtoD");
  CUDA_LOAD_REQUIRED2(memcpyDtoH, "cuMemcpyDtoH_v2", "cuMemcpyDtoH");
  CUDA_LOAD_REQUIRED(array3DCreate, "cuArray3DCreate_v2");
  CUDA_LOAD_REQUIRED(arrayDestroy, "cuArrayDestroy");
  CUDA_LOAD_REQUIRED(mipmappedArrayCreate, "cuMipmappedArrayCreate");
  CUDA_LOAD_REQUIRED(mipmappedArrayDestroy, "cuMipmappedArrayDestroy");
  CUDA_LOAD_REQUIRED(mipmappedArrayGetLevel, "cuMipmappedArrayGetLevel");
  CUDA_LOAD_REQUIRED(memcpy3D, "cuMemcpy3D_v2");
  CUDA_LOAD_REQUIRED(surfObjectCreate, "cuSurfObjectCreate");
  CUDA_LOAD_REQUIRED(surfObjectDestroy, "cuSurfObjectDestroy");
  CUDA_LOAD_REQUIRED(texObjectCreate, "cuTexObjectCreate");
  CUDA_LOAD_REQUIRED(texObjectDestroy, "cuTexObjectDestroy");
  CUDA_LOAD_REQUIRED(moduleLoadData, "cuModuleLoadDataEx");
  CUDA_LOAD_REQUIRED(moduleUnload, "cuModuleUnload");
  CUDA_LOAD_REQUIRED(moduleGetFunction, "cuModuleGetFunction");
  CUDA_LOAD_REQUIRED(launchKernel, "cuLaunchKernel");
  CUDA_LOAD_OPTIONAL(getErrorName, "cuGetErrorName");
  CUDA_LOAD_OPTIONAL(getErrorString, "cuGetErrorString");

#undef CUDA_LOAD_OPTIONAL
#undef CUDA_LOAD_REQUIRED2
#undef CUDA_LOAD_REQUIRED

  driverVersion = 0;
  if (cuda.init(0u) != CUDA_SUCCESS ||
      cuda.driverGetVersion(&driverVersion) != CUDA_SUCCESS ||
      driverVersion < CUDA_MIN_DRIVER_VERSION) {
    goto fail;
  }
  return true;

fail:
  cuda__close(library);
  memset(&cuda, 0, sizeof(cuda));
  return false;
}

GPUCUDA *
cuda_driver(void) {
  uint32_t state;

#if defined(_WIN32) || defined(WIN32)
  state = (uint32_t)InterlockedCompareExchange((volatile LONG *)&cudaState,
                                                1,
                                                0);
  if (state == 0u) {
    InterlockedExchange((volatile LONG *)&cudaState, cuda__load() ? 2 : 3);
  } else {
    while ((state = (uint32_t)InterlockedCompareExchange(
              (volatile LONG *)&cudaState,
              0,
              0)) == 1u) {
      SwitchToThread();
    }
  }
#else
  state = 0u;
  if (__atomic_compare_exchange_n(&cudaState,
                                  &state,
                                  1u,
                                  false,
                                  __ATOMIC_ACQ_REL,
                                  __ATOMIC_ACQUIRE)) {
    __atomic_store_n(&cudaState,
                     cuda__load() ? 2u : 3u,
                     __ATOMIC_RELEASE);
  } else {
    while (__atomic_load_n(&cudaState, __ATOMIC_ACQUIRE) == 1u) {
      sched_yield();
    }
  }
#endif
#if defined(_WIN32) || defined(WIN32)
  state = (uint32_t)InterlockedCompareExchange((volatile LONG *)&cudaState,
                                                0,
                                                0);
#else
  state = __atomic_load_n(&cudaState, __ATOMIC_ACQUIRE);
#endif
  return state == 2u ? &cuda : NULL;
}

GPUResult
cuda_push(GPUCUDA *driver, CUcontext context) {
  if (!driver || !context ||
      driver->ctxPushCurrent(context) != CUDA_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

void
cuda_pop(GPUCUDA *driver) {
  CUcontext context;

  if (driver) {
    context = NULL;
    (void)driver->ctxPopCurrent(&context);
  }
}

void
cuda_report(GPUDevice *device, CUresult result, const char *operation) {
  const char *name;
  const char *detail;
  char        message[256];

  if (!device || result == CUDA_SUCCESS) {
    return;
  }

  name   = NULL;
  detail = NULL;
  if (cuda.getErrorName) {
    (void)cuda.getErrorName(result, &name);
  }
  if (cuda.getErrorString) {
    (void)cuda.getErrorString(result, &detail);
  }
  snprintf(message,
           sizeof(message),
           "CUDA %s failed: %s%s%s (%d)",
           operation ? operation : "operation",
           name ? name : "error",
           detail ? ": " : "",
           detail ? detail : "",
           result);
  gpuDeviceReportError(device,
                       result == CUDA_ERROR_OUT_OF_MEMORY
                         ? GPU_DEVICE_ERROR_OUT_OF_MEMORY
                         : GPU_DEVICE_ERROR_BACKEND,
                       GPU_DEVICE_LOST_REASON_UNKNOWN,
                       result == CUDA_ERROR_OUT_OF_MEMORY
                         ? GPU_ERROR_OUT_OF_MEMORY
                         : GPU_ERROR_BACKEND_FAILURE,
                       message);
}
