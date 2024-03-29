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

#ifndef gl_impl_h
#define gl_impl_h

#include "../common.h"

GPU_HIDE void gl_initDevice(GPUApiDevice *apiDevice);
GPU_HIDE void gl_initRenderPipeline(GPUApiRender *api);
GPU_HIDE void gl_initRCE(GPUApiRCE *api);
GPU_HIDE void gl_initCmdBuff(GPUApiCommandBuffer *api);
GPU_HIDE void gl_initCmdQue(GPUApiCommandQueue *api);
GPU_HIDE void gl_initBuff(GPUApiBuffer *api);
GPU_HIDE void gl_initPass(GPUApiRenderPass *api);
GPU_HIDE void gl_initDepthStencil(GPUApiDepthStencil *api);
GPU_HIDE void gl_initVertex(GPUApiVertex *api);
GPU_HIDE void gl_initLibrary(GPUApiLibrary *api);

#endif /* gl_impl_h */
