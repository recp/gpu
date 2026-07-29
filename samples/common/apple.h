#ifndef gpu_sample_apple_h
#define gpu_sample_apple_h

#include <gpu/gpu.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct GPUAppleSample GPUAppleSample;
typedef int (*GPUAppleSampleStart)(void);
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

GPUAppleSample*
GPUSampleAppleCreate(void                *nativeView,
                     const char          *name,
                     float                contentScale,
                     GPUAppleSampleStart  start);

bool
GPUSampleAppleRender(GPUAppleSample *sample);

bool
GPUSampleAppleHasRenderedFrame(const GPUAppleSample *sample);

void
GPUSampleAppleStop(GPUAppleSample *sample);

const char*
GPUSampleAppleStatus(const GPUAppleSample *sample);

bool
GPUSampleAppleFailed(const GPUAppleSample *sample);

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
gpu_apple_set_main_loop(void (*callback)(void *),
                        void  *userData,
                        int    fps,
                        bool   simulateInfiniteLoop);

void
gpu_apple_cancel_main_loop(void);

double
gpu_apple_get_now(void);

void*
gpu_apple_load_image(const char *path, int *width, int *height);

GPUResult
gpu_apple_sample_create_instance(const GPUInstanceCreateInfo *info,
                                 GPUInstance                **outInstance);

GPUSurface*
gpu_apple_sample_create_surface(GPUInstance   *instance,
                                GPUAdapter    *adapter,
                                void          *nativeHandle,
                                GPUSurfaceType nativeType,
                                float          contentScale);

GPUSwapchain*
gpu_apple_sample_create_swapchain(GPUDevice  *device,
                                  GPUSurface *surface,
                                  uint32_t    width,
                                  uint32_t    height);

#endif
