/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "test.h"
#include "../../src/api/device_internal.h"

#if GPU_TEST_WGPU_NATIVE
#  include <stdatomic.h>
#  if defined(_WIN32) || defined(WIN32)
#    include <windows.h>
typedef HANDLE WebGPUThread;
#  else
#    include <pthread.h>
typedef pthread_t WebGPUThread;
#  endif

enum {
  WEBGPU_ERROR_THREADS = 4u,
  WEBGPU_ERROR_ROUNDS  = 8u
};

typedef struct WebGPUErrorTest {
  GPUComputePipelineCreateInfo compute;
  GPURenderPipelineCreateInfo  render;
  GPUDevice                   *device;
  atomic_uint                  errors;
  atomic_uint                  failures;
  atomic_bool                  reentered;
} WebGPUErrorTest;

static const char webgpu_pipelineSource[] =
  "@compute @workgroup_size(1) fn good_cs() {}\n"
  "@vertex fn good_vs() -> @builtin(position) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }\n"
  "@fragment fn good_fs() -> @location(0) vec4f { return vec4f(1.0, 0.0, 0.0, 1.0); }\n";

static bool
webgpu_computeError(WebGPUErrorTest *test) {
  GPUComputePipelineCreateInfo info;
  GPUComputePipeline          *pipeline = NULL;
  GPUResult                    result;

  info            = test->compute;
  info.entryPoint = "missing_cs";
  result          = GPUCreateComputePipeline(test->device, &info, &pipeline);

  if (result != GPU_ERROR_INVALID_ARGUMENT || pipeline) {
    GPUDestroyComputePipeline(pipeline);
    return false;
  }

  return true;
}

static void
webgpu_pipelineError(GPUDevice                *device,
                     const GPUDeviceErrorInfo *info,
                     void                     *userData) {
  WebGPUErrorTest     *test;
  GPUComputePipeline *pipeline = NULL;
  bool                expected = false;

  test = userData;
  atomic_fetch_add_explicit(&test->errors, 1u, memory_order_relaxed);

  if (device != test->device || !info || info->type != GPU_DEVICE_ERROR_VALIDATION
      || info->result != GPU_ERROR_INVALID_ARGUMENT || !info->message || !info->message[0]) {
    atomic_fetch_add_explicit(&test->failures, 1u, memory_order_relaxed);
  }

  if (atomic_compare_exchange_strong(&test->reentered, &expected, true)) {
    /* a nested failure must report outside the native error-sink lock. */

    if (!webgpu_computeError(test)
        || GPUCreateComputePipeline(device, &test->compute, &pipeline) != GPU_OK || !pipeline) {
      atomic_fetch_add_explicit(&test->failures, 1u, memory_order_relaxed);
    }

    GPUDestroyComputePipeline(pipeline);
  }
}

