/*
 * Copyright (C) 2020 Recep Aslantas
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

#ifndef gpu_library_h
#define gpu_library_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "cmdqueue.h"
#include "format.h"
#include "device.h"
#include "bindgroup.h"
//#include <us/us.h>

typedef struct GPUShaderLibrary GPUShaderLibrary;

typedef struct GPUShaderResourceReflection {
  const char         *name;
  union {
    GPUTextureBindingLayout        sampledTexture;
    GPUStorageTextureBindingLayout storageTexture;
    GPUSamplerBindingLayout        sampler;
  };
  uint32_t            groupIndex;
  uint32_t            binding;
  GPUBindingType      bindingType;
  GPUShaderStageFlags visibility;
  uint32_t            arrayCount;
  bool                hasDynamicOffset;
} GPUShaderResourceReflection;

typedef struct GPUShaderReflection {
  const GPUShaderResourceReflection *pResources;
  uint32_t                           resourceCount;
  uint32_t                           pushConstantSizeBytes;
} GPUShaderReflection;

typedef enum GPUShaderSourceKind {
  GPU_SHADER_SOURCE_USL_TEXT     = 0,
  GPU_SHADER_SOURCE_USL_BYTECODE = 1,
  GPU_SHADER_SOURCE_MSL_TEXT     = 2,
  GPU_SHADER_SOURCE_SPIRV_BINARY = 3,
  GPU_SHADER_SOURCE_WGSL_TEXT    = 4
} GPUShaderSourceKind;

typedef struct GPUShaderDefine {
  const char *name;
  const char *value;
} GPUShaderDefine;

typedef struct GPUShaderLibraryCreateInfo {
  GPUChainedStruct       chain;
  const char            *label;
  const void            *sourceData;
  const char            *sourcePathHint;
  const GPUShaderDefine *pDefines;
  uint64_t               sourceSize;
  GPUShaderSourceKind    sourceKind;
  uint32_t               defineCount;
  bool                   generateReflection;
  bool                   disableDiskCache;
} GPUShaderLibraryCreateInfo;

GPU_EXPORT
GPUResult
GPUCreateShaderLibrary(GPUDevice *device,
                       const GPUShaderLibraryCreateInfo *info,
                       GPUShaderLibrary **outLibrary);

GPU_EXPORT
GPUResult
GPUCreateShaderLibraryFromUSL(GPUDevice *device,
                              const void *artifactData,
                              uint64_t artifactSize,
                              GPUShaderLibrary **outLibrary);

GPU_EXPORT
GPUResult
GPUGetShaderReflection(const GPUShaderLibrary *library,
                       GPUShaderReflection *outReflection);

GPU_EXPORT
void
GPUFreeShaderReflection(GPUShaderReflection *reflection);

GPU_EXPORT
void
GPUDestroyShaderLibrary(GPUShaderLibrary *library);

#ifdef __cplusplus
}
#endif
#endif /* gpu_library_h */
