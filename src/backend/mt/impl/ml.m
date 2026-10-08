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
#include "../../api/ml.h"
#include "../../../api/ml_internal.h"
#include "../../../api/tensor_internal.h"

#if MT_HAS_METAL4

typedef struct MLModelMT {
  id<MTLLibrary> library;
  id            reflection;
  NSLock       *lock;
} MLModelMT;

typedef struct MLBindingsMT {
  id        table;
  id        allocations[2u * MT_ARGUMENT_BUFFER_COUNT + 2u];
  uint32_t  allocationCount;
} MLBindingsMT;

static GPUResult
modelInputs(GPUMLModelEXT *model) API_AVAILABLE(macos(26.0), ios(26.0));

static GPUResult
pipelineInfo(MLProfile *profile, id<MTL4MachineLearningPipelineState> pipeline)
  API_AVAILABLE(macos(26.0), ios(26.0));

static GPUResult
preparePipeline(GPUMLPipelineEXT *pipeline) API_AVAILABLE(macos(26.0), ios(26.0));

static GPUResult
modelInputs(GPUMLModelEXT *model) {
  MLModelMT                  *native;
  id<MTLBinding>              binding;
  id<MTLTensorBinding>        tensor;
  const GPUMLShapeProfileEXT *profile;
  const GPUMLTensorShapeEXT  *input;
  uint64_t                    dimension;
  uint32_t                    i;
  uint32_t                    j;
  uint32_t                    axis;
  uint32_t                    count;
  uint32_t                    seen;

  native = model->_priv;
  count  = 0u;
  seen   = 0u;

  for (binding in ((MTLFunctionReflection *)native->reflection).bindings) {
    if (binding.type != MTLBindingTypeTensor || binding.index >= MT_ARGUMENT_BUFFER_COUNT
        || (binding.access != MTLBindingAccessReadOnly && binding.access != MTLBindingAccessWriteOnly
            && binding.access != MTLBindingAccessReadWrite)) {
      return GPU_ERROR_UNSUPPORTED;
    }

    if ((seen & (1u << binding.index)) != 0u) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    seen  |= 1u << binding.index;
    tensor = (id<MTLTensorBinding>)binding;

    if (tensor.tensorDataType != MTLTensorDataTypeFloat32 || tensor.dimensions.rank != 2u) {
      return GPU_ERROR_UNSUPPORTED;
    }

    if (binding.access == MTLBindingAccessWriteOnly) {
      continue;
    }

    count++;

    for (i = 0u; i < model->profileCount; i++) {
      profile = &model->profiles[i].desc;
      input   = NULL;

      for (j = 0u; j < profile->inputCount; j++) {
        if (profile->pInputs[j].slot == binding.index) {
          input = &profile->pInputs[j];
          break;
        }
      }

      if (!input) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }

      for (axis = 0u; axis < input->rank; axis++) {
        dimension = (uint64_t)[tensor.dimensions extentAtDimensionIndex:axis];

        if (input->pDimensions[axis] > NSIntegerMax
            || (dimension != UINT64_MAX && dimension != input->pDimensions[axis])) {
          return GPU_ERROR_INVALID_ARGUMENT;
        }
      }
    }
  }

  for (i = 0u; i < model->profileCount; i++) {
    if (model->profiles[i].desc.inputCount != count) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  return count > 0u ? GPU_OK : GPU_ERROR_UNSUPPORTED;
}

