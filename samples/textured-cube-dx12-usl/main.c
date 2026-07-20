#include <gpu/gpu.h>

#include "../common/SampleStats.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../common/Win32Sample.h"
#include "../textured-cube-usl/CubeData.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct TexturedCubeApp {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  GPUBuffer         *vertexBuffer;
  GPUBuffer         *indexBuffer;
  GPUBuffer         *uniformBuffer;
  GPUTexture        *texture;
  GPUTextureView    *textureView;
  GPUTexture        *depthTexture;
  GPUTextureView    *depthView;
  GPUSampler        *sampler;
  GPUBindGroup      *materialGroup;
  GPUBindGroup      *samplerGroup;
  HWND               window;
  LARGE_INTEGER      animationStart;
  double             secondsPerTick;
  mat4               viewProjection;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
  uint32_t           exitAfterFrames;
  bool               assertZeroAlloc;
  bool               ready;
  bool               running;
} TexturedCubeApp;

static TexturedCubeApp *textured_cube_app;

static void
textured_cube_log(const char *message) {
  fprintf(stderr, "GPU DX12 textured cube: %s\n", message);
}

static bool
textured_cube_loadArtifact(void **outData, uint64_t *outSize) {
  wchar_t  path[MAX_PATH];
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
           L"textured_cube.us");

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
textured_cube_createDepthTarget(TexturedCubeApp *app,
                                uint32_t         width,
                                uint32_t         height) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTexture              *texture;
  GPUTextureView          *view;

  texture = NULL;
  view    = NULL;
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "textured-cube-depth";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  textureInfo.width            = width;
  textureInfo.height           = height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_DEPTH_STENCIL;
  if (GPUCreateTexture(app->device, &textureInfo, &texture) != GPU_OK) {
    return false;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "textured-cube-depth-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK) {
    GPUDestroyTexture(texture);
    return false;
  }

  GPUDestroyTextureView(app->depthView);
  GPUDestroyTexture(app->depthTexture);
  app->depthTexture = texture;
  app->depthView    = view;
  return true;
}

