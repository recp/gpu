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
#include "../../api/adapter_internal.h"
#include "../../api/buffer_internal.h"
#include "../../api/cmdqueue_internal.h"
#include "../../api/descr/descriptor_internal.h"
#include "../../api/device_internal.h"
#include "../../api/frame_internal.h"
#include "../../api/instance_internal.h"
#include "../../api/library_internal.h"
#include "../../api/render/pipeline_internal.h"
#include "../../api/sampler_internal.h"
#include "../../api/surface_internal.h"
#include "../../api/swapchain_internal.h"
#include "../../api/texture_internal.h"

#include <dxgi1_4.h>
#include <d3d12.h>

#define DXCHECK(D) hr = D; if (FAILED(hr)) { goto err; }

typedef struct GPUAdapterDX12 {
  /* IDXGIAdapter1*dxgiAdapter; */
  IUnknown             *dxgiAdapter;
  DXGI_ADAPTER_DESC1    desc1;
  SRWLOCK               formatCapsLock;
  uint32_t              minSubgroupSize;
  uint32_t              maxSubgroupSize;
  char                  name[256];
  bool                  isWarp;
  bool                  formatCapsReady;
  bool                  subgroups;
  bool                  shaderF16;
  bool                  descriptorIndexing;
  GPUFormatCapabilities formatCaps[GPU_FORMAT_COUNT];
} GPUAdapterDX12;

typedef struct GPUDescriptorHeapDX12 {
  ID3D12DescriptorHeap *heap;
  uint64_t             *used;
  uint32_t              descriptorSize;
  uint32_t              capacity;
} GPUDescriptorHeapDX12;

#if GPU_BUILD_WITH_DEBUG_MARKERS
typedef void (WINAPI *DX12PixBeginEventFn)(ID3D12GraphicsCommandList *commandList,
                                           UINT64                      color,
                                           PCSTR                       label);
typedef void (WINAPI *DX12PixEndEventFn)(ID3D12GraphicsCommandList *commandList);
#endif

typedef struct GPUDeviceDX12 {
  ID3D12Device               *d3dDevice;
  ID3D12CommandSignature     *drawSignature;
  ID3D12CommandSignature     *drawIndexedSignature;
  ID3D12CommandSignature     *dispatchSignature;
  GPUQueue                  **createdQueues;
  HMODULE                     dxcModule;
#if GPU_BUILD_WITH_DEBUG_MARKERS
  HMODULE                     pixModule;
  DX12PixBeginEventFn         pixBeginEvent;
  DX12PixEndEventFn           pixEndEvent;
#endif
  GPUDescriptorHeapDX12       resourceDescriptors;
  GPUDescriptorHeapDX12       samplerDescriptors;
  GPUDescriptorHeapDX12       rtvDescriptors;
  GPUDescriptorHeapDX12       dsvDescriptors;
  SRWLOCK                     descriptorLock;
  D3D_ROOT_SIGNATURE_VERSION  rootSignatureVersion;
  D3D_SHADER_MODEL            shaderModel;
  uint32_t                    nCreatedQueues;
  bool                        enhancedBarriers;
  bool                        dxcAvailable;
  bool                        subgroups;
  bool                        shaderF16;
  bool                        shaderF16Enabled;
  bool                        descriptorIndexing;
  bool                        queryResultsReliable;
  bool                        stencilPlaneCopies;
} GPUDeviceDX12;

static inline bool
dx12_combinedStencilPlane(GPUFormat format, uint32_t plane) {
  return plane == 1u &&
         (format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
          format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8);
}

static inline bool
dx12_stencilPlaneCopiesSupported(const GPUDevice *device) {
  const GPUDeviceDX12 *native;

  native = device ? device->_priv : NULL;
  return native && native->stencilPlaneCopies;
}

