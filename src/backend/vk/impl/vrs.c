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
vk_getVRSCapabilities(const GPUAdapter      *adapter,
                      GPUVRSCapabilitiesEXT *outCaps) {
  GPUAdapterVk *native;

  native = adapter ? adapter->_priv : NULL;
  if (!native || !outCaps) {
    return;
  }

  memset(outCaps, 0, sizeof(*outCaps));
  if (native->vrsDrawRate) {
    outCaps->modes |= GPU_VRS_DRAW_RATE_BIT_EXT;
  }
  if (native->vrsAttachment) {
    outCaps->modes |= GPU_VRS_ATTACHMENT_BIT_EXT;
  }
  outCaps->rates                         = native->vrsRates;
  outCaps->combiners                     = native->vrsCombiners;
  outCaps->minAttachmentTexelSize.width  = native->minVRSTexelSize.width;
  outCaps->minAttachmentTexelSize.height = native->minVRSTexelSize.height;
  outCaps->maxAttachmentTexelSize.width  = native->maxVRSTexelSize.width;
  outCaps->maxAttachmentTexelSize.height = native->maxVRSTexelSize.height;
}

GPU_HIDE
void
vk_initVRS(GPUApiVRS *api) {
  api->getCapabilities = vk_getVRSCapabilities;
}
