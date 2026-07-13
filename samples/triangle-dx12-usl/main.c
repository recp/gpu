#include <gpu/gpu.h>

#include "../common/SampleStats.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct FragmentUniforms {
  float tint[4];
} FragmentUniforms;

typedef struct TriangleApp {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUCommandQueue   *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPUBuffer         *uniformBuffer;
  GPUBindGroup      *bindGroup;
  GPURenderPipeline *pipeline;
  HWND               window;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
  uint32_t           exitAfterFrames;
  bool               assertZeroAlloc;
  bool               ready;
  bool               running;
} TriangleApp;

static TriangleApp *triangle_app;

static void
triangle_log(const char *message) {
  fprintf(stderr, "GPU DX12 triangle: %s\n", message);
}

static bool
triangle_loadArtifact(void **outData, uint64_t *outSize) {
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
           L"triangle.us");

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
triangle_createWindow(TriangleApp *app, HINSTANCE instance) {
  WNDCLASSW windowClass = {0};
  RECT      rect;

  windowClass.lpfnWndProc   = DefWindowProcW;
  windowClass.hInstance     = instance;
  windowClass.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
  windowClass.lpszClassName = L"GPUUSLDX12Triangle";
  if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  rect.left   = 0;
  rect.top    = 0;
  rect.right  = 960;
  rect.bottom = 640;
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  app->window = CreateWindowExW(0,
                                windowClass.lpszClassName,
                                L"GPU + USL Direct3D 12 Triangle",
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
triangle_createGPU(TriangleApp *app) {
  GPUInstanceCreateInfo          instanceInfo = {0};
  GPUBindGroupLayout            *groupLayout;
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUBufferCreateInfo            bufferInfo = {0};
  GPUBindGroupEntry              groupEntry = {0};
  GPUBindGroupCreateInfo         groupInfo = {0};
  GPUColorTargetState            colorTarget = {0};
  GPURenderPipelineCreateInfo    pipelineInfo = {0};
  FragmentUniforms               uniforms = {{0.15f, 0.78f, 0.34f, 1.0f}};
  GPUResult                      result;
  void                          *artifact;
  uint64_t                       artifactSize;
  uint32_t                       adapterCount;
  uint32_t                       layoutEntryCount;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &app->instance) != GPU_OK ||
      !app->instance) {
    triangle_log("instance creation failed");
    return false;
  }

  adapterCount = 1u;
  result = GPUEnumerateAdapters(app->instance,
                                &adapterCount,
                                &app->adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !app->adapter) {
    triangle_log("adapter enumeration failed");
    return false;
  }

  app->device = GPUCreateDeviceWithDefaultQueues(app->adapter);
  app->queue  = GPUGetQueue(app->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!app->device || !app->queue) {
    triangle_log("device or graphics queue creation failed");
    return false;
  }

  app->surface = GPUCreateSurfaceFromNative(app->instance,
                                             app->adapter,
                                             app->window,
                                             GPU_SURFACE_WINDOWS_HWND,
                                             1.0f);
  if (!app->surface) {
    triangle_log("surface creation failed");
    return false;
  }
  app->swapchain = GPUCreateSwapchainDefault(app->device,
                                              app->surface,
                                              app->width,
                                              app->height);
  if (!app->swapchain) {
    triangle_log("swapchain creation failed");
    return false;
  }

  artifact     = NULL;
  artifactSize = 0u;
  if (!triangle_loadArtifact(&artifact, &artifactSize)) {
    triangle_log("triangle.us was not found beside the executable");
    return false;
  }
  result = GPUCreateShaderLibraryFromUSL(app->device,
                                         artifact,
                                         artifactSize,
                                         &app->library);
  free(artifact);
  if (result != GPU_OK || !app->library) {
    triangle_log("USL shader library creation failed");
    return false;
  }

  if (GPUCreateShaderLayout(app->device,
                            app->library,
                            &app->shaderLayout) != GPU_OK ||
      !app->shaderLayout || app->shaderLayout->bindGroupLayoutCount != 1u ||
      !app->shaderLayout->pipelineLayout) {
    triangle_log("shader layout creation failed");
    return false;
  }

  groupLayout = app->shaderLayout->bindGroupLayouts[0];
  layoutEntries = GPUGetBindGroupLayoutEntries(groupLayout,
                                                &layoutEntryCount);
  if (!layoutEntries || layoutEntryCount != 1u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_UNIFORM_BUFFER ||
      layoutEntries[0].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      layoutEntries[0].arrayCount != 1u ||
      layoutEntries[0].hasDynamicOffset) {
    triangle_log("unexpected shader reflection layout");
    return false;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "triangle-dx12-uniforms";
  bufferInfo.sizeBytes        = sizeof(uniforms);
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(app->device,
                      &bufferInfo,
                      &app->uniformBuffer) != GPU_OK ||
      !app->uniformBuffer ||
      GPUQueueWriteBuffer(app->queue,
                          app->uniformBuffer,
                          0u,
                          &uniforms,
                          sizeof(uniforms)) != GPU_OK) {
    triangle_log("uniform buffer creation failed");
    return false;
  }

  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  groupEntry.buffer.buffer = app->uniformBuffer;
  groupEntry.buffer.size   = sizeof(uniforms);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "triangle-dx12-group0";
  groupInfo.layout           = groupLayout;
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &groupEntry;
  if (GPUCreateBindGroup(app->device,
                         &groupInfo,
                         &app->bindGroup) != GPU_OK ||
      !app->bindGroup) {
    triangle_log("bind group creation failed");
    return false;
  }

  colorTarget.format          = GPU_FORMAT_BGRA8_UNORM;
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;
  pipelineInfo.chain.sType       = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize  = sizeof(pipelineInfo);
  pipelineInfo.label             = "triangle-dx12-usl-pipeline";
  pipelineInfo.layout            = app->shaderLayout->pipelineLayout;
  pipelineInfo.library           = app->library;
  pipelineInfo.vertexEntry       = "tri_vs";
  pipelineInfo.fragmentEntry     = "tri_fs";
  pipelineInfo.colorTargetCount  = 1u;
  pipelineInfo.pColorTargets     = &colorTarget;
  pipelineInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode          = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace         = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = 0xffffffffu;
  if (GPUCreateRenderPipeline(app->device,
                              &pipelineInfo,
                              &app->pipeline) != GPU_OK ||
      !app->pipeline) {
    triangle_log("render pipeline creation failed");
    return false;
  }

  app->ready = true;
  return true;
}

