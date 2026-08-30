#ifndef gpu_cuda_driver_h
#define gpu_cuda_driver_h

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(WIN32)
#  define CUDA_CALL __stdcall
#else
#  define CUDA_CALL
#endif

typedef int                       CUdevice;
typedef int                       CUresult;
typedef uint64_t                  CUdeviceptr;
typedef uint64_t                  CUsurfObject;
typedef uint64_t                  CUtexObject;
typedef struct CUarray_st         *CUarray;
typedef struct CUctx_st           *CUcontext;
typedef struct CUevent_st         *CUevent;
typedef struct CUextMemory_st     *CUexternalMemory;
typedef struct CUextSemaphore_st  *CUexternalSemaphore;
typedef struct CUfunc_st          *CUfunction;
typedef struct CUmipmappedArray_st *CUmipmappedArray;
typedef struct CUmod_st           *CUmodule;
typedef struct CUstream_st        *CUstream;

typedef enum CUarray_format {
  CU_AD_FORMAT_UNSIGNED_INT8  = 0x01,
  CU_AD_FORMAT_UNSIGNED_INT16 = 0x02,
  CU_AD_FORMAT_UNSIGNED_INT32 = 0x03,
  CU_AD_FORMAT_SIGNED_INT8    = 0x08,
  CU_AD_FORMAT_SIGNED_INT16   = 0x09,
  CU_AD_FORMAT_SIGNED_INT32   = 0x0a,
  CU_AD_FORMAT_HALF           = 0x10,
  CU_AD_FORMAT_FLOAT          = 0x20
} CUarray_format;

typedef enum CUaddress_mode {
  CU_TR_ADDRESS_MODE_WRAP   = 0,
  CU_TR_ADDRESS_MODE_CLAMP  = 1,
  CU_TR_ADDRESS_MODE_MIRROR = 2,
  CU_TR_ADDRESS_MODE_BORDER = 3
} CUaddress_mode;

typedef enum CUfilter_mode {
  CU_TR_FILTER_MODE_POINT  = 0,
  CU_TR_FILTER_MODE_LINEAR = 1
} CUfilter_mode;

typedef enum CUmemorytype {
  CU_MEMORYTYPE_HOST    = 0x01,
  CU_MEMORYTYPE_DEVICE  = 0x02,
  CU_MEMORYTYPE_ARRAY   = 0x03,
  CU_MEMORYTYPE_UNIFIED = 0x04
} CUmemorytype;

typedef enum CUresourcetype {
  CU_RESOURCE_TYPE_ARRAY            = 0x00,
  CU_RESOURCE_TYPE_MIPMAPPED_ARRAY = 0x01,
  CU_RESOURCE_TYPE_LINEAR           = 0x02,
  CU_RESOURCE_TYPE_PITCH2D          = 0x03
} CUresourcetype;

typedef enum CUresourceViewFormat {
  CU_RES_VIEW_FORMAT_NONE       = 0x00,
  CU_RES_VIEW_FORMAT_UINT_1X8   = 0x01,
  CU_RES_VIEW_FORMAT_UINT_2X8   = 0x02,
  CU_RES_VIEW_FORMAT_UINT_4X8   = 0x03,
  CU_RES_VIEW_FORMAT_SINT_1X8   = 0x04,
  CU_RES_VIEW_FORMAT_SINT_2X8   = 0x05,
  CU_RES_VIEW_FORMAT_SINT_4X8   = 0x06,
  CU_RES_VIEW_FORMAT_UINT_1X16  = 0x07,
  CU_RES_VIEW_FORMAT_UINT_2X16  = 0x08,
  CU_RES_VIEW_FORMAT_UINT_4X16  = 0x09,
  CU_RES_VIEW_FORMAT_SINT_1X16  = 0x0a,
  CU_RES_VIEW_FORMAT_SINT_2X16  = 0x0b,
  CU_RES_VIEW_FORMAT_SINT_4X16  = 0x0c,
  CU_RES_VIEW_FORMAT_UINT_1X32  = 0x0d,
  CU_RES_VIEW_FORMAT_UINT_2X32  = 0x0e,
  CU_RES_VIEW_FORMAT_UINT_4X32  = 0x0f,
  CU_RES_VIEW_FORMAT_SINT_1X32  = 0x10,
  CU_RES_VIEW_FORMAT_SINT_2X32  = 0x11,
  CU_RES_VIEW_FORMAT_SINT_4X32  = 0x12,
  CU_RES_VIEW_FORMAT_FLOAT_1X16 = 0x13,
  CU_RES_VIEW_FORMAT_FLOAT_2X16 = 0x14,
  CU_RES_VIEW_FORMAT_FLOAT_4X16 = 0x15,
  CU_RES_VIEW_FORMAT_FLOAT_1X32 = 0x16,
  CU_RES_VIEW_FORMAT_FLOAT_2X32 = 0x17,
  CU_RES_VIEW_FORMAT_FLOAT_4X32 = 0x18
} CUresourceViewFormat;

