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

typedef struct GPUPhysicalDeviceDX12 {
  /* IDXGIAdapter1*dxgiAdapter; */
  IUnknown          *dxgiAdapter;
  DXGI_ADAPTER_DESC1 desc1;
  char               name[256];
  bool               isWarp;
} GPUPhysicalDeviceDX12;

typedef struct GPUDescriptorHeapDX12 {
  ID3D12DescriptorHeap *heap;
  uint64_t             *used;
  uint32_t              descriptorSize;
  uint32_t              capacity;
} GPUDescriptorHeapDX12;

typedef struct GPUDeviceDX12 {
  ID3D12Device               *d3dDevice;
  ID3D12CommandSignature     *drawSignature;
  ID3D12CommandSignature     *drawIndexedSignature;
  ID3D12CommandSignature     *dispatchSignature;
  GPUCommandQueue           **createdQueues;
  HMODULE                     dxcModule;
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
} GPUDeviceDX12;

typedef struct GPUCommandQueueDX12 GPUCommandQueueDX12;
typedef struct GPUSwapChainDX12    GPUSwapChainDX12;

typedef struct GPULibraryDX12 {
  char    *source;
  uint64_t sourceSize;
} GPULibraryDX12;

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
  uint32_t                    width;
  uint32_t                    height;
  uint32_t                    baseMip;
  uint32_t                    mipCount;
  uint32_t                    baseLayer;
  uint32_t                    layerCount;
  uint32_t                    rtvOffset;
  uint32_t                    dsvOffset;
  bool                        hasSrv;
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
} GPURenderEncoderDX12;

typedef struct GPUComputeEncoderDX12 {
  GPUDeviceDX12             *device;
  ID3D12GraphicsCommandList *commandList;
  ID3D12RootSignature       *rootSignature;
  ID3D12DescriptorHeap      *resourceHeap;
  ID3D12DescriptorHeap      *samplerHeap;
} GPUComputeEncoderDX12;

typedef struct GPUCommandBufferDX12 {
  GPUCommandQueueDX12          *owner;
  ID3D12CommandAllocator       *allocator;
  ID3D12GraphicsCommandList    *commandList;
  ID3D12GraphicsCommandList7   *commandList7;
  GPUSwapChainDX12             *presentSwapchain;
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
} GPUCommandBufferDX12;

struct GPUCommandQueueDX12 {
  GPUCommandQueue        *queue;
  ID3D12CommandQueue     *commandQueue;
  ID3D12Fence            *completionFence;
  GPUCommandBufferDX12   *commands;
  GPUCommandBufferDX12   *freeCommands;
  GPUCommandBufferDX12   *pendingHead;
  GPUCommandBufferDX12   *pendingTail;
  HANDLE                  completionEvent;
  HANDLE                  worker;
  UINT64                  nextFenceValue;
  D3D12_COMMAND_LIST_TYPE type;
  bool                    workerStarted;
  bool                    stopping;
  CRITICAL_SECTION        poolLock;
  CONDITION_VARIABLE      pendingCondition;
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
  GPUSwapChainDX12      *swapchain;
  ID3D12Resource        *renderTarget;
  D3D12_RESOURCE_STATES  state;
  GPUFrame               frame;
  GPUTexture             target;
  GPUTextureView         targetView;
  GPUTextureViewDX12     nativeView;
} GPUFrameDX12;

struct GPUSwapChainDX12 {
  GPUCommandQueueDX12  *queue;
  IDXGISwapChain3      *swapChain;
  ID3D12DescriptorHeap *rtvHeap;
  GPUFrameDX12         *frames;
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
dx12_fillStaticSamplerDescFromUSL(const GPUUSLStaticSamplerDesc *uslDesc,
                                  uint32_t shaderRegister,
                                  D3D12_SHADER_VISIBILITY visibility,
                                  D3D12_STATIC_SAMPLER_DESC *outDesc);

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
dx12_transitionTexture(ID3D12GraphicsCommandList *commandList,
                       GPUTextureDX12            *texture,
                       uint32_t                   baseMip,
                       uint32_t                   mipCount,
                       uint32_t                   baseLayer,
                       uint32_t                   layerCount,
                       D3D12_RESOURCE_STATES      state);

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
