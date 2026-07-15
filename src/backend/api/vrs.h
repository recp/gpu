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

#ifndef gpu_gpudef_vrs_h
#define gpu_gpudef_vrs_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/vrs.h>

typedef struct GPUApiVRS {
  void (*getCapabilities)(const GPUAdapter      *adapter,
                          GPUVRSCapabilitiesEXT *outCaps);
  GPUResult (*createRateMap)(
    GPUDevice                                  *device,
    const GPURasterizationRateMapCreateInfoEXT *info,
    GPURasterizationRateMapEXT                **outMap
  );
  void (*destroyRateMap)(GPURasterizationRateMapEXT *map);
  GPUResult (*getRateMapPhysicalSize)(
    const GPURasterizationRateMapEXT *map,
    uint32_t                           layer,
    GPUExtent2D                       *outSize
  );
} GPUApiVRS;

#ifdef __cplusplus
}
#endif
#endif /* gpu_gpudef_vrs_h */
