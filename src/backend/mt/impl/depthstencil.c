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
gpuNewDepthStencil(GPUCompareFunction depthCompareFunc,
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
gpuNewDepthStencilState(GPUDevice       * __restrict device,
                        GPUDepthStencil * __restrict depthStencil) {
  GPUDepthStencilState *depthStencilState;
  MtRenderPipeline     *mtDepthStencilState;
  
  mtDepthStencilState = mtNewDepthStencilState(device->priv, depthStencil->priv);
  depthStencilState   = calloc(1, sizeof(*depthStencilState));
  
  depthStencilState->priv = mtDepthStencilState;
  
  return depthStencilState;
}
