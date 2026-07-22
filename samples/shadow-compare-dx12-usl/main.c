#include <gpu/gpu.h>

#include "../common/ShadowCompare.h"
#include "../common/SampleStats.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../common/Win32Sample.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct ShadowCompareApp {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface            *surface;
  GPUSwapchain          *swapchain;
  HWND                   window;
  GPUSampleShadowCompare  renderer;
  uint32_t               width;
  uint32_t               height;
  uint32_t               exitAfterFrames;
  bool                   assertZeroAlloc;
  bool                   ready;
  bool                   running;
} ShadowCompareApp;

static ShadowCompareApp *shadow_compare_app;

static void
shadow_compare_log(const char *message) {
  fprintf(stderr, "GPU DX12 shadow compare: %s\n", message);
}

static bool
shadow_compare_loadArtifact(void **outData, uint64_t *outSize) {
  wchar_t path[MAX_PATH];
  wchar_t *slash;
  FILE    *file;
  void    *data;
  long     size;

  if (!outData || !outSize ||
      GetModuleFileNameW(NULL, path, (DWORD)GPU_ARRAY_LEN(path)) == 0u) {
    return false;
  }

  slash = wcsrchr(path, L'\\');
  if (!slash) {
    return false;
  }
  wcscpy_s(slash + 1u,
           GPU_ARRAY_LEN(path) - (size_t)(slash + 1u - path),
           L"shadow_compare.us");

  file = NULL;
  if (_wfopen_s(&file, path, L"rb") != 0 || !file) {
    return false;
  }
  if (fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return false;
  }

  data = malloc((size_t)size);
  if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return false;
  }

  fclose(file);
  *outData = data;
  *outSize = (uint64_t)size;
  return true;
}

static bool
shadow_compare_createWindow(ShadowCompareApp *app, HINSTANCE instance) {
  WNDCLASSW windowClass = {0};
  RECT      rect;

  windowClass.lpfnWndProc   = DefWindowProcW;
  windowClass.hInstance     = instance;
  windowClass.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
  windowClass.lpszClassName = L"GPUUSLDX12ShadowCompare";
  if (!RegisterClassW(&windowClass) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  rect.left   = 0;
  rect.top    = 0;
  rect.right  = 960;
  rect.bottom = 640;
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  app->window = CreateWindowExW(0,
                                windowClass.lpszClassName,
                                L"GPU + USL Direct3D 12 Shadow Compare",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                rect.right - rect.left,
                                rect.bottom - rect.top,
                                NULL,
                                NULL,
                                instance,
                                NULL);
  if (!app->window) {
    return false;
  }

  app->width  = 960u;
  app->height = 640u;
  ShowWindow(app->window, SW_SHOWDEFAULT);
  UpdateWindow(app->window);
  return true;
}

static bool
shadow_compare_createGPU(ShadowCompareApp *app) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUShaderLibrary     *library;
  GPUShaderLayout      *shaderLayout;
  GPUResult             result;
  void                 *artifact;
  uint64_t              artifactSize;
  uint32_t              adapterCount;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.label            = "shadow-compare-dx12-usl";
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &app->instance) != GPU_OK ||
      !app->instance) {
    return false;
  }

  adapterCount = 1u;
  result = GPUEnumerateAdapters(app->instance,
                                &adapterCount,
                                &app->adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !app->adapter) {
    return false;
  }

  app->device = GPUCreateDeviceWithDefaultQueues(app->adapter);
  if (!app->device) {
    return false;
  }
  app->queue = GPUGetQueue(app->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!app->queue) {
    return false;
  }

  app->surface = GPUCreateSurfaceFromNative(app->instance,
                                             app->adapter,
                                             app->window,
                                             GPU_SURFACE_WINDOWS_HWND,
                                             1.0f);
  if (!app->surface) {
    return false;
  }
  app->swapchain = GPUCreateSwapchainDefault(app->device,
                                              app->surface,
                                              app->width,
                                              app->height);
  if (!app->swapchain) {
    return false;
  }

  artifact     = NULL;
  artifactSize = 0u;
  if (!shadow_compare_loadArtifact(&artifact, &artifactSize)) {
    return false;
  }
  library = NULL;
  result = GPUCreateShaderLibraryFromUSL(app->device,
                                         artifact,
                                         artifactSize,
                                         &library);
  free(artifact);
  if (result != GPU_OK || !library) {
    return false;
  }

  shaderLayout = NULL;
  result = GPUCreateShaderLayout(app->device, library, &shaderLayout);
  if (result != GPU_OK || !shaderLayout) {
    GPUDestroyShaderLibrary(library);
    return false;
  }

  result = GPUSampleShadowCompareInit(&app->renderer,
                                      app->device,
                                      app->queue,
                                      app->swapchain,
                                      library,
                                      shaderLayout,
                                      app->width,
                                      app->height);
  app->ready = result == GPU_OK;
  return app->ready;
}

