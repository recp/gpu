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

GPU_HIDE
GPUDevice*
gl_createSystemDefaultDevice(GPUApi *api) {
  GPUDevice *device;

  device = calloc(1, sizeof(*device));
  
#ifdef __APPLE__
  device->_priv = CGLGetCurrentContext();
#endif

  return device;
}

GPU_HIDE
void
gl_initDevice(GPUApiDevice *apiDevice) {
  apiDevice->createSystemDefaultDevice = gl_createSystemDefaultDevice;
}
