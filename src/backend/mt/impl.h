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

#ifndef mt_apis_h
#define mt_apis_h

GPU_HIDE
void
mt_initDevice(GPUDeviceApi *apiDevice);

GPU_HIDE
void
mt_initRenderPipeline(GPURenderApi *api);

GPU_HIDE
void
mt_initRCE(GPURCEApi *api);

GPU_HIDE
void
mt_initCompute(GPUComputeApi *api);

GPU_HIDE
void
mt_initCmdBuff(GPUCommandBufferApi *api);

GPU_HIDE
void
mt_initCmdQue(GPUCommandQueueApi *api);

GPU_HIDE
void
mt_initBuff(GPUBufferApi *api);

GPU_HIDE
void
mt_initMemory(GPUMemoryApi *api);

GPU_HIDE
void
mt_initMultiGPU(GPUMultiGPUApi *api);

GPU_HIDE
void
mt_initTexture(GPUTextureApi *api);

GPU_HIDE
void
mt_initTensor(GPUTensorApi *api);

GPU_HIDE
void
mt_initML(GPUMLApi *api);

GPU_HIDE
void
mt_initRenderPass(GPURenderPassApi *api);

GPU_HIDE
void
mt_encodeBarriers(GPUCommandBuffer      *cmdb,
                  const GPUBarrierBatch *barriers);

GPU_HIDE
void
mt_initDepthStencil(GPUDepthStencilApi *api);

GPU_HIDE
void
mt_initVertex(GPUVertexApi *api);

GPU_HIDE
void
mt_initLibrary(GPULibraryApi *api);

GPU_HIDE
void
mt_initSampler(GPUSamplerApi *api);

GPU_HIDE
void
mt_initSwapchain(GPUSwapchainApi *api);

GPU_HIDE
void
mt_initFrame(GPUFrameApi *api);

GPU_HIDE
void
mt_initInstance(GPUInstanceApi *api);

GPU_HIDE
void
mt_initSurface(GPUSurfaceApi *apiDevice);

GPU_HIDE
void
mt_initPipelineCache(GPUPipelineCacheApi *api);

GPU_HIDE
void
mt_initVRS(GPUVRSApi *api);

GPU_HIDE
void
mt_initRayQuery(GPURayQueryApi *api);

GPU_HIDE
void
mt_initDescriptor(GPUDescriptorApi *api);

GPU_HIDE
void
mt_blitTexture(GPUCommandBuffer         *cmdb,
               const GPUTextureBlitInfo *info);

#endif /* mt_apis_h */
