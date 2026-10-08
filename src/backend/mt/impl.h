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
mt_initDevice(ApiDevice    *apiDevice);

GPU_HIDE
void
mt_initRenderPipeline(ApiRender    *api);

GPU_HIDE
void
mt_initRCE(ApiRCE    *api);

GPU_HIDE
void
mt_initCompute(ApiCompute    *api);

GPU_HIDE
void
mt_initCmdBuff(ApiCommandBuffer    *api);

GPU_HIDE
void
mt_initCmdQue(ApiCommandQueue    *api);

GPU_HIDE
void
mt_initBuff(ApiBuffer    *api);

GPU_HIDE
void
mt_initMemory(ApiMemory    *api);

GPU_HIDE
void
mt_initMultiGPU(ApiMultiGPU    *api);

GPU_HIDE
void
mt_initTexture(ApiTexture    *api);

GPU_HIDE
void
mt_initTensor(ApiTensor    *api);

GPU_HIDE
void
mt_initRenderPass(ApiRenderPass    *api);

GPU_HIDE
void
mt_encodeBarriers(GPUCommandBuffer      *cmdb,
                  const GPUBarrierBatch *barriers);

GPU_HIDE
void
mt_initDepthStencil(ApiDepthStencil    *api);

GPU_HIDE
void
mt_initVertex(ApiVertex    *api);

GPU_HIDE
void
mt_initLibrary(ApiLibrary    *api);

GPU_HIDE
void
mt_initSampler(ApiSampler    *api);

GPU_HIDE
void
mt_initSwapchain(ApiSwapchain    *api);

GPU_HIDE
void
mt_initFrame(ApiFrame    *api);

GPU_HIDE
void
mt_initInstance(ApiInstance    *api);

GPU_HIDE
void
mt_initSurface(ApiSurface    *apiDevice);

GPU_HIDE
void
mt_initPipelineCache(ApiPipelineCache    *api);

GPU_HIDE
void
mt_initVRS(ApiVRS    *api);

GPU_HIDE
void
mt_initRayQuery(ApiRayQuery    *api);

GPU_HIDE
void
mt_initDescriptor(ApiDescriptor    *api);

GPU_HIDE
void
mt_blitTexture(GPUCommandBuffer         *cmdb,
               const GPUTextureBlitInfo *info);

#endif /* mt_apis_h */
