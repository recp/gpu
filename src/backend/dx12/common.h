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

typedef struct GPUDeviceDX12 {
  ID3D12Device              *d3dDevice;
  GPUCommandQueue          **createdQueues;
  HMODULE                    dxcModule;
  D3D_ROOT_SIGNATURE_VERSION rootSignatureVersion;
  D3D_SHADER_MODEL           shaderModel;
  uint32_t                   nCreatedQueues;
  bool                       enhancedBarriers;
  bool                       dxcAvailable;
} GPUDeviceDX12;

typedef struct GPUCommandQueueDX12 GPUCommandQueueDX12;
typedef struct GPUSwapChainDX12    GPUSwapChainDX12;

typedef struct GPULibraryDX12 {
  char    *source;
  uint64_t sourceSize;
} GPULibraryDX12;

typedef struct GPURootBindingDX12 {
  uint32_t            groupIndex;
  uint32_t            binding;
  uint32_t            rootParameter;
  GPUShaderStageFlags visibility;
  GPUBindingType      bindingType;
} GPURootBindingDX12;

typedef struct GPUPipelineLayoutDX12 {
  ID3D12RootSignature *rootSignature;
  GPURootBindingDX12  *bindings;
  uint32_t             bindingCount;
  uint32_t             groupCount;
  uint32_t             groupOffsets[GPU_ENCODER_MAX_BIND_GROUPS + 1u];
} GPUPipelineLayoutDX12;

typedef struct GPUBufferDX12 {
  ID3D12Resource            *resource;
  void                      *mapped;
  D3D12_GPU_VIRTUAL_ADDRESS  gpuAddress;
} GPUBufferDX12;

typedef struct GPURenderPipelineDX12 {
  ID3D12PipelineState       *pipelineState;
  ID3D12RootSignature      *rootSignature;
  D3D12_PRIMITIVE_TOPOLOGY  topology;
} GPURenderPipelineDX12;

typedef struct GPUTextureViewDX12 {
  ID3D12Resource             *resource;
  D3D12_RESOURCE_STATES      *state;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv;
  uint32_t                    width;
  uint32_t                    height;
  bool                        swapchain;
} GPUTextureViewDX12;

typedef struct GPURenderPassDX12 {
  GPUTextureViewDX12  *colorViews[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  float                clearColors[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS][4];
  GPULoadOp            loadOps[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  GPUStoreOp           storeOps[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  uint32_t             colorCount;
  uint32_t             width;
  uint32_t             height;
} GPURenderPassDX12;

typedef struct GPURenderEncoderDX12 {
  ID3D12GraphicsCommandList  *commandList;
  ID3D12GraphicsCommandList7 *commandList7;
  ID3D12RootSignature        *rootSignature;
  GPURenderPassDX12          *renderPass;
} GPURenderEncoderDX12;

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
  bool isStaticSampler;
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