#  if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI
webgpu_pipelineThread(void *userData) {
#  else
static void*
webgpu_pipelineThread(void *userData) {
#  endif
  GPURenderPipelineCreateInfo info;
  WebGPUErrorTest             *test;
  GPURenderPipeline          *render = NULL;
  GPUComputePipeline         *compute = NULL;
  GPUResult                   result;
  uint32_t                    i;
  bool                        ok;

  test = userData;

  for (i = 0u; i < WEBGPU_ERROR_ROUNDS; i++) {
    ok   = webgpu_computeError(test);
    info = test->render;

    if ((i & 1u) == 0u)
      info.vertexEntry = "missing_vs";
    else
      info.fragmentEntry = "missing_fs";

    result = GPUCreateRenderPipeline(test->device, &info, &render);
    ok     = ok && result == GPU_ERROR_INVALID_ARGUMENT && !render;
    GPUDestroyRenderPipeline(render);
    render = NULL;

    if (GPUCreateComputePipeline(test->device, &test->compute, &compute) != GPU_OK || !compute
        || GPUCreateRenderPipeline(test->device, &test->render, &render) != GPU_OK || !render) {
      ok = false;
    }

    GPUDestroyComputePipeline(compute);
    GPUDestroyRenderPipeline(render);
    compute = NULL;
    render  = NULL;

    if (!ok)
      atomic_fetch_add_explicit(&test->failures, 1u, memory_order_relaxed);
  }

  return 0;
}
#endif

int
gpu_test_webgpu_pipeline_error(GPUDevice *device) {
#if GPU_TEST_WGPU_NATIVE
  GPUShaderLibraryCreateInfo  libraryInfo = {0};
  GPUPipelineLayoutCreateInfo layoutInfo = {0};
  GPUColorTargetState         target = {0};
  WebGPUErrorTest             test = {0};
  WebGPUThread                threads[WEBGPU_ERROR_THREADS];
  GPUShaderLibrary           *library = NULL;
  GPUPipelineLayout          *layout = NULL;
  void                       *savedData;
  uint32_t                    count;
  uint32_t                    i;
  uint32_t                    errors;
  int                         ok = 0;

  GPUDeviceErrorCallback savedCallback;

  if (!device)
    return 0;

  layoutInfo.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);

  libraryInfo.chain.sType      = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  libraryInfo.chain.structSize = sizeof(libraryInfo);
  libraryInfo.sourceKind       = GPU_SHADER_SOURCE_WGSL_TEXT;
  libraryInfo.sourceData       = webgpu_pipelineSource;
  libraryInfo.sourceSize       = sizeof(webgpu_pipelineSource) - 1u;

  if (GPUCreateShaderLibrary(device, &libraryInfo, &library) != GPU_OK || !library
      || GPUCreatePipelineLayout(device, &layoutInfo, &layout) != GPU_OK || !layout) {
    goto cleanup;
  }

  test.device                   = device;
  test.compute.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  test.compute.chain.structSize = sizeof(test.compute);
  test.compute.library          = library;
  test.compute.layout           = layout;
  test.compute.entryPoint       = "good_cs";

  target.format                = GPU_FORMAT_RGBA8_UNORM;
  test.render.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  test.render.chain.structSize = sizeof(test.render);
  test.render.library          = library;
  test.render.layout           = layout;
  test.render.vertexEntry      = "good_vs";
  test.render.fragmentEntry    = "good_fs";
  test.render.colorTargetCount = 1u;
  test.render.pColorTargets    = &target;

  savedCallback        = device->errorCallback;
  savedData            = device->errorUserData;
  device->errorCallback = webgpu_pipelineError;
  device->errorUserData = &test;
  count                = 0u;

  for (i = 0u; i < WEBGPU_ERROR_THREADS; i++) {
#  if defined(_WIN32) || defined(WIN32)
    threads[i] = CreateThread(NULL, 0u, webgpu_pipelineThread, &test, 0u, NULL);

    if (!threads[i])
      break;
#  else
    if (pthread_create(&threads[i], NULL, webgpu_pipelineThread, &test) != 0)
      break;
#  endif

    count++;
  }

  for (i = 0u; i < count; i++) {
#  if defined(_WIN32) || defined(WIN32)
    WaitForSingleObject(threads[i], INFINITE);
    CloseHandle(threads[i]);
#  else
    pthread_join(threads[i], NULL);
#  endif
  }

  device->errorCallback = savedCallback;
  device->errorUserData = savedData;

  errors = atomic_load_explicit(&test.errors, memory_order_relaxed);
  ok = count == WEBGPU_ERROR_THREADS && errors == WEBGPU_ERROR_THREADS * WEBGPU_ERROR_ROUNDS * 2u + 1u
       && atomic_load_explicit(&test.failures, memory_order_relaxed) == 0u
       && atomic_load_explicit(&test.reentered, memory_order_relaxed);

  if (!ok)
    fprintf(stderr, "pipeline error isolation failed: %u errors, %u failures\n",
            errors, atomic_load_explicit(&test.failures, memory_order_relaxed));
  else
    puts("pipeline errors: 65 reentrant/concurrent failures rejected; valid pipelines recover");

cleanup:
  GPUDestroyPipelineLayout(layout);
  GPUDestroyShaderLibrary(library);
  return ok;
#else
  GPU__UNUSED(device);
  puts("pipeline errors: wgpu-native provider required");
  return 1;
#endif
}
