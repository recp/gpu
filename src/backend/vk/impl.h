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

#ifndef vk_apis_h
#define vk_apis_h

GPU_HIDE
void
vk_initInstance(GPUInstanceApi *api);

GPU_HIDE
void
vk_initDevice(GPUDeviceApi *api);

GPU_HIDE
void
vk_initBuff(GPUBufferApi *api);

GPU_HIDE
void
vk_initMemory(GPUMemoryApi *api);

GPU_HIDE
void
vk_initMultiGPU(GPUMultiGPUApi *api);

GPU_HIDE
void
vk_initTexture(GPUTextureApi *api);

GPU_HIDE
void
vk_initSampler(GPUSamplerApi *api);

GPU_HIDE
void
vk_initCmdQue(GPUCommandQueueApi *api);

GPU_HIDE
void
vk_initCmdbuf(GPUCommandBufferApi *api);

GPU_HIDE
void
vk_initQuery(GPUCommandBufferApi *api);

GPU_HIDE
void
vk_initSwapchain(GPUSwapchainApi *api);

GPU_HIDE
void
vk_initFrame(GPUFrameApi *api);

GPU_HIDE
void
vk_initDescriptor(GPUDescriptorApi *api);

GPU_HIDE
void
vk_initSurface(GPUSurfaceApi *api);

GPU_HIDE
void
vk_initLibrary(GPULibraryApi *api);

GPU_HIDE
void
vk_initRenderPipeline(GPURenderApi *api);

GPU_HIDE
void
vk_initRenderPass(GPURenderPassApi *api);

GPU_HIDE
void
vk_encodeBarriers(GPUCommandBuffer      *cmdb,
                  const GPUBarrierBatch *barriers);

GPU_HIDE
void
vk_blitTextureRenderFallback(GPUCommandBuffer         *cmdb,
                             const GPUTextureBlitInfo *info);

GPU_HIDE
void
vk_initRCE(GPURCEApi *api);

GPU_HIDE
void
vk_initCompute(GPUComputeApi *api);

GPU_HIDE
void
vk_initPipelineCache(GPUPipelineCacheApi *api);

GPU_HIDE
void
vk_initVRS(GPUVRSApi *api);

GPU_HIDE
void
vk_initRayQuery(GPURayQueryApi *api);

GPU_HIDE
void
vk_initRayTracing(GPURayTracingApi *api);

GPU_HIDE
void
vk_initExecutionGraph(GPUExecutionGraphApi *api);

GPU_HIDE
void
vk_resetGraphInitializations(GPUCommandBufferVk *command);

GPU_HIDE
void
vk_submitGraphInitializations(GPUCommandBufferVk *command);

GPU_HIDE
void
vk_destroyGraphInputScratch(GPUCommandBufferVk *command);

GPU_HIDE
bool
vk_bindRayTracingGroup(GPURayTracingPassEncoderEXT *pass,
                       GPUPipelineLayout           *pipelineLayout,
                       uint32_t                     groupIndex,
                       GPUBindGroup                *group,
                       uint32_t                     dynamicOffsetCount,
                       const uint32_t              *dynamicOffsets);

GPU_HIDE
GPUQueue*
vk_createCommandQueue(GPUDevice       *device,
                      uint32_t         familyIndex,
                      uint32_t         queueIndex,
                      GPUQueueFlagBits bits);

GPU_HIDE
void
vk_destroyCommandQueue(GPUQueue *queue);

GPU_HIDE
GPUResult
vk_waitDeviceIdle(GPUDevice *__restrict device);

GPU_HIDE
GPUResult
vk_waitCommandQueueIdle(GPUQueue *queue);

GPU_HIDE
GPUResult
vk_createBuffer(GPUDevice                 *__restrict device,
                const GPUBufferCreateInfo *__restrict info,
                GPUBuffer                **__restrict outBuffer);

GPU_HIDE
GPUResult
vk_bufferCreateInfo(GPUDevice                 *device,
                    const GPUBufferCreateInfo *info,
                    VkBufferCreateInfo        *outInfo);

GPU_HIDE
GPUResult
vk_wrapBuffer(GPUDevice                 *device,
              const GPUBufferCreateInfo *info,
              const VkBufferCreateInfo  *bufferInfo,
              GPUBufferVk               *state,
              GPUBuffer                **outBuffer);

GPU_HIDE
GPUResult
vk_createHostBuffer(GPUDevice                 *__restrict device,
                    const GPUBufferCreateInfo *__restrict info,
                    GPUBuffer                **__restrict outBuffer);

GPU_HIDE
GPUResult
vk_getBufferMemoryRequirements(GPUDevice                 *device,
                               const GPUBufferCreateInfo *info,
                               GPUMemoryRequirements     *outRequirements);

GPU_HIDE
GPUResult
vk_getSparseBufferRequirements(GPUDevice                   *device,
                               const GPUBufferCreateInfo   *info,
                               GPUSparseBufferRequirements *outRequirements);

GPU_HIDE
GPUResult
vk_createSparseBuffer(GPUDevice                 *device,
                      const GPUBufferCreateInfo *info,
                      GPUHeap                   *heap,
                      GPUBuffer                **outBuffer);

GPU_HIDE
GPUResult
vk_createPlacedBuffer(GPUDevice                 *device,
                      const GPUBufferCreateInfo *info,
                      GPUHeap                   *heap,
                      uint64_t                   heapOffset,
                      GPUBuffer                **outBuffer);

GPU_HIDE
GPUResult
vk_getTextureMemoryRequirements(GPUDevice                  *device,
                                const GPUTextureCreateInfo *info,
                                GPUMemoryRequirements      *outRequirements);

GPU_HIDE
GPUResult
vk_textureCreateInfo(GPUDevice                  *device,
                     const GPUTextureCreateInfo *info,
                     VkImageCreateInfo          *outInfo,
                     VkImageAspectFlags         *outAspect);

GPU_HIDE
GPUResult
vk_finishTexture(GPUDevice                  *device,
                 const GPUTextureCreateInfo *info,
                 const VkImageCreateInfo    *imageInfo,
                 GPUTextureVk               *state,
                 GPUTexture                **outTexture);

GPU_HIDE
GPUResult
vk_createPlacedTexture(GPUDevice                  *device,
                       const GPUTextureCreateInfo *info,
                       GPUHeap                    *heap,
                       uint64_t                    heapOffset,
                       GPUTexture                **outTexture);

GPU_HIDE
GPUResult
vk_getSparseTextureRequirements(GPUDevice                    *device,
                                const GPUTextureCreateInfo   *info,
                                GPUSparseTextureRequirements *outRequirements);

GPU_HIDE
GPUResult
vk_createSparseTexture(GPUDevice                  *device,
                       const GPUTextureCreateInfo *info,
                       GPUHeap                    *heap,
                       GPUTexture                **outTexture);

GPU_HIDE
GPUResult
vk_flushTransfers(GPUQueue *queue);

GPU_HIDE
void
vk_destroyBuffer(GPUBuffer *__restrict buffer);

GPU_HIDE
GPUResult
vk_writeBuffer(GPUQueue   *__restrict queue,
               GPUBuffer  *__restrict buffer,
               uint64_t               dstOffset,
               const void *__restrict data,
               uint64_t               sizeBytes);

#endif /* vk_apis_h */