static GPUResult
pipelineInfo(MLProfile *profile, id<MTL4MachineLearningPipelineState> pipeline) {
  GPUMLBindingInfoEXT  *bindings;
  id<MTLBinding>        binding;
  id<MTLTensorBinding>  tensor;
  const char           *name;
  char                 *names;
  uint64_t             *dimensions;
  size_t                size;
  size_t                nameSize;
  uint32_t              count;
  uint32_t              i;
  uint32_t              j;

  if (!pipeline.reflection || pipeline.reflection.bindings.count == 0u
      || pipeline.reflection.bindings.count > MT_ARGUMENT_BUFFER_COUNT) {
    return GPU_ERROR_UNSUPPORTED;
  }

  count = (uint32_t)pipeline.reflection.bindings.count;
  size  = count * (sizeof(*bindings) + 2u * sizeof(uint64_t));

  for (binding in pipeline.reflection.bindings) {
    name = binding.name.UTF8String;

    if (!name || strlen(name) >= SIZE_MAX - size) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    size += strlen(name) + 1u;
  }

  if (!(bindings = calloc(1u, size))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  dimensions = (uint64_t *)(bindings + count);
  names      = (char *)(dimensions + 2u * count);
  i          = 0u;

  for (binding in pipeline.reflection.bindings) {
    if (binding.type != MTLBindingTypeTensor || binding.index >= MT_ARGUMENT_BUFFER_COUNT
        || (binding.access != MTLBindingAccessReadOnly && binding.access != MTLBindingAccessWriteOnly
            && binding.access != MTLBindingAccessReadWrite)) {
      free(bindings);
      return GPU_ERROR_UNSUPPORTED;
    }

    tensor = (id<MTLTensorBinding>)binding;

    if (tensor.tensorDataType != MTLTensorDataTypeFloat32 || tensor.dimensions.rank != 2u) {
      free(bindings);
      return GPU_ERROR_UNSUPPORTED;
    }

    for (j = 0u; j < i; j++) {
      if (bindings[j].shape.slot == binding.index) {
        free(bindings);
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }

    for (j = 0u; j < 2u; j++) {
      if ([tensor.dimensions extentAtDimensionIndex:j] <= 0) {
        free(bindings);
        return GPU_ERROR_UNSUPPORTED;
      }

      dimensions[2u * i + j] = (uint64_t)[tensor.dimensions extentAtDimensionIndex:j];
    }

    if (dimensions[2u * i] > INT64_MAX / 4u
        || dimensions[2u * i + 1u] > INT64_MAX / 4u / dimensions[2u * i]) {
      free(bindings);
      return GPU_ERROR_UNSUPPORTED;
    }

    name                         = binding.name.UTF8String;
    nameSize                     = strlen(name) + 1u;
    bindings[i].name              = names;
    bindings[i].access            = 0u;
    bindings[i].shape.pDimensions = dimensions + 2u * i;
    bindings[i].shape.dataType    = GPU_TENSOR_DATA_TYPE_F32_EXT;
    bindings[i].shape.slot        = (uint32_t)binding.index;
    bindings[i].shape.rank        = 2u;

    if (binding.access != MTLBindingAccessWriteOnly) {
      bindings[i].access |= GPU_ACCESS_SHADER_READ;
    }

    if (binding.access != MTLBindingAccessReadOnly) {
      bindings[i].access |= GPU_ACCESS_SHADER_WRITE;
    }

    memcpy(names, name, nameSize);
    names += nameSize;
    i++;
  }

  for (i = 0u; i < profile->desc.inputCount; i++) {
    for (j = 0u; j < count; j++) {
      if (bindings[j].shape.slot == profile->desc.pInputs[i].slot) {
        break;
      }
    }

    if (j == count || (bindings[j].access & GPU_ACCESS_SHADER_READ) == 0u
        || memcmp(bindings[j].shape.pDimensions, profile->desc.pInputs[i].pDimensions, 2u * sizeof(uint64_t)) != 0) {
      free(bindings);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  profile->info.pBindings    = bindings;
  profile->info.bindingCount = count;

  profile->info.scratchHeap.chain.sType       = GPU_STRUCTURE_TYPE_HEAP_CREATE_INFO;
  profile->info.scratchHeap.chain.structSize  = sizeof(profile->info.scratchHeap);
  profile->info.scratchHeap.sizeBytes         = MAX(64u, pipeline.intermediatesHeapSize);
  profile->info.scratchHeap.compatibilityMask = UINT64_C(1);
  profile->info.scratchHeap.usage             = GPU_HEAP_USAGE_PLACED;

  return GPU_OK;
}

static GPUResult
preparePipeline(GPUMLPipelineEXT *pipeline) {
  NSInteger                              dimensions[2];
  MLModelMT                             *native;
  DeviceMT                              *device;
  MTL4MachineLearningPipelineDescriptor *descriptor;
  MTL4LibraryFunctionDescriptor         *function;
  MTL4PipelineOptions                   *options;
  MTLTensorExtents                      *extents;
  id<MTL4MachineLearningPipelineState>   state;
  NSError                               *error;
  const GPUMLTensorShapeEXT             *input;
  GPUResult                              result;
  uint32_t                               i;

  native = pipeline->model->_priv;
  device = pipeline->model->device->_priv;

  if (pipeline->profile->_priv) {
    return GPU_OK;
  }

  function   = [MTL4LibraryFunctionDescriptor new];
  options    = [MTL4PipelineOptions new];
  descriptor = [MTL4MachineLearningPipelineDescriptor new];

  if (!function || !options || !descriptor) {
    [function release];
    [options release];
    [descriptor release];
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  function.library = native->library;
  function.name    = [NSString stringWithUTF8String:pipeline->model->functionName];

  options.shaderReflection = MTL4ShaderReflectionBindingInfo;

  descriptor.machineLearningFunctionDescriptor = function;

  descriptor.options = options;
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (deviceDebugMarkersEnabled(pipeline->model->device)) {
    descriptor.label = [NSString stringWithUTF8String:pipeline->label];
  }
#endif
  for (i = 0u; i < pipeline->profile->desc.inputCount; i++) {
    input         = &pipeline->profile->desc.pInputs[i];
    dimensions[0] = (NSInteger)input->pDimensions[0];
    dimensions[1] = (NSInteger)input->pDimensions[1];
    extents       = [[MTLTensorExtents alloc] initWithRank:2u values:dimensions];

    if (!extents) {
      [descriptor release];
      [options release];
      [function release];
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    [descriptor setInputDimensions:extents atBufferIndex:input->slot];
    [extents release];
  }

  error = nil;
  state = [(id<MTL4Compiler>)device->compiler newMachineLearningPipelineStateWithDescriptor:descriptor error:&error];
  [descriptor release];
  [options release];
  [function release];

  if (!state) {
    NSLog(@"ML pipeline creation failed: %@", error);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = pipelineInfo(pipeline->profile, state);

  if (result != GPU_OK) {
    [state release];
    return result;
  }

  pipeline->profile->_priv = state;

  return GPU_OK;
}
#endif

static GPUResult
createModel(GPUMLModelEXT *model) {
#if MT_HAS_METAL4
  MLModelMT *native;
  DeviceMT  *device;
  NSString  *path;
  NSString  *name;
  NSError   *error;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    @autoreleasepool {
      device = model->device->_priv;
      path   = [NSString stringWithUTF8String:model->path];
      name   = [NSString stringWithUTF8String:model->functionName];

      if (!device || device->commandMode != MTCommandMode4 || !device->compiler) {
        return GPU_ERROR_UNSUPPORTED;
      }

      if (!path || !name) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }

      if (!(native = calloc(1u, sizeof(*native)))) {
        return GPU_ERROR_OUT_OF_MEMORY;
      }

      model->_priv    = native;
      native->lock    = [NSLock new];
      error           = nil;
      native->library = [device->device newLibraryWithURL:[NSURL fileURLWithPath:path] error:&error];

      if (!native->lock) {
        return GPU_ERROR_OUT_OF_MEMORY;
      }

      if (!native->library) {
        NSLog(@"ML model loading failed: %@", error);
        return GPU_ERROR_BACKEND_FAILURE;
      }

      native->reflection = [[native->library reflectionForFunctionWithName:name] retain];

      if (!native->reflection) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }

      return modelInputs(model);
    }
  }
#else
  GPU__UNUSED(model);
#endif

  return GPU_ERROR_UNSUPPORTED;
}

static void
destroyModel(GPUMLModelEXT *model) {
#if MT_HAS_METAL4
  MLModelMT *native;
  uint32_t   i;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    if (!(native = model->_priv)) {
      return;
    }

    for (i = 0u; i < model->profileCount; i++) {
      [(id)model->profiles[i]._priv release];
    }

    [native->reflection release];
    [native->library release];
    [native->lock release];
    free(native);
  }
#else
  GPU__UNUSED(model);
#endif
}

static GPUResult
createPipeline(GPUMLPipelineEXT *pipeline) {
#if MT_HAS_METAL4
  MLModelMT *native;
  GPUResult  result;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    @autoreleasepool {
      native = pipeline->model->_priv;
      [native->lock lock];
      result = preparePipeline(pipeline);
      [native->lock unlock];
      return result;
    }
  }
#else
  GPU__UNUSED(pipeline);
#endif

  return GPU_ERROR_UNSUPPORTED;
}

static GPUResult
createBindings(GPUMLBindingsEXT *bindings) {
#if MT_HAS_METAL4
  const GPUMLPipelineInfoEXT *info;
  MLBindingsMT               *native;
  GPUTensorEXT               *tensor;
  DeviceMT                   *device;
  HeapMT                     *heap;
  NSError                    *error;
  id                          descriptor;
  id<MTLTensor>               nativeTensor;
  uint32_t                    count;
  uint32_t                    i;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    @autoreleasepool {
      device = bindings->pipeline->model->device->_priv;
      heap   = bindings->scratch->_priv;
      info   = &bindings->pipeline->profile->info;
      count  = 0u;

      if (!heap || !heap->heap || heap->heap.device != device->device
          || heap->heap.type != MTLHeapTypePlacement || heap->heap.storageMode != MTLStorageModePrivate
          || heap->heap.size < info->scratchHeap.sizeBytes) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }

      for (i = 0u; i < bindings->bindingCount; i++) {
        tensor       = bindings->tensors[i];
        nativeTensor = tensor->_priv;

        if (!nativeTensor || nativeTensor.device != device->device
            || nativeTensor.buffer != (id<MTLBuffer>)tensor->buffer->_priv
            || nativeTensor.bufferOffset != tensor->offsetBytes) {
          return GPU_ERROR_INVALID_ARGUMENT;
        }

        count = MAX(count, info->pBindings[i].shape.slot + 1u);
      }

      if (!(native = calloc(1u, sizeof(*native)))) {
        return GPU_ERROR_OUT_OF_MEMORY;
      }

      bindings->_priv = native;
      descriptor      = [MTL4ArgumentTableDescriptor new];

      if (!descriptor) {
        return GPU_ERROR_OUT_OF_MEMORY;
      }

      [descriptor setMaxBufferBindCount:count];
      [descriptor setInitializeBindings:YES];
#if GPU_BUILD_WITH_DEBUG_MARKERS
      if (deviceDebugMarkersEnabled(bindings->pipeline->model->device)) {
        [descriptor setLabel:[NSString stringWithUTF8String:bindings->label]];
      }
#endif
      error         = nil;
      native->table = [device->device newArgumentTableWithDescriptor:descriptor error:&error];
      [descriptor release];

      if (!native->table) {
        return GPU_ERROR_BACKEND_FAILURE;
      }

      native->allocations[native->allocationCount++] = bindings->pipeline->profile->_priv;
      native->allocations[native->allocationCount++] = heap->heap;

      for (i = 0u; i < bindings->bindingCount; i++) {
        tensor = bindings->tensors[i];
        [(id<MTL4ArgumentTable>)native->table setResource:((id<MTLTensor>)tensor->_priv).gpuResourceID
                                          atBufferIndex:info->pBindings[i].shape.slot];
        native->allocations[native->allocationCount++] = tensor->buffer->_priv;
        native->allocations[native->allocationCount++] = tensor->_priv;
      }

      return GPU_OK;
    }
  }
#else
  GPU__UNUSED(bindings);
#endif

  return GPU_ERROR_UNSUPPORTED;
}

