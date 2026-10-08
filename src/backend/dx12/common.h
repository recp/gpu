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

#ifndef dx12_common_h
#define dx12_common_h

#include "../common.h"
#include "buffer_range.h"
#include "../../api/adapter_internal.h"
#include "../../api/buffer_internal.h"
#include "../../api/cmdqueue_internal.h"
#include "../../api/descr/descriptor_internal.h"
#include "../../api/device_internal.h"
#include "../../api/execution_graph_internal.h"
#include "../../api/frame_internal.h"
#include "../../api/instance_internal.h"
#include "../../api/library_internal.h"
#include "../../api/memory_internal.h"
#include "../../api/ray_internal.h"
#include "../../api/render/pipeline_internal.h"
#include "../../api/sampler_internal.h"
#include "../../api/sampler_feedback_internal.h"
#include "../../api/surface_internal.h"
#include "../../api/swapchain_internal.h"
#include "../../api/texture_internal.h"

#include <dxgi1_6.h>
#include <d3d12.h>

#if defined(__ID3D12GraphicsCommandList10_INTERFACE_DEFINED__) \
    && defined(__ID3D12StateObjectProperties1_INTERFACE_DEFINED__) \
    && defined(__ID3D12WorkGraphProperties_INTERFACE_DEFINED__)
#  define GPU_DX12_HAS_EXECUTION_GRAPHS 1
#else
#  define GPU_DX12_HAS_EXECUTION_GRAPHS 0
#endif

#if defined(__ID3D12Device8_INTERFACE_DEFINED__)
#  define GPU_DX12_HAS_SAMPLER_FEEDBACK 1
#else
#  define GPU_DX12_HAS_SAMPLER_FEEDBACK 0
#endif

#define GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE_HLSL "space4"

enum {
  GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE = GPU_ENCODER_MAX_BIND_GROUPS
};

_Static_assert(GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE == 4u,
               "update the HLSL push-constant register space literal");

#define DXCHECK(D) \
  hr = D; \
  if (FAILED(hr)) { \
    goto err; \
  }

#if !GPU_BUILD_WITH_DEBUG_MARKERS
#  define dx12_beginDebugEvent(device, commandList, label) false
#  define dx12_endDebugEvent(device, commandList) ((void)0)
#  define dx12_setCommandListName(device, commandList, label) ((void)0)
#endif

typedef struct AdapterDX12 {
  /* IDXGIAdapter1*dxgiAdapter; */
  IUnknown                        *dxgiAdapter;
  ID3D12Device                    *capabilityDevice;
  GPUSubgroupMatrixPropertiesEXT  *subgroupMatrixProperties;
  DXGI_ADAPTER_DESC1               desc1;
  SRWLOCK                          capabilityLock;
  SRWLOCK                          formatCapsLock;
  SRWLOCK                          subgroupMatrixLock;
  GPUShadingRateFlagsEXT           vrsRates;
  GPUShadingRateCombinerFlagsEXT   vrsCombiners;
  D3D12_VARIABLE_SHADING_RATE_TIER vrsTier;
  D3D12_TILED_RESOURCES_TIER       tiledResourcesTier;
  D3D_SHADER_MODEL                 shaderModel;
  LONG                             capabilityState;
  LONG                             subgroupMatrixState;
  uint32_t                         dxcTargetProfile;
  uint32_t                         samplerFeedbackTier;
  uint32_t                         minSubgroupSize;
  uint32_t                         maxSubgroupSize;
  uint32_t                         subgroupMatrixPropertyCount;
  uint32_t                         vrsTileSize;
  char                             name[256];
  bool                             isWarp;
  bool                             formatCapsReady;
  bool                             subgroups;
  bool                             shaderF16;
  bool                             atomic64;
  bool                             descriptorIndexing;
  bool                             bindless;
  bool                             meshShader;
  bool                             rayQuery;
  bool                             rayTracingPipeline;
  bool                             executionGraph;
  GPUFormatCapabilities            formatCaps[GPU_FORMAT_COUNT];
} AdapterDX12;

