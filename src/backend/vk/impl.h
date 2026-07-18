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

GPU_HIDE void vk_initInstance(GPUApiInstance *api);
GPU_HIDE void vk_initDevice(GPUApiDevice *api);
GPU_HIDE void vk_initBuff(GPUApiBuffer *api);
GPU_HIDE void vk_initMemory(GPUApiMemory *api);
GPU_HIDE void vk_initTexture(GPUApiTexture *api);
GPU_HIDE void vk_initSampler(GPUApiSampler *api);
GPU_HIDE void vk_initCmdQue(GPUApiCommandQueue *api);
GPU_HIDE void vk_initCmdbuf(GPUApiCommandBuffer *api);
GPU_HIDE void vk_initQuery(GPUApiCommandBuffer *api);
GPU_HIDE void vk_initSwapchain(GPUApiSwapchain *api);
GPU_HIDE void vk_initFrame(GPUApiFrame *api);
GPU_HIDE void vk_initDescriptor(GPUApiDescriptor *api);
GPU_HIDE void vk_initSurface(GPUApiSurface *api);
GPU_HIDE void vk_initLibrary(GPUApiLibrary *api);
GPU_HIDE void vk_initRenderPipeline(GPUApiRender *api);
GPU_HIDE void vk_initRenderPass(GPUApiRenderPass *api);
GPU_HIDE void vk_initRCE(GPUApiRCE *api);
GPU_HIDE void vk_initCompute(GPUApiCompute *api);
GPU_HIDE void vk_initPipelineCache(GPUApiPipelineCache *api);
GPU_HIDE void vk_initVRS(GPUApiVRS *api);
GPU_HIDE void vk_initRayQuery(GPUApiRayQuery *api);
GPU_HIDE void vk_initRayTracing(GPUApiRayTracing *api);
GPU_HIDE void vk_initExecutionGraph(GPUApiExecutionGraph *api);

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
vk_waitDeviceIdle(GPUDevice * __restrict device);

GPU_HIDE
GPUResult
vk_createBuffer(GPUDevice                 * __restrict device,
                const GPUBufferCreateInfo * __restrict info,
                GPUBuffer                ** __restrict outBuffer);

GPU_HIDE
GPUResult
vk_createHostBuffer(GPUDevice                 * __restrict device,
                    const GPUBufferCreateInfo * __restrict info,
                    GPUBuffer                ** __restrict outBuffer);

GPU_HIDE
GPUResult
vk_getBufferMemoryRequirements(GPUDevice                 *device,
                               const GPUBufferCreateInfo *info,
                               GPUMemoryRequirements     *outRequirements);

GPU_HIDE
GPUResult
vk_getSparseBufferRequirements(
  GPUDevice                   *device,
  const GPUBufferCreateInfo   *info,
  GPUSparseBufferRequirements *outRequirements
);

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
vk_createPlacedTexture(GPUDevice                  *device,
                       const GPUTextureCreateInfo *info,
                       GPUHeap                    *heap,
                       uint64_t                    heapOffset,
                       GPUTexture                **outTexture);

GPU_HIDE
GPUResult
vk_getSparseTextureRequirements(
  GPUDevice                    *device,
  const GPUTextureCreateInfo   *info,
  GPUSparseTextureRequirements *outRequirements
);

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
vk_destroyBuffer(GPUBuffer * __restrict buffer);

GPU_HIDE
GPUResult
vk_writeBuffer(GPUQueue * __restrict queue,
               GPUBuffer       * __restrict buffer,
               uint64_t                     dstOffset,
               const void      * __restrict data,
               uint64_t                     sizeBytes);

#endif /* vk_apis_h */
