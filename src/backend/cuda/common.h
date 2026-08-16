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
#include "../../api/pipeline_cache_internal.h"
#include "../../api/sampler_internal.h"
#include "../../api/texture_internal.h"
#include "driver.h"

#if !defined(_WIN32) && !defined(WIN32)
#  include <pthread.h>
#endif

enum {
  CUDA_COMMAND_SLOT_COUNT        = 16u,
  CUDA_INITIAL_DISPATCH_CAPACITY = 64u,
  CUDA_INLINE_PARAM_BYTES        = 64u,
  CUDA_INLINE_TEXTURE_CACHE_CAPACITY = 4u,
  CUDA_TEXTURE_CACHE_CAPACITY        = 64u,
  CUDA_PARAM_MASK_WORD_COUNT     =
    GPU_SHADER_PTX_MAX_PARAM_COUNT / 64u
};

typedef struct GPUAdapterCuda {
  GPUCUDA *driver;
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
} GPUAdapterCuda;

typedef struct GPUBufferCuda {
  GPUCUDA          *driver;
  CUexternalMemory  externalMemory;
  CUdeviceptr       address;
} GPUBufferCuda;

typedef struct GPUTextureCuda {
  GPUCUDA *driver;
  CUarray  array;
} GPUTextureCuda;

typedef struct GPUCudaTextureCacheEntry {
  CUDA_TEXTURE_DESC desc;
  CUtexObject       texture;
} GPUCudaTextureCacheEntry;

typedef struct GPUTextureViewCuda {
  GPUCUDA                  *driver;
  GPUCudaTextureCacheEntry *cache;
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION lock;
#else
  pthread_mutex_t lock;
#endif
  CUsurfObject             surface;
  uint32_t                 cacheCount;
  uint32_t                 cacheCapacity;
  bool                     cacheDynamic;
  GPUCudaTextureCacheEntry inlineCache[CUDA_INLINE_TEXTURE_CACHE_CAPACITY];
} GPUTextureViewCuda;

typedef struct GPUCudaTextureMetadata {
  uint32_t mipLevelCount;
  uint32_t arrayLayerCount;
  uint32_t sampleCount;
  uint32_t reserved;
} GPUCudaTextureMetadata;

_Static_assert(sizeof(GPUCudaTextureMetadata) == 16u,
               "CUDA texture metadata ABI drift");

static inline uint32_t
cuda_ptxParamSize(GPUShaderPTXParamKind kind) {
  switch (kind) {
    case GPUShaderPTXParamBuffer:
    case GPUShaderPTXParamSurface:
    case GPUShaderPTXParamTexture:
    case GPUShaderPTXParamSampledTexture:
      return 8u;
    case GPUShaderPTXParamTextureMetadata:
      return sizeof(GPUCudaTextureMetadata);
    default:
      return 0u;
  }
}

typedef struct GPUSemaphoreCuda {
  GPUCUDA             *driver;
  CUexternalSemaphore  semaphore;
} GPUSemaphoreCuda;

typedef struct GPUCudaModule {
  GPUCUDA   *driver;
  CUcontext context;
  CUmodule  module;
  uint32_t  refCount;
} GPUCudaModule;

typedef struct GPUShaderLibraryCuda {
  GPUCudaModule *module;
} GPUShaderLibraryCuda;

typedef struct GPUComputePipelineCuda {
  GPUComputePipelineState base;
  GPUComputePipeline     *pipeline;
  GPUCudaModule           *module;
  GPUStaticSamplerDesc    *staticSamplers;
  CUfunction               function;
  uint32_t                 paramCount;
  uint32_t                 paramDataSize;
  uint32_t                 staticSamplerCount;
  GPUShaderPTXParamInfo     params[];
} GPUComputePipelineCuda;

typedef struct GPUDispatchCuda {
  GPUComputePipeline *pipeline;
  uint32_t            grid[3];
  uint32_t            block[3];
  uint32_t            paramDataOffset;
  uint32_t            paramDataSize;
  uint8_t             inlineParams[CUDA_INLINE_PARAM_BYTES];
} GPUDispatchCuda;

typedef struct GPUCommandCuda GPUCommandCuda;

