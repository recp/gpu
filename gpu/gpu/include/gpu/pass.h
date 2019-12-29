/*
* Copyright (c), Recep Aslantas.
*
* MIT License (MIT), http://opensource.org/licenses/MIT
* Full license can be found in the LICENSE file
*/

#ifndef gpu_pass_h
#define gpu_pass_h

#include "common.h"

typedef struct GPURenderPassDesc {
  void *priv;
} GPURenderPassDesc;

GPU_EXPORT
GPURenderPassDesc*
gpu_pass_new(void);

#define gpu_pass_begin() /* */
#define gpu_pass_finish() /* */

#define gpu_begin() /* */
#define gpu_finish() /* */


//GPU_INLINE
//GPURenderPassDesc*
//gpu_metal_pass(MTLRenderPassDescriptor * __restrict mpass) {
//  
//}

#endif /* gpu_pass_h */