#if GPU_BUILD_WITH_DEBUG_MARKERS
static inline bool
dx12_beginDebugEvent(GPUDevice                 *device,
                     ID3D12GraphicsCommandList *commandList,
                     const char                *label) {
  GPUDeviceDX12 *deviceDX12;

  deviceDX12 = device ? device->_priv : NULL;
  if (!gpuDeviceDebugMarkersEnabled(device) || !commandList ||
      !label || label[0] == '\0' || !deviceDX12 ||
      !deviceDX12->pixBeginEvent) {
    return false;
  }

  deviceDX12->pixBeginEvent(commandList, 0u, label);
  return true;
}

static inline void
dx12_endDebugEvent(GPUDevice                 *device,
                   ID3D12GraphicsCommandList *commandList) {
  GPUDeviceDX12 *deviceDX12;

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

  if (!gpuDeviceDebugMarkersEnabled(device) || !commandList ||
      !label || label[0] == '\0' ||
      MultiByteToWideChar(CP_UTF8,
                          MB_ERR_INVALID_CHARS,
                          label,
                          -1,
                          name,
                          (int)GPU_ARRAY_LEN(name)) <= 0) {
    return;
  }
  (void)commandList->lpVtbl->SetName(commandList, name);
}
#else
#  define dx12_beginDebugEvent(device, commandList, label) false
#  define dx12_endDebugEvent(device, commandList) ((void)0)
#  define dx12_setCommandListName(device, commandList, label) ((void)0)
#endif

typedef struct GPUQueueDX12 GPUQueueDX12;
typedef struct GPUSwapchainDX12    GPUSwapchainDX12;

typedef struct GPUShaderLibraryDX12 {
  char    *source;
  uint64_t sourceSize;
} GPUShaderLibraryDX12;

typedef struct DX12ShaderCode {
  void  *data;
  SIZE_T size;
} DX12ShaderCode;

typedef struct GPURootBindingDX12 {
  uint32_t            groupIndex;
  uint32_t            binding;
  uint32_t            rootParameter;
  GPUShaderStageFlags visibility;
  GPUBindingType      bindingType;
} GPURootBindingDX12;

typedef struct GPUDescriptorTableDX12 {
  uint32_t            rootParameter;
  uint32_t            descriptorCount;
  uint32_t            rangeCount;
  uint32_t            rangeOffset;
  GPUShaderStageFlags visibility;
} GPUDescriptorTableDX12;

typedef struct GPUPipelineLayoutDX12 {
  ID3D12RootSignature *rootSignature;
  GPURootBindingDX12  *bindings;
  uint32_t             bindingCount;
  uint32_t             rangeCount;
  uint32_t             rootParameterCount;
  uint32_t             groupCount;
  uint32_t             pushConstantRootParameter;
  uint32_t             pushConstantDwordCount;
  uint32_t             groupOffsets[GPU_ENCODER_MAX_BIND_GROUPS + 1u];
  GPUDescriptorTableDX12 resourceTables[GPU_ENCODER_MAX_BIND_GROUPS];
  GPUDescriptorTableDX12 samplerTables[GPU_ENCODER_MAX_BIND_GROUPS];
} GPUPipelineLayoutDX12;

typedef struct GPUBindGroupDX12 {
  GPUDeviceDX12 *device;
  uint32_t       resourceOffset;
  uint32_t       resourceCount;
  uint32_t       samplerOffset;
  uint32_t       samplerCount;
} GPUBindGroupDX12;

typedef struct GPUBufferDX12 {
  ID3D12Resource            *resource;
  void                      *mapped;
  D3D12_GPU_VIRTUAL_ADDRESS  gpuAddress;
  D3D12_RESOURCE_STATES      state;
  bool                       defaultHeap;
} GPUBufferDX12;

typedef struct GPUTextureDX12 {
  ID3D12Resource         *resource;
  D3D12_RESOURCE_STATES  *states;
  D3D12_RESOURCE_STATES   state;
  uint32_t                mipLevelCount;
  uint32_t                arrayLayerCount;
  uint32_t                subresourceCount;
  uint32_t                planeCount;
  bool                    stateUniform;
} GPUTextureDX12;

