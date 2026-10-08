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

#ifndef dx12_apis_h
#define dx12_apis_h

GPU_HIDE
bool
dx12_createAgilityFactory(ID3D12DeviceFactory **outFactory);

GPU_HIDE
bool
dx12_createExperimentalFactory(uint32_t              featureCount,
                               const IID            *features,
                               ID3D12DeviceFactory **outFactory);

GPU_HIDE
HRESULT
dx12_createNativeDevice(ID3D12DeviceFactory *factory,
                        IUnknown            *adapter,
                        REFIID               iid,
                        void               **outDevice);

GPU_HIDE
HRESULT
dx12_getConfigurationInterface(ID3D12DeviceFactory *factory,
                               REFCLSID             classId,
                               REFIID               interfaceId,
                               void               **outInterface);

GPU_HIDE
void
dx12_initDevice(ApiDevice    *apiDevice);

/* GPU_HIDE void dx12_initRenderPipeline(ApiRender* api); */
/* GPU_HIDE void dx12_initRCE(ApiRCE* api); */
/* GPU_HIDE void dx12_initCmdBuff(ApiCommandBuffer* api); */

GPU_HIDE
void
dx12_initCmdQue(ApiCommandQueue    *api);

GPU_HIDE
GPUQueue*
dx12_createCommandQueue(GPUDevice       *device,
                        GPUQueueFlagBits bits);

GPU_HIDE
void
dx12_destroyCommandQueue(GPUQueue *queue);

GPU_HIDE
bool
dx12_waitCommandQueueIdle(QueueDX12    *queue);

GPU_HIDE
bool
dx12_waitQueueFence(QueueDX12    *queue,
                    UINT64        value,
                    HANDLE        event);

GPU_HIDE
GPUResult
dx12_waitDeviceIdle(GPUDevice *__restrict device);

GPU_HIDE
DXGI_FORMAT
dx12_format(GPUFormat format);

GPU_HIDE
void
dx12_getFormatCapabilities(const GPUAdapter      *__restrict adapter,
                           GPUFormat                         format,
                           GPUFormatCapabilities *__restrict outCaps);

GPU_HIDE
void
dx12_initCmdbuf(ApiCommandBuffer    *api);

GPU_HIDE
void
dx12_initQuery(ApiCommandBuffer    *api);

GPU_HIDE
void
dx12_initLibrary(ApiLibrary    *api);

GPU_HIDE
void
dx12_initPipelineCache(ApiPipelineCache    *api);

GPU_HIDE
void
dx12_initRenderPipeline(ApiRender    *api);

GPU_HIDE
void
dx12_initCompute(ApiCompute    *api);

GPU_HIDE
void
dx12_initRenderPass(ApiRenderPass    *api);

GPU_HIDE
void
dx12_encodeBarriers(GPUCommandBuffer      *cmdb,
                    const GPUBarrierBatch *barriers);

GPU_HIDE
void
dx12_blitTexture(GPUCommandBuffer         *cmdb,
                 const GPUTextureBlitInfo *info);

GPU_HIDE
void
dx12_resetCopyScratch(CommandBufferDX12    *command);

GPU_HIDE
void
dx12_destroyCopyScratch(CommandBufferDX12    *command);

GPU_HIDE
void
dx12_initRCE(ApiRCE    *api);

GPU_HIDE
void
dx12_initBuff(ApiBuffer    *api);

GPU_HIDE
void
dx12_initMemory(ApiMemory    *api);

GPU_HIDE
void
dx12_initTexture(ApiTexture    *api);

GPU_HIDE
GPUResult
dx12_getBufferMemoryRequirements(GPUDevice                 *device,
                                 const GPUBufferCreateInfo *info,
                                 GPUMemoryRequirements     *outRequirements);

GPU_HIDE
GPUResult
dx12_bufferDesc(const GPUBufferCreateInfo *info,
                D3D12_RESOURCE_DESC       *outDesc);

GPU_HIDE
GPUResult
dx12_wrapBuffer(GPUDevice                 *device,
                const GPUBufferCreateInfo *info,
                ID3D12Resource            *resource,
                D3D12_RESOURCE_STATES      initialState,
                GPUBuffer                **outBuffer);

GPU_HIDE
GPUResult
dx12_getSparseBufferRequirements(GPUDevice                   *device,
                                 const GPUBufferCreateInfo   *info,
                                 GPUSparseBufferRequirements *outRequirements);

GPU_HIDE
GPUResult
dx12_createSparseBuffer(GPUDevice                 *device,
                        const GPUBufferCreateInfo *info,
                        GPUHeap                   *heap,
                        GPUBuffer                **outBuffer);

GPU_HIDE
GPUResult
dx12_createPlacedBuffer(GPUDevice                 *device,
                        const GPUBufferCreateInfo *info,
                        GPUHeap                   *heap,
                        uint64_t                   heapOffset,
                        GPUBuffer                **outBuffer);

GPU_HIDE
GPUResult
dx12_getTextureMemoryRequirements(GPUDevice                  *device,
                                  const GPUTextureCreateInfo *info,
                                  GPUMemoryRequirements      *outRequirements);

