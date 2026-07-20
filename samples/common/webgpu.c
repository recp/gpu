#include "webgpu.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <stdio.h>
#include <stdlib.h>

EM_JS(void, set_status_js, (const char *message, int failed), {
  const status = document.getElementById("status");
  status.textContent = UTF8ToString(message);
  status.dataset.failed = failed ? "true" : "false";
});

EM_JS(int, prepare_canvas_layout_js, (), {
  const canvas = document.getElementById("canvas");
  const root = document.documentElement;
  const scale = window.devicePixelRatio || 1;

  if (!canvas) return -1;
  if (canvas.gpuInnerWidth === window.innerWidth &&
      canvas.gpuInnerHeight === window.innerHeight &&
      canvas.gpuClientWidth === root.clientWidth &&
      canvas.gpuClientHeight === root.clientHeight &&
      canvas.gpuPixelRatio === scale) {
    return 0;
  }

  canvas.style.width = "";
  canvas.style.height = "";
  canvas.gpuInnerWidth = window.innerWidth;
  canvas.gpuInnerHeight = window.innerHeight;
  canvas.gpuClientWidth = root.clientWidth;
  canvas.gpuClientHeight = root.clientHeight;
  canvas.gpuPixelRatio = scale;
  return 1;
});

void
set_status(const char *message, int failed) {
  set_status_js(message, failed);
  if (failed) {
    fprintf(stderr, "%s\n", message);
  } else {
    puts(message);
  }
}

int
read_file(const char *path, void **outData, uint64_t *outSize) {
  void *data;
  long  size;
  FILE *file;

  file = fopen(path, "rb");
  if (!file ||
      fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    if (file) {
      fclose(file);
    }
    return 0;
  }

  data = malloc((size_t)size);
  if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return 0;
  }

  fclose(file);
  *outData = data;
  *outSize = (uint64_t)size;
  return 1;
}

static void
finish_webgpu_request(WebGPURequest *request,
                      GPUResult      result,
                      GPUAdapter    *adapter,
                      GPUDevice     *device) {
  if (request->completed) {
    return;
  }

  request->result    = result;
  request->completed = true;
  request->callback(result, adapter, device, request->userData);
}

static void
webgpu_device_ready(GPUResult result, GPUDevice *device, void *userData) {
  WebGPURequest *request;

  request = userData;
  if (result == GPU_OK && !device) {
    result = GPU_ERROR_BACKEND_FAILURE;
  }
  finish_webgpu_request(request, result, request->adapter, device);
}

static void
webgpu_adapter_ready(GPUResult result, GPUAdapter *adapter, void *userData) {
  WebGPURequest *request;

  request = userData;
  if (result != GPU_OK || !adapter) {
    if (result == GPU_OK) {
      result = GPU_ERROR_BACKEND_FAILURE;
    }
    finish_webgpu_request(request, result, NULL, NULL);
    return;
  }

  request->adapter = adapter;
  result = GPURequestDevice(adapter, NULL, webgpu_device_ready, request);
  if (result != GPU_OK && !request->completed) {
    finish_webgpu_request(request, result, adapter, NULL);
  }
}

GPUResult
request_webgpu_device(GPUInstance        *instance,
                      WebGPURequest      *request,
                      WebGPUReadyCallback callback,
                      void               *userData) {
  GPUResult result;

  if (!instance || !request || !callback) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  request->callback  = callback;
  request->adapter   = NULL;
  request->userData  = userData;
  request->result    = GPU_OK;
  request->completed = false;

  result = GPURequestAdapter(instance, webgpu_adapter_ready, request);
  if (result != GPU_OK && !request->completed) {
    finish_webgpu_request(request, result, NULL, NULL);
  }
  return request->completed ? request->result : result;
}

int
resize_webgpu_canvas(GPUSwapchain *swapchain,
                     uint32_t     *width,
                     uint32_t     *height) {
  double   cssWidth;
  double   cssHeight;
  double   scale;
  uint32_t nextWidth;
  uint32_t nextHeight;
  int      layoutChanged;

  layoutChanged = prepare_canvas_layout_js();
  if (layoutChanged < 0) {
    return 0;
  }
  if (!layoutChanged && *width != 0u && *height != 0u) {
    return 1;
  }
  if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) !=
      EMSCRIPTEN_RESULT_SUCCESS) {
    return 0;
  }

  scale = emscripten_get_device_pixel_ratio();
  if (scale <= 0.0) {
    scale = 1.0;
  }
  nextWidth  = (uint32_t)(cssWidth * scale + 0.5);
  nextHeight = (uint32_t)(cssHeight * scale + 0.5);
  if (nextWidth == 0u || nextHeight == 0u) {
    return 0;
  }

  if ((layoutChanged || nextWidth != *width || nextHeight != *height) &&
      emscripten_set_element_css_size("#canvas",
                                      (double)nextWidth / scale,
                                      (double)nextHeight / scale) !=
        EMSCRIPTEN_RESULT_SUCCESS) {
    return 0;
  }
  if (nextWidth == *width && nextHeight == *height) {
    return 1;
  }

  if (emscripten_set_canvas_element_size("#canvas",
                                         (int)nextWidth,
                                         (int)nextHeight) !=
      EMSCRIPTEN_RESULT_SUCCESS) {
    return 0;
  }
  if (swapchain &&
      GPUResizeSwapchain(swapchain, nextWidth, nextHeight) != GPU_OK) {
    return 0;
  }

  *width  = nextWidth;
  *height = nextHeight;
  return 1;
}