typedef struct DescriptorHeapDX12 {
  ID3D12DescriptorHeap *heap;
  uint64_t             *used;
  uint32_t              descriptorSize;
  uint32_t              capacity;
  uint32_t              searchOffset;
} DescriptorHeapDX12;

#if GPU_BUILD_WITH_DEBUG_MARKERS
typedef void (WINAPI *DX12PixBeginEventFn)(ID3D12GraphicsCommandList *commandList,
                                           UINT64                     color,
                                           PCSTR                      label);
typedef void (WINAPI *DX12PixEndEventFn)(ID3D12GraphicsCommandList *commandList);
#endif

typedef struct DeviceDX12 {
  ID3D12Device                    *d3dDevice;
  ID3D12Device2                   *d3dDevice2;
  ID3D12Device5                   *d3dDevice5;
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
  ID3D12Device8                   *d3dDevice8;
#endif
  ID3D12CommandSignature          *drawSignature;
  ID3D12CommandSignature          *drawIndexedSignature;
  ID3D12CommandSignature          *dispatchSignature;
  GPUQueue                       **createdQueues;
  HMODULE                          dxcModule;
#if GPU_BUILD_WITH_DEBUG_MARKERS
  HMODULE                          pixModule;
  DX12PixBeginEventFn              pixBeginEvent;
  DX12PixEndEventFn                pixEndEvent;
#endif
  DescriptorHeapDX12               resourceDescriptors;
  DescriptorHeapDX12               samplerDescriptors;
  DescriptorHeapDX12               rtvDescriptors;
  DescriptorHeapDX12               dsvDescriptors;
  SRWLOCK                          descriptorLock;
  D3D_ROOT_SIGNATURE_VERSION       rootSignatureVersion;
  D3D_SHADER_MODEL                 shaderModel;
  D3D12_RESOURCE_HEAP_TIER         resourceHeapTier;
  D3D12_TILED_RESOURCES_TIER       tiledResourcesTier;
  uint32_t                         samplerFeedbackTier;
  uint32_t                         dxcTargetProfile;
  uint32_t                         uslTargetProfile;
  D3D12_VARIABLE_SHADING_RATE_TIER vrsTier;
  uint32_t                         nCreatedQueues;
  uint32_t                         vrsTileSize;
  bool                             enhancedBarriers;
  bool                             dxcAvailable;
  bool                             subgroups;
  bool                             shaderF16;
  bool                             shaderF16Enabled;
  bool                             atomic64;
  bool                             atomic64Enabled;
  bool                             descriptorIndexing;
  bool                             bindless;
  bool                             meshShader;
  bool                             rayQuery;
  bool                             rayTracingPipeline;
  bool                             executionGraph;
  bool                             subgroupMatrix;
  bool                             subgroupMatrixEnabled;
  bool                             queryResultsReliable;
  bool                             stencilPlaneCopies;
  bool                             manualBlitFiltering;
  bool                             samplerTableOffsetsReliable;
  bool                             rootCbvSpacesReliable;
} DeviceDX12;

typedef struct QueueDX12        QueueDX12;
typedef struct SwapchainDX12    SwapchainDX12;

typedef struct DX12ShaderCacheEntry {
  struct DX12ShaderCacheEntry *next;
  char                        *entry;
  void                        *data;
  SIZE_T                       size;
  GPUShaderStageFlags          stage;
  uint32_t                     constantCount;
  GPUConstant                  constants[];
} DX12ShaderCacheEntry;

typedef struct ShaderLibraryDX12 {
  DX12ShaderCacheEntry *cache;
  char                 *source;
  SRWLOCK               cacheLock;
  uint64_t              sourceSize;
  uint32_t              cacheCount;
  bool                  binary;
} ShaderLibraryDX12;

typedef struct DX12ShaderCode {
  void  *data;
  SIZE_T size;
  bool   owned;
} DX12ShaderCode;

typedef struct RootBindingDX12 {
  uint32_t            groupIndex;
  uint32_t            binding;
  uint32_t            rootParameter;
  GPUShaderStageFlags visibility;
  GPUBindingType      bindingType;
} RootBindingDX12;