static bool
textured_cube_createPipeline(TexturedCubeApp *app) {
  GPUVertexAttribute          attributes[3] = {0};
  GPUVertexBufferLayout       vertexLayout  = {0};
  GPUColorTargetState         color         = {0};
  GPUDepthStencilState        depth         = {0};
  GPURenderPipelineCreateInfo info          = {0};

  attributes[0].format          = GPU_VERTEX_FORMAT_FLOAT32X3;
  attributes[0].offset          = offsetof(CubeVertex, position);
  attributes[0].shaderLocation = 0u;
  attributes[1].format          = GPU_VERTEX_FORMAT_FLOAT32X3;
  attributes[1].offset          = offsetof(CubeVertex, normal);
  attributes[1].shaderLocation = 1u;
  attributes[2].format          = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[2].offset          = offsetof(CubeVertex, uv);
  attributes[2].shaderLocation = 2u;
  vertexLayout.pAttributes      = attributes;
  vertexLayout.strideBytes      = sizeof(CubeVertex);
  vertexLayout.attributeCount   = 3u;
  vertexLayout.stepMode         = GPU_VERTEX_STEP_MODE_VERTEX;

  color.format          = GPUGetSwapchainFormat(app->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  depth.depthCompare     = GPU_COMPARE_LESS;
  depth.depthTestEnable  = true;
  depth.depthWriteEnable = true;

  info.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize         = sizeof(info);
  info.label                    = "textured-cube-usl-pipeline";
  info.layout                   = app->shaderLayout->pipelineLayout;
  info.library                  = app->library;
  info.vertexEntry              = "cube_vs";
  info.fragmentEntry            = "cube_fs";
  info.pColorTargets            = &color;
  info.pDepthStencilState       = &depth;
  info.vertex.pBufferLayouts    = &vertexLayout;
  info.vertex.bufferLayoutCount = 1u;
  info.colorTargetCount         = 1u;
  info.depthStencilFormat       = GPU_FORMAT_DEPTH32_FLOAT;
  info.primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                 = GPU_CULL_MODE_BACK;
  info.frontFace                = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount  = 1u;
  info.multisample.sampleMask   = UINT32_MAX;
  return GPUCreateRenderPipeline(app->device,
                                 &info,
                                 &app->pipeline) == GPU_OK;
}

static bool
textured_cube_createGeometry(TexturedCubeApp *app) {
  CubeUniforms        uniforms;
  GPUBufferCreateInfo info = {0};

  CubeBuildUniforms(0.0f, app->viewProjection, &uniforms);

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "textured-cube-vertices";
  info.sizeBytes        = sizeof(kCubeVertices);
  info.usage            = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(app->device, &info, &app->vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(app->queue,
                          app->vertexBuffer,
                          0u,
                          kCubeVertices,
                          sizeof(kCubeVertices)) != GPU_OK) {
    return false;
  }

  info.label     = "textured-cube-indices";
  info.sizeBytes = sizeof(kCubeIndices);
  info.usage     = GPU_BUFFER_USAGE_INDEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(app->device, &info, &app->indexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(app->queue,
                          app->indexBuffer,
                          0u,
                          kCubeIndices,
                          sizeof(kCubeIndices)) != GPU_OK) {
    return false;
  }

  info.label     = "textured-cube-uniforms";
  info.sizeBytes = sizeof(uniforms);
  info.usage     = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(app->device, &info, &app->uniformBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(app->queue,
                          app->uniformBuffer,
                          0u,
                          &uniforms,
                          sizeof(uniforms)) != GPU_OK) {
    return false;
  }
  return true;
}

static bool
textured_cube_createMaterial(TexturedCubeApp *app) {
  uint8_t                  pixels[CUBE_CHECKER_SIZE * CUBE_CHECKER_SIZE * 4u];
  GPUTextureCreateInfo     textureInfo      = {0};
  GPUTextureWriteRegion    writeRegion      = {0};
  GPUTextureViewCreateInfo viewInfo         = {0};
  GPUSamplerCreateInfo     samplerInfo      = {0};
  GPUBindGroupEntry        materialEntries[2] = {0};
  GPUBindGroupEntry        samplerEntry     = {0};
  GPUBindGroupCreateInfo   materialInfo     = {0};
  GPUBindGroupCreateInfo   samplerGroupInfo = {0};

  CubeFillChecker(pixels);
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "textured-cube-checker";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = CUBE_CHECKER_SIZE;
  textureInfo.height           = CUBE_CHECKER_SIZE;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(app->device, &textureInfo, &app->texture) != GPU_OK) {
    return false;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = CUBE_CHECKER_SIZE;
  writeRegion.height       = CUBE_CHECKER_SIZE;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = CUBE_CHECKER_SIZE * 4u;
  writeRegion.rowsPerImage = CUBE_CHECKER_SIZE;
  if (GPUQueueWriteTexture(app->queue,
                           app->texture,
                           &writeRegion,
                           pixels,
                           sizeof(pixels)) != GPU_OK) {
    return false;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "textured-cube-checker-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(app->texture,
                           &viewInfo,
                           &app->textureView) != GPU_OK) {
    return false;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "textured-cube-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_REPEAT;
  if (GPUCreateSampler(app->device,
                       &samplerInfo,
                       false,
                       &app->sampler) != GPU_OK) {
    return false;
  }

  materialEntries[0].buffer.buffer = app->uniformBuffer;
  materialEntries[0].buffer.size   = sizeof(CubeUniforms);
  materialEntries[0].binding       = 0u;
  materialEntries[0].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  materialEntries[1].textureView   = app->textureView;
  materialEntries[1].binding       = 1u;
  materialEntries[1].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  materialInfo.chain.sType         = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  materialInfo.chain.structSize    = sizeof(materialInfo);
  materialInfo.label               = "textured-cube-group0";
  materialInfo.layout              = app->shaderLayout->bindGroupLayouts[0];
  materialInfo.pEntries            = materialEntries;
  materialInfo.entryCount          = 2u;
  if (GPUCreateBindGroup(app->device,
                         &materialInfo,
                         &app->materialGroup) != GPU_OK) {
    return false;
  }

  samplerEntry.sampler              = app->sampler;
  samplerEntry.binding              = 0u;
  samplerEntry.bindingType          = GPU_BINDING_SAMPLER;
  samplerGroupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  samplerGroupInfo.chain.structSize = sizeof(samplerGroupInfo);
  samplerGroupInfo.label            = "textured-cube-group1";
  samplerGroupInfo.layout           = app->shaderLayout->bindGroupLayouts[1];
  samplerGroupInfo.pEntries         = &samplerEntry;
  samplerGroupInfo.entryCount       = 1u;
  return GPUCreateBindGroup(app->device,
                            &samplerGroupInfo,
                            &app->samplerGroup) == GPU_OK;
}

static bool
textured_cube_createGPU(TexturedCubeApp *app) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPURuntimeConfig      runtime      = {0};
  GPUResult             result;
  void                 *artifact;
  uint64_t              artifactSize;
  uint32_t              adapterCount;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &app->instance) != GPU_OK ||
      !app->instance) {
    textured_cube_log("instance creation failed");
    return false;
  }

  adapterCount = 1u;
  result = GPUEnumerateAdapters(app->instance,
                                &adapterCount,
                                &app->adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !app->adapter) {
    textured_cube_log("adapter enumeration failed");
    return false;
  }

  app->device = GPUCreateDeviceWithDefaultQueues(app->adapter);
  if (!app->device) {
    textured_cube_log("device creation failed");
    return false;
  }
  app->queue = GPUGetQueue(app->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!app->queue) {
    textured_cube_log("graphics queue creation failed");
    return false;
  }

  runtime.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtime.chain.structSize = sizeof(runtime);
  runtime.validationMode   = GPU_VALIDATION_FULL;
  runtime.enableStats      = true;
  if (GPUConfigureRuntime(app->device, &runtime) != GPU_OK) {
    textured_cube_log("runtime stats configuration failed");
    return false;
  }

  app->surface = GPUCreateSurfaceFromNative(app->instance,
                                             app->adapter,
                                             app->window,
                                             GPU_SURFACE_WINDOWS_HWND,
                                             1.0f);
  if (!app->surface) {
    textured_cube_log("surface creation failed");
    return false;
  }
  app->swapchain = GPUCreateSwapchainDefault(app->device,
                                              app->surface,
                                              app->width,
                                              app->height);
  if (!app->swapchain) {
    textured_cube_log("swapchain creation failed");
    return false;
  }

  artifact     = NULL;
  artifactSize = 0u;
  if (!textured_cube_loadArtifact(&artifact, &artifactSize)) {
    textured_cube_log("textured_cube.us was not found beside the executable");
    return false;
  }
  result = GPUCreateShaderLibraryFromUSL(app->device,
                                         artifact,
                                         artifactSize,
                                         &app->library);
  free(artifact);
  if (result != GPU_OK || !app->library) {
    textured_cube_log("USL shader library creation failed");
    return false;
  }
  if (GPUCreateShaderLayout(app->device,
                            app->library,
                            &app->shaderLayout) != GPU_OK ||
      !app->shaderLayout ||
      app->shaderLayout->bindGroupLayoutCount != 2u ||
      !app->shaderLayout->bindGroupLayouts ||
      !app->shaderLayout->bindGroupLayouts[0] ||
      !app->shaderLayout->bindGroupLayouts[1] ||
      !app->shaderLayout->pipelineLayout) {
    textured_cube_log("shader layout creation failed");
    return false;
  }

  CubeBuildViewProjection(app->width,
                          app->height,
                          app->viewProjection);
  if (!textured_cube_createDepthTarget(app, app->width, app->height) ||
      !textured_cube_createPipeline(app) ||
      !textured_cube_createGeometry(app) ||
      !textured_cube_createMaterial(app)) {
    textured_cube_log("resource creation failed");
    return false;
  }
  return true;
}