typedef struct CUuuid {
  uint8_t bytes[16];
} CUuuid;

typedef struct CUDA_ARRAY3D_DESCRIPTOR {
  size_t         Width;
  size_t         Height;
  size_t         Depth;
  CUarray_format Format;
  uint32_t       NumChannels;
  uint32_t       Flags;
} CUDA_ARRAY3D_DESCRIPTOR;

typedef struct CUDA_MEMCPY3D {
  size_t       srcXInBytes;
  size_t       srcY;
  size_t       srcZ;
  size_t       srcLOD;
  CUmemorytype srcMemoryType;
  const void  *srcHost;
  CUdeviceptr  srcDevice;
  CUarray      srcArray;
  void        *reserved0;
  size_t       srcPitch;
  size_t       srcHeight;
  size_t       dstXInBytes;
  size_t       dstY;
  size_t       dstZ;
  size_t       dstLOD;
  CUmemorytype dstMemoryType;
  void        *dstHost;
  CUdeviceptr  dstDevice;
  CUarray      dstArray;
  void        *reserved1;
  size_t       dstPitch;
  size_t       dstHeight;
  size_t       WidthInBytes;
  size_t       Height;
  size_t       Depth;
} CUDA_MEMCPY3D;

typedef struct CUDA_RESOURCE_DESC {
  CUresourcetype resType;
  union {
    struct {
      CUarray hArray;
    } array;
    struct {
      CUmipmappedArray hMipmappedArray;
    } mipmap;
    struct {
      CUdeviceptr    devPtr;
      CUarray_format format;
      uint32_t       numChannels;
      size_t         sizeInBytes;
    } linear;
    struct {
      CUdeviceptr    devPtr;
      CUarray_format format;
      uint32_t       numChannels;
      size_t         width;
      size_t         height;
      size_t         pitchInBytes;
    } pitch2D;
    struct {
      int reserved[32];
    } reserved;
  } res;
  uint32_t flags;
} CUDA_RESOURCE_DESC;

typedef struct CUDA_TEXTURE_DESC {
  CUaddress_mode addressMode[3];
  CUfilter_mode  filterMode;
  uint32_t       flags;
  uint32_t       maxAnisotropy;
  CUfilter_mode  mipmapFilterMode;
  float          mipmapLevelBias;
  float          minMipmapLevelClamp;
  float          maxMipmapLevelClamp;
  float          borderColor[4];
  int32_t        reserved[12];
} CUDA_TEXTURE_DESC;

typedef struct CUDA_RESOURCE_VIEW_DESC {
  CUresourceViewFormat format;
  size_t               width;
  size_t               height;
  size_t               depth;
  uint32_t             firstMipmapLevel;
  uint32_t             lastMipmapLevel;
  uint32_t             firstLayer;
  uint32_t             lastLayer;
  uint32_t             reserved[16];
} CUDA_RESOURCE_VIEW_DESC;

typedef enum CUexternalMemoryHandleType {
  CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD        = 1,
  CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32     = 2,
  CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT = 3,
  CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP       = 4,
  CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE   = 5
} CUexternalMemoryHandleType;

typedef enum CUexternalSemaphoreHandleType {
  CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD                = 1,
  CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32             = 2,
  CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_KMT         = 3,
  CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE              = 4,
  CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_FD    = 9,
  CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_WIN32 = 10
} CUexternalSemaphoreHandleType;

typedef union CUDAExternalHandle {
  struct {
    void       *handle;
    const void *name;
  } win32;
  const void *object;
  int         fd;
} CUDAExternalHandle;

