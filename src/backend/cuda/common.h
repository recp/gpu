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

#ifndef gpu_cuda_common_h
#define gpu_cuda_common_h

#include "../common.h"
#include "../../api/adapter_internal.h"
#include "../../api/buffer_internal.h"
#include "../../api/cmdqueue_internal.h"
#include "../../api/compute_internal.h"
#include "../../api/descr/descriptor_internal.h"
#include "../../api/instance_internal.h"
#include "../../api/library_internal.h"
#include "../../api/constants_internal.h"
#include "../../api/pipeline_cache_internal.h"
#include "../../api/sampler_internal.h"
#include "../../api/texture_internal.h"
#include "driver.h"
#include "format.h"
#include "sampler_plan.h"
#include "texture_plan.h"

#if !defined(_WIN32) && !defined(WIN32)
#  include <pthread.h>
#endif

enum {
  CUDA_COMMAND_SLOT_COUNT            = 16u,
  CUDA_INITIAL_DISPATCH_CAPACITY     = 64u,
  CUDA_INLINE_PARAM_BYTES            = 64u,
  CUDA_INLINE_TEXTURE_CACHE_CAPACITY = 4u,
  CUDA_TEXTURE_CACHE_CAPACITY        = 64u,
  CUDA_PARAM_MASK_WORD_COUNT         = GPU_SHADER_PTX_MAX_PARAM_COUNT / 64u
};

typedef struct AdapterCuda {
  CUDA    *driver;
  CUdevice device;
  CUuuid   uuid;
  char     name[256];
  int      maxBlockDim[3];
  int      maxGridDim[3];
  int      ordinal;
  int      computeMajor;
  int      computeMinor;
  int      maxThreadsPerBlock;
  int      warpSize;
  int      unifiedAddressing;
} AdapterCuda;

typedef struct BufferCuda {
  CUDA            *driver;
  CUexternalMemory externalMemory;
  CUdeviceptr      address;
} BufferCuda;

typedef struct TextureCuda {
  CUDA             *driver;
  CUexternalMemory  externalMemory;
  CUmipmappedArray  mipmap;
  CUarray           array;
  CudaFormatInfo    format;
  uint32_t          arrayFlags;
} TextureCuda;

typedef struct CudaTextureCacheEntry {
  CUDA_TEXTURE_DESC desc;
  CUtexObject       texture;
} CudaTextureCacheEntry;

typedef struct TextureViewCuda {
  CUDA                     *driver;
  CudaTextureCacheEntry    *cache;
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION lock;
#else
  pthread_mutex_t lock;
#endif
  CUDA_RESOURCE_VIEW_DESC  resourceView;
  CUarray                  array;
  CUsurfObject             surface;
  uint32_t                 cacheCount;
  uint32_t                 cacheCapacity;
  bool                     cacheDynamic;
  bool                     hasResourceView;
  CudaTextureCacheEntry    inlineCache[CUDA_INLINE_TEXTURE_CACHE_CAPACITY];
} TextureViewCuda;

typedef struct CudaTextureMetadata {
  uint32_t mipLevelCount;
  uint32_t arrayLayerCount;
  uint32_t sampleCount;
  uint32_t reserved;
} CudaTextureMetadata;

_Static_assert(sizeof(CudaTextureMetadata) == 16u,
               "CUDA texture metadata ABI drift");

typedef struct SemaphoreCuda {
  CUDA               *driver;
  CUexternalSemaphore semaphore;
} SemaphoreCuda;

typedef struct CudaModule {
  CUDA     *driver;
  CUcontext context;
  CUmodule  module;
  uint32_t  refCount;
} CudaModule;

typedef struct ShaderLibraryCuda {
  CudaModule    *module;
} ShaderLibraryCuda;

typedef struct SamplerCuda {
  CUDA_TEXTURE_DESC desc;
} SamplerCuda;

typedef struct ComputePipelineCuda {
  ComputePipelineState    base;
  GPUComputePipeline     *pipeline;
  CudaModule             *module;
  CUDA_TEXTURE_DESC      *staticSamplers;
  CUfunction              function;
  uint32_t                paramCount;
  uint32_t                paramDataSize;
  uint32_t                staticSamplerCount;
  ShaderPTXParamInfo      params[];
} ComputePipelineCuda;

typedef struct DispatchCuda {
  GPUComputePipeline *pipeline;
  uint32_t            grid[3];
  uint32_t            block[3];
  uint32_t            paramDataOffset;
  uint32_t            paramDataSize;
  uint8_t             inlineParams[CUDA_INLINE_PARAM_BYTES];
} DispatchCuda;

typedef struct CommandCuda    CommandCuda;

