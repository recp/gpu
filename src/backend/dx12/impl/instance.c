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
GPUInstance *
dx12_createInstance(struct GPUApi * __restrict api, 
                    GPUInitParams * __restrict params) {
  GPUInstance *inst;
  inst = calloc(1, sizeof(*inst));
  return inst;
}

GPU_HIDE
void
dx12_initInstance(GPUApiInstance *apiInstance) {
  apiInstance->createInstance = dx12_createInstance;
}