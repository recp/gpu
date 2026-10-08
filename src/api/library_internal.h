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

typedef struct USLRuntimeSpecConstant USLRuntimeSpecConstant;

typedef struct StaticSamplerDesc {
  uint32_t logicalIndex;
  uint32_t minFilter;
  uint32_t magFilter;
  uint32_t mipFilter;
  uint32_t addressMode;
  uint32_t coordSpace;
  uint32_t compareFunc;
  uint32_t hasCompare;
  uint32_t maxAnisotropy;
} StaticSamplerDesc;

typedef struct ShaderStaticSamplerInfo {
  uint64_t             entryMask;
  StaticSamplerDesc    desc;
  GPUShaderStageFlags  visibility;
  uint32_t             hlslIndex;
  uint32_t             spirvGroup;
  uint32_t             spirvBinding;
  uint32_t             wgslGroup;
  uint32_t             wgslBinding;
} ShaderStaticSamplerInfo;

typedef struct ShaderStaticSamplerInfoList {
  uint32_t                   count;
  ShaderStaticSamplerInfo    items[];
} ShaderStaticSamplerInfoList;

typedef struct ShaderExecutionGraphEntryInfo {
  const char *entryPoint;
  const char *nodeName;
  uint32_t    nodeIndex;
  uint32_t    recordSizeBytes;
  uint32_t    nodeLaunch;
  bool        programEntry;
} ShaderExecutionGraphEntryInfo;

typedef struct ShaderSourceBlob {
  void    *data;
  uint64_t size;
} ShaderSourceBlob;

enum {
  GPU_SHADER_PTX_MAX_PARAM_COUNT = 512u,
  GPU_SHADER_PTX_MAX_PARAM_BYTES = 4096u
};

typedef enum ShaderPTXParamKind {
  GPUShaderPTXParamInvalid         = 0,
  GPUShaderPTXParamBuffer          = 1,
  GPUShaderPTXParamSurface         = 2,
  GPUShaderPTXParamTexture         = 3,
  GPUShaderPTXParamSampledTexture  = 4,
  GPUShaderPTXParamTextureMetadata = 5
} ShaderPTXParamKind;

typedef enum ShaderPTXTextureMetadataFlags {
  GPUShaderPTXTextureMetadataNone               = 0,
  GPUShaderPTXTextureMetadataMipLevelCountBit   = 1u << 0,
  GPUShaderPTXTextureMetadataArrayLayerCountBit = 1u << 1,
  GPUShaderPTXTextureMetadataSampleCountBit     = 1u << 2
} ShaderPTXTextureMetadataFlags;

typedef struct ShaderPTXParamInfo {
  GPUBindingType        bindingType;
  ShaderPTXParamKind    kind;
  uint32_t              groupIndex;
  uint32_t              binding;
  uint32_t              arrayIndex;
  uint32_t              samplerGroupIndex;
  uint32_t              samplerBinding;
  uint32_t              samplerArrayIndex;
  uint32_t              staticSamplerId;
  uint32_t              dataOffset;
  uint32_t              metadataFlags;
} ShaderPTXParamInfo;

typedef struct ShaderPTXEntryInfo {
  uint32_t paramStart;
  uint32_t paramCount;
  uint32_t paramDataSize;
} ShaderPTXEntryInfo;

typedef struct ShaderPTXInfo {
  ShaderPTXEntryInfo    *entries;
  ShaderPTXParamInfo    *params;
  uint32_t               entryCount;
  uint32_t               paramCount;
} ShaderPTXInfo;

typedef struct ShaderPTXEntryView {
  const ShaderPTXParamInfo    *params;
  uint32_t                     paramCount;
  uint32_t                     paramDataSize;
} ShaderPTXEntryView;

struct GPUShaderLibrary {
  Api                            *_api;
  GPUDevice                      *_device;
  void                           *_priv;
  void                           *_metadata;
  void                           *_uslSource;
  ShaderStaticSamplerInfoList    *_staticSamplers;
  ShaderPTXInfo                  *_ptxInfo;
  void                           *_entryInfo;
  void                           *_entryResources;
  void                           *_resourceBindings;
  USLRuntimeSpecConstant         *_constants;
  GPUShaderReflection             _reflection;
  uint32_t                        _constantCount;
};

struct ShaderFunction {
  void *_priv;
};

