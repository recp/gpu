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

#include "common.h"
#include "impl.h"

static Api    cuda = {
  .backend     = GPU_BACKEND_CUDA,
  .initialized = false
};

GPU_HIDE
Api*
backend_cuda(void) {
  if (!cuda.initialized) {
    cuda_initInstance(&cuda.instance);
    cuda_initDevice(&cuda.device);
    cuda_initQueue(&cuda.cmdque);
    cuda_initBuffer(&cuda.buf);
    cuda_initTexture(&cuda.texture);
    cuda_initSampler(&cuda.sampler);
    cuda_initDescriptor(&cuda.descriptor);
    cuda_initMultiGPU(&cuda.multigpu);
    cuda_initLibrary(&cuda.library);
    cuda_initCompute(&cuda.compute);

    cuda.initialized = true;
  }

  return &cuda;
}
