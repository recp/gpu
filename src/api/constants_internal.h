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

#ifndef gpu_constants_internal_h
#define gpu_constants_internal_h

#include "../common.h"
#include "library_internal.h"
#include <us/compiler.h>

/* owned cold-creation snapshot of the known pipeline extensions. */
typedef struct GPUPreparedConstants {
  GPUPipelineConstants                 constants;
  GPUMeshPipelineEXT                   mesh;
  GPUIntersectionFunctionPipelineEXT   intersection;
  const GPUChainedStruct              *chain;
  GPUConstant                         *values;
} GPUPreparedConstants;

GPU_HIDE
GPUResult
gpuPrepareConstants(const GPUShaderLibrary *library,
                    const GPUChainedStruct *chain,
                    bool                    compute,
                    GPUPreparedConstants   *out);

GPU_HIDE
const GPUPipelineConstants*
gpuPipelineConstants(const GPUChainedStruct *chain);

GPU_HIDE
const GPUMeshPipelineEXT*
gpuPipelineMesh(const GPUChainedStruct *chain);

GPU_HIDE
GPUShaderFunction*
gpuShaderVariant(GPUShaderLibrary           *library,
                 const char                 *name,
                 const GPUPipelineConstants *constants);

#endif /* gpu_constants_internal_h */
