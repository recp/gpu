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

#include "constants_internal.h"
#include "device_internal.h"

#include <math.h>

static GPUConstantType
gpu_constantType(uint32_t kind) {
  switch (kind) {
    case USL_RUNTIME_TYPE_BOOL: return GPU_CONSTANT_BOOL;
    case USL_RUNTIME_TYPE_I32:  return GPU_CONSTANT_I32;
    case USL_RUNTIME_TYPE_U32:  return GPU_CONSTANT_U32;
    case USL_RUNTIME_TYPE_F32:  return GPU_CONSTANT_F32;
    case USL_RUNTIME_TYPE_I64:  return GPU_CONSTANT_I64;
    case USL_RUNTIME_TYPE_U64:  return GPU_CONSTANT_U64;
    case USL_RUNTIME_TYPE_F64:  return GPU_CONSTANT_F64;
    default: return (GPUConstantType)0;
  }
}

static int
gpu_constantCompare(const void *left, const void *right) {
  const GPUConstant *a;
  const GPUConstant *b;

  a = left;
  b = right;

  return (a->id > b->id) - (a->id < b->id);
}

GPU_HIDE
const GPUPipelineConstants*
gpuPipelineConstants(const GPUChainedStruct *chain) {
  uint32_t count;

  for (count = 0u; chain && count < 3u; count++, chain = chain->pNext) {
    if (chain->sType == GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS) {
      return (const GPUPipelineConstants *)chain;
    }
  }

  return NULL;
}

GPU_HIDE
const GPUMeshPipelineEXT*
gpuPipelineMesh(const GPUChainedStruct *chain) {
  uint32_t count;

  for (count = 0u; chain && count < 3u; count++, chain = chain->pNext) {
    if (chain->sType == GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT) {
      return (const GPUMeshPipelineEXT *)chain;
    }
  }

  return NULL;
}

GPU_HIDE
GPUResult
gpuPrepareConstants(const GPUShaderLibrary *library,
                    const GPUChainedStruct *chain,
                    bool                    compute,
                    GPUPreparedConstants   *out) {
  const GPUPipelineConstants *constants;
  const GPUConstant          *value;
  GPUBackend                  backend;
  uint32_t                    count;
  uint32_t                    i;
  uint32_t                    j;
  bool                        mesh;
  bool                        intersection;

  memset(out, 0, sizeof(*out));
  constants    = NULL;
  mesh         = false;
  intersection = false;

  for (count = 0u; chain; count++, chain = chain->pNext) {
    if (count == 3u) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    switch (chain->sType) {
      case GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS:
        if (constants || chain->structSize < sizeof(GPUPipelineConstants)) {
          return GPU_ERROR_INVALID_ARGUMENT;
        }

        constants = (const GPUPipelineConstants *)chain;
        break;
      case GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT:
        if (compute || mesh
            || (chain->structSize != 0u && chain->structSize < sizeof(out->mesh))) {
          return GPU_ERROR_INVALID_ARGUMENT;
        }

        out->mesh = *(const GPUMeshPipelineEXT *)chain;
        mesh      = true;
        break;
      case GPU_STRUCTURE_TYPE_INTERSECTION_FUNCTION_PIPELINE_EXT:
        if (intersection
            || (chain->structSize != 0u && chain->structSize < sizeof(out->intersection))) {
          return GPU_ERROR_INVALID_ARGUMENT;
        }

        out->intersection = *(const GPUIntersectionFunctionPipelineEXT *)chain;
        intersection      = true;
        break;
      default:
        return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  if (intersection) {
    out->intersection.chain.pNext = NULL;
    out->chain                    = &out->intersection.chain;
  }

  if (mesh) {
    out->mesh.chain.pNext = out->chain;
    out->chain            = &out->mesh.chain;
  }

  if (!constants || constants->constantCount == 0u) {
    return GPU_OK;
  }

  if (intersection) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!library || !library->_device || !constants->pConstants) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!library->_metadata) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (constants->constantCount > library->_constantCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  backend = library->_api->backend;

  if (backend != GPU_BACKEND_METAL && backend != GPU_BACKEND_VULKAN
      && backend != GPU_BACKEND_WEBGPU && backend != GPU_BACKEND_DX12
      && backend != GPU_BACKEND_CUDA) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if ((backend == GPU_BACKEND_DX12 || backend == GPU_BACKEND_CUDA) && !library->_uslSource) {
    return GPU_ERROR_UNSUPPORTED;
  }

  /* validate before allocation; the bounded metadata table owns the types. */

  for (i = 0u; i < constants->constantCount; i++) {
    value = &constants->pConstants[i];

    if ((uint32_t)value->type < GPU_CONSTANT_BOOL
        || (uint32_t)value->type > GPU_CONSTANT_F64
        || (value->type == GPU_CONSTANT_F32 && !isfinite(value->value.f32))
        || (value->type == GPU_CONSTANT_F64 && !isfinite(value->value.f64))) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    for (j = 0u; j < library->_constantCount; j++) {
      if (library->_constants[j].function_constant_id == value->id) {
        break;
      }
    }

    if (j == library->_constantCount || gpu_constantType(library->_constants[j].type.kind) != value->type) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    if (value->type > GPU_CONSTANT_F32) {
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  if (!(out->values = calloc(constants->constantCount, sizeof(*out->values)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  for (i = 0u; i < constants->constantCount; i++) {
    value               = &constants->pConstants[i];
    out->values[i].id   = value->id;
    out->values[i].type = value->type;

    switch (value->type) {
      case GPU_CONSTANT_BOOL: out->values[i].value.boolean = value->value.boolean; break;
      case GPU_CONSTANT_I32:  out->values[i].value.i32 = value->value.i32; break;
      case GPU_CONSTANT_U32:  out->values[i].value.u32 = value->value.u32; break;
      case GPU_CONSTANT_F32:  out->values[i].value.f32 = value->value.f32; break;
      default: break;
    }
  }

  qsort(out->values, constants->constantCount, sizeof(*out->values), gpu_constantCompare);

  for (i = 1u; i < constants->constantCount; i++) {
    if (out->values[i - 1u].id == out->values[i].id) {
      free(out->values);
      out->values = NULL;
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  out->constants             = *constants;
  out->constants.pConstants  = out->values;
  out->constants.chain.pNext = out->chain;
  out->chain                 = &out->constants.chain;

  return GPU_OK;
}

GPU_HIDE
GPUShaderFunction*
gpuShaderVariant(GPUShaderLibrary           *library,
                 const char                 *name,
                 const GPUPipelineConstants *constants) {
  if (!constants || constants->constantCount == 0u) {
    return gpuShaderFunction(library, name);
  }

  if (!library || !name || !library->_api
      || !library->_api->library.newVariant) {
    return NULL;
  }

  return library->_api->library.newVariant(library, name, constants);
}
