#ifndef gpu_sample_win32_h
#define gpu_sample_win32_h

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <gpu/gpu.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct GPUWin32Sample GPUWin32Sample;
typedef int (*GPUWin32SampleStart)(void);
typedef void (*WebGPUReadyCallback)(GPUResult  result,
                                    GPUAdapter *adapter,
                                    GPUDevice  *device,
                                    void       *userData);

typedef struct GPUWin32Window {
  HWND     handle;
  uint32_t width;
  uint32_t height;
  float    scale;
} GPUWin32Window;

typedef struct WebGPURequest {
  WebGPUReadyCallback callback;
  GPUAdapter         *adapter;
  void               *userData;
  GPUResult           result;
  uint32_t            optionalFeatureCount;
  bool                completed;
  GPUFeature          optionalFeatures[8];
} WebGPURequest;

GPUWin32Sample*
GPUSampleWin32Create(GPUWin32Window      *window,
                     const char          *name,
                     GPUWin32SampleStart  start);

bool
GPUSampleWin32Render(GPUWin32Sample *sample);

void
GPUSampleWin32Stop(GPUWin32Sample *sample);

const char*
GPUSampleWin32Status(const GPUWin32Sample *sample);

bool
GPUSampleWin32Failed(const GPUWin32Sample *sample);

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
gpu_win32_set_main_loop(void (*callback)(void *),
                        void  *userData,
                        int    fps,
                        bool   simulateInfiniteLoop);

void
gpu_win32_cancel_main_loop(void);

double
gpu_win32_get_now(void);

void*
gpu_win32_load_image(const char *path, int *width, int *height);

GPUResult
gpu_win32_sample_create_instance(const GPUInstanceCreateInfo *info,
                                 GPUInstance                **outInstance);

GPUSurface*
gpu_win32_sample_create_surface(GPUInstance   *instance,
                                GPUAdapter    *adapter,
                                void          *nativeHandle,
                                GPUSurfaceType nativeType,
                                float          contentScale);

GPUSwapchain*
gpu_win32_sample_create_swapchain(GPUDevice  *device,
                                  GPUSurface *surface,
                                  uint32_t    width,
                                  uint32_t    height);

#endif /* gpu_sample_win32_h */
