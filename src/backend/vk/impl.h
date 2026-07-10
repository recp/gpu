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
GPU_HIDE void vk_initTexture(GPUApiTexture *api);
GPU_HIDE void vk_initSampler(GPUApiSampler *api);
GPU_HIDE void vk_initCmdQue(GPUApiCommandQueue *api);
GPU_HIDE void vk_initCmdbuf(GPUApiCommandBuffer *api);
GPU_HIDE void vk_initSwapChain(GPUApiSwapChain *api);
GPU_HIDE void vk_initFrame(GPUApiFrame *api);
GPU_HIDE void vk_initDescriptor(GPUApiDescriptor *api);
GPU_HIDE void vk_initSurface(GPUApiSurface *api);
GPU_HIDE void vk_initLibrary(GPUApiLibrary *api);
GPU_HIDE void vk_initRenderPipeline(GPUApiRender *api);
GPU_HIDE void vk_initRenderPass(GPUApiRenderPass *api);
GPU_HIDE void vk_initRCE(GPUApiRCE *api);

GPU_HIDE
GPUCommandQueue*
vk_createCommandQueue(GPUDevice       *device,
                      uint32_t         familyIndex,
                      uint32_t         queueIndex,
                      GPUQueueFlagBits bits);

GPU_HIDE
void
vk_destroyCommandQueue(GPUCommandQueue *queue);

GPU_HIDE
GPUResult
vk_createBuffer(GPUDevice                 * __restrict device,
                const GPUBufferCreateInfo * __restrict info,
                GPUBuffer                ** __restrict outBuffer);

GPU_HIDE
void
vk_destroyBuffer(GPUBuffer * __restrict buffer);

GPU_HIDE
GPUResult
vk_writeBuffer(GPUCommandQueue * __restrict queue,
               GPUBuffer       * __restrict buffer,
               uint64_t                     dstOffset,
               const void      * __restrict data,
               uint64_t                     sizeBytes);

#endif /* vk_apis_h */
