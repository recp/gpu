#ifndef GPU_SAMPLE_WEBGPU_H
#define GPU_SAMPLE_WEBGPU_H

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <gpu/gpu.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void
set_status(const char *message, int failed);

int
read_file(const char *path, void **outData, uint64_t *outSize);

int
resize_webgpu_canvas(GPUSwapchain *swapchain,
                     uint32_t     *width,
                     uint32_t     *height);

#endif /* GPU_SAMPLE_WEBGPU_H */
