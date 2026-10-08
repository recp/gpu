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

#ifndef gpu_gpudef_h
#define gpu_gpudef_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/common.h>
#include <gpu/gpu.h>
#include "device.h"
#include "render-pipeline.h"
#include "rce.h"
#include "compute.h"
#include "buffer.h"
#include "memory.h"
#include "multigpu.h"
#include "texture.h"
#include "commandbuffer.h"
#include "commandqueue.h"
#include "pass.h"
#include "depthstencil.h"
#include "vertex.h"
#include "library.h"
#include "swapchain.h"
#include "frame.h"
#include "descriptor.h"
#include "sampler.h"
#include "instance.h"
#include "surface.h"
#include "pipeline-cache.h"
#include "vrs.h"
#include "ray.h"
#include "execution-graph.h"
#include "sampler-feedback.h"
#include "tensor.h"

typedef struct Api {
  GPUBackend            backend;
  bool                  initialized;
  ApiDevice             device;
  ApiRender             render;
  ApiRCE                rce;
  ApiCompute            compute;
  ApiBuffer             buf;
  ApiMemory             memory;
  ApiMultiGPU           multigpu;
  ApiTexture            texture;
  ApiCommandBuffer      cmdbuf;
  ApiCommandQueue       cmdque;
  ApiRenderPass         renderPass;
  ApiDepthStencil       depthStencil;
  ApiVertex             vertex;
  ApiLibrary            library;
  ApiSwapchain          swapchain;
  ApiFrame              frame;
  ApiDescriptor         descriptor;
  ApiSampler            sampler;
  ApiInstance           instance;
  ApiSurface            surface;
  ApiPipelineCache      pipelineCache;
  ApiVRS                vrs;
  ApiRayQuery           rayQuery;
  ApiRayTracing         rayTracing;
  ApiExecutionGraph     executionGraph;
  ApiSamplerFeedback    samplerFeedback;
  ApiTensor             tensor;
  void                 *reserved;
} Api;

#ifdef __cplusplus
}
#endif
#endif /* gpu_gpudef_h */