typedef struct CUDAExternalMemoryHandleDesc {
  CUexternalMemoryHandleType type;
  CUDAExternalHandle         handle;
  uint64_t                   size;
  uint32_t                   flags;
  uint32_t                   reserved[16];
} CUDAExternalMemoryHandleDesc;

typedef struct CUDAExternalMemoryBufferDesc {
  uint64_t offset;
  uint64_t size;
  uint32_t flags;
  uint32_t reserved[16];
} CUDAExternalMemoryBufferDesc;

typedef struct CUDAExternalMemoryMipmappedArrayDesc {
  uint64_t                offset;
  CUDA_ARRAY3D_DESCRIPTOR arrayDesc;
  uint32_t                numLevels;
  uint32_t                reserved[16];
} CUDAExternalMemoryMipmappedArrayDesc;

typedef struct CUDAExternalSemaphoreHandleDesc {
  CUexternalSemaphoreHandleType type;
  CUDAExternalHandle            handle;
  uint32_t                      flags;
  uint32_t                      reserved[16];
} CUDAExternalSemaphoreHandleDesc;

typedef union CUDAExternalSemaphoreNvSciSync {
  void    *fence;
  uint64_t reserved;
} CUDAExternalSemaphoreNvSciSync;

typedef struct CUDAExternalSemaphoreSignalParams {
  struct {
    struct {
      uint64_t value;
    } fence;
    CUDAExternalSemaphoreNvSciSync nvSciSync;
    struct {
      uint64_t key;
    } keyedMutex;
    uint32_t reserved[12];
  } params;
  uint32_t flags;
  uint32_t reserved[16];
} CUDAExternalSemaphoreSignalParams;

typedef struct CUDAExternalSemaphoreWaitParams {
  struct {
    struct {
      uint64_t value;
    } fence;
    CUDAExternalSemaphoreNvSciSync nvSciSync;
    struct {
      uint64_t key;
      uint32_t timeoutMs;
    } keyedMutex;
    uint32_t reserved[10];
  } params;
  uint32_t flags;
  uint32_t reserved[16];
} CUDAExternalSemaphoreWaitParams;

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(CUDAExternalMemoryHandleDesc) == 104u,
               "CUDA external-memory ABI drift");
_Static_assert(sizeof(CUDAExternalMemoryBufferDesc) == 88u,
               "CUDA external-buffer ABI drift");
_Static_assert(sizeof(CUDAExternalMemoryMipmappedArrayDesc) == 120u,
               "CUDA external-mipmapped-array ABI drift");
_Static_assert(sizeof(CUDAExternalSemaphoreHandleDesc) == 96u,
               "CUDA external-semaphore ABI drift");
_Static_assert(sizeof(CUDAExternalSemaphoreSignalParams) == 144u,
               "CUDA external-signal ABI drift");
_Static_assert(sizeof(CUDAExternalSemaphoreWaitParams) == 144u,
               "CUDA external-wait ABI drift");
_Static_assert(sizeof(CUDA_ARRAY3D_DESCRIPTOR) == 40u,
               "CUDA array descriptor ABI drift");
_Static_assert(sizeof(CUDA_MEMCPY3D) == 200u,
               "CUDA 3D copy ABI drift");
_Static_assert(sizeof(CUDA_RESOURCE_DESC) == 144u,
               "CUDA resource descriptor ABI drift");
_Static_assert(sizeof(CUDA_TEXTURE_DESC) == 104u,
               "CUDA texture descriptor ABI drift");
_Static_assert(sizeof(CUDA_RESOURCE_VIEW_DESC) == 112u,
               "CUDA resource-view descriptor ABI drift");
#endif

