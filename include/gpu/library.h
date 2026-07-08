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
#include "pixelformat.h"
#include "device.h"
//#include <us/us.h>

#define GPU_SHADER_LIBRARY_USL_INFO_VERSION 2u

typedef enum GPUShaderLibraryUSLSourceKind {
  GPUShaderLibraryUSLSourceNone      = 0,
  GPUShaderLibraryUSLSourceEmbedded  = 1,
  GPUShaderLibraryUSLSourceGenerated = 2
} GPUShaderLibraryUSLSourceKind;

typedef struct GPUShaderLibraryUSLInfo {
  uint32_t abiVersion;
  uint32_t targetBackend;
  uint32_t bytecodeVersion;
  uint32_t sourceKind;
  uint32_t targetAtomCount;
  uint32_t targetAtomTotalCount;
  uint32_t targetInfoFlags;
  uint32_t reserved0;
  uint32_t selectedEntryCount;
  uint32_t entryTargetInfoVersion;
  uint32_t targetSupported;
  uint32_t targetSupportStatus;
  uint64_t bytecodeSize;
  uint64_t bytecodeDataSize;
  uint64_t bytecodeContentHash;
  uint64_t targetAtomHash;
  uint64_t backendContentHash;
  uint64_t selectedEntryHash;
} GPUShaderLibraryUSLInfo;

typedef struct GPUShaderResourceReflection {
  const char *name;
  uint32_t setIndex;
  uint32_t binding;
  GPUBindingType bindingType;
  GPUShaderStageFlags visibility;
  uint32_t arrayCount;
  bool hasDynamicOffset;
} GPUShaderResourceReflection;

typedef struct GPUShaderReflection {
  uint32_t resourceCount;
  const GPUShaderResourceReflection *pResources;
  uint32_t pushConstantSizeBytes;
} GPUShaderReflection;

typedef struct GPULibrary {
  void *_priv;
  GPUShaderLibraryUSLInfo _uslInfo;
  GPUShaderReflection _reflection;
} GPULibrary;

typedef GPULibrary GPUShaderLibrary;

typedef struct GPUFunction {
  void *_priv;
} GPUFunction;

typedef enum GPUShaderSourceKind {
  GPU_SHADER_SOURCE_USL_TEXT = 0,
  GPU_SHADER_SOURCE_USL_BYTECODE = 1,
  GPU_SHADER_SOURCE_MSL_TEXT = 2,
  GPU_SHADER_SOURCE_SPIRV_BINARY = 3
} GPUShaderSourceKind;

typedef struct GPUShaderDefine {
  const char *name;
  const char *value;
} GPUShaderDefine;

typedef struct GPUShaderLibraryCreateInfo {
  GPUChainedStruct chain;
  const char *label;
  GPUShaderSourceKind sourceKind;
  const void *sourceData;
  uint64_t sourceSize;
  const char *sourcePathHint;
  uint32_t defineCount;
  const GPUShaderDefine *pDefines;
  bool generateReflection;
  bool enableDiskCache;
} GPUShaderLibraryCreateInfo;

GPU_EXPORT
GPULibrary*
GPUDefaultLibrary(GPUDevice *device);

GPU_EXPORT
GPUFunction*
GPUShaderFunction(GPULibrary *lib, const char *name);

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
GPUCreateShaderLibraryFromUSLEntries(GPUDevice *device,
                                     const void *artifactData,
                                     uint64_t artifactSize,
                                     const char * const *entryPointNames,
                                     uint32_t entryPointCount,
                                     GPUShaderLibrary **outLibrary);

GPU_EXPORT
int
GPUCreateShaderLibraryFromUSLBytecode(GPUDevice *device,
                                      const void *bytecodeData,
                                      uint64_t bytecodeSize,
                                      GPUShaderLibrary **outLibrary);

GPU_EXPORT
int
GPUCreateShaderLibraryFromUSLBytecodeForEntries(
                                      GPUDevice *device,
                                      const void *bytecodeData,
                                      uint64_t bytecodeSize,
                                      const char * const *entryPointNames,
                                      uint32_t entryPointCount,
                                      GPUShaderLibrary **outLibrary);

GPU_EXPORT
int
GPUGetShaderLibraryUSLInfo(GPUShaderLibrary *library,
                           GPUShaderLibraryUSLInfo *outInfo);

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
