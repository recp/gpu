#ifndef GPU_SAMPLE_WEBGPU_H
#define GPU_SAMPLE_WEBGPU_H

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <gpu/gpu.h>

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
  bool                completed;
} WebGPURequest;

void
set_status(const char *message, int failed);

int
read_file(const char *path, void **outData, uint64_t *outSize);

GPUResult
request_webgpu_device(GPUInstance        *instance,
                      WebGPURequest      *request,
                      WebGPUReadyCallback callback,
                      void               *userData);

int
resize_webgpu_canvas(GPUSwapchain *swapchain,
                     uint32_t     *width,
                     uint32_t     *height);

#endif /* GPU_SAMPLE_WEBGPU_H */