GPU_HIDE
GPUResult
dx12_textureDesc(GPUDevice                  *device,
                 const GPUTextureCreateInfo *info,
                 D3D12_RESOURCE_DESC        *outDesc,
                 D3D12_CLEAR_VALUE          *outClearValue,
                 D3D12_RESOURCE_STATES      *outInitialState,
                 uint32_t                   *outMipLevelCount,
                 uint32_t                   *outArrayLayerCount,
                 uint32_t                   *outPlaneCount,
                 uint32_t                   *outSubresourceCount);

GPU_HIDE
GPUResult
dx12_wrapTexture(GPUDevice                  *device,
                 const GPUTextureCreateInfo *info,
                 ID3D12Resource             *resource,
                 D3D12_RESOURCE_STATES       initialState,
                 uint32_t                    mipLevelCount,
                 uint32_t                    arrayLayerCount,
                 uint32_t                    planeCount,
                 uint32_t                    subresourceCount,
                 GPUTexture                **outTexture);

GPU_HIDE
GPUResult
dx12_createPlacedTexture(GPUDevice                  *device,
                         const GPUTextureCreateInfo *info,
                         GPUHeap                    *heap,
                         uint64_t                    heapOffset,
                         GPUTexture                **outTexture);

GPU_HIDE
GPUResult
dx12_getSparseTextureRequirements(GPUDevice                    *device,
                                  const GPUTextureCreateInfo   *info,
                                  GPUSparseTextureRequirements *outRequirements);

GPU_HIDE
GPUResult
dx12_createSparseTexture(GPUDevice                  *device,
                         const GPUTextureCreateInfo *info,
                         GPUHeap                    *heap,
                         GPUTexture                **outTexture);

GPU_HIDE
GPUResult
dx12_flushTransfers(GPUQueue *queue);

GPU_HIDE
void
dx12_initSampler(ApiSampler    *api);

GPU_HIDE
void
dx12_destroyDescriptorHeaps(DeviceDX12    *device);

/* GPU_HIDE void dx12_initPass(ApiRenderPass* api); */
/* GPU_HIDE void dx12_initDepthStencil(ApiDepthStencil* api); */
/* GPU_HIDE void dx12_initVertex(ApiVertex* api); */
/* GPU_HIDE void dx12_initLibrary(ApiLibrary* api); */

GPU_HIDE
void
dx12_initSwapchain(ApiSwapchain    *apiSwapchain);

GPU_HIDE
void
dx12_initFrame(ApiFrame    *apiFrame);

GPU_HIDE
void
dx12_initDescriptor(ApiDescriptor    *apiDescriptor);

GPU_HIDE
void
dx12_initInstance(ApiInstance    *apiInstance);

GPU_HIDE
void
dx12_initSurface(ApiSurface    *apiDevice);

GPU_HIDE
void
dx12_initVRS(ApiVRS    *api);

GPU_HIDE
void
dx12_initRayQuery(ApiRayQuery    *api);

GPU_HIDE
void
dx12_initRayTracing(ApiRayTracing    *api);

GPU_HIDE
void
dx12_initExecutionGraph(ApiExecutionGraph    *api);

GPU_HIDE
void
dx12_initSamplerFeedback(ApiSamplerFeedback    *api);

GPU_HIDE
void
dx12_initMultiGPU(ApiMultiGPU    *api);

GPU_HIDE
void
dx12_resetGraphInitializations(CommandBufferDX12    *command);

GPU_HIDE
void
dx12_submitGraphInitializations(CommandBufferDX12    *command);

GPU_HIDE
void
dx12_destroyGraphInputScratch(CommandBufferDX12    *command);

GPU_HIDE
void
dx12_rebindRenderGroups(GPURenderPassEncoder *pass);

GPU_HIDE
void
dx12_rebindComputeGroups(GPUComputePassEncoder *pass);

GPU_HIDE
void
dx12_rebindRayGroups(GPURayTracingPassEncoderEXT *pass);

GPU_HIDE
bool
dx12_bindRayTracingGroup(GPURayTracingPassEncoderEXT *pass,
                         GPUPipelineLayout           *pipelineLayout,
                         uint32_t                     groupIndex,
                         GPUBindGroup                *group,
                         uint32_t                     dynamicOffsetCount,
                         const uint32_t              *dynamicOffsets);

GPU_HIDE
bool
dx12_compileShader(DeviceDX12                 *device,
                   GPUShaderLibrary           *library,
                   const char                 *entry,
                   GPUShaderStageFlags         stage,
                   const GPUPipelineConstants *constants,
                   DX12ShaderCode             *outCode);

GPU_HIDE
bool
dx12_compileRayLibrary(DeviceDX12       *device,
                       GPUShaderLibrary *library,
                       uint64_t          entryMask,
                       DX12ShaderCode   *outCode);

GPU_HIDE
bool
dx12_compileExecutionGraphLibrary(DeviceDX12       *device,
                                  GPUShaderLibrary *library,
                                  uint64_t          entryMask,
                                  DX12ShaderCode   *outCode);

GPU_HIDE
uint32_t
dx12_queryDXCTargetProfile(HMODULE module);

GPU_HIDE
void
dx12_freeShaderCode(DX12ShaderCode *code);

#endif /* dx12_apis_h */
