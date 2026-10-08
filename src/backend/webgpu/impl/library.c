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

#include "../common.h"
#include "../impl.h"

static GPUShaderLibrary*
webgpu_newLibraryWithSource(GPUDevice  *device,
                            const char *source,
                            uint64_t    sourceSize,
                            uint32_t    compileFlags) {
  WGPUShaderSourceWGSL       sourceInfo = WGPU_SHADER_SOURCE_WGSL_INIT;
  WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  GPUDeviceWebGPU           *native;
  GPUShaderLibrary          *library;

  (void)compileFlags;
  native = webgpuDevice(device);

  if (!native || !native->device || !source || sourceSize == 0u
      || sourceSize > (uint64_t)SIZE_MAX) {
    return NULL;
  }

  if (!(library = calloc(1, sizeof(*library)))) {
    return NULL;
  }

  sourceInfo.code        = webgpuStringSize(source, sourceSize);
  descriptor.nextInChain = &sourceInfo.chain;

  if (!(library->_priv = wgpuDeviceCreateShaderModule(native->device, &descriptor))) {
    free(library);
    return NULL;
  }

  return library;
}

static void
webgpu_destroyLibrary(GPUShaderLibrary *library) {
  if (!library) {
    return;
  }

  if (library->_priv) {
    wgpuShaderModuleRelease(library->_priv);
  }

  free(library);
}

void
webgpu_initLibrary(GPULibraryApi *api) {
  api->newLibraryWithSource = webgpu_newLibraryWithSource;
  api->destroyLibrary       = webgpu_destroyLibrary;
}
