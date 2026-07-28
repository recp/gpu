#ifndef gpu_sample_platform_h
#define gpu_sample_platform_h

#if defined(__ANDROID__)

#include "android.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef void (*WebGPUReadyCallback)(GPUResult  result,
                                    GPUAdapter *adapter,
                                    GPUDevice  *device,
                                    void       *userData);

typedef struct WebGPURequest {
  WebGPUReadyCallback callback;
  GPUAdapter         *adapter;
  void               *userData;
  GPUResult           result;
  uint32_t            optionalFeatureCount;
  bool                completed;
  GPUFeature          optionalFeatures[8];
} WebGPURequest;

void
set_status(const char *message, int failed);

void
set_status_notice(const char *message);

int
read_file(const char *path, void **outData, uint64_t *outSize);

GPUResult
request_webgpu_device(GPUInstance        *instance,
                      WebGPURequest      *request,
                      WebGPUReadyCallback callback,
                      void               *userData);

GPUResult
request_webgpu_device_features(GPUInstance        *instance,
                               WebGPURequest      *request,
                               WebGPUReadyCallback callback,
                               void               *userData,
                               const GPUFeature   *optionalFeatures,
                               uint32_t            optionalFeatureCount);

int
resize_webgpu_canvas(GPUSwapchain *swapchain,
                     uint32_t     *width,
                     uint32_t     *height);

void
gpu_android_set_main_loop(void (*callback)(void *),
                          void  *userData,
                          int    fps,
                          bool   simulateInfiniteLoop);

void
gpu_android_cancel_main_loop(void);

double
gpu_android_get_now(void);

void*
gpu_android_load_image(const char *path, int *width, int *height);

GPUResult
gpu_android_sample_create_instance(const GPUInstanceCreateInfo *info,
                                   GPUInstance                **outInstance);

GPUSurface*
gpu_android_sample_create_surface(GPUInstance          *instance,
                                  GPUAdapter           *adapter,
                                  void                 *nativeHandle,
                                  GPUSurfaceType        nativeType,
                                  float                 contentScale);

GPUSwapchain*
gpu_android_sample_create_swapchain(GPUDevice  *device,
                                    GPUSurface *surface,
                                    uint32_t    width,
                                    uint32_t    height);

#define emscripten_set_main_loop_arg(callback, userData, fps, simulate) \
  gpu_android_set_main_loop((callback), (userData), (fps), (simulate))
#define emscripten_cancel_main_loop() gpu_android_cancel_main_loop()
#define emscripten_get_now() gpu_android_get_now()
#define emscripten_get_preloaded_image_data(path, width, height) \
  gpu_android_load_image((path), (width), (height))

#define GPUCreateInstance gpu_android_sample_create_instance
#define GPUCreateSurfaceFromNative gpu_android_sample_create_surface
#define GPUCreateSwapchainDefault gpu_android_sample_create_swapchain

#else

#include "webgpu.h"

#endif

#endif