typedef struct GPUQueueCuda {
  GPUQueue        queue;
  GPUCUDA        *driver;
  CUcontext       context;
  CUstream        stream;
  GPUCommandCuda *freeCommands;
  GPUCommandCuda *pendingHead;
  GPUCommandCuda *pendingTail;
  GPUCommandCuda *commands;
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION lock;
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
} GPUQueueCuda;

struct GPUCommandCuda {
  GPUCommandBuffer        command;
  GPUComputePassEncoder   compute;
  GPUCommandCuda         *next;
  GPUCommandCuda         *allNext;
  GPUQueueCuda           *owner;
  GPUDispatchCuda        *dispatches;
  GPUComputePipelineCuda *pipeline;
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

typedef struct GPUDeviceCuda {
  GPUCUDA      *driver;
  CUdevice      cudaDevice;
  CUcontext     context;
  GPUQueueCuda *queues;
  uint32_t      maxBlockDim[3];
  uint32_t      maxGridDim[3];
  uint32_t      queueCount;
  uint32_t      maxThreadsPerBlock;
} GPUDeviceCuda;

static GPU_INLINE GPUAdapterCuda *
cuda_adapter(const GPUAdapter *adapter) {
  return adapter ? adapter->_priv : NULL;
}

static GPU_INLINE GPUDeviceCuda *
cuda_device(const GPUDevice *device) {
  return device ? device->_priv : NULL;
}

static GPU_INLINE GPUQueueCuda *
cuda_queue(const GPUQueue *queue) {
  return queue ? queue->_priv : NULL;
}

static GPU_INLINE GPUCommandCuda *
cuda_command(const GPUCommandBuffer *command) {
  return command ? command->_priv : NULL;
}

GPUResult cuda_push(GPUCUDA *driver, CUcontext context);
void      cuda_pop(GPUCUDA *driver);
void      cuda_report(GPUDevice *device, CUresult result, const char *operation);
GPUCudaModule *cuda_createModule(GPUDevice   *device,
                                 const void  *image,
                                 uint64_t     imageSize);
void           cuda_retainModule(GPUCudaModule *module);
void           cuda_releaseModule(GPUCudaModule *module);
CUresult       cuda_getModuleFunction(GPUCudaModule *module,
                                      const char    *name,
                                      CUfunction    *outFunction);
void      cuda_queueLock(GPUQueueCuda *queue);
void      cuda_queueUnlock(GPUQueueCuda *queue);
void      cuda_queueSignal(GPUQueueCuda *queue);
void      cuda_recycleCommand(GPUCommandBuffer *cmdb);
GPUCommandCuda *cuda_createCommand(GPUQueueCuda *queue);

void cuda_initInstance(GPUApiInstance *api);
void cuda_initDevice(GPUApiDevice *api);
void cuda_initQueue(GPUApiCommandQueue *api);
void cuda_initBuffer(GPUApiBuffer *api);
void cuda_initTexture(GPUApiTexture *api);
void cuda_initSampler(GPUApiSampler *api);
void cuda_initDescriptor(GPUApiDescriptor *api);
void cuda_initLibrary(GPUApiLibrary *api);
void cuda_initCompute(GPUApiCompute *api);
void cuda_initMultiGPU(GPUApiMultiGPU *api);

bool cuda_samplerDescSupported(const GPUSamplerDesc *desc);
bool cuda_staticSamplerDescSupported(const GPUStaticSamplerDesc *desc);
GPUResult cuda_getTextureObject(GPUTextureView         *view,
                                const CUDA_TEXTURE_DESC *desc,
                                CUtexObject             *outTexture);
void cuda_setComputeBuffer(GPUComputePassEncoder *encoder,
                           GPUBuffer             *buffer,
                           uint64_t               offset,
                           uint32_t               index);
void cuda_setComputeTexture(GPUComputePassEncoder *encoder,
                            GPUTextureView        *view,
                            uint32_t               index);
bool cuda_bindComputeGroup(GPUComputePassEncoder *pass,
                           GPUPipelineLayout     *pipelineLayout,
                           uint32_t               groupIndex,
                           GPUBindGroup          *group,
                           uint32_t               dynamicOffsetCount,
                           const uint32_t        *dynamicOffsets);
void cuda_rebindComputeGroups(GPUComputePassEncoder *pass);

#endif