typedef struct QueueCuda {
  GPUQueue        queue;
  CUDA           *driver;
  CUcontext       context;
  CUstream        stream;
  CommandCuda    *freeCommands;
  CommandCuda    *pendingHead;
  CommandCuda    *pendingTail;
  CommandCuda    *commands;
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION   lock;
  CONDITION_VARIABLE condition;
  HANDLE             worker;
#else
  pthread_mutex_t lock;
  pthread_cond_t  condition;
  pthread_t       worker;
#endif
  uint32_t pendingCount;
  bool     stopping;
  bool     workerStarted;
} QueueCuda;

struct CommandCuda {
  GPUCommandBuffer        command;
  GPUComputePassEncoder   compute;
  CommandCuda            *next;
  CommandCuda            *allNext;
  QueueCuda              *owner;
  DispatchCuda           *dispatches;
  ComputePipelineCuda    *pipeline;
  uint8_t                *paramData;
  CUevent                 completion;
  uint64_t                boundParamMask[CUDA_PARAM_MASK_WORD_COUNT];
  uint32_t                dispatchCount;
  uint32_t                dispatchCapacity;
  uint32_t                paramDataCount;
  uint32_t                paramDataCapacity;
  GPUResult               recordResult;
  bool                    pending;
  uint8_t                 boundParams[GPU_SHADER_PTX_MAX_PARAM_BYTES];
};

typedef struct DeviceCuda {
  CUDA         *driver;
  CUdevice      cudaDevice;
  CUcontext     context;
  QueueCuda    *queues;
  uint32_t      maxBlockDim[3];
  uint32_t      maxGridDim[3];
  uint32_t      queueCount;
  uint32_t      maxThreadsPerBlock;
} DeviceCuda;

GPUResult
cuda_push(CUDA    *driver, CUcontext context);
void
cuda_pop(CUDA    *driver);
void
cuda_report(GPUDevice *device, CUresult result, const char *operation);
CudaModule*
cuda_createModule(GPUDevice  *device,
                  const void *image,
                  uint64_t    imageSize);
void
cuda_retainModule(CudaModule    *module);
void
cuda_releaseModule(CudaModule    *module);
CUresult
cuda_getModuleFunction(CudaModule    *module,
                       const char    *name,
                       CUfunction    *outFunction);
void
cuda_queueLock(QueueCuda    *queue);
void
cuda_queueUnlock(QueueCuda    *queue);
void
cuda_queueSignal(QueueCuda    *queue);
void
cuda_recycleCommand(GPUCommandBuffer *cmdb);
CommandCuda*
cuda_createCommand(QueueCuda    *queue);

void
cuda_initInstance(ApiInstance    *api);
void
cuda_initDevice(ApiDevice    *api);
void
cuda_initQueue(ApiCommandQueue    *api);
void
cuda_initBuffer(ApiBuffer    *api);
void
cuda_initTexture(ApiTexture    *api);
void
cuda_initSampler(ApiSampler    *api);
void
cuda_initDescriptor(ApiDescriptor    *api);
void
cuda_initLibrary(ApiLibrary    *api);
void
cuda_initCompute(ApiCompute    *api);
void
cuda_initMultiGPU(ApiMultiGPU    *api);

GPUResult
cuda_getTextureObject(GPUTextureView          *view,
                      const CUDA_TEXTURE_DESC *desc,
                      bool                     exactCoordinates,
                      CUtexObject             *outTexture);
void
cuda_setComputeBuffer(GPUComputePassEncoder *encoder,
                      GPUBuffer             *buffer,
                      uint64_t               offset,
                      uint32_t               index);
void
cuda_setComputeTexture(GPUComputePassEncoder *encoder,
                       GPUTextureView        *view,
                       uint32_t               index);
bool
cuda_bindComputeGroup(GPUComputePassEncoder *pass,
                      GPUPipelineLayout     *pipelineLayout,
                      uint32_t               groupIndex,
                      GPUBindGroup          *group,
                      uint32_t               dynamicOffsetCount,
                      const uint32_t        *dynamicOffsets);
void
cuda_rebindComputeGroups(GPUComputePassEncoder *pass);

static inline uint32_t
cuda_ptxParamSize(ShaderPTXParamKind    kind) {
  switch (kind) {
    case GPUShaderPTXParamBuffer:
    case GPUShaderPTXParamSurface:
    case GPUShaderPTXParamTexture:
    case GPUShaderPTXParamSampledTexture:
      return 8u;

    case GPUShaderPTXParamTextureMetadata:
      return sizeof(CudaTextureMetadata);

    default:
      return 0u;
  }
}

static GPU_INLINE AdapterCuda*
cuda_adapter(const GPUAdapter *adapter) {
  return adapter ? adapter->_priv : NULL;
}

static GPU_INLINE DeviceCuda*
cuda_device(const GPUDevice *device) {
  return device ? device->_priv : NULL;
}

static GPU_INLINE QueueCuda*
cuda_queue(const GPUQueue *queue) {
  return queue ? queue->_priv : NULL;
}

static GPU_INLINE CommandCuda*
cuda_command(const GPUCommandBuffer *command) {
  return command ? command->_priv : NULL;
}

#endif /* gpu_cuda_common_h */
