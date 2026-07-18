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

#ifndef gpu_library_internal_h
#define gpu_library_internal_h

#include "../common.h"

#include <us/compiler.h>

typedef struct GPUStaticSamplerDesc {
  uint32_t logicalIndex;
  uint32_t minFilter;
  uint32_t magFilter;
  uint32_t mipFilter;
  uint32_t addressMode;
  uint32_t coordSpace;
  uint32_t compareFunc;
  uint32_t hasCompare;
  uint32_t maxAnisotropy;
} GPUStaticSamplerDesc;

typedef struct GPUShaderStaticSamplerInfo {
  GPUStaticSamplerDesc desc;
  GPUShaderStageFlags  visibility;
  uint32_t             hlslIndex;
  uint32_t             spirvGroup;
  uint32_t             spirvBinding;
} GPUShaderStaticSamplerInfo;

typedef struct GPUShaderStaticSamplerInfoList {
  uint32_t                   count;
  GPUShaderStaticSamplerInfo items[];
} GPUShaderStaticSamplerInfoList;

typedef struct GPUShaderExecutionGraphEntryInfo {
  const char *entryPoint;
  const char *nodeName;
  uint32_t    nodeIndex;
  uint32_t    recordSizeBytes;
  uint32_t    nodeLaunch;
  bool        programEntry;
} GPUShaderExecutionGraphEntryInfo;

struct GPUShaderLibrary {
  GPUApi                         *_api;
  void                           *_priv;
  void                           *_metadata;
  GPUShaderStaticSamplerInfoList *_staticSamplers;
  void                           *_entryInfo;
  void                           *_entryResources;
  void                           *_resourceBindings;
  GPUShaderReflection             _reflection;
};

struct GPUShaderFunction {
  void *_priv;
};

GPU_HIDE
GPUShaderFunction *
gpuShaderFunction(GPUShaderLibrary *library, const char *name);

GPU_HIDE
void
gpuDestroyShaderFunction(GPUShaderLibrary  *library,
                         GPUShaderFunction *function);

GPU_HIDE
int
gpuGetShaderLibraryWorkgroupSize(const GPUShaderLibrary *library,
                                 const char               *entryPoint,
                                 GPUShaderStageFlags       stage,
                                 uint32_t                  outSize[3]);

GPU_HIDE
int
gpuGetShaderLibraryComputeWorkgroupSize(const GPUShaderLibrary *library,
                                        const char *entryPoint,
                                        uint32_t outSize[3]);

GPU_HIDE
int
gpuGetShaderLibraryEntryStage(const GPUShaderLibrary *library,
                              const char *entryPoint,
                              GPUShaderStageFlags *outStage);

GPU_HIDE
int
gpuGetShaderLibraryExecutionGraphEntry(
  const GPUShaderLibrary                 *library,
  const char                             *entryPoint,
  GPUShaderExecutionGraphEntryInfo       *outEntry
);

GPU_HIDE
uint32_t
gpuGetShaderLibraryExecutionGraphEntryCount(const GPUShaderLibrary *library);

GPU_HIDE
int
gpuGetShaderLibraryExecutionGraphEntryAt(
  const GPUShaderLibrary                 *library,
  uint32_t                                index,
  GPUShaderExecutionGraphEntryInfo       *outEntry
);

GPU_HIDE
int
gpuGetShaderLibraryPayloadInfo(const GPUShaderLibrary *library,
                               const char               *entryPoint,
                               GPUShaderStageFlags       stage,
                               uint32_t                 *outSizeBytes,
                               const char              **outType);

GPU_HIDE
int
gpuGetShaderLibraryRayInterfaceInfo(const GPUShaderLibrary *library,
                                    const char               *entryPoint,
                                    GPUShaderStageFlags       stage,
                                    uint32_t                 *outPayloadSizeBytes,
                                    uint32_t                 *outHitAttributeSizeBytes,
                                    uint32_t                 *outCallableDataSizeBytes);

GPU_HIDE
int
gpuShaderLibraryHasEntryResourceInfo(const GPUShaderLibrary *library);

GPU_HIDE
const GPUShaderReflection *
gpuShaderReflectionView(const GPUShaderLibrary *library);

GPU_HIDE
int
gpuShaderEntryView(const GPUShaderLibrary *library,
                   const char *entryPoint,
                   GPUShaderStageFlags *outStage,
                   GPUShaderReflection *outReflection);

GPU_HIDE
int
gpuGetShaderResourceBackendBinding(const GPUShaderLibrary *library,
                                   const GPUShaderResourceReflection *resource,
                                   uint32_t *outBinding);

GPU_HIDE
const GPUShaderStaticSamplerInfo *
gpuGetShaderLibraryStaticSamplers(const GPUShaderLibrary *library,
                                  uint32_t *outCount);

GPU_HIDE
int
gpuStaticSamplerDescIsValid(const GPUStaticSamplerDesc *desc);

#endif /* gpu_library_internal_h */
