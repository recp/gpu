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

#include "../common.h"

GPU_EXPORT
GPUDepthStencil*
mt_newDepthStencil(GPUCompareFunction depthCompareFunc,
                   bool               depthWriteEnabled) {
  GPUDepthStencil *ds;
  MtDepthStencilDescriptor *mds;
  
  mds = mtDepthStencilDesc((MtCompareFunction)depthCompareFunc, depthWriteEnabled);
  ds  = calloc(1, sizeof(*ds));

  ds->priv = mds;

  return ds;
}

GPU_EXPORT
GPUDepthStencilState*
mt_newDepthStencilState(GPUDevice       * __restrict device,
                        GPUDepthStencil * __restrict depthStencil) {
  GPUDepthStencilState *depthStencilState;
  MtRenderPipeline     *mtDepthStencilState;
  
  mtDepthStencilState = mtNewDepthStencilState(device->priv, depthStencil->priv);
  depthStencilState   = calloc(1, sizeof(*depthStencilState));
  
  depthStencilState->priv = mtDepthStencilState;
  
  return depthStencilState;
}

GPU_INLINE
GPUTextureDesc
GPUDefaultTextureDesc(void) {
  GPUTextureDesc desc;
  desc.width            = 0;
  desc.height           = 0;
  desc.format           = GPUPixelFormatInvalid;
  desc.mipmapLevelCount = 1;
  desc.usage            = GPUTextureUsageUnknown;
  desc.storageMode      = GPUStorageModePrivate;
  return desc;
}

GPU_INLINE
GPUTextureDesc
GPUMakeTextureDesc(uint32_t width, uint32_t height, GPUPixelFormat format) {
  GPUTextureDesc desc;
  desc        = GPUDefaultTextureDesc();
  desc.width  = width;
  desc.height = height;
  desc.format = format;
  return desc;
}

GPU_EXPORT
GPUTexture*
GPUNewTextureWith(GPUDevice * __restrict device, GPUTextureDesc * __restrict desc) {
  MTLTextureDescriptor *texdesc;
  id<MTLDevice>         mtlDevice;

  mtlDevice = device->priv;

  texdesc             = [MTLTextureDescriptor new];
  texdesc.pixelFormat = desc->format;
  texdesc.width       = desc->width;
  texdesc.height      = desc->height;
  texdesc.storageMode = desc->storageMode;
  texdesc.usage       = desc->usage;

  return [mtlDevice newTextureWithDescriptor: texdesc];
}

GPU_EXPORT
GPUTexture*
GPUNewTexture(GPUDevice * __restrict device, uint32_t width, uint32_t height, GPUPixelFormat format) {
  GPUTextureDesc desc;
  desc = GPUMakeTextureDesc(width, height, format);
  return GPUNewTextureWith(device, &desc);
}


GPU_EXPORT
void
GPUSetDepthStencilPixelFormat(GPUSwapChain* swapChain, GPUDepthStencilFormat format) {
  // Implement logic to set the desired depth-stencil pixel format.
  // Create or recreate the depth-stencil texture as needed.

//  MTLTextureDescriptor *depthTextureDescriptor = [[MTLTextureDescriptor alloc] init];
//  depthTextureDescriptor.pixelFormat = MTLPixelFormatDepth32Float_Stencil8;
//  depthTextureDescriptor.width  = drawableSize.width;
//  depthTextureDescriptor.height = drawableSize.height;
//  depthTextureDescriptor.storageMode = MTLStorageModePrivate;
//  depthTextureDescriptor.usage = MTLTextureUsageRenderTarget;
//
//  id<MTLTexture> depthTexture = [device newTextureWithDescriptor:depthTextureDescriptor];

}

GPU_HIDE
void
mt_initDepthStencil(GPUApiDepthStencil *api) {
  api->newDepthStencil      = mt_newDepthStencil;
  api->newDepthStencilState = mt_newDepthStencilState;
}
