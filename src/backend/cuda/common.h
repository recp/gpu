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
#include "driver.h"

#if !defined(_WIN32) && !defined(WIN32)
#  include <pthread.h>
#endif

enum {
  CUDA_COMMAND_SLOT_COUNT = 16u,
  CUDA_INITIAL_DISPATCH_CAPACITY = 64u
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
  int      unifiedAddressing;
} GPUAdapterCuda;

typedef struct GPUBufferCuda {
  GPUCUDA    *driver;
  CUdeviceptr address;
} GPUBufferCuda;

typedef struct GPUShaderLibraryCuda {
  char    *source;
  uint64_t size;
} GPUShaderLibraryCuda;

typedef struct GPUComputePipelineCuda {
  GPUComputePipelineState base;
  GPUComputePipeline     *pipeline;
  GPUCUDA                 *driver;
  CUcontext                context;
  CUmodule                 module;
  CUfunction               function;
} GPUComputePipelineCuda;

typedef struct GPUDispatchCuda {
  GPUComputePipeline *pipeline;
  CUdeviceptr         buffer;
  uint32_t            grid[3];
  uint32_t            block[3];
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
  GPUBufferCuda          *buffer;
  CUevent                 completion;
  uint64_t                bufferOffset;
  uint32_t                dispatchCount;
  uint32_t                dispatchCapacity;
  GPUResult               recordResult;
  bool                    pending;
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
void      cuda_queueLock(GPUQueueCuda *queue);
void      cuda_queueUnlock(GPUQueueCuda *queue);
void      cuda_queueSignal(GPUQueueCuda *queue);
void      cuda_recycleCommand(GPUCommandBuffer *cmdb);
GPUCommandCuda *cuda_createCommand(GPUQueueCuda *queue);

void cuda_initInstance(GPUApiInstance *api);
void cuda_initDevice(GPUApiDevice *api);
void cuda_initQueue(GPUApiCommandQueue *api);
void cuda_initBuffer(GPUApiBuffer *api);
void cuda_initLibrary(GPUApiLibrary *api);
void cuda_initCompute(GPUApiCompute *api);

#endif