typedef struct GPURenderPipelineDX12 {
  ID3D12PipelineState       *pipelineState;
  ID3D12RootSignature      *rootSignature;
  D3D12_PRIMITIVE_TOPOLOGY  topology;
  uint32_t                  vertexBufferCount;
  uint32_t                  vertexStrides[
    D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT
  ];
} GPURenderPipelineDX12;

typedef struct GPUComputePipelineDX12 {
  ID3D12PipelineState  *pipelineState;
  ID3D12RootSignature *rootSignature;
} GPUComputePipelineDX12;

typedef struct GPUTextureViewDX12 {
  ID3D12Resource             *resource;
  GPUDeviceDX12              *device;
  D3D12_RESOURCE_STATES      *state;
  GPUTextureDX12             *texture;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv;
  D3D12_CPU_DESCRIPTOR_HANDLE dsv;
  D3D12_SHADER_RESOURCE_VIEW_DESC srv;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
  uint32_t                    width;
  uint32_t                    height;
  uint32_t                    baseMip;
  uint32_t                    mipCount;
  uint32_t                    baseLayer;
  uint32_t                    layerCount;
  uint32_t                    rtvOffset;
  uint32_t                    dsvOffset;
  bool                        hasSrv;
  bool                        hasUav;
  bool                        hasRtv;
  bool                        hasDsv;
  bool                        swapchain;
} GPUTextureViewDX12;