typedef struct DescriptorTableDX12 {
  uint32_t            rootParameter;
  uint32_t            descriptorCount;
  uint32_t            descriptorOffset;
  uint32_t            rangeCount;
  uint32_t            rangeOffset;
  uint32_t            nullOffset;
  GPUShaderStageFlags visibility;
} DescriptorTableDX12;

typedef struct PipelineLayoutDX12 {
  ID3D12RootSignature   *rootSignature;
  RootBindingDX12       *bindings;
  uint64_t               rootSignatureKey[2];
  uint32_t               bindingCount;
  uint32_t               rangeCount;
  uint32_t               rootParameterCount;
  uint32_t               groupCount;
  uint32_t               pushConstantRootParameter;
  uint32_t               pushConstantDwordCount;
  uint32_t               samplerDescriptorCount;
  uint32_t               groupOffsets[GPU_ENCODER_MAX_BIND_GROUPS + 1u];
  DescriptorTableDX12    resourceTables[GPU_ENCODER_MAX_BIND_GROUPS];
  DescriptorTableDX12    samplerTables[GPU_ENCODER_MAX_BIND_GROUPS];
  bool                   samplerTableBaseOnly;
} PipelineLayoutDX12;

typedef struct BindGroupDX12 {
  DeviceDX12    *device;
  uint32_t       resourceOffset;
  uint32_t       resourceCount;
  uint32_t       samplerOffset;
  uint32_t       samplerCount;
  uint32_t       entryCount;
  uint32_t       dynamicOffsetCount;
  uint32_t       descriptorOffsets[];
} BindGroupDX12;

_Static_assert(_Alignof(DX12DynamicBufferRange) <= _Alignof(uint32_t),
               "dynamic ranges follow the descriptor-offset tail");

typedef struct SamplerFeedbackMapDX12 {
  DeviceDX12           *device;
  ID3D12Resource       *resource;
  D3D12_RESOURCE_STATES state;
  uint32_t              descriptorOffset;
} SamplerFeedbackMapDX12;

typedef struct BufferDX12 {
  ID3D12Resource           *resource;
  void                     *mapped;
  D3D12_GPU_VIRTUAL_ADDRESS gpuAddress;
  uint64_t                  sizeBytes;
  D3D12_RESOURCE_STATES     state;
  bool                      defaultHeap;
  bool                      sparse;
} BufferDX12;

typedef struct HeapDX12 {
  ID3D12Heap      *heap;
  D3D12_HEAP_TYPE  type;
  D3D12_HEAP_FLAGS flags;
} HeapDX12;

typedef struct AccelerationStructureDX12 {
  DeviceDX12                     *device;
  ID3D12Resource                 *resource;
  ID3D12Resource                 *instanceBuffer;
  D3D12_RAYTRACING_GEOMETRY_DESC *geometries;
  void                           *instanceMapped;
  D3D12_GPU_VIRTUAL_ADDRESS       address;
  D3D12_GPU_VIRTUAL_ADDRESS       instanceAddress;
  uint64_t                        instanceCapacity;
  uint32_t                        geometryCapacity;
} AccelerationStructureDX12;

typedef struct AccelerationStructureEncoderDX12 {
  ID3D12GraphicsCommandList  *commandList;
  ID3D12GraphicsCommandList5 *commandList5;
  bool                        debugEventActive;
} AccelerationStructureEncoderDX12;

typedef struct RayTracingPipelineDX12 {
  ID3D12StateObject           *stateObject;
  ID3D12StateObjectProperties *properties;
  ID3D12RootSignature         *rootSignature;
  wchar_t                    **groupExports;
  uint32_t                     groupCount;
} RayTracingPipelineDX12;

typedef struct ShaderTableDX12 {
  ID3D12Resource                            *resource;
  D3D12_GPU_VIRTUAL_ADDRESS_RANGE            rayGeneration;
  D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE miss;
  D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE hit;
  D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE callable;
} ShaderTableDX12;

