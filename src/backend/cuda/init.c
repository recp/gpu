/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "common.h"
#include "impl.h"

static GPUApi cuda = {
  .backend     = GPU_BACKEND_CUDA,
  .initialized = false
};

GPU_HIDE
GPUApi *
backend_cuda(void) {
  if (!cuda.initialized) {
    cuda_initInstance(&cuda.instance);
    cuda_initDevice(&cuda.device);
    cuda_initQueue(&cuda.cmdque);
    cuda_initBuffer(&cuda.buf);
    cuda_initMultiGPU(&cuda.multigpu);
    cuda_initLibrary(&cuda.library);
    cuda_initCompute(&cuda.compute);
    cuda.initialized = true;
  }
  return &cuda;
}
