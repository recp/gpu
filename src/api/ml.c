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

#include "../common.h"
#include "buffer_internal.h"
#include "cmdqueue_internal.h"
#include "memory_internal.h"
#include "ml_internal.h"
#include "tensor_internal.h"

_Static_assert(sizeof(GPUMLTensorShapeEXT) % _Alignof(uint64_t) == 0u, "shape dimension alignment");

static GPUResult
mlChain(const GPUChainedStruct *chain, GPUStructureType type, size_t size) {
  if ((chain->sType != GPU_STRUCTURE_TYPE_NONE && chain->sType != type)
      || (chain->structSize != 0u && chain->structSize < size)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return chain->pNext ? GPU_ERROR_UNSUPPORTED : GPU_OK;
}

static bool
mlEnabled(const GPUDevice *device) {
  return GPUIsFeatureEnabled(device, GPU_FEATURE_ML_MODEL_EXT)
         && GPUIsFeatureEnabled(device, GPU_FEATURE_TENSOR_RESOURCES_EXT)
         && GPUIsFeatureEnabled(device, GPU_FEATURE_PLACED_RESOURCES)
         && GPUIsFeatureEnabled(device, GPU_FEATURE_COMPUTE);
}

static char*
copyString(const char *text) {
  char   *copy;
  size_t  size;

  text = text ? text : "";
  size = strlen(text) + 1u;

  if ((copy = malloc(size))) {
    memcpy(copy, text, size);
  }

  return copy;
}

static int
compareProfiles(const void *first, const void *second) {
  const MLProfile *a;
  const MLProfile *b;

  a = first;
  b = second;

  return (a->desc.id > b->desc.id) - (a->desc.id < b->desc.id);
}

static MLProfile*
findProfile(GPUMLModelEXT *model, uint32_t id) {
  uint32_t  low;
  uint32_t  high;
  uint32_t  mid;

  low  = 0u;
  high = model->profileCount;

  while (low < high) {
    mid = low + (high - low) / 2u;

    if (model->profiles[mid].desc.id == id) {
      return &model->profiles[mid];
    }

    if (model->profiles[mid].desc.id < id) {
      low = mid + 1u;
    } else {
      high = mid;
    }
  }

  return NULL;
}

static GPUResult
copyProfiles(GPUMLModelEXT *model, const GPUMLModelCreateInfoEXT *info) {
  const GPUMLTensorShapeEXT  *source;
  const GPUMLShapeProfileEXT *profile;
  GPUMLTensorShapeEXT        *inputs;
  uint64_t                   *dimensions;
  uint32_t                    i;
  uint32_t                    j;
  uint32_t                    k;

  if (!(model->profiles = calloc(info->profileCount, sizeof(*model->profiles)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  model->profileCount = info->profileCount;

  for (i = 0u; i < info->profileCount; i++) {
    profile = &info->pProfiles[i];

    if (!profile->pInputs || profile->inputCount == 0u) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    if (profile->inputCount > 31u) {
      return GPU_ERROR_UNSUPPORTED;
    }

    /* shape metadata and its aligned dimensions share one cold allocation. */
    if (!(inputs = calloc(profile->inputCount, sizeof(*inputs) + 2u * sizeof(uint64_t)))) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    dimensions = (uint64_t *)(inputs + profile->inputCount);

    model->profiles[i].desc         = *profile;
    model->profiles[i].desc.pInputs = inputs;

    for (j = 0u; j < profile->inputCount; j++) {
      source = &profile->pInputs[j];

      if (source->rank != 2u || source->dataType != GPU_TENSOR_DATA_TYPE_F32_EXT) {
        return GPU_ERROR_UNSUPPORTED;
      }

      if (!source->pDimensions || source->pDimensions[0] == 0u || source->pDimensions[1] == 0u
          || source->pDimensions[0] > INT64_MAX / 4u
          || source->pDimensions[1] > INT64_MAX / 4u / source->pDimensions[0]) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }

      for (k = 0u; k < j; k++) {
        if (inputs[k].slot == source->slot) {
          return GPU_ERROR_INVALID_ARGUMENT;
        }
      }

      inputs[j]             = *source;
      inputs[j].pDimensions = dimensions + 2u * j;
      memcpy(dimensions + 2u * j, source->pDimensions, 2u * sizeof(uint64_t));
    }
  }

  qsort(model->profiles, model->profileCount, sizeof(*model->profiles), compareProfiles);

  for (i = 1u; i < model->profileCount; i++) {
    if (model->profiles[i - 1u].desc.id == model->profiles[i].desc.id) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  return GPU_OK;
}

static bool
writeOverlap(const GPUTensorEXT *a, const GPUTensorEXT *b) {
  const GPUBuffer *first;
  const GPUBuffer *second;
  uint64_t         firstOffset;
  uint64_t         secondOffset;

  first        = a->buffer;
  second       = b->buffer;
  firstOffset  = a->offsetBytes;
  secondOffset = b->offsetBytes;

  if (first != second) {
    if (!first->_heap || first->_heap != second->_heap) {
      return false;
    }

    firstOffset  += first->_heapOffset;
    secondOffset += second->_heapOffset;
  }

  /* validated buffer/heap spans cannot wrap at either interval end. */
  return firstOffset < secondOffset + b->sizeBytes
         && secondOffset < firstOffset + a->sizeBytes;
}

GPU_EXPORT
GPUResult
GPUCreateMLModelEXT(GPUDevice                     *device,
                    const GPUMLModelCreateInfoEXT *info,
                    GPUMLModelEXT                **outModel) {
  GPUMLModelEXT *model;
  Api           *api;
  GPUResult      result;

  if (!outModel) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outModel = NULL;

  if (!device || !info || !info->path || !info->path[0] || !info->functionName || !info->functionName[0]
      || !info->pProfiles || info->profileCount == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if ((result = mlChain(&info->chain, GPU_STRUCTURE_TYPE_ML_MODEL_CREATE_INFO_EXT, sizeof(*info))) != GPU_OK) {
    return result;
  }

  if (!mlEnabled(device) || !(api = deviceApi(device)) || !api->ml.createModel || !api->ml.destroyModel) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!(model = calloc(1u, sizeof(*model)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  model->device       = device;
  model->label        = copyString(info->label);
  model->path         = copyString(info->path);
  model->functionName = copyString(info->functionName);

  result = model->label && model->path && model->functionName ? copyProfiles(model, info) : GPU_ERROR_OUT_OF_MEMORY;

  if (result == GPU_OK) {
    result = api->ml.createModel(model);
  }

  if (result != GPU_OK) {
    GPUDestroyMLModelEXT(model);
    return result;
  }

  *outModel = model;

  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyMLModelEXT(GPUMLModelEXT *model) {
  Api      *api;
  uint32_t  i;

  if (!model) {
    return;
  }

  if ((api = deviceApi(model->device)) && api->ml.destroyModel) {
    api->ml.destroyModel(model);
  }

  for (i = 0u; i < model->profileCount; i++) {
    free((void *)model->profiles[i].desc.pInputs);
    free((void *)model->profiles[i].info.pBindings);
  }

  free(model->profiles);
  free(model->functionName);
  free(model->path);
  free(model->label);
  free(model);
}

GPU_EXPORT
GPUResult
GPUCreateMLPipelineEXT(GPUDevice                        *device,
                       const GPUMLPipelineCreateInfoEXT *info,
                       GPUMLPipelineEXT                **outPipeline) {
  GPUMLPipelineEXT *pipeline;
  MLProfile        *profile;
  Api              *api;
  GPUResult         result;

  if (!outPipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outPipeline = NULL;

  if (!device || !info || !info->model || info->model->device != device
      || !(profile = findProfile(info->model, info->profileId))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if ((result = mlChain(&info->chain, GPU_STRUCTURE_TYPE_ML_PIPELINE_CREATE_INFO_EXT, sizeof(*info))) != GPU_OK) {
    return result;
  }

  if (!mlEnabled(device) || !(api = deviceApi(device)) || !api->ml.createPipeline) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!(pipeline = calloc(1u, sizeof(*pipeline)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  pipeline->model   = info->model;
  pipeline->profile = profile;
  pipeline->label   = copyString(info->label);
  result            = pipeline->label ? api->ml.createPipeline(pipeline) : GPU_ERROR_OUT_OF_MEMORY;

  if (result != GPU_OK) {
    GPUDestroyMLPipelineEXT(pipeline);
    return result;
  }

  *outPipeline = pipeline;

  return GPU_OK;
}

GPU_EXPORT
const GPUMLPipelineInfoEXT*
GPUGetMLPipelineInfoEXT(const GPUMLPipelineEXT *pipeline) {
  return pipeline ? &pipeline->profile->info : NULL;
}

GPU_EXPORT
void
GPUDestroyMLPipelineEXT(GPUMLPipelineEXT *pipeline) {
  if (!pipeline) {
    return;
  }

  free(pipeline->label);
  free(pipeline);
}

GPU_EXPORT
GPUResult
GPUCreateMLBindingsEXT(GPUDevice                        *device,
                       const GPUMLBindingsCreateInfoEXT *info,
                       GPUMLBindingsEXT                **outBindings) {
  const GPUMLPipelineInfoEXT *layout;
  const GPUMLBindingInfoEXT  *binding;
  GPUMLBindingsEXT           *bindings;
  GPUTensorEXT               *tensor;
  Api                        *api;
  GPUResult                   result;
  uint32_t                    i;
  uint32_t                    j;

  if (!outBindings) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outBindings = NULL;

  if (!device || !info || !info->pipeline || info->pipeline->model->device != device
      || !info->scratch || info->scratch->device != device || !info->pBindings) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if ((result = mlChain(&info->chain, GPU_STRUCTURE_TYPE_ML_BINDINGS_CREATE_INFO_EXT, sizeof(*info))) != GPU_OK) {
    return result;
  }

  if (!mlEnabled(device) || !(api = deviceApi(device)) || !api->ml.createBindings || !api->ml.destroyBindings) {
    return GPU_ERROR_UNSUPPORTED;
  }

  layout = GPUGetMLPipelineInfoEXT(info->pipeline);

  if (info->bindingCount != layout->bindingCount || info->scratch->usage != GPU_HEAP_USAGE_PLACED
      || info->scratch->sizeBytes < layout->scratchHeap.sizeBytes
      || (info->scratch->compatibilityMask & layout->scratchHeap.compatibilityMask) == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(bindings = calloc(1u, sizeof(*bindings)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  bindings->pipeline     = info->pipeline;
  bindings->scratch      = info->scratch;
  bindings->bindingCount = layout->bindingCount;
  bindings->label        = copyString(info->label);
  result                 = GPU_OK;

  if (!bindings->label
      || !(bindings->tensors = calloc(layout->bindingCount, sizeof(*bindings->tensors)))) {
    GPUDestroyMLBindingsEXT(bindings);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  for (i = 0u; i < info->bindingCount; i++) {
    tensor = info->pBindings[i].tensor;

    if (!tensor || tensor->device != device || !tensor->buffer || tensor->sizeBytes == 0u
        || tensor->buffer->_heap == info->scratch || tensor->buffer->_sparse
        || !bufferRangeValid(tensor->buffer, tensor->offsetBytes, tensor->sizeBytes)) {
      result = GPU_ERROR_INVALID_ARGUMENT;
      break;
    }

    if (tensor->buffer->_sharedPeer || tensor->buffer->_hostImported) {
      result = GPU_ERROR_UNSUPPORTED;
      break;
    }

    for (j = 0u; j < layout->bindingCount; j++) {
      if (layout->pBindings[j].shape.slot == info->pBindings[i].slot) {
        break;
      }
    }

    if (j == layout->bindingCount || bindings->tensors[j]) {
      result = GPU_ERROR_INVALID_ARGUMENT;
      break;
    }

    binding = &layout->pBindings[j];

    if ((tensor->desc.usage & GPU_TENSOR_USAGE_ML_EXT) == 0u
        || tensor->desc.rank != binding->shape.rank || tensor->desc.dataType != binding->shape.dataType
        || memcmp(tensor->dimensions, binding->shape.pDimensions, sizeof(tensor->dimensions)) != 0) {
      result = GPU_ERROR_INVALID_ARGUMENT;
      break;
    }

    bindings->tensors[j] = tensor;
  }

  for (i = 0u; result == GPU_OK && i < layout->bindingCount; i++) {
    for (j = 0u; j < i; j++) {
      if (((layout->pBindings[i].access | layout->pBindings[j].access) & GPU_ACCESS_SHADER_WRITE) != 0u
          && writeOverlap(bindings->tensors[i], bindings->tensors[j])) {
        result = GPU_ERROR_INVALID_ARGUMENT;
        break;
      }
    }
  }

  if (result == GPU_OK) {
    result = api->ml.createBindings(bindings);
  }

  if (result != GPU_OK) {
    GPUDestroyMLBindingsEXT(bindings);
    return result;
  }

  *outBindings = bindings;

  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyMLBindingsEXT(GPUMLBindingsEXT *bindings) {
  Api *api;

  if (!bindings) {
    return;
  }

  if ((api = deviceApi(bindings->pipeline->model->device)) && api->ml.destroyBindings) {
    api->ml.destroyBindings(bindings);
  }

  free(bindings->tensors);
  free(bindings->label);
  free(bindings);
}

GPU_EXPORT
GPUResult
GPUEncodeMLEXT(GPUCommandBuffer *cmdb, GPUMLBindingsEXT *bindings) {
  GPUDevice *device;
  Api       *api;

  if (!cmdb || !bindings || cmdb->_submitted || cmdb->_activeEncoder
      || !(device = commandBufferDevice(cmdb)) || bindings->pipeline->model->device != device
      || (cmdb->_queue->bits & (GPU_QUEUE_COMPUTE_BIT | GPU_QUEUE_GRAPHICS_BIT)) == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = deviceApi(device)) || !api->ml.encode) {
    return GPU_ERROR_UNSUPPORTED;
  }

  return api->ml.encode(cmdb, bindings);
}
