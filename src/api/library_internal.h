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
  uint64_t             entryMask;
  GPUStaticSamplerDesc desc;
  GPUShaderStageFlags  visibility;
  uint32_t             hlslIndex;
  uint32_t             spirvGroup;
  uint32_t             spirvBinding;
  uint32_t             wgslGroup;
  uint32_t             wgslBinding;
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

typedef struct GPUShaderSourceBlob {
  void    *data;
  uint64_t size;
} GPUShaderSourceBlob;

enum {
  GPU_SHADER_PTX_MAX_PARAM_COUNT = 512u,
  GPU_SHADER_PTX_MAX_PARAM_BYTES = 4096u
};

typedef enum GPUShaderPTXParamKind {
  GPUShaderPTXParamInvalid         = 0,
  GPUShaderPTXParamBuffer          = 1,
  GPUShaderPTXParamSurface         = 2,
  GPUShaderPTXParamTexture         = 3,
  GPUShaderPTXParamSampledTexture  = 4,
  GPUShaderPTXParamTextureMetadata = 5
} GPUShaderPTXParamKind;

typedef enum GPUShaderPTXTextureMetadataFlags {
  GPUShaderPTXTextureMetadataNone               = 0,
  GPUShaderPTXTextureMetadataMipLevelCountBit   = 1u << 0,
  GPUShaderPTXTextureMetadataArrayLayerCountBit = 1u << 1,
  GPUShaderPTXTextureMetadataSampleCountBit     = 1u << 2
} GPUShaderPTXTextureMetadataFlags;

typedef struct GPUShaderPTXParamInfo {
  GPUBindingType        bindingType;
  GPUShaderPTXParamKind kind;
  uint32_t              groupIndex;
  uint32_t              binding;
  uint32_t              arrayIndex;
  uint32_t              samplerGroupIndex;
  uint32_t              samplerBinding;
  uint32_t              samplerArrayIndex;
  uint32_t              staticSamplerId;
  uint32_t              dataOffset;
  uint32_t              metadataFlags;
} GPUShaderPTXParamInfo;

typedef struct GPUShaderPTXEntryInfo {
  uint32_t paramStart;
  uint32_t paramCount;
  uint32_t paramDataSize;
} GPUShaderPTXEntryInfo;

typedef struct GPUShaderPTXInfo {
  GPUShaderPTXEntryInfo *entries;
  GPUShaderPTXParamInfo *params;
  uint32_t               entryCount;
  uint32_t               paramCount;
} GPUShaderPTXInfo;

typedef struct GPUShaderPTXEntryView {
  const GPUShaderPTXParamInfo *params;
  uint32_t                     paramCount;
  uint32_t                     paramDataSize;
} GPUShaderPTXEntryView;

struct GPUShaderLibrary {
  GPUApi                         *_api;
  GPUDevice                      *_device;
  void                           *_priv;
  void                           *_metadata;
  void                           *_uslSource;
  GPUShaderStaticSamplerInfoList *_staticSamplers;
  GPUShaderPTXInfo               *_ptxInfo;
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
gpuGetShaderLibraryPTXEntry(const GPUShaderLibrary *library,
                            const char               *entryPoint,
                            GPUShaderPTXEntryView     *outEntry);

GPU_HIDE
int
gpuGetShaderLibraryMeshOutputInfo(const GPUShaderLibrary *library,
                                  const char               *entryPoint,
                                  uint32_t                 *outTopology,
                                  uint32_t                 *outMaxVertices,
                                  uint32_t                 *outMaxPrimitives);

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
uint64_t
gpuShaderEntryBit(const GPUShaderLibrary *library, const char *entryPoint);

GPU_HIDE
uint32_t
gpuShaderWGSLStaticGroups(const GPUShaderLibrary *library,
                          uint64_t                entryMask);

GPU_HIDE
GPUResult
gpuCompileShaderLibraryEntry(const GPUShaderLibrary *library,
                             const char             *entryPoint,
                             GPUShaderSourceBlob     *outSource);

GPU_HIDE
GPUResult
gpuCompileShaderLibraryEntryMask(const GPUShaderLibrary *library,
                                 uint64_t                entryMask,
                                 GPUShaderSourceBlob     *outSource);

GPU_HIDE
void
gpuFreeShaderSourceBlob(GPUShaderSourceBlob *source);

GPU_HIDE
int
gpuStaticSamplerDescIsValid(const GPUStaticSamplerDesc *desc);

GPU_HIDE
int
gpuStaticSamplerToSamplerDesc(const GPUStaticSamplerDesc *source,
                              GPUSamplerDesc             *outDesc);

#endif /* gpu_library_internal_h */