typedef struct RayTracingEncoderDX12 {
  DeviceDX12                 *device;
  ID3D12GraphicsCommandList  *commandList;
  ID3D12GraphicsCommandList5 *commandList5;
  ID3D12RootSignature        *rootSignature;
  ID3D12DescriptorHeap       *resourceHeap;
  ID3D12DescriptorHeap       *samplerHeap;
  uint32_t                    resourceOffsets[GPU_ENCODER_MAX_BIND_GROUPS];
  uint32_t                    resourceOffsetMask;
  bool                        debugEventActive;
} RayTracingEncoderDX12;

typedef struct TextureDX12 {
  ID3D12Resource        *resource;
  D3D12_RESOURCE_STATES *states;
  D3D12_PACKED_MIP_INFO  packedMipInfo;
  D3D12_TILE_SHAPE       tileShape;
  D3D12_RESOURCE_STATES  state;
  uint32_t               mipLevelCount;
  uint32_t               arrayLayerCount;
  uint32_t               subresourceCount;
  uint32_t               planeCount;
  bool                   stateUniform;
  bool                   sparse;
} TextureDX12;

typedef struct RenderPipelineDX12 {
  ID3D12PipelineState     *pipelineState;
  ID3D12RootSignature     *rootSignature;
  D3D12_PRIMITIVE_TOPOLOGY topology;
  uint32_t                 vertexBufferCount;
  bool                     mesh;
  uint32_t                 vertexStrides[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
} RenderPipelineDX12;

typedef struct ComputePipelineDX12 {
  ID3D12PipelineState *pipelineState;
  ID3D12RootSignature *rootSignature;
} ComputePipelineDX12;

typedef struct TextureViewDX12 {
  ID3D12Resource                  *resource;
  DeviceDX12                      *device;
  D3D12_RESOURCE_STATES           *state;
  TextureDX12                     *texture;
  D3D12_CPU_DESCRIPTOR_HANDLE      rtv;
  D3D12_CPU_DESCRIPTOR_HANDLE      dsv;
  D3D12_SHADER_RESOURCE_VIEW_DESC  srv;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
  uint32_t                         width;
  uint32_t                         height;
  uint32_t                         baseMip;
  uint32_t                         mipCount;
  uint32_t                         baseLayer;
  uint32_t                         layerCount;
  uint32_t                         subresource;
  uint32_t                         rtvOffset;
  uint32_t                         dsvOffset;
  bool                             hasSrv;
  bool                             hasUav;
  bool                             hasRtv;
  bool                             hasDsv;
  bool                             swapchain;
} TextureViewDX12;

typedef struct RenderPassDX12 {
  TextureViewDX12    *depthStencilView;
  TextureViewDX12    *shadingRateView;
  TextureViewDX12    *colorViews[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  TextureViewDX12    *resolveViews[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  DXGI_FORMAT         resolveFormats[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  float               clearColors[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS][4];
  float               clearDepth;
  GPULoadOp           loadOps[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  GPUStoreOp          storeOps[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  GPULoadOp           depthLoadOp;
  GPUStoreOp          depthStoreOp;
  GPULoadOp           stencilLoadOp;
  GPUStoreOp          stencilStoreOp;
  uint32_t            colorCount;
  uint32_t            width;
  uint32_t            height;
  uint32_t            clearStencil;
  bool                depthHasStencil;
} RenderPassDX12;

typedef struct RenderEncoderDX12 {
  DeviceDX12                 *device;
  ID3D12GraphicsCommandList  *commandList;
  ID3D12GraphicsCommandList5 *commandList5;
  ID3D12GraphicsCommandList6 *commandList6;
  ID3D12GraphicsCommandList7 *commandList7;
  ID3D12RootSignature        *rootSignature;
  ID3D12DescriptorHeap       *resourceHeap;
  ID3D12DescriptorHeap       *samplerHeap;
  RenderPassDX12             *renderPass;
  RenderPipelineDX12         *pipeline;
  GPUBuffer                  *indexBuffer;
  GPUBuffer                  *vertexBuffers[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
  uint64_t                    indexOffset;
  uint64_t                    vertexOffsets[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
  GPUIndexType                indexType;
  uint32_t                    vertexBufferMask;
  uint32_t                    resourceOffsets[GPU_ENCODER_MAX_BIND_GROUPS];
  uint32_t                    resourceOffsetMask;
  bool                        indexBound;
  bool                        debugEventActive;
} RenderEncoderDX12;

typedef struct ComputeEncoderDX12 {
  DeviceDX12                   *device;
  ID3D12GraphicsCommandList    *commandList;
#if GPU_DX12_HAS_EXECUTION_GRAPHS
  ID3D12GraphicsCommandList10  *commandList10;
#endif
  ID3D12RootSignature          *rootSignature;
  ID3D12DescriptorHeap         *resourceHeap;
  ID3D12DescriptorHeap         *samplerHeap;
  GPUExecutionGraphEXT         *executionGraph;
  GPUExecutionGraphInstanceEXT *executionGraphInstance;
  uint32_t                      resourceOffsets[GPU_ENCODER_MAX_BIND_GROUPS];
  uint32_t                      resourceOffsetMask;
  bool                          debugEventActive;
} ComputeEncoderDX12;

typedef struct CopyScratchDX12 {
  ID3D12Resource            *resource;
  struct CopyScratchDX12    *next;
  uint64_t                   capacity;
  uint64_t                   offset;
  D3D12_RESOURCE_STATES      state;
} CopyScratchDX12;

typedef struct DescriptorAllocationDX12 {
  uint32_t offset;
  uint32_t count;
} DescriptorAllocationDX12;

enum {
  GPU_DX12_INLINE_DESCRIPTOR_ALLOCATION_COUNT = 16u,
  GPU_DX12_DESCRIPTOR_ALLOCATION_CHUNK_COUNT  = 64u
};

typedef struct DescriptorAllocationChunkDX12 {
  struct DescriptorAllocationChunkDX12    *next;
  uint32_t                                 count;
  DescriptorAllocationDX12                 allocations[GPU_DX12_DESCRIPTOR_ALLOCATION_CHUNK_COUNT];
} DescriptorAllocationChunkDX12;

typedef struct CommandSamplerHeapDX12 {
  struct CommandSamplerHeapDX12    *next;
  ID3D12DescriptorHeap             *heap;
  uint32_t                          capacity;
} CommandSamplerHeapDX12;

typedef struct ExecutionGraphInputChunkDX12    ExecutionGraphInputChunkDX12;

enum {
  GPU_DX12_GRAPH_INIT_TRACK_COUNT = 8u
};

typedef struct CommandBufferDX12 {
  QueueDX12                             *owner;
  ID3D12CommandAllocator                *allocator;
  ID3D12GraphicsCommandList             *commandList;
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
  ID3D12GraphicsCommandList1            *commandList1;
#endif
  ID3D12GraphicsCommandList5            *commandList5;
  ID3D12GraphicsCommandList6            *commandList6;
  ID3D12GraphicsCommandList7            *commandList7;
#if GPU_DX12_HAS_EXECUTION_GRAPHS
  ID3D12GraphicsCommandList10           *commandList10;
#endif
  ID3D12QueryHeap                       *frameTimeQueries;
  ID3D12Resource                        *frameTimeReadback;
  UINT64                                *frameTimeMapped;
  SwapchainDX12                         *presentSwapchain;
  CopyScratchDX12                       *copyScratch;
  DescriptorAllocationChunkDX12         *descriptorAllocationChunks;
  CommandSamplerHeapDX12                *samplerHeaps;
  ExecutionGraphInputChunkDX12          *graphInputChunks;
  GPUExecutionGraphInstanceEXT          *graphInitializations[GPU_DX12_GRAPH_INIT_TRACK_COUNT];
  struct CommandBufferDX12              *next;
  struct CommandBufferDX12              *poolNext;
  struct CommandBufferDX12              *pendingNext;
  UINT64                                 fenceValue;
  GPUCommandBuffer                       commandBuffer;
  RenderPassDesc                         renderPassDesc;
  RenderPassDX12                         renderPass;
  GPURenderPassEncoder                   renderEncoder;
  RenderEncoderDX12                      renderState;
  GPUComputePassEncoder                  computeEncoder;
  ComputeEncoderDX12                     computeState;
  GPUTransferPassEncoder                 copyEncoder;
  GPUAccelerationStructurePassEncoderEXT rayQueryEncoder;
  AccelerationStructureEncoderDX12       rayQueryState;
  GPURayTracingPassEncoderEXT            rayTracingEncoder;
  RayTracingEncoderDX12                  rayTracingState;
  DescriptorAllocationDX12               descriptorAllocations[GPU_DX12_INLINE_DESCRIPTOR_ALLOCATION_COUNT];
  uint32_t                               descriptorAllocationCount;
  uint32_t                               samplerHeapUseCount;
  uint32_t                               graphInitializationCount;
  bool                                   frameTimeActive;
  bool                                   copyDebugEventActive;
} CommandBufferDX12;

enum {
  GPU_DX12_BUFFER_TRANSFER_CAPACITY  = 256u * 1024u,
  GPU_DX12_TEXTURE_TRANSFER_CAPACITY = 1024u * 1024u,
  GPU_DX12_COPY_SCRATCH_CAPACITY     = 1024u * 1024u,
  GPU_DX12_TRANSFER_SLOT_COUNT       = 8
};

typedef struct TransferSlotDX12 {
  ID3D12CommandAllocator    *allocator;
  ID3D12GraphicsCommandList *commandList;
  ID3D12Resource            *uploadStaging;
  void                      *uploadMapped;
  UINT64                     fenceValue;
  uint64_t                   uploadCapacity;
  uint64_t                   uploadUsed;
  bool                       pending;
} TransferSlotDX12;

struct QueueDX12 {
  GPUQueue               *queue;
  ID3D12CommandQueue     *commandQueue;
  ID3D12Fence            *completionFence;
  ID3D12Fence            *transferFence;
  ID3D12Resource         *readbackStaging;
  CommandBufferDX12      *commands;
  CommandBufferDX12      *freeCommands;
  CommandBufferDX12      *pendingHead;
  CommandBufferDX12      *pendingTail;
  TransferSlotDX12        transferSlots[GPU_DX12_TRANSFER_SLOT_COUNT];
  HANDLE                  completionEvent;
  HANDLE                  transferEvent;
  HANDLE                  worker;
  UINT64                  nextFenceValue;
  UINT64                  finishedFenceValue;
  UINT64                  transferFenceValue;
  UINT64                  timestampFrequency;
  uint64_t                readbackCapacity;
  D3D12_COMMAND_LIST_TYPE type;
  uint32_t                inFlightCount;
  uint32_t                activeTransferSlot;
  uint32_t                nextTransferSlot;
  bool                    workerStarted;
  bool                    stopping;
  bool                    transferOpen;
  bool                    transferUpload;
  CRITICAL_SECTION        poolLock;
  CONDITION_VARIABLE      pendingCondition;
};

typedef struct InstanceDX12 {
  ID3D12DeviceFactory *deviceFactory;
  ID3D12DeviceFactory *linearAlgebraFactory;
  IDXGIFactory4       *dxgiFactory;
  UINT                 dxgiFactoryFlags;
  bool                 allowTearing;
} InstanceDX12;

typedef struct SamplerDX12 {
  DeviceDX12        *device;
  D3D12_SAMPLER_DESC desc;
  bool               isStaticSampler;
} SamplerDX12;

typedef struct FrameDX12 {
  SwapchainDX12        *swapchain;
  ID3D12Resource       *renderTarget;
  UINT64                fenceValue;
  D3D12_RESOURCE_STATES state;
  GPUFrame              frame;
  GPUTexture            target;
  GPUTextureView        targetView;
  TextureViewDX12       nativeView;
} FrameDX12;

struct SwapchainDX12 {
  GPUSwapchain         *gpuSwapchain;
  QueueDX12            *queue;
  IDXGISwapChain3      *swapchain;
  ID3D12DescriptorHeap *rtvHeap;
  FrameDX12            *frames;
  HANDLE                frameEvent;
  DXGI_FORMAT           format;
  UINT                  imageCount;
  UINT                  frameIndex;
  UINT                  rtvDescriptorSize;
  UINT                  syncInterval;
  UINT                  presentFlags;
  UINT                  swapchainFlags;
  bool                  frameActive;
  bool                  frameScheduled;
};

typedef struct GPU__DX12 {
  ID3D12Device  *d3dDevice;
  IDXGIFactory4 *dxgiFactory;
  IDXGIAdapter1 *adapter;
} GPU__DX12;

GPU_HIDE
uint64_t
dx12_memoryCompatibility(GPUDevice                 *device,
                         const D3D12_RESOURCE_DESC *desc);

GPU_HIDE
int
dx12_fillSourceSamplerDesc(const StaticSamplerDesc    *sourceDesc,
                           uint32_t                    shaderRegister,
                           D3D12_SHADER_VISIBILITY     visibility,
                           D3D12_STATIC_SAMPLER_DESC  *outDesc);

GPU_HIDE
int
dx12_fillStaticSamplerDesc(const GPUSamplerDesc      *desc,
                           uint32_t                   shaderRegister,
                           uint32_t                   registerSpace,
                           D3D12_SHADER_VISIBILITY    visibility,
                           D3D12_STATIC_SAMPLER_DESC *outDesc);

GPU_HIDE
GPUResult
dx12_createShaderRootSignature(GPUDevice              *device,
                               GPUPipelineLayout      *layout,
                               const GPUShaderLibrary *library,
                               uint64_t                entryMask,
                               ID3D12RootSignature   **outRootSignature,
                               uint64_t                outKey[2]);

GPU_HIDE
void
dx12_setTextureState(TextureDX12          *texture,
                     uint32_t              baseMip,
                     uint32_t              mipCount,
                     uint32_t              baseLayer,
                     uint32_t              layerCount,
                     D3D12_RESOURCE_STATES state);

GPU_HIDE
bool
dx12_transitionBuffer(ID3D12GraphicsCommandList *commandList,
                      BufferDX12                *buffer,
                      D3D12_RESOURCE_STATES      state);

GPU_HIDE
bool
dx12_transitionTexture(ID3D12GraphicsCommandList *commandList,
                       TextureDX12               *texture,
                       uint32_t                   baseMip,
                       uint32_t                   mipCount,
                       uint32_t                   baseLayer,
                       uint32_t                   layerCount,
                       D3D12_RESOURCE_STATES      state);

GPU_HIDE
bool
dx12_transitionTexturePlane(ID3D12GraphicsCommandList *commandList,
                            TextureDX12               *texture,
                            uint32_t                   baseMip,
                            uint32_t                   mipCount,
                            uint32_t                   baseLayer,
                            uint32_t                   layerCount,
                            uint32_t                   plane,
                            D3D12_RESOURCE_STATES      state);

GPU_HIDE
bool
dx12_transitionSamplerFeedback(ID3D12GraphicsCommandList *commandList,
                               SamplerFeedbackMapDX12    *map,
                               D3D12_RESOURCE_STATES      state);

GPU_HIDE
GPUResult
dx12_beginTransfer(GPUQueue                   *queue,
                   D3D12_HEAP_TYPE             heapType,
                   uint64_t                    stagingBytes,
                   uint64_t                    minimumCapacity,
                   ID3D12GraphicsCommandList **outCommandList,
                   ID3D12Resource            **outStaging,
                   void                      **outMapped,
                   uint64_t                   *outOffset);

GPU_HIDE
GPUResult
dx12_submitTransfer(GPUQueue *queue, bool wait);

GPU_HIDE
void
dx12_abortTransfer(GPUQueue *queue);

GPU_HIDE
GPUResult
dx12_allocateDescriptors(DeviceDX12                *device,
                         D3D12_DESCRIPTOR_HEAP_TYPE type,
                         uint32_t                   count,
                         uint32_t                  *outOffset);

GPU_HIDE
bool
dx12_hasLinearAlgebraCompiler(HMODULE module);

GPU_HIDE
void
dx12_freeDescriptors(DeviceDX12                *device,
                     D3D12_DESCRIPTOR_HEAP_TYPE type,
                     uint32_t                   offset,
                     uint32_t                   count);

GPU_HIDE
GPUResult
dx12_allocateCommandDescriptors(CommandBufferDX12    *command,
                                uint32_t              count,
                                uint32_t             *outOffset);

GPU_HIDE
void
dx12_resetCommandDescriptors(CommandBufferDX12    *command);

GPU_HIDE
void
dx12_destroyCommandDescriptors(CommandBufferDX12    *command);

GPU_HIDE
void
dx12_resetCommandSamplerHeaps(CommandBufferDX12    *command);

GPU_HIDE
void
dx12_destroyCommandSamplerHeaps(CommandBufferDX12    *command);

GPU_HIDE
D3D12_CPU_DESCRIPTOR_HANDLE
dx12_cpuDescriptor(const DescriptorHeapDX12    *heap, uint32_t offset);

GPU_HIDE
D3D12_GPU_DESCRIPTOR_HANDLE
dx12_gpuDescriptor(const DescriptorHeapDX12    *heap, uint32_t offset);

static inline bool
dx12_combinedStencilPlane(GPUFormat format, uint32_t plane) {
  return plane == 1u
         && (format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8
             || format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8);
}

static inline bool
dx12_stencilPlaneCopiesSupported(const GPUDevice *device) {
  const DeviceDX12    *native;

  native = device ? device->_priv : NULL;

  return native && native->stencilPlaneCopies;
}

#if GPU_BUILD_WITH_DEBUG_MARKERS
static inline bool
dx12_beginDebugEvent(GPUDevice                 *device,
                     ID3D12GraphicsCommandList *commandList,
                     const char                *label) {
  DeviceDX12    *deviceDX12;

  deviceDX12 = device ? device->_priv : NULL;

  if (!deviceDebugMarkersEnabled(device) || !commandList
      || !label || label[0] == '\0' || !deviceDX12
      || !deviceDX12->pixBeginEvent) {
    return false;
  }

  deviceDX12->pixBeginEvent(commandList, 0u, label);

  return true;
}

static inline void
dx12_endDebugEvent(GPUDevice                 *device,
                   ID3D12GraphicsCommandList *commandList) {
  DeviceDX12    *deviceDX12;

  deviceDX12 = device ? device->_priv : NULL;

  if (commandList && deviceDX12 && deviceDX12->pixEndEvent) {
    deviceDX12->pixEndEvent(commandList);
  }
}

static inline void
dx12_setCommandListName(GPUDevice                 *device,
                        ID3D12GraphicsCommandList *commandList,
                        const char                *label) {
  wchar_t name[256];

  if (!deviceDebugMarkersEnabled(device) || !commandList
      || !label || label[0] == '\0'
      || MultiByteToWideChar(CP_UTF8,
                             MB_ERR_INVALID_CHARS,
                             label,
                             -1,
                             name,
                             (int)GPU_ARRAY_LEN(name)) <= 0) {
    return;
  }

  (void)commandList->lpVtbl->SetName(commandList, name);
}
#endif

static inline void
dx12_setSwapchainStatus(SwapchainDX12    *swapchain, HRESULT result) {
  GPUSwapchainStatus status;

  if (result == DXGI_STATUS_OCCLUDED) {
    status = GPU_SWAPCHAIN_STATUS_UNAVAILABLE;
  } else {
    status = SUCCEEDED(result) ? GPU_SWAPCHAIN_STATUS_READY
                              : GPU_SWAPCHAIN_STATUS_UNAVAILABLE;
  }

  swapchainSetStatus(swapchain ? swapchain->gpuSwapchain : NULL, status);
}

GPU_INLINE
void
dxThrowIfFailed(HRESULT hr) {
  if (FAILED(hr)) {
    /* print an error message and exit. */
    fprintf(stderr, "An error occurred: 0x%08lx\n", hr);
    exit(EXIT_FAILURE);
  }
}

#endif /* dx12_common_h */