static bool
shadow_compare_render(ShadowCompareApp *app) {
  GPUResult result;

  result = GPUSampleShadowCompareRender(&app->renderer, NULL, NULL);
  if (result != GPU_OK) {
    return false;
  }
  if (!GPUSampleCheckZeroAlloc(app->device,
                               app->renderer.frameCount,
                               app->assertZeroAlloc,
                               "GPU DX12 shadow compare")) {
    return false;
  }
  if (app->exitAfterFrames > 0u &&
      app->renderer.frameCount >= app->exitAfterFrames) {
    app->running = false;
  }
  return true;
}

static bool
shadow_compare_waitForGPU(ShadowCompareApp *app) {
  GPUCommandBuffer   *buffers[1];
  GPUCommandBuffer   *cmdb;
  GPUFence           *fence;
  GPUFenceCreateInfo  fenceInfo = {0};
  GPUQueueSubmitInfo  submitInfo = {0};
  GPUResult           result;

  fenceInfo.chain.sType      = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fenceInfo.label            = "shadow-compare-dx12-shutdown";
  if (GPUCreateFence(app->device, &fenceInfo, &fence) != GPU_OK) {
    return false;
  }

  cmdb = NULL;
  result = GPUAcquireCommandBuffer(app->queue,
                                   "shadow-compare-dx12-drain",
                                   &cmdb);
  if (result != GPU_OK || !cmdb) {
    GPUDestroyFence(fence);
    return false;
  }

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = fence;
  result = GPUQueueSubmit(app->queue, &submitInfo);
  if (result == GPU_OK) {
    result = GPUWaitFence(fence, UINT64_MAX);
  }
  GPUDestroyFence(fence);
  return result == GPU_OK;
}

static void
shadow_compare_destroyGPU(ShadowCompareApp *app) {
  GPUSampleShadowCompareDestroy(&app->renderer);
  GPUDestroySwapchain(app->swapchain);
  GPUDestroySurface(app->surface);
  GPUDestroyDevice(app->device);
  GPUDestroyInstance(app->instance);
}

static LRESULT CALLBACK
shadow_compare_windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CLOSE:
      if (shadow_compare_app) {
        shadow_compare_app->running = false;
      }
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_SIZE:
      if (shadow_compare_app && shadow_compare_app->ready &&
          wparam != SIZE_MINIMIZED) {
        uint32_t width;
        uint32_t height;

        width  = LOWORD(lparam);
        height = HIWORD(lparam);
        if (width > 0u && height > 0u &&
            GPUResizeSwapchain(shadow_compare_app->swapchain,
                               width,
                               height) == GPU_OK &&
            GPUSampleShadowCompareResize(&shadow_compare_app->renderer,
                                         width,
                                         height) == GPU_OK) {
          shadow_compare_app->width  = width;
          shadow_compare_app->height = height;
        } else {
          shadow_compare_app->running = false;
        }
      }
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

int
main(void) {
  ShadowCompareApp  app = {0};
  HINSTANCE         instance;
  WNDPROC           previousProc;
  MSG               message;
  const char       *exitFrames;
  int               result;

  if (GPUSampleShouldSkipNonInteractive()) {
    return GPU_SAMPLE_SKIP_RETURN_CODE;
  }

  instance           = GetModuleHandleW(NULL);
  shadow_compare_app = &app;
  if (!shadow_compare_createWindow(&app, instance)) {
    shadow_compare_log("window creation failed");
    return 1;
  }

  previousProc = (WNDPROC)SetWindowLongPtrW(app.window,
                                             GWLP_WNDPROC,
                                             (LONG_PTR)shadow_compare_windowProc);
  if (!previousProc || !shadow_compare_createGPU(&app)) {
    shadow_compare_log("GPU initialization failed");
    shadow_compare_destroyGPU(&app);
    DestroyWindow(app.window);
    return 1;
  }

  exitFrames = getenv("GPU_SAMPLE_EXIT_AFTER_FRAMES");
  if (exitFrames) {
    app.exitAfterFrames = (uint32_t)strtoul(exitFrames, NULL, 10);
  }
  app.assertZeroAlloc = GPUSampleEnvEnabled("GPU_SAMPLE_ASSERT_ZERO_ALLOC");
  app.running         = true;
  result              = 0;
  while (app.running) {
    while (PeekMessageW(&message, NULL, 0u, 0u, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        app.running = false;
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (app.running && !shadow_compare_render(&app)) {
      shadow_compare_log("frame rendering failed");
      result      = 1;
      app.running = false;
    }
  }

  if (!shadow_compare_waitForGPU(&app)) {
    shadow_compare_log("queue drain failed");
    result = 1;
  }
  shadow_compare_destroyGPU(&app);
  if (IsWindow(app.window)) {
    DestroyWindow(app.window);
  }
  shadow_compare_app = NULL;
  return result;
}