static bool
textured_cube_updateUniforms(TexturedCubeApp *app) {
  CubeUniforms uniforms;
  LARGE_INTEGER now;
  float seconds;

  QueryPerformanceCounter(&now);
  seconds = (float)((double)(now.QuadPart - app->animationStart.QuadPart) *
                    app->secondsPerTick);
  CubeBuildUniforms(seconds, app->viewProjection, &uniforms);
  return GPUQueueWriteBuffer(app->queue,
                             app->uniformBuffer,
                             0u,
                             &uniforms,
                             sizeof(uniforms)) == GPU_OK;
}

static bool
textured_cube_render(TexturedCubeApp *app) {
  GPUFrame                           *frame;
  GPUCommandBuffer                   *cmdb;
  GPURenderPassEncoder               *pass;
  GPUBufferBinding                    vertexBuffer = {0};
  GPURenderPassColorAttachment        color        = {0};
  GPURenderPassDepthStencilAttachment depth        = {0};
  GPURenderPassCreateInfo             passInfo     = {0};

  if (!textured_cube_updateUniforms(app)) {
    return false;
  }

  frame = GPUBeginFrame(app->swapchain);
  if (!frame) {
    return false;
  }

  cmdb = NULL;
  pass = NULL;
  if (GPUAcquireCommandBuffer(app->queue,
                              "textured-cube-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return false;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.008f;
  color.clearColor.float32[1] = 0.018f;
  color.clearColor.float32[2] = 0.048f;
  color.clearColor.float32[3] = 1.0f;
  depth.view                  = app->depthView;
  depth.depthLoadOp           = GPU_LOAD_OP_CLEAR;
  depth.depthStoreOp          = GPU_STORE_OP_DONT_CARE;
  depth.stencilLoadOp         = GPU_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp        = GPU_STORE_OP_DONT_CARE;
  depth.clearDepth            = 1.0f;
  passInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize        = sizeof(passInfo);
  passInfo.label                   = "textured-cube-pass";
  passInfo.pColorAttachments       = &color;
  passInfo.pDepthStencilAttachment = &depth;
  passInfo.colorAttachmentCount    = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return false;
  }

  vertexBuffer.buffer = app->vertexBuffer;
  GPUBindRenderPipeline(pass, app->pipeline);
  GPUBindRenderGroup(pass, 0u, app->materialGroup, 0u, NULL);
  GPUBindRenderGroup(pass, 1u, app->samplerGroup, 0u, NULL);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBuffer);
  GPUBindIndexBuffer(pass, app->indexBuffer, 0u, GPU_INDEX_TYPE_UINT16);
  GPUDrawIndexed(pass, CUBE_INDEX_COUNT, 1u, 0u, 0, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(app->queue, cmdb, frame) != GPU_OK) {
    return false;
  }

  app->frameCount++;
  if (!GPUSampleCheckZeroAlloc(app->device,
                               app->frameCount,
                               app->assertZeroAlloc,
                               "GPU DX12 textured cube")) {
    return false;
  }
  if (app->exitAfterFrames > 0u &&
      app->frameCount >= app->exitAfterFrames) {
    app->running = false;
  }
  return true;
}

