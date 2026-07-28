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

#ifndef gpu_gpudef_pass_h
#define gpu_gpudef_pass_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/common.h>
#include <gpu/gpu.h>

typedef struct GPUBarrierBatch GPUBarrierBatch;

typedef struct GPURenderPassDesc {
  void       *_priv;
  const char *label;
} GPURenderPassDesc;

struct GPUTransferPassEncoder {
  void             *_priv;
  GPUCommandBuffer *_cmdb;
  bool              _ended;
};

typedef struct GPUApiRenderPass {
  GPURenderPassDesc* (*beginRenderPass)  (GPUCommandBuffer *cmdb,
                                          const GPURenderPassCreateInfo *info);
  void               (*destroyRenderPass)(GPURenderPassDesc *pass);

  GPUTransferPassEncoder* (*beginTransferPass)(GPUCommandBuffer *cmdb,
                                               const char       *label);
  void (*copyBufferToBuffer)(GPUTransferPassEncoder   *pass,
                             GPUBuffer                *src,
                             GPUBuffer                *dst,
                             const GPUBufferCopyRegion *region);
  void (*copyBufferToTexture)(GPUTransferPassEncoder          *pass,
                              GPUBuffer                       *src,
                              GPUTexture                      *dst,
                              const GPUBufferTextureCopyRegion *region);
  void (*copyTextureToBuffer)(GPUTransferPassEncoder          *pass,
                              GPUTexture                      *src,
                              GPUBuffer                       *dst,
                              const GPUBufferTextureCopyRegion *region);
  void (*copyTextureToTexture)(GPUTransferPassEncoder             *pass,
                               GPUTexture                         *src,
                               GPUTexture                         *dst,
                               const GPUTextureToTextureCopyRegion *region);
  void (*copyMemoryIndirect)(GPUTransferPassEncoder             *pass,
                             const GPUIndirectMemoryCopyInfoEXT *info);
  void (*copyMemoryToTextureIndirect)(GPUTransferPassEncoder                     *pass,
                                      const GPUIndirectMemoryToTextureCopyInfoEXT *info);
  void (*endTransferPass)(GPUTransferPassEncoder *pass);
  void (*blitTexture)(GPUCommandBuffer         *cmdb,
                      const GPUTextureBlitInfo *info);
  void (*generateMipmaps)(GPUCommandBuffer *cmdb, GPUTexture *texture);
  void (*encodeBarriers)(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers);
} GPUApiRenderPass;

#ifdef __cplusplus
}
#endif
#endif /* gpu_gpudef_pass_h */
