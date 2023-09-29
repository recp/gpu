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
// GPU_HIDE void dx12_initBuff(GPUApiBuffer* api);
// GPU_HIDE void dx12_initPass(GPUApiRenderPass* api);
// GPU_HIDE void dx12_initDepthStencil(GPUApiDepthStencil* api);
// GPU_HIDE void dx12_initVertex(GPUApiVertex* api);
// GPU_HIDE void dx12_initLibrary(GPUApiLibrary* api);
GPU_HIDE void dx12_initSwapChain(GPUApiSwapChain* apiSwapChain);
GPU_HIDE void dx12_initFrame(GPUApiFrame *apiFrame);
GPU_HIDE void dx12_initDescriptor(GPUApiDescriptor *apiDescriptor);
GPU_HIDE void dx12_initSampler(GPUApiSampler *apiSampler);

#endif /* dx12_apis_h */
