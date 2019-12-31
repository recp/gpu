/*
* Copyright (c), Recep Aslantas.
*
* MIT License (MIT), http://opensource.org/licenses/MIT
* Full license can be found in the LICENSE file
*/

#ifndef gpu_pass_h
#define gpu_pass_h

#include "common.h"

#define gpu_pass_begin() /* */
#define gpu_pass_finish() /* */

#define gpu_begin() /* */
#define gpu_finish() /* */

typedef struct GPURenderPassDesc GPURenderPassDesc;

GPU_EXPORT
GPURenderPassDesc*
gpuPassNew(void);

#if defined(__APPLE__) && defined(__OBJC__)
@class MTKView;
GPU_INLINE
GPURenderPassDesc*
gpuPassFromMTKView(MTKView * __restrict view) {
  return (void *)view.currentRenderPassDescriptor;
}
#endif
#endif /* gpu_pass_h */