static void
destroyBindings(GPUMLBindingsEXT *bindings) {
#if MT_HAS_METAL4
  MLBindingsMT *native;

  if ((native = bindings->_priv)) {
    [native->table release];
    free(native);
  }
#else
  GPU__UNUSED(bindings);
#endif
}

static GPUResult
encode(GPUCommandBuffer *cmdb, GPUMLBindingsEXT *bindings) {
#if MT_HAS_METAL4
  MTCommandBuffer                       *command;
  MLBindingsMT                          *native;
  HeapMT                                *heap;
  id<MTL4MachineLearningCommandEncoder>  encoder;
  uint32_t                               i;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    command = mt_commandBuffer(cmdb);
    native  = bindings->_priv;
    heap    = bindings->scratch->_priv;

    if (!command || command->mode != MTCommandMode4 || !native) {
      return GPU_ERROR_UNSUPPORTED;
    }

    if (!(encoder = [(id<MTL4CommandBuffer>)command->modern machineLearningCommandEncoder])) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    for (i = 0u; i < native->allocationCount; i++) {
      mt_useAllocation(cmdb, native->allocations[i]);
    }

    mt_applyPendingBarrier(cmdb, encoder, MT_ENCODER_STAGES(MTLStageMachineLearning));
    [encoder setPipelineState:bindings->pipeline->profile->_priv];
    [encoder setArgumentTable:native->table];
    [encoder dispatchNetworkWithIntermediatesHeap:heap->heap];
    [encoder endEncoding];
    return GPU_OK;
  }
#else
  GPU__UNUSED(cmdb);
  GPU__UNUSED(bindings);
#endif

  return GPU_ERROR_UNSUPPORTED;
}

GPU_HIDE
void
mt_initML(ApiML *api) {
  api->createModel     = createModel;
  api->destroyModel    = destroyModel;
  api->createPipeline  = createPipeline;
  api->createBindings  = createBindings;
  api->destroyBindings = destroyBindings;
  api->encode          = encode;
}
