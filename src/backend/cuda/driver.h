#ifndef gpu_cuda_driver_h
#define gpu_cuda_driver_h

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(WIN32)
#  define CUDA_CALL __stdcall
#else
#  define CUDA_CALL
#endif

typedef int      CUdevice;
typedef int      CUresult;
typedef uint64_t CUdeviceptr;
typedef struct CUctx_st      *CUcontext;
typedef struct CUevent_st    *CUevent;
typedef struct CUfunc_st     *CUfunction;
typedef struct CUmod_st      *CUmodule;
typedef struct CUstream_st   *CUstream;

typedef struct CUuuid {
  uint8_t bytes[16];
} CUuuid;

enum {
  CUDA_MIN_DRIVER_VERSION                    = 11000,
  CUDA_SUCCESS                               = 0,
  CUDA_ERROR_OUT_OF_MEMORY                   = 2,
  CU_STREAM_NON_BLOCKING                     = 1,
  CU_EVENT_DISABLE_TIMING                    = 2,
  CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK  = 1,
  CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X        = 2,
  CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y        = 3,
  CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z        = 4,
  CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X         = 5,
  CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y         = 6,
  CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z         = 7,
  CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING     = 41,
  CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75,
  CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76
};

typedef struct GPUCUDA {
  void *library;

  CUresult (CUDA_CALL *init)(unsigned int flags);
  CUresult (CUDA_CALL *driverGetVersion)(int *driverVersion);
  CUresult (CUDA_CALL *deviceGetCount)(int *count);
  CUresult (CUDA_CALL *deviceGet)(CUdevice *device, int ordinal);
  CUresult (CUDA_CALL *deviceGetName)(char *name, int length, CUdevice device);
  CUresult (CUDA_CALL *deviceGetUuid)(CUuuid *uuid, CUdevice device);
  CUresult (CUDA_CALL *deviceGetLuid)(char *luid,
                                     unsigned int *nodeMask,
                                     CUdevice device);
  CUresult (CUDA_CALL *deviceGetAttribute)(int *value,
                                          int attribute,
                                          CUdevice device);
  CUresult (CUDA_CALL *primaryCtxRetain)(CUcontext *context, CUdevice device);
  CUresult (CUDA_CALL *primaryCtxRelease)(CUdevice device);
  CUresult (CUDA_CALL *ctxPushCurrent)(CUcontext context);
  CUresult (CUDA_CALL *ctxPopCurrent)(CUcontext *context);
  CUresult (CUDA_CALL *streamCreate)(CUstream *stream, unsigned int flags);
  CUresult (CUDA_CALL *streamDestroy)(CUstream stream);
  CUresult (CUDA_CALL *streamSynchronize)(CUstream stream);
  CUresult (CUDA_CALL *eventCreate)(CUevent *event, unsigned int flags);
  CUresult (CUDA_CALL *eventDestroy)(CUevent event);
  CUresult (CUDA_CALL *eventRecord)(CUevent event, CUstream stream);
  CUresult (CUDA_CALL *eventSynchronize)(CUevent event);
  CUresult (CUDA_CALL *memAlloc)(CUdeviceptr *address, size_t sizeBytes);
  CUresult (CUDA_CALL *memFree)(CUdeviceptr address);
  CUresult (CUDA_CALL *memcpyHtoD)(CUdeviceptr dst,
                                  const void *src,
                                  size_t sizeBytes);
  CUresult (CUDA_CALL *memcpyDtoH)(void *dst,
                                  CUdeviceptr src,
                                  size_t sizeBytes);
  CUresult (CUDA_CALL *moduleLoadData)(CUmodule *module,
                                      const void *image,
                                      unsigned int optionCount,
                                      int *options,
                                      void **optionValues);
  CUresult (CUDA_CALL *moduleUnload)(CUmodule module);
  CUresult (CUDA_CALL *moduleGetFunction)(CUfunction *function,
                                         CUmodule module,
                                         const char *name);
  CUresult (CUDA_CALL *launchKernel)(CUfunction function,
                                    unsigned int gridX,
                                    unsigned int gridY,
                                    unsigned int gridZ,
                                    unsigned int blockX,
                                    unsigned int blockY,
                                    unsigned int blockZ,
                                    unsigned int sharedMemoryBytes,
                                    CUstream stream,
                                    void **kernelParams,
                                    void **extra);
  CUresult (CUDA_CALL *getErrorName)(CUresult result, const char **name);
  CUresult (CUDA_CALL *getErrorString)(CUresult result, const char **message);
} GPUCUDA;

GPUCUDA *cuda_driver(void);

#endif
