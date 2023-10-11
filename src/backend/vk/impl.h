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

GPU_HIDE void vk_initInstance(GPUApiInstance *apiInstance);
GPU_HIDE void vk_initDevice(GPUApiDevice* apiDevice);
GPU_HIDE void vk_initCmdQue(GPUApiCommandQueue* api);
GPU_HIDE void vk_initSwapChain(GPUApiSwapChain* apiSwapChain);
GPU_HIDE void vk_initFrame(GPUApiFrame *apiFrame);
GPU_HIDE void vk_initDescriptor(GPUApiDescriptor *apiDescriptor);
GPU_HIDE void vk_initSampler(GPUApiSampler *apiSampler);
GPU_HIDE void vk_initSurface(GPUApiSurface * apiDevice);
GPU_HIDE void vk_initCmdQue(GPUApiCommandQueue * apiQue);

#endif /* vk_apis_h */
