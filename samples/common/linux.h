#ifndef gpu_sample_linux_h
#define gpu_sample_linux_h

#include <gpu/gpu.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct GPULinuxSample GPULinuxSample;
typedef int (*GPULinuxSampleStart)(void);
typedef void (*WebGPUReadyCallback)(GPUResult  result,
                                    GPUAdapter *adapter,
                                    GPUDevice  *device,
                                    void       *userData);

typedef enum GPULinuxWindowSystem {
  GPU_LINUX_WINDOW_XLIB,
  GPU_LINUX_WINDOW_WAYLAND
} GPULinuxWindowSystem;

typedef struct GPULinuxWindow {
  void                 *display;
  void                 *surface;
  uintptr_t             window;
  uint32_t              width;
  uint32_t              height;
  float                 scale;
  GPULinuxWindowSystem  system;
} GPULinuxWindow;

typedef struct WebGPURequest {
  WebGPUReadyCallback callback;
  GPUAdapter         *adapter;
  void               *userData;
  GPUResult           result;
  uint32_t            optionalFeatureCount;
  bool                completed;
  GPUFeature          optionalFeatures[8];
} WebGPURequest;

GPULinuxSample*
GPUSampleLinuxCreate(GPULinuxWindow      *window,
                     const char          *name,
                     GPULinuxSampleStart  start);

bool
GPUSampleLinuxRender(GPULinuxSample *sample);

void
GPUSampleLinuxStop(GPULinuxSample *sample);

const char*
GPUSampleLinuxStatus(const GPULinuxSample *sample);

bool
GPUSampleLinuxFailed(const GPULinuxSample *sample);

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
gpu_linux_set_main_loop(void (*callback)(void *),
                        void  *userData,
                        int    fps,
                        bool   simulateInfiniteLoop);

void
gpu_linux_cancel_main_loop(void);

double
gpu_linux_get_now(void);

void*
gpu_linux_load_image(const char *path, int *width, int *height);

GPUResult
gpu_linux_sample_create_instance(const GPUInstanceCreateInfo *info,
                                 GPUInstance                **outInstance);

GPUSurface*
gpu_linux_sample_create_surface(GPUInstance   *instance,
                                GPUAdapter    *adapter,
                                void          *nativeHandle,
                                GPUSurfaceType nativeType,
                                float          contentScale);

GPUSwapchain*
gpu_linux_sample_create_swapchain(GPUDevice  *device,
                                  GPUSurface *surface,
                                  uint32_t    width,
                                  uint32_t    height);

#endif /* gpu_sample_linux_h */