GPU_HIDE
ShaderFunction*
shaderFunction(GPUShaderLibrary *library, const char *name);

GPU_HIDE
void
destroyShaderFunction(GPUShaderLibrary  *library,
                      ShaderFunction    *function);

GPU_HIDE
int
getShaderLibraryWorkgroupSize(const GPUShaderLibrary *library,
                              const char             *entryPoint,
                              GPUShaderStageFlags     stage,
                              uint32_t                outSize[3]);

GPU_HIDE
int
getShaderLibraryComputeWorkgroupSize(const GPUShaderLibrary *library,
                                     const char             *entryPoint,
                                     uint32_t                outSize[3]);

GPU_HIDE
int
getShaderLibraryPTXEntry(const GPUShaderLibrary *library,
                         const char             *entryPoint,
                         ShaderPTXEntryView     *outEntry);

GPU_HIDE
int
getShaderLibraryMeshOutputInfo(const GPUShaderLibrary *library,
                               const char             *entryPoint,
                               uint32_t               *outTopology,
                               uint32_t               *outMaxVertices,
                               uint32_t               *outMaxPrimitives);

GPU_HIDE
int
getShaderLibraryEntryStage(const GPUShaderLibrary *library,
                           const char             *entryPoint,
                           GPUShaderStageFlags    *outStage);

GPU_HIDE
int
getShaderLibraryExecutionGraphEntry(const GPUShaderLibrary           *library,
                                    const char                       *entryPoint,
                                    ShaderExecutionGraphEntryInfo    *outEntry);

GPU_HIDE
uint32_t
getShaderLibraryExecutionGraphEntryCount(const GPUShaderLibrary *library);

GPU_HIDE
int
getShaderLibraryExecutionGraphEntryAt(const GPUShaderLibrary           *library,
                                      uint32_t                          index,
                                      ShaderExecutionGraphEntryInfo    *outEntry);

GPU_HIDE
int
getShaderLibraryPayloadInfo(const GPUShaderLibrary *library,
                            const char             *entryPoint,
                            GPUShaderStageFlags     stage,
                            uint32_t               *outSizeBytes,
                            const char            **outType);

GPU_HIDE
int
getShaderLibraryRayInterfaceInfo(const GPUShaderLibrary *library,
                                 const char             *entryPoint,
                                 GPUShaderStageFlags     stage,
                                 uint32_t               *outPayloadSizeBytes,
                                 uint32_t               *outHitAttributeSizeBytes,
                                 uint32_t               *outCallableDataSizeBytes);

GPU_HIDE
int
shaderLibraryHasEntryResourceInfo(const GPUShaderLibrary *library);

GPU_HIDE
const GPUShaderReflection*
shaderReflectionView(const GPUShaderLibrary *library);

GPU_HIDE
int
shaderEntryView(const GPUShaderLibrary *library,
                const char             *entryPoint,
                GPUShaderStageFlags    *outStage,
                GPUShaderReflection    *outReflection);

GPU_HIDE
int
getShaderResourceBackendBinding(const GPUShaderLibrary            *library,
                                const GPUShaderResourceReflection *resource,
                                uint32_t                          *outBinding);

GPU_HIDE
const ShaderStaticSamplerInfo*
getShaderLibraryStaticSamplers(const GPUShaderLibrary *library,
                               uint32_t               *outCount);

GPU_HIDE
uint64_t
shaderEntryBit(const GPUShaderLibrary *library, const char *entryPoint);

GPU_HIDE
uint32_t
shaderWGSLStaticGroups(const GPUShaderLibrary *library,
                       uint64_t                entryMask);

GPU_HIDE
GPUResult
compileShaderLibraryEntry(const GPUShaderLibrary     *library,
                          const char                 *entryPoint,
                          const GPUPipelineConstants *constants,
                          ShaderSourceBlob           *outSource);

GPU_HIDE
GPUResult
compileShaderLibraryEntryMask(const GPUShaderLibrary *library,
                              uint64_t                entryMask,
                              ShaderSourceBlob       *outSource);

GPU_HIDE
void
freeShaderSourceBlob(ShaderSourceBlob    *source);

GPU_HIDE
int
staticSamplerDescIsValid(const StaticSamplerDesc    *desc);

GPU_HIDE
int
staticSamplerToSamplerDesc(const StaticSamplerDesc    *source,
                           GPUSamplerDesc             *outDesc);

#endif /* gpu_library_internal_h */
