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

GPU_HIDE void mt_initDevice(GPUApiDevice *apiDevice);
GPU_HIDE void mt_initRenderPipeline(GPUApiRender *api);
GPU_HIDE void mt_initRCE(GPUApiRCE *api);
GPU_HIDE void mt_initCmdBuff(GPUApiCommandBuffer *api);
GPU_HIDE void mt_initCmdQue(GPUApiCommandQueue *api);
GPU_HIDE void mt_initBuff(GPUApiBuffer *api);
GPU_HIDE void mt_initRenderPass(GPUApiRenderPass *api);
GPU_HIDE void mt_initDepthStencil(GPUApiDepthStencil *api);
GPU_HIDE void mt_initVertex(GPUApiVertex *api);
GPU_HIDE void mt_initLibrary(GPUApiLibrary *api);
GPU_HIDE void mt_initSwapChain(GPUApiSwapChain *api);
GPU_HIDE void mt_initFrame(GPUApiFrame *api);
GPU_HIDE void mt_initInstance(GPUApiInstance *api);
GPU_HIDE void mt_initSurface(GPUApiSurface * apiDevice);

#endif /* mt_apis_h */
