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

#ifndef gpu_tensor_internal_h
#define gpu_tensor_internal_h

#include "../common.h"

struct GPUTensorEXT {
  void             *_priv;
  GPUDevice        *device;
  GPUBuffer        *buffer;
  char             *label;
  uint64_t          offsetBytes;
  GPUTensorDescEXT  desc;
  uint64_t          dimensions[2];
  uint64_t          strides[2];
};

#endif /* gpu_tensor_internal_h */
