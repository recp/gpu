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

#ifndef gpu_descriptor_internal_h
#define gpu_descriptor_internal_h

#include "../../common.h"

struct GPUBindGroupLayout {
  void *_priv;
};

struct GPUBindGroup {
  void *_priv;
};

struct GPUPipelineLayout {
  void *_priv;
};

typedef struct GPUBindGroupBindingView {
  GPUBindStage stage;
  GPUBindKind  kind;
  uint32_t     binding;
  GPUBuffer   *buffer;
  GPUTextureView *textureView;
  GPUSampler  *sampler;
  uint64_t     offset;
  uint64_t     size;
  GPUShaderStageFlags visibility;
} GPUBindGroupBindingView;

typedef void (*GPUBindGroupBindingFn)(void *ctx,
                                      const GPUBindGroupBindingView *binding);

GPU_HIDE
int
gpuForEachBindGroupBinding(GPUBindGroup *group,
                           GPUBindGroupBindingFn fn,
                           void *ctx);

#endif /* gpu_descriptor_internal_h */