static bool
triangle_render(TriangleApp *app) {
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPURenderPassColorAttachment  color = {0};
  GPURenderPassCreateInfo       passInfo = {0};

  frame = GPUBeginFrame(app->swapchain);
  if (!frame) {
    return false;
  }

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(app->queue,
                              "triangle-dx12-usl-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return false;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.02f;
  color.clearColor.float32[1] = 0.025f;
  color.clearColor.float32[2] = 0.035f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType        = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize   = sizeof(passInfo);
  passInfo.label              = "triangle-dx12-usl-pass";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments  = &color;

  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    GPUEndFrame(frame);
    return false;
  }

  GPUBindRenderPipeline(pass, app->pipeline);
  GPUBindRenderGroup(pass, 0u, app->bindGroup, 0u, NULL);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(app->queue, cmdb, frame) != GPU_OK) {
    return false;
  }

  app->frameCount++;
  if (!GPUSampleCheckZeroAlloc(app->device,
                               app->frameCount,
                               app->assertZeroAlloc,
                               "GPU DX12 triangle")) {
    return false;
  }
  if (app->exitAfterFrames > 0u &&
      app->frameCount >= app->exitAfterFrames) {
    app->running = false;
  }
  return true;
}

static void
triangle_destroyGPU(TriangleApp *app) {
  GPUDestroyRenderPipeline(app->pipeline);
  GPUDestroyBindGroup(app->bindGroup);
  GPUDestroyBuffer(app->uniformBuffer);
  GPUDestroyShaderLayout(app->shaderLayout);
  GPUDestroyShaderLibrary(app->library);
  GPUDestroySwapchain(app->swapchain);
  GPUDestroySurface(app->surface);
  GPUDestroyDevice(app->device);
  GPUDestroyInstance(app->instance);
}

static bool
triangle_waitForGPU(TriangleApp *app) {
  GPUCommandBuffer   *buffers[1];
  GPUCommandBuffer   *cmdb;
  GPUFence           *fence;
  GPUFenceCreateInfo  fenceInfo = {0};
  GPUQueueSubmitInfo  submitInfo = {0};
  GPUResult           result;

  fenceInfo.chain.sType      = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fenceInfo.label            = "triangle-dx12-usl-shutdown";
  if (GPUCreateFence(app->device, &fenceInfo, &fence) != GPU_OK) {
    return false;
  }

  cmdb = NULL;
  result = GPUAcquireCommandBuffer(app->queue,
                                   "triangle-dx12-usl-drain",
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

static LRESULT CALLBACK
triangle_windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CLOSE:
      if (triangle_app) {
        triangle_app->running = false;
      }
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_SIZE:
      if (triangle_app && triangle_app->ready && wparam != SIZE_MINIMIZED) {
        uint32_t width;
        uint32_t height;

        width  = LOWORD(lparam);
        height = HIWORD(lparam);
        if (width > 0u && height > 0u &&
            GPUResizeSwapchain(triangle_app->swapchain,
                               width,
                               height) == GPU_OK) {
          triangle_app->width  = width;
          triangle_app->height = height;
        }
      }
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

int
main(void) {
  TriangleApp app = {0};
  HINSTANCE   instance;
  WNDPROC     previousProc;
  MSG         message;
  const char *exitFrames;
  int         result;

  instance    = GetModuleHandleW(NULL);
  triangle_app = &app;
  if (!triangle_createWindow(&app, instance)) {
    triangle_log("window creation failed");
    return 1;
  }

  previousProc = (WNDPROC)SetWindowLongPtrW(app.window,
                                             GWLP_WNDPROC,
                                             (LONG_PTR)triangle_windowProc);
  if (!previousProc || !triangle_createGPU(&app)) {
    triangle_destroyGPU(&app);
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
    if (app.running && !triangle_render(&app)) {
      triangle_log("frame rendering failed");
      result      = 1;
      app.running = false;
    }
  }

  if (!triangle_waitForGPU(&app)) {
    triangle_log("queue drain failed");
    result = 1;
  }
  triangle_destroyGPU(&app);
  if (IsWindow(app.window)) {
    DestroyWindow(app.window);
  }
  triangle_app = NULL;
  return result;
}
