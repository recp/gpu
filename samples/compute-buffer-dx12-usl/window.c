#include <gpu/gpu.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct GeneratedVertex {
  float position[4];
  float color[4];
} GeneratedVertex;

typedef struct ComputeRenderApp {
  GPUInstance        *instance;
  GPUAdapter         *adapter;
  GPUDevice          *device;
  GPUCommandQueue    *queue;
  GPUSurface         *surface;
  GPUSwapchain       *swapchain;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *shaderLayout;
  GPUBuffer          *vertexBuffer;
  GPUBindGroup       *vertexBindGroup;
  GPUComputePipeline *computePipeline;
  GPURenderPipeline  *renderPipeline;
  HWND                window;
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
  uint32_t            exitAfterFrames;
  bool                ready;
  bool                running;
} ComputeRenderApp;

static ComputeRenderApp *compute_render_app;

static void
compute_render_log(const char *message) {
  fprintf(stderr, "GPU DX12 compute render: %s\n", message);
}

static bool
compute_render_loadArtifact(void **outData, uint64_t *outSize) {
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
           L"compute_buffer.us");

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
compute_render_createWindow(ComputeRenderApp *app, HINSTANCE instance) {
  WNDCLASSW windowClass = {0};
  RECT      rect;

  windowClass.lpfnWndProc   = DefWindowProcW;
  windowClass.hInstance     = instance;
  windowClass.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
  windowClass.lpszClassName = L"GPUUSLDX12ComputeRender";
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
                                L"GPU + USL Direct3D 12 Compute Render",
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
compute_render_createGPU(ComputeRenderApp *app) {
  GPUInstanceCreateInfo          instanceInfo = {0};
  GPUBindGroupLayout            *group1Layout;
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUBufferCreateInfo            bufferInfo = {0};
  GPUBindGroupEntry              groupEntry = {0};
  GPUBindGroupCreateInfo         groupInfo = {0};
  GPUComputePipelineCreateInfo   computeInfo = {0};
  GPUVertexAttribute             vertexAttributes[2] = {0};
  GPUVertexBufferLayout          vertexLayout = {0};
  GPUColorTargetState            colorTarget = {0};
  GPURenderPipelineCreateInfo    renderInfo = {0};
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
    compute_render_log("instance creation failed");
    return false;
  }

  adapterCount = 1u;
  result = GPUEnumerateAdapters(app->instance,
                                &adapterCount,
                                &app->adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !app->adapter) {
    compute_render_log("adapter enumeration failed");
    return false;
  }

  app->device = GPUCreateDeviceWithDefaultQueues(app->adapter);
  app->queue  = GPUGetQueue(app->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!app->device || !app->queue) {
    compute_render_log("device or graphics queue creation failed");
    return false;
  }

  app->surface = GPUCreateSurfaceFromNative(app->instance,
                                             app->adapter,
                                             app->window,
                                             GPU_SURFACE_WINDOWS_HWND,
                                             1.0f);
  if (!app->surface) {
    compute_render_log("surface creation failed");
    return false;
  }
  app->swapchain = GPUCreateSwapchainDefault(app->device,
                                              app->surface,
                                              app->width,
                                              app->height);
  if (!app->swapchain) {
    compute_render_log("swapchain creation failed");
    return false;
  }

  artifact     = NULL;
  artifactSize = 0u;
  if (!compute_render_loadArtifact(&artifact, &artifactSize)) {
    compute_render_log("compute_buffer.us was not found beside the executable");
    return false;
  }
  result = GPUCreateShaderLibraryFromUSL(app->device,
                                         artifact,
                                         artifactSize,
                                         &app->library);
  free(artifact);
  if (result != GPU_OK || !app->library) {
    compute_render_log("USL shader library creation failed");
    return false;
  }

  if (GPUCreateShaderLayout(app->device,
                            app->library,
                            &app->shaderLayout) != GPU_OK ||
      !app->shaderLayout || app->shaderLayout->bindGroupLayoutCount != 2u ||
      !app->shaderLayout->bindGroupLayouts[1] ||
      !app->shaderLayout->pipelineLayout) {
    compute_render_log("shader layout creation failed");
    return false;
  }

  group1Layout = app->shaderLayout->bindGroupLayouts[1];
  layoutEntries = GPUGetBindGroupLayoutEntries(group1Layout,
                                                &layoutEntryCount);
  if (!layoutEntries || layoutEntryCount != 1u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_STORAGE_BUFFER ||
      layoutEntries[0].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
      layoutEntries[0].arrayCount != 1u ||
      layoutEntries[0].hasDynamicOffset) {
    compute_render_log("unexpected shader reflection layout");
    return false;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "compute-render-dx12-vertices";
  bufferInfo.sizeBytes        = sizeof(GeneratedVertex) * 3u;
  bufferInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(app->device,
                      &bufferInfo,
                      &app->vertexBuffer) != GPU_OK ||
      !app->vertexBuffer) {
    compute_render_log("vertex/storage buffer creation failed");
    return false;
  }

  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntry.buffer.buffer = app->vertexBuffer;
  groupEntry.buffer.size   = bufferInfo.sizeBytes;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "compute-render-dx12-group1";
  groupInfo.layout           = group1Layout;
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &groupEntry;
  if (GPUCreateBindGroup(app->device,
                         &groupInfo,
                         &app->vertexBindGroup) != GPU_OK ||
      !app->vertexBindGroup) {
    compute_render_log("bind group creation failed");
    return false;
  }

  computeInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  computeInfo.chain.structSize = sizeof(computeInfo);
  computeInfo.label            = "compute-render-dx12-fill";
  computeInfo.layout           = app->shaderLayout->pipelineLayout;
  computeInfo.library          = app->library;
  computeInfo.entryPoint       = "fill_vertices";
  if (GPUCreateComputePipeline(app->device,
                               &computeInfo,
                               &app->computePipeline) != GPU_OK ||
      !app->computePipeline) {
    compute_render_log("compute pipeline creation failed");
    return false;
  }

  vertexAttributes[0].shaderLocation = 0u;
  vertexAttributes[0].format         = GPU_VERTEX_FORMAT_FLOAT4;
  vertexAttributes[0].offset         = offsetof(GeneratedVertex, position);
  vertexAttributes[1].shaderLocation = 1u;
  vertexAttributes[1].format         = GPU_VERTEX_FORMAT_FLOAT4;
  vertexAttributes[1].offset         = offsetof(GeneratedVertex, color);
  vertexLayout.strideBytes           = sizeof(GeneratedVertex);
  vertexLayout.stepMode              = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount        = 2u;
  vertexLayout.pAttributes           = vertexAttributes;
  colorTarget.format          = GPU_FORMAT_BGRA8_UNORM;
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;
  renderInfo.chain.sType =
    GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  renderInfo.chain.structSize         = sizeof(renderInfo);
  renderInfo.label                    = "compute-render-dx12-pipeline";
  renderInfo.layout                   = app->shaderLayout->pipelineLayout;
  renderInfo.library                  = app->library;
  renderInfo.vertexEntry              = "tri_vs";
  renderInfo.fragmentEntry            = "tri_fs";
  renderInfo.vertex.bufferLayoutCount = 1u;
  renderInfo.vertex.pBufferLayouts    = &vertexLayout;
  renderInfo.colorTargetCount         = 1u;
  renderInfo.pColorTargets            = &colorTarget;
  renderInfo.primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  renderInfo.cullMode                 = GPU_CULL_MODE_NONE;
  renderInfo.frontFace                = GPU_FRONT_FACE_CCW;
  renderInfo.multisample.sampleCount  = 1u;
  renderInfo.multisample.sampleMask   = 0xffffffffu;
  if (GPUCreateRenderPipeline(app->device,
                              &renderInfo,
                              &app->renderPipeline) != GPU_OK ||
      !app->renderPipeline) {
    compute_render_log("render pipeline creation failed");
    return false;
  }

  app->ready = true;
  return true;
}

