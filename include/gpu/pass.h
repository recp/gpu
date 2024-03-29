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

#ifndef gpu_pass_h
#define gpu_pass_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "swapchain.h"
#include "frame.h"

#define gpu_pass_begin() /* */
#define gpu_pass_finish() /* */

#define gpu_begin() /* */
#define gpu_finish() /* */

typedef struct GPURenderPassDesc {
  void *_priv;
} GPURenderPassDesc;

GPU_EXPORT
GPURenderPassDesc*
gpuNewPass(void);

GPU_EXPORT
GPURenderPassDesc*
GPUBeginRenderPass(GPUTexture *target);

GPU_EXPORT
void
GPUEndRenderPass(GPURenderPassDesc *pass);

//#if defined(__APPLE__) && defined(__OBJC__)
//@class MTKView;
//GPU_INLINE
//GPURenderPassDesc*
//GPUPassFromMTKView(MTKView * __restrict view) {
//#if __has_feature(objc_arc)
//  return (__bridge void *)view.currentRenderPassDescriptor;
//#else
//  return (void *)view.currentRenderPassDescriptor;
//#endif
//}
//#endif

#ifdef __cplusplus
}
#endif
#endif /* gpu_pass_h */
