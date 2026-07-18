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

GPU_HIDE void dx12_initDevice(GPUApiDevice* apiDevice);
// GPU_HIDE void dx12_initRenderPipeline(GPUApiRender* api);
// GPU_HIDE void dx12_initRCE(GPUApiRCE* api);
// GPU_HIDE void dx12_initCmdBuff(GPUApiCommandBuffer* api);
GPU_HIDE void dx12_initCmdQue(GPUApiCommandQueue* api);
GPU_HIDE GPUQueue *dx12_createCommandQueue(GPUDevice *device,
                                                   GPUQueueFlagBits bits);
GPU_HIDE void dx12_destroyCommandQueue(GPUQueue *queue);
GPU_HIDE bool dx12_waitCommandQueueIdle(GPUQueueDX12 *queue);
GPU_HIDE bool dx12_waitQueueFence(GPUQueueDX12 *queue,
                                  UINT64                value,
                                  HANDLE                event);
GPU_HIDE GPUResult dx12_waitDeviceIdle(GPUDevice * __restrict device);
GPU_HIDE DXGI_FORMAT dx12_format(GPUFormat format);
GPU_HIDE void dx12_getFormatCapabilities(
  const GPUAdapter      * __restrict adapter,
  GPUFormat              format,
  GPUFormatCapabilities * __restrict outCaps);
GPU_HIDE void dx12_initCmdbuf(GPUApiCommandBuffer *api);
GPU_HIDE void dx12_initQuery(GPUApiCommandBuffer *api);
GPU_HIDE void dx12_initLibrary(GPUApiLibrary *api);
GPU_HIDE void dx12_initPipelineCache(GPUApiPipelineCache *api);
GPU_HIDE void dx12_initRenderPipeline(GPUApiRender *api);
GPU_HIDE void dx12_initCompute(GPUApiCompute *api);
GPU_HIDE void dx12_initRenderPass(GPUApiRenderPass *api);
GPU_HIDE void dx12_resetCopyScratch(GPUCommandBufferDX12 *command);
GPU_HIDE void dx12_destroyCopyScratch(GPUCommandBufferDX12 *command);
GPU_HIDE void dx12_initRCE(GPUApiRCE *api);
GPU_HIDE void dx12_initBuff(GPUApiBuffer *api);
GPU_HIDE void dx12_initMemory(GPUApiMemory *api);
GPU_HIDE void dx12_initTexture(GPUApiTexture *api);
GPU_HIDE GPUResult dx12_getBufferMemoryRequirements(
  GPUDevice                 *device,
  const GPUBufferCreateInfo *info,
  GPUMemoryRequirements     *outRequirements);
GPU_HIDE GPUResult dx12_getSparseBufferRequirements(
  GPUDevice                   *device,
  const GPUBufferCreateInfo   *info,
  GPUSparseBufferRequirements *outRequirements);
GPU_HIDE GPUResult dx12_createSparseBuffer(GPUDevice                 *device,
                                           const GPUBufferCreateInfo *info,
                                           GPUHeap                   *heap,
                                           GPUBuffer                **outBuffer);
GPU_HIDE GPUResult dx12_createPlacedBuffer(GPUDevice                 *device,
                                           const GPUBufferCreateInfo *info,
                                           GPUHeap                   *heap,
                                           uint64_t                   heapOffset,
                                           GPUBuffer                **outBuffer);
GPU_HIDE GPUResult dx12_getTextureMemoryRequirements(
  GPUDevice                  *device,
  const GPUTextureCreateInfo *info,
  GPUMemoryRequirements      *outRequirements);
GPU_HIDE GPUResult dx12_createPlacedTexture(GPUDevice                  *device,
                                            const GPUTextureCreateInfo *info,
                                            GPUHeap                    *heap,
                                            uint64_t                    heapOffset,
                                            GPUTexture                **outTexture);
GPU_HIDE GPUResult dx12_getSparseTextureRequirements(
  GPUDevice                    *device,
  const GPUTextureCreateInfo   *info,
  GPUSparseTextureRequirements *outRequirements);
GPU_HIDE GPUResult dx12_createSparseTexture(
  GPUDevice                  *device,
  const GPUTextureCreateInfo *info,
  GPUHeap                    *heap,
  GPUTexture                **outTexture);
GPU_HIDE GPUResult dx12_flushTransfers(GPUQueue *queue);
GPU_HIDE void dx12_initSampler(GPUApiSampler *api);
GPU_HIDE void dx12_destroyDescriptorHeaps(GPUDeviceDX12 *device);
// GPU_HIDE void dx12_initPass(GPUApiRenderPass* api);
// GPU_HIDE void dx12_initDepthStencil(GPUApiDepthStencil* api);
// GPU_HIDE void dx12_initVertex(GPUApiVertex* api);
// GPU_HIDE void dx12_initLibrary(GPUApiLibrary* api);
GPU_HIDE void dx12_initSwapchain(GPUApiSwapchain* apiSwapchain);
GPU_HIDE void dx12_initFrame(GPUApiFrame *apiFrame);
GPU_HIDE void dx12_initDescriptor(GPUApiDescriptor *apiDescriptor);
GPU_HIDE void dx12_initInstance(GPUApiInstance *apiInstance);
GPU_HIDE void dx12_initSurface(GPUApiSurface *apiDevice);
GPU_HIDE void dx12_initVRS(GPUApiVRS *api);
GPU_HIDE void dx12_initRayQuery(GPUApiRayQuery *api);
GPU_HIDE void dx12_initRayTracing(GPUApiRayTracing *api);
GPU_HIDE void dx12_initExecutionGraph(GPUApiExecutionGraph *api);
GPU_HIDE void dx12_resetGraphInitializations(GPUCommandBufferDX12 *command);
GPU_HIDE void dx12_submitGraphInitializations(GPUCommandBufferDX12 *command);
GPU_HIDE void dx12_destroyGraphInputScratch(GPUCommandBufferDX12 *command);
GPU_HIDE bool dx12_bindRayTracingGroup(GPURayTracingPassEncoderEXT *pass,
                                       GPUPipelineLayout           *pipelineLayout,
                                       uint32_t                     groupIndex,
                                       GPUBindGroup                *group,
                                       uint32_t                     dynamicOffsetCount,
                                       const uint32_t              *dynamicOffsets);
GPU_HIDE bool dx12_compileShader(GPUDeviceDX12        *device,
                                 GPUShaderLibraryDX12 *library,
                                 const char           *entry,
                                 GPUShaderStageFlags   stage,
                                 DX12ShaderCode       *outCode);
GPU_HIDE bool dx12_compileRayLibrary(GPUDeviceDX12        *device,
                                     GPUShaderLibraryDX12 *library,
                                     DX12ShaderCode       *outCode);
GPU_HIDE bool dx12_compileExecutionGraphLibrary(
  GPUDeviceDX12        *device,
  GPUShaderLibraryDX12 *library,
  DX12ShaderCode       *outCode);
GPU_HIDE void dx12_freeShaderCode(DX12ShaderCode *code);

#endif /* dx12_apis_h */