static bool
textured_cube_waitForGPU(TexturedCubeApp *app) {
  GPUCommandBuffer   *buffers[1];
  GPUCommandBuffer   *cmdb;
  GPUFence           *fence;
  GPUFenceCreateInfo  fenceInfo  = {0};
  GPUQueueSubmitInfo  submitInfo = {0};
  GPUResult           result;

  fenceInfo.chain.sType      = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fenceInfo.label            = "textured-cube-shutdown";
  if (GPUCreateFence(app->device, &fenceInfo, &fence) != GPU_OK) {
    return false;
  }

  cmdb = NULL;
  result = GPUAcquireCommandBuffer(app->queue,
                                   "textured-cube-drain",
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
textured_cube_destroyGPU(TexturedCubeApp *app) {
  GPUDestroyBindGroup(app->samplerGroup);
  GPUDestroyBindGroup(app->materialGroup);
  GPUDestroyRenderPipeline(app->pipeline);
  GPUDestroySampler(app->sampler);
  GPUDestroyTextureView(app->depthView);
  GPUDestroyTexture(app->depthTexture);
  GPUDestroyTextureView(app->textureView);
  GPUDestroyTexture(app->texture);
  GPUDestroyBuffer(app->uniformBuffer);
  GPUDestroyBuffer(app->indexBuffer);
  GPUDestroyBuffer(app->vertexBuffer);
  GPUDestroyShaderLayout(app->shaderLayout);
  GPUDestroyShaderLibrary(app->library);
  GPUDestroySwapchain(app->swapchain);
  GPUDestroySurface(app->surface);
  GPUDestroyDevice(app->device);
  GPUDestroyInstance(app->instance);
}

static bool
textured_cube_resize(TexturedCubeApp *app,
                     uint32_t         width,
                     uint32_t         height) {
  if (width == 0u || height == 0u ||
      GPUResizeSwapchain(app->swapchain, width, height) != GPU_OK ||
      !textured_cube_createDepthTarget(app, width, height)) {
    return false;
  }

  app->width  = width;
  app->height = height;
  CubeBuildViewProjection(width, height, app->viewProjection);
  return true;
}

static LRESULT CALLBACK
textured_cube_windowProc(HWND window,
                         UINT message,
                         WPARAM wparam,
                         LPARAM lparam) {
  switch (message) {
    case WM_CLOSE:
      if (textured_cube_app) {
        textured_cube_app->running = false;
      }
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      if (textured_cube_app && textured_cube_app->ready &&
          wparam != SIZE_MINIMIZED) {
        uint32_t width;
        uint32_t height;

        width  = LOWORD(lparam);
        height = HIWORD(lparam);
        if (!textured_cube_resize(textured_cube_app, width, height)) {
          textured_cube_log("resize failed");
          textured_cube_app->running = false;
        }
      }
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

static bool
textured_cube_createWindow(TexturedCubeApp *app, HINSTANCE instance) {
  WNDCLASSW windowClass = {0};
  RECT      rect;

  windowClass.lpfnWndProc   = textured_cube_windowProc;
  windowClass.hInstance     = instance;
  windowClass.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
  windowClass.lpszClassName = L"GPUUSLDX12TexturedCube";
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
                                L"GPU + USL Direct3D 12 Rotating Cube",
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

int
main(void) {
  TexturedCubeApp app = {0};
  LARGE_INTEGER   frequency;
  HINSTANCE       instance;
  MSG             message;
  const char     *exitFrames;
  int             result;

  if (GPUSampleShouldSkipNonInteractive()) {
    return GPU_SAMPLE_SKIP_RETURN_CODE;
  }
  if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
    textured_cube_log("performance timer unavailable");
    return 1;
  }

  app.secondsPerTick = 1.0 / (double)frequency.QuadPart;
  instance           = GetModuleHandleW(NULL);
  textured_cube_app  = &app;
  if (!textured_cube_createWindow(&app, instance)) {
    textured_cube_log("window creation failed");
    textured_cube_app = NULL;
    return 1;
  }
  if (!textured_cube_createGPU(&app)) {
    textured_cube_destroyGPU(&app);
    DestroyWindow(app.window);
    textured_cube_app = NULL;
    return 1;
  }

  exitFrames = getenv("GPU_SAMPLE_EXIT_AFTER_FRAMES");
  if (exitFrames) {
    app.exitAfterFrames = (uint32_t)strtoul(exitFrames, NULL, 10);
  }
  app.assertZeroAlloc = GPUSampleEnvEnabled("GPU_SAMPLE_ASSERT_ZERO_ALLOC");
  app.ready           = true;
  app.running         = true;
  QueryPerformanceCounter(&app.animationStart);

  result = 0;
  while (app.running) {
    while (PeekMessageW(&message, NULL, 0u, 0u, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        app.running = false;
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (app.running && !textured_cube_render(&app)) {
      textured_cube_log("frame rendering failed");
      result      = 1;
      app.running = false;
    }
  }

  if (!textured_cube_waitForGPU(&app)) {
    textured_cube_log("queue drain failed");
    result = 1;
  }
  app.ready = false;
  textured_cube_destroyGPU(&app);
  if (IsWindow(app.window)) {
    DestroyWindow(app.window);
  }
  textured_cube_app = NULL;
  return result;
}