typedef struct GPURenderPassDX12 {
  GPUTextureViewDX12  *depthStencilView;
  GPUTextureViewDX12  *colorViews[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  GPUTextureViewDX12  *resolveViews[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  DXGI_FORMAT          resolveFormats[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  float                clearColors[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS][4];
  float                clearDepth;
  GPULoadOp            loadOps[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  GPUStoreOp           storeOps[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  GPULoadOp            depthLoadOp;
  GPUStoreOp           depthStoreOp;
  GPULoadOp            stencilLoadOp;
  GPUStoreOp           stencilStoreOp;
  uint32_t             colorCount;
  uint32_t             width;
  uint32_t             height;
  uint32_t             clearStencil;
  bool                 depthHasStencil;
} GPURenderPassDX12;

typedef struct GPURenderEncoderDX12 {
  GPUDeviceDX12             *device;
  ID3D12GraphicsCommandList  *commandList;
  ID3D12GraphicsCommandList7 *commandList7;
  ID3D12RootSignature        *rootSignature;
  ID3D12DescriptorHeap       *resourceHeap;
  ID3D12DescriptorHeap       *samplerHeap;
  GPURenderPassDX12          *renderPass;
  GPURenderPipelineDX12      *pipeline;
  GPUBuffer                  *indexBuffer;
  GPUBuffer                  *vertexBuffers[
    D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT
  ];
  uint64_t                    indexOffset;
  uint64_t                    vertexOffsets[
    D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT
  ];
  GPUIndexType                indexType;
  uint32_t                    vertexBufferMask;
  bool                        indexBound;
  bool                        debugEventActive;
} GPURenderEncoderDX12;

typedef struct GPUComputeEncoderDX12 {
  GPUDeviceDX12             *device;
  ID3D12GraphicsCommandList *commandList;
  ID3D12RootSignature       *rootSignature;
  ID3D12DescriptorHeap      *resourceHeap;
  ID3D12DescriptorHeap      *samplerHeap;
  bool                       debugEventActive;
} GPUComputeEncoderDX12;

typedef struct GPUCommandBufferDX12 {
  GPUQueueDX12                 *owner;
  ID3D12CommandAllocator       *allocator;
  ID3D12GraphicsCommandList    *commandList;
  ID3D12GraphicsCommandList7   *commandList7;
  ID3D12QueryHeap              *frameTimeQueries;
  ID3D12Resource               *frameTimeReadback;
  UINT64                       *frameTimeMapped;
  GPUSwapchainDX12             *presentSwapchain;
  struct GPUCommandBufferDX12  *next;
  struct GPUCommandBufferDX12  *poolNext;
  struct GPUCommandBufferDX12  *pendingNext;
  UINT64                        fenceValue;
  GPUCommandBuffer              commandBuffer;
  GPURenderPassDesc             renderPassDesc;
  GPURenderPassDX12             renderPass;
  GPURenderPassEncoder          renderEncoder;
  GPURenderEncoderDX12          renderState;
  GPUComputePassEncoder         computeEncoder;
  GPUComputeEncoderDX12         computeState;
  GPUCopyPassEncoder            copyEncoder;
  bool                          frameTimeActive;
  bool                          copyDebugEventActive;
} GPUCommandBufferDX12;

enum {
  GPU_DX12_BUFFER_TRANSFER_CAPACITY  = 64u * 1024u,
  GPU_DX12_TEXTURE_TRANSFER_CAPACITY = 512u * 1024u,
  GPU_DX12_TRANSFER_SLOT_COUNT       = 8
};

typedef struct GPUTransferSlotDX12 {
  ID3D12CommandAllocator    *allocator;
  ID3D12GraphicsCommandList *commandList;
  ID3D12Resource            *uploadStaging;
  void                      *uploadMapped;
  UINT64                     fenceValue;
  uint64_t                   uploadCapacity;
  uint64_t                   uploadUsed;
  bool                       pending;
} GPUTransferSlotDX12;

struct GPUQueueDX12 {
  GPUQueue                    *queue;
  ID3D12CommandQueue          *commandQueue;
  ID3D12Fence                 *completionFence;
  ID3D12Fence                 *transferFence;
  ID3D12Resource              *readbackStaging;
  GPUCommandBufferDX12        *commands;
  GPUCommandBufferDX12        *freeCommands;
  GPUCommandBufferDX12        *pendingHead;
  GPUCommandBufferDX12        *pendingTail;
  GPUTransferSlotDX12          transferSlots[GPU_DX12_TRANSFER_SLOT_COUNT];
  HANDLE                       completionEvent;
  HANDLE                       transferEvent;
  HANDLE                       worker;
  UINT64                       nextFenceValue;
  UINT64                       finishedFenceValue;
  UINT64                       transferFenceValue;
  UINT64                       timestampFrequency;
  uint64_t                     readbackCapacity;
  D3D12_COMMAND_LIST_TYPE      type;
  uint32_t                     inFlightCount;
  uint32_t                     activeTransferSlot;
  uint32_t                     nextTransferSlot;
  bool                         workerStarted;
  bool                         stopping;
  bool                         transferOpen;
  bool                         transferUpload;
  CRITICAL_SECTION             poolLock;
  CONDITION_VARIABLE           pendingCondition;
};

typedef struct GPUInstanceDX12 {
  IDXGIFactory4 *dxgiFactory;
  UINT           dxgiFactoryFlags;
} GPUInstanceDX12;

typedef struct GPUSamplerDX12 {
  GPUDeviceDX12      *device;
  D3D12_SAMPLER_DESC  desc;
  bool                isStaticSampler;
} GPUSamplerDX12;

typedef struct GPUFrameDX12 {
  GPUSwapchainDX12      *swapchain;
  ID3D12Resource        *renderTarget;
  UINT64                 fenceValue;
  D3D12_RESOURCE_STATES  state;
  GPUFrame               frame;
  GPUTexture             target;
  GPUTextureView         targetView;
  GPUTextureViewDX12     nativeView;
} GPUFrameDX12;

struct GPUSwapchainDX12 {
  GPUQueueDX12         *queue;
  IDXGISwapChain3      *swapchain;
  ID3D12DescriptorHeap *rtvHeap;
  GPUFrameDX12         *frames;
  HANDLE                 frameEvent;
  DXGI_FORMAT           format;
  UINT                  imageCount;
  UINT                  frameIndex;
  UINT                  rtvDescriptorSize;
  UINT                  syncInterval;
  UINT                  presentFlags;
  bool                  frameActive;
  bool                  frameScheduled;
};

typedef struct GPU__DX12 {
  ID3D12Device  *d3dDevice;
  IDXGIFactory4 *dxgiFactory;
  IDXGIAdapter1 *adapter;
} GPU__DX12;

GPU_HIDE
int
dx12_fillSourceSamplerDesc(const GPUStaticSamplerDesc    *sourceDesc,
                           uint32_t                       shaderRegister,
                           D3D12_SHADER_VISIBILITY        visibility,
                           D3D12_STATIC_SAMPLER_DESC     *outDesc);

GPU_HIDE
int
dx12_fillStaticSamplerDesc(const GPUSamplerDesc       *desc,
                           uint32_t                    shaderRegister,
                           uint32_t                    registerSpace,
                           D3D12_SHADER_VISIBILITY     visibility,
                           D3D12_STATIC_SAMPLER_DESC *outDesc);

GPU_HIDE
GPUResult
dx12_createShaderRootSignature(GPUDevice              *device,
                               GPUPipelineLayout      *layout,
                               const GPUShaderLibrary *library,
                               ID3D12RootSignature   **outRootSignature);

GPU_HIDE
void
dx12_setTextureState(GPUTextureDX12        *texture,
                     uint32_t               baseMip,
                     uint32_t               mipCount,
                     uint32_t               baseLayer,
                     uint32_t               layerCount,
                     D3D12_RESOURCE_STATES  state);

GPU_HIDE
bool
dx12_transitionBuffer(ID3D12GraphicsCommandList *commandList,
                      GPUBufferDX12             *buffer,
                      D3D12_RESOURCE_STATES      state);

GPU_HIDE
bool
dx12_transitionTexture(ID3D12GraphicsCommandList *commandList,
                       GPUTextureDX12            *texture,
                       uint32_t                   baseMip,
                       uint32_t                   mipCount,
                       uint32_t                   baseLayer,
                       uint32_t                   layerCount,
                       D3D12_RESOURCE_STATES      state);

GPU_HIDE
bool
dx12_transitionTexturePlane(ID3D12GraphicsCommandList *commandList,
                            GPUTextureDX12            *texture,
                            uint32_t                   baseMip,
                            uint32_t                   mipCount,
                            uint32_t                   baseLayer,
                            uint32_t                   layerCount,
                            uint32_t                   plane,
                            D3D12_RESOURCE_STATES      state);

GPU_HIDE
GPUResult
dx12_beginTransfer(GPUQueue             *queue,
                   D3D12_HEAP_TYPE              heapType,
                   uint64_t                     stagingBytes,
                   uint64_t                     minimumCapacity,
                   ID3D12GraphicsCommandList  **outCommandList,
                   ID3D12Resource             **outStaging,
                   void                       **outMapped,
                   uint64_t                    *outOffset);

GPU_HIDE
GPUResult
dx12_submitTransfer(GPUQueue *queue, bool wait);

GPU_HIDE
void
dx12_abortTransfer(GPUQueue *queue);

GPU_HIDE
GPUResult
dx12_allocateDescriptors(GPUDeviceDX12             *device,
                         D3D12_DESCRIPTOR_HEAP_TYPE type,
                         uint32_t                    count,
                         uint32_t                   *outOffset);

GPU_HIDE
void
dx12_freeDescriptors(GPUDeviceDX12             *device,
                     D3D12_DESCRIPTOR_HEAP_TYPE type,
                     uint32_t                    offset,
                     uint32_t                    count);

GPU_HIDE
D3D12_CPU_DESCRIPTOR_HANDLE
dx12_cpuDescriptor(const GPUDescriptorHeapDX12 *heap, uint32_t offset);

GPU_INLINE
void
dxThrowIfFailed(HRESULT hr) {
  if (FAILED(hr)) {
    /* Print an error message and exit. */
    fprintf(stderr, "An error occurred: 0x%08lx\n", hr);
    exit(EXIT_FAILURE);
  }
}

#endif /* dx12_common_h */
