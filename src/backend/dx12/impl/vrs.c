/*
 * Copyright (C) 2026 Recep Aslantas
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
#include "../impl.h"

static void
dx12_getVRSCapabilities(const GPUAdapter      *adapter,
                        GPUVRSCapabilitiesEXT *outCaps) {
  GPUAdapterDX12 *native;

  native = adapter ? adapter->_priv : NULL;
  if (!native || !outCaps) {
    return;
  }

  memset(outCaps, 0, sizeof(*outCaps));
  if (native->vrsTier >= D3D12_VARIABLE_SHADING_RATE_TIER_1) {
    outCaps->modes |= GPU_VRS_DRAW_RATE_BIT_EXT;
  }
  if (native->vrsTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2) {
    outCaps->modes |= GPU_VRS_ATTACHMENT_BIT_EXT;
    outCaps->minAttachmentTexelSize.width  = native->vrsTileSize;
    outCaps->minAttachmentTexelSize.height = native->vrsTileSize;
    outCaps->maxAttachmentTexelSize        =
      outCaps->minAttachmentTexelSize;
  }
  outCaps->rates     = native->vrsRates;
  outCaps->combiners = native->vrsCombiners;
}

GPU_HIDE
void
dx12_initVRS(GPUApiVRS *api) {
  api->getCapabilities = dx12_getVRSCapabilities;
}