static bool
compute_render_render(ComputeRenderApp *app) {
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPUComputePassEncoder        *compute;
  GPURenderPassEncoder         *render;
  GPUBufferBarrier              vertexBarrier = {0};
  GPUBarrierBatch               barrierBatch = {0};
  GPUBufferBinding              vertexBinding = {0};
  GPURenderPassColorAttachment  color = {0};
  GPURenderPassCreateInfo       passInfo = {0};

  frame = GPUBeginFrame(app->swapchain);
  if (!frame) {
    return false;
  }

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(app->queue,
                              "compute-render-dx12-usl-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return false;
  }

  compute = GPUBeginComputePass(cmdb, "compute-render-dx12-fill");
  if (!compute) {
    GPUEndFrame(frame);
    return false;
  }
  GPUBindComputePipeline(compute, app->computePipeline);
  GPUBindComputeGroup(compute, 1u, app->vertexBindGroup, 0u, NULL);
  GPUDispatch(compute, 3u, 1u, 1u);
  GPUEndComputePass(compute);

  vertexBarrier.buffer              = app->vertexBuffer;
  vertexBarrier.srcAccess           = GPU_ACCESS_SHADER_WRITE;
  vertexBarrier.dstAccess           = GPU_ACCESS_SHADER_READ;
  vertexBarrier.sizeBytes           = sizeof(GeneratedVertex) * 3u;
  barrierBatch.srcStages          = GPU_STAGE_COMPUTE;
  barrierBatch.dstStages          = GPU_STAGE_VERTEX;
  barrierBatch.bufferBarrierCount = 1u;
  barrierBatch.pBufferBarriers     = &vertexBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.02f;
  color.clearColor.float32[1] = 0.025f;
  color.clearColor.float32[2] = 0.035f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType =
    GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize       = sizeof(passInfo);
  passInfo.label                  = "compute-render-dx12-usl-pass";
  passInfo.colorAttachmentCount   = 1u;
  passInfo.pColorAttachments      = &color;

  render = GPUBeginRenderPass(cmdb, &passInfo);
  if (!render) {
    GPUEndFrame(frame);
    return false;
  }

  vertexBinding.buffer = app->vertexBuffer;
  GPUBindRenderPipeline(render, app->renderPipeline);
  GPUBindVertexBuffers(render, 0u, 1u, &vertexBinding);
  GPUDraw(render, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(render);
  if (GPUFinishFrame(app->queue, cmdb, frame) != GPU_OK) {
    return false;
  }

  app->frameCount++;
  if (app->exitAfterFrames > 0u &&
      app->frameCount >= app->exitAfterFrames) {
    app->running = false;
  }
  return true;
}

static void
compute_render_destroyGPU(ComputeRenderApp *app) {
  GPUDestroyRenderPipeline(app->renderPipeline);
  GPUDestroyComputePipeline(app->computePipeline);
  GPUDestroyBindGroup(app->vertexBindGroup);
  GPUDestroyBuffer(app->vertexBuffer);
  GPUDestroyShaderLayout(app->shaderLayout);
  GPUDestroyShaderLibrary(app->library);
  GPUDestroySwapchain(app->swapchain);
  GPUDestroySurface(app->surface);
  GPUDestroyDevice(app->device);
  GPUDestroyInstance(app->instance);
}

static bool
compute_render_waitForGPU(ComputeRenderApp *app) {
  GPUCommandBuffer   *buffers[1];
  GPUCommandBuffer   *cmdb;
  GPUFence           *fence;
  GPUFenceCreateInfo  fenceInfo = {0};
  GPUQueueSubmitInfo  submitInfo = {0};
  GPUResult           result;

  fenceInfo.chain.sType      = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fenceInfo.label            = "compute-render-dx12-usl-shutdown";
  if (GPUCreateFence(app->device, &fenceInfo, &fence) != GPU_OK) {
    return false;
  }

  cmdb = NULL;
  result = GPUAcquireCommandBuffer(app->queue,
                                   "compute-render-dx12-usl-drain",
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
compute_render_windowProc(HWND   window,
                          UINT   message,
                          WPARAM wparam,
                          LPARAM lparam) {
  switch (message) {
    case WM_CLOSE:
      if (compute_render_app) {
        compute_render_app->running = false;
      }
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_SIZE:
      if (compute_render_app && compute_render_app->ready &&
          wparam != SIZE_MINIMIZED) {
        uint32_t width;
        uint32_t height;

        width  = LOWORD(lparam);
        height = HIWORD(lparam);
        if (width > 0u && height > 0u &&
            GPUResizeSwapchain(compute_render_app->swapchain,
                               width,
                               height) == GPU_OK) {
          compute_render_app->width  = width;
          compute_render_app->height = height;
        }
      }
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

int
main(void) {
  ComputeRenderApp app = {0};
  HINSTANCE       instance;
  WNDPROC         previousProc;
  MSG             message;
  const char     *exitFrames;
  int             result;

  instance           = GetModuleHandleW(NULL);
  compute_render_app = &app;
  if (!compute_render_createWindow(&app, instance)) {
    compute_render_log("window creation failed");
    return 1;
  }

  previousProc = (WNDPROC)SetWindowLongPtrW(app.window,
                                             GWLP_WNDPROC,
                                             (LONG_PTR)compute_render_windowProc);
  if (!previousProc || !compute_render_createGPU(&app)) {
    compute_render_destroyGPU(&app);
    DestroyWindow(app.window);
    return 1;
  }

  exitFrames = getenv("GPU_SAMPLE_EXIT_AFTER_FRAMES");
  if (exitFrames) {
    app.exitAfterFrames = (uint32_t)strtoul(exitFrames, NULL, 10);
  }
  app.running = true;
  result      = 0;
  while (app.running) {
    while (PeekMessageW(&message, NULL, 0u, 0u, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        app.running = false;
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (app.running && !compute_render_render(&app)) {
      compute_render_log("frame rendering failed");
      result      = 1;
      app.running = false;
    }
  }

  if (!compute_render_waitForGPU(&app)) {
    compute_render_log("queue drain failed");
    result = 1;
  }
  compute_render_destroyGPU(&app);
  if (IsWindow(app.window)) {
    DestroyWindow(app.window);
  }
  compute_render_app = NULL;
  return result;
}