enum {
  CUDA_MIN_DRIVER_VERSION                    = 11000,
  CUDA_SUCCESS                               = 0,
  CUDA_ERROR_INVALID_VALUE                   = 1,
  CUDA_ERROR_OUT_OF_MEMORY                   = 2,
  CUDA_EXTERNAL_MEMORY_DEDICATED             = 1u,
  CUDA_ARRAY3D_LAYERED                       = 0x01u,
  CUDA_ARRAY3D_SURFACE_LDST                  = 0x02u,
  CUDA_ARRAY3D_CUBEMAP                       = 0x04u,
  CUDA_ARRAY3D_COLOR_ATTACHMENT              = 0x20u,
  CU_TRSF_READ_AS_INTEGER                    = 0x01u,
  CU_TRSF_NORMALIZED_COORDINATES             = 0x02u,
  CU_TRSF_SRGB                               = 0x10u,
  CU_TRSF_SEAMLESS_CUBEMAP                   = 0x40u,
  CU_STREAM_NON_BLOCKING                     = 1,
  CU_EVENT_DISABLE_TIMING                    = 2,
  CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK  = 1,
  CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X        = 2,
  CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y        = 3,
  CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z        = 4,
  CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X         = 5,
  CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y         = 6,
  CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z         = 7,
  CU_DEVICE_ATTRIBUTE_WARP_SIZE              = 10,
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
  CUresult (CUDA_CALL *importExternalMemory)(
    CUexternalMemory                   *externalMemory,
    const CUDAExternalMemoryHandleDesc *desc
  );
  CUresult (CUDA_CALL *externalMemoryGetMappedBuffer)(
    CUdeviceptr                        *address,
    CUexternalMemory                    externalMemory,
    const CUDAExternalMemoryBufferDesc *desc
  );
  CUresult (CUDA_CALL *externalMemoryGetMappedMipmappedArray)(
    CUmipmappedArray                           *mipmap,
    CUexternalMemory                            externalMemory,
    const CUDAExternalMemoryMipmappedArrayDesc *desc
  );
  CUresult (CUDA_CALL *destroyExternalMemory)(
    CUexternalMemory externalMemory
  );
  CUresult (CUDA_CALL *importExternalSemaphore)(
    CUexternalSemaphore                   *externalSemaphore,
    const CUDAExternalSemaphoreHandleDesc *desc
  );
  CUresult (CUDA_CALL *signalExternalSemaphoresAsync)(
    const CUexternalSemaphore                *semaphores,
    const CUDAExternalSemaphoreSignalParams  *params,
    unsigned int                              count,
    CUstream                                  stream
  );
  CUresult (CUDA_CALL *waitExternalSemaphoresAsync)(
    const CUexternalSemaphore              *semaphores,
    const CUDAExternalSemaphoreWaitParams  *params,
    unsigned int                            count,
    CUstream                                stream
  );
  CUresult (CUDA_CALL *destroyExternalSemaphore)(
    CUexternalSemaphore externalSemaphore
  );
  CUresult (CUDA_CALL *memAlloc)(CUdeviceptr *address, size_t sizeBytes);
  CUresult (CUDA_CALL *memFree)(CUdeviceptr address);
  CUresult (CUDA_CALL *memcpyHtoD)(CUdeviceptr dst,
                                  const void *src,
                                  size_t sizeBytes);
  CUresult (CUDA_CALL *memcpyDtoH)(void *dst,
                                  CUdeviceptr src,
                                  size_t sizeBytes);
  CUresult (CUDA_CALL *array3DCreate)(
    CUarray                       *array,
    const CUDA_ARRAY3D_DESCRIPTOR *desc
  );
  CUresult (CUDA_CALL *arrayDestroy)(CUarray array);
  CUresult (CUDA_CALL *mipmappedArrayCreate)(
    CUmipmappedArray              *array,
    const CUDA_ARRAY3D_DESCRIPTOR *desc,
    unsigned int                   mipLevelCount
  );
  CUresult (CUDA_CALL *mipmappedArrayDestroy)(CUmipmappedArray array);
  CUresult (CUDA_CALL *mipmappedArrayGetLevel)(CUarray          *level,
                                                CUmipmappedArray  array,
                                                unsigned int      mipLevel);
  CUresult (CUDA_CALL *memcpy3D)(const CUDA_MEMCPY3D *copy);
  CUresult (CUDA_CALL *surfObjectCreate)(
    CUsurfObject             *surface,
    const CUDA_RESOURCE_DESC *desc
  );
  CUresult (CUDA_CALL *surfObjectDestroy)(CUsurfObject surface);
  CUresult (CUDA_CALL *texObjectCreate)(
    CUtexObject                   *texture,
    const CUDA_RESOURCE_DESC      *resourceDesc,
    const CUDA_TEXTURE_DESC       *textureDesc,
    const CUDA_RESOURCE_VIEW_DESC *resourceViewDesc
  );
  CUresult (CUDA_CALL *texObjectDestroy)(CUtexObject texture);
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
  int driverVersion;
} GPUCUDA;

GPUCUDA *cuda_driver(void);

#endif
