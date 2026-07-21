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
#include "../../../api/compute_internal.h"
#include "../../../api/descr/descriptor_internal.h"
#include "../../../api/render/rce_internal.h"

typedef struct MTBufferDescriptorArray {
  GPUBuffer      argumentBuffer;
  uint32_t       layoutEntryIndex;
  uint32_t       arrayCount;
  GPUBindingType bindingType;
} MTBufferDescriptorArray;

typedef struct MTBindGroup {
  uint32_t                arrayCount;
  uint32_t                layoutEntryCount;
  MTBufferDescriptorArray arrays[];
} MTBindGroup;

typedef struct MTBindGroupScan {
  uint32_t arrayCount;
  uint32_t layoutEntryCount;
} MTBindGroupScan;

typedef struct MTBindGroupBuild {
  GPUDevice   *device;
  MTBindGroup *native;
  bool         valid;
} MTBindGroupBuild;

typedef struct MTBindContext {
  union {
    GPURenderPassEncoder  *render;
    GPUComputePassEncoder *compute;
  };
  MTBindGroup             *native;
  MTBufferDescriptorArray *dynamicRecord;
  GPUBuffer                dynamicBuffer;
  uint64_t                 dynamicOffset;
  uint32_t                 dynamicIndex;
  bool                     valid;
} MTBindContext;

static uint32_t *
mt_bindGroupRecordMap(MTBindGroup *native) {
  return native
           ? (uint32_t *)&native->arrays[native->arrayCount]
           : NULL;
}

static MTBufferDescriptorArray *
mt_bufferDescriptorArray(MTBindGroup *native, uint32_t layoutEntryIndex) {
  uint32_t *recordMap;
  uint32_t  recordIndex;

  if (!native || layoutEntryIndex >= native->layoutEntryCount) {
    return NULL;
  }
  recordMap   = mt_bindGroupRecordMap(native);
  recordIndex = recordMap[layoutEntryIndex];
  return recordIndex < native->arrayCount
           ? &native->arrays[recordIndex]
           : NULL;
}

static bool
mt_isBufferDescriptorArray(const GPUBindGroupBindingView *binding) {
  return binding && binding->kind == GPUBindKindBuffer &&
         binding->arrayCount > 1u;
}

static uint64_t
mt_bufferAddress(const GPUBindGroupBindingView *binding) {
  return binding && binding->buffer
           ? binding->buffer->_gpuAddress + binding->offset
           : 0u;
}

static void
mt_scanBindGroup(void *ctx, const GPUBindGroupBindingView *binding) {
  MTBindGroupScan *scan;

  if (!ctx || !binding) {
    return;
  }
  scan = ctx;
  if (binding->layoutEntryIndex >= scan->layoutEntryCount) {
    scan->layoutEntryCount = binding->layoutEntryIndex + 1u;
  }
  if (mt_isBufferDescriptorArray(binding) && binding->arrayIndex == 0u) {
    scan->arrayCount++;
  }
}

static void
mt_buildBindGroup(void *ctx, const GPUBindGroupBindingView *binding) {
  MTBufferDescriptorArray *record;
  MTBindGroupBuild         *build;
  GPUDeviceMT              *deviceMT;
  id<MTLBuffer>             argumentBuffer;
  uint32_t                 *recordMap;
  uint32_t                  recordIndex;
  uint64_t                 *addresses;

  if (!ctx || !mt_isBufferDescriptorArray(binding)) {
    return;
  }
  build     = ctx;
  recordMap = mt_bindGroupRecordMap(build->native);
  if (!build->valid || !recordMap ||
      binding->layoutEntryIndex >= build->native->layoutEntryCount) {
    build->valid = false;
    return;
  }

  recordIndex = recordMap[binding->layoutEntryIndex];
  if (binding->arrayIndex == 0u) {
    if (recordIndex != UINT32_MAX) {
      build->valid = false;
      return;
    }
    for (recordIndex = 0u;
         recordIndex < build->native->arrayCount;
         recordIndex++) {
      if (!build->native->arrays[recordIndex].argumentBuffer._priv) {
        break;
      }
    }
    if (recordIndex >= build->native->arrayCount) {
      build->valid = false;
      return;
    }

    deviceMT = build->device->_priv;
    argumentBuffer = [deviceMT->device
      newBufferWithLength:(NSUInteger)binding->arrayCount * sizeof(*addresses)
                  options:MTLResourceStorageModeShared];
    if (!argumentBuffer) {
      build->valid = false;
      return;
    }

    record                            = &build->native->arrays[recordIndex];
    record->argumentBuffer._priv      = argumentBuffer;
    record->argumentBuffer.device     = build->device;
    record->argumentBuffer.sizeBytes = argumentBuffer.length;
    record->argumentBuffer.usage     = GPU_BUFFER_USAGE_UNIFORM;
    record->layoutEntryIndex         = binding->layoutEntryIndex;
    record->arrayCount               = binding->arrayCount;
    record->bindingType              = binding->bindingType;
    if (@available(macOS 13.0, iOS 16.0, *)) {
      record->argumentBuffer._gpuAddress = argumentBuffer.gpuAddress;
    }
    recordMap[binding->layoutEntryIndex] = recordIndex;
  } else if (recordIndex == UINT32_MAX) {
    build->valid = false;
    return;
  }

  record = &build->native->arrays[recordIndex];
  if (record->arrayCount != binding->arrayCount ||
      binding->arrayIndex >= record->arrayCount) {
    build->valid = false;
    return;
  }
  addresses = record->argumentBuffer._priv
                ? [(id<MTLBuffer>)record->argumentBuffer._priv contents]
                : NULL;
  if (!addresses) {
    build->valid = false;
    return;
  }
  addresses[binding->arrayIndex] = mt_bufferAddress(binding);
}

static GPUResult
mt_createBindGroup(GPUDevice *device, GPUBindGroup *group) {
  MTBindGroupBuild build;
  MTBindGroupScan  scan;
  MTBindGroup     *native;
  uint32_t        *recordMap;
  size_t           storageSize;

  if (!device || !group) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(&scan, 0, sizeof(scan));
  if (!gpuForEachBindGroupBinding(group, mt_scanBindGroup, &scan)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (scan.arrayCount == 0u) {
    return GPU_OK;
  }
  if ((size_t)scan.arrayCount >
        (SIZE_MAX - sizeof(*native)) / sizeof(*native->arrays) ||
      (size_t)scan.layoutEntryCount >
        (SIZE_MAX - sizeof(*native) -
         (size_t)scan.arrayCount * sizeof(*native->arrays)) /
          sizeof(*recordMap)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  storageSize = sizeof(*native) +
                (size_t)scan.arrayCount * sizeof(*native->arrays) +
                (size_t)scan.layoutEntryCount * sizeof(*recordMap);
  native = calloc(1, storageSize);
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->arrayCount       = scan.arrayCount;
  native->layoutEntryCount = scan.layoutEntryCount;
  recordMap = mt_bindGroupRecordMap(native);
  for (uint32_t i = 0u; i < scan.layoutEntryCount; i++) {
    recordMap[i] = UINT32_MAX;
  }

  build.device = device;
  build.native = native;
  build.valid  = true;
  if (!gpuForEachBindGroupBinding(group, mt_buildBindGroup, &build) ||
      !build.valid) {
    for (uint32_t i = 0u; i < native->arrayCount; i++) {
      [(id<MTLBuffer>)native->arrays[i].argumentBuffer._priv release];
    }
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  group->_native = native;
  return GPU_OK;
}

static void
mt_updateBindGroupBinding(void *ctx,
                          const GPUBindGroupBindingView *binding) {
  MTBufferDescriptorArray *record;
  MTBindGroupBuild         *build;
  uint64_t                 *addresses;

  if (!ctx || !mt_isBufferDescriptorArray(binding)) {
    return;
  }
  build  = ctx;
  record = mt_bufferDescriptorArray(build->native,
                                    binding->layoutEntryIndex);
  if (!build->valid || !record ||
      binding->arrayIndex >= record->arrayCount) {
    build->valid = false;
    return;
  }

  addresses = record->argumentBuffer._priv
                ? [(id<MTLBuffer>)record->argumentBuffer._priv contents]
                : NULL;
  if (!addresses) {
    build->valid = false;
    return;
  }
  addresses[binding->arrayIndex] = mt_bufferAddress(binding);
}

static bool
mt_updateBindGroup(GPUBindGroup            *group,
                   uint32_t                 entryCount,
                   const GPUBindGroupEntry *entries) {
  MTBindGroupBuild build;

  if (!group || (entryCount > 0u && !entries)) {
    return false;
  }
  if (!group->_native) {
    return true;
  }

  build.device = gpuBindGroupGetDevice(group);
  build.native = group->_native;
  build.valid  = true;
  return gpuForEachBindGroupEntry(group,
                                  entryCount,
                                  entries,
                                  mt_updateBindGroupBinding,
                                  &build) &&
         build.valid;
}

static void
mt_destroyBindGroup(GPUBindGroup *group) {
  MTBindGroup *native;

  native = group ? group->_native : NULL;
  if (!native) {
    return;
  }
  for (uint32_t i = 0u; i < native->arrayCount; i++) {
    [(id<MTLBuffer>)native->arrays[i].argumentBuffer._priv release];
  }
  free(native);
  group->_native = NULL;
}

static MTLResourceUsage
mt_resourceUsage(GPUBindingType type) {
  return type == GPU_BINDING_STORAGE_BUFFER
           ? MTLResourceUsageRead | MTLResourceUsageWrite
           : MTLResourceUsageRead;
}

static MTLRenderStages
mt_renderStages(GPUShaderStageFlags visibility) {
  MTLRenderStages stages;

  stages = 0u;
  if ((visibility & GPU_SHADER_STAGE_VERTEX_BIT) != 0u) {
    stages |= MTLRenderStageVertex;
  }
  if ((visibility & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0u) {
    stages |= MTLRenderStageFragment;
  }
  if (@available(macOS 13.0, iOS 16.0, *)) {
    if ((visibility & GPU_SHADER_STAGE_TASK_BIT) != 0u) {
      stages |= MTLRenderStageObject;
    }
    if ((visibility & GPU_SHADER_STAGE_MESH_BIT) != 0u) {
      stages |= MTLRenderStageMesh;
    }
  }
  return stages;
}

static void
mt_useRenderBuffer(MTBindContext                  *ctx,
                   const GPUBindGroupBindingView *binding) {
  MTRenderEncoder *native;
  id<MTLBuffer>    buffer;

  if (!ctx || !binding || !binding->buffer) {
    return;
  }
  native = ctx->render->_priv;
  buffer = binding->buffer->_priv;
  if (!native || !buffer) {
    ctx->valid = false;
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    mt_useAllocation(ctx->render->_cmdb, buffer);
    return;
  }
#endif
  if (@available(macOS 10.15, iOS 13.0, *)) {
    [native->classic useResource:buffer
                           usage:mt_resourceUsage(binding->bindingType)
                          stages:mt_renderStages(binding->visibility)];
  }
}

static void
mt_useComputeBuffer(MTBindContext                  *ctx,
                    const GPUBindGroupBindingView *binding) {
  MTComputeEncoder *native;
  id<MTLBuffer>     buffer;

  if (!ctx || !binding || !binding->buffer) {
    return;
  }
  native = ctx->compute->_priv;
  buffer = binding->buffer->_priv;
  if (!native || !buffer) {
    ctx->valid = false;
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    mt_useAllocation(ctx->compute->_cmdb, buffer);
    return;
  }
#endif
  [native->classic useResource:buffer
                         usage:mt_resourceUsage(binding->bindingType)];
}

static void
mt_bindRenderBuffer(GPURenderPassEncoder *pass,
                    GPUBuffer            *buffer,
                    uint64_t              offset,
                    uint32_t              index,
                    GPUShaderStageFlags   visibility) {
  if ((visibility & GPU_SHADER_STAGE_VERTEX_BIT) != 0u) {
    gpuSetRenderVertexBuffer(pass, buffer, offset, index);
  }
  if ((visibility & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0u) {
    gpuSetRenderFragmentBuffer(pass, buffer, offset, index);
  }
  if ((visibility & GPU_SHADER_STAGE_TASK_BIT) != 0u) {
    gpuSetRenderTaskBuffer(pass, buffer, offset, index);
  }
  if ((visibility & GPU_SHADER_STAGE_MESH_BIT) != 0u) {
    gpuSetRenderMeshBuffer(pass, buffer, offset, index);
  }
}

static void
mt_bindRenderArgumentBuffer(MTBindContext                  *ctx,
                            MTBufferDescriptorArray        *record,
                            const GPUBindGroupBindingView *binding) {
  GPUBuffer    *argumentBuffer;
  uint64_t     *addresses;
  id<MTLBuffer> upload;
  uint64_t      argumentOffset;

  if (!binding->hasDynamicOffset) {
    if (binding->arrayIndex == 0u) {
      mt_bindRenderBuffer(ctx->render,
                          &record->argumentBuffer,
                          0u,
                          binding->binding,
                          binding->visibility);
    }
    mt_useRenderBuffer(ctx, binding);
    return;
  }

  if (binding->arrayIndex == 0u) {
    if (!mt_reserveUpload(ctx->render->_cmdb,
                          (uint64_t)record->arrayCount * sizeof(*addresses),
                          sizeof(*addresses),
                          &upload,
                          &ctx->dynamicOffset)) {
      ctx->valid = false;
      return;
    }
    ctx->dynamicRecord = record;
    ctx->dynamicIndex  = 0u;
    memset(&ctx->dynamicBuffer, 0, sizeof(ctx->dynamicBuffer));
    ctx->dynamicBuffer._priv     = upload;
    ctx->dynamicBuffer.device    = record->argumentBuffer.device;
    ctx->dynamicBuffer.sizeBytes = upload.length;
    if (@available(macOS 13.0, iOS 16.0, *)) {
      ctx->dynamicBuffer._gpuAddress = upload.gpuAddress;
    }
    mt_bindRenderBuffer(ctx->render,
                        &ctx->dynamicBuffer,
                        ctx->dynamicOffset,
                        binding->binding,
                        binding->visibility);
  }
  if (ctx->dynamicRecord != record ||
      ctx->dynamicIndex != binding->arrayIndex) {
    ctx->valid = false;
    return;
  }

  argumentBuffer = &ctx->dynamicBuffer;
  argumentOffset = ctx->dynamicOffset;
  addresses = argumentBuffer->_priv
                ? (uint64_t *)((uint8_t *)[(id<MTLBuffer>)argumentBuffer->_priv
                                            contents] + argumentOffset)
                : NULL;
  if (!addresses) {
    ctx->valid = false;
    return;
  }
  addresses[binding->arrayIndex] = mt_bufferAddress(binding);
  ctx->dynamicIndex++;
  mt_useRenderBuffer(ctx, binding);
}

static void
mt_bindComputeArgumentBuffer(MTBindContext                  *ctx,
                             MTBufferDescriptorArray        *record,
                             const GPUBindGroupBindingView *binding) {
  uint64_t      *addresses;
  id<MTLBuffer>  upload;

  if (!binding->hasDynamicOffset) {
    if (binding->arrayIndex == 0u) {
      gpuSetComputeBuffer(ctx->compute,
                          &record->argumentBuffer,
                          0u,
                          binding->binding);
    }
    mt_useComputeBuffer(ctx, binding);
    return;
  }

  if (binding->arrayIndex == 0u) {
    if (!mt_reserveUpload(ctx->compute->_cmdb,
                          (uint64_t)record->arrayCount * sizeof(*addresses),
                          sizeof(*addresses),
                          &upload,
                          &ctx->dynamicOffset)) {
      ctx->valid = false;
      return;
    }
    ctx->dynamicRecord = record;
    ctx->dynamicIndex  = 0u;
    memset(&ctx->dynamicBuffer, 0, sizeof(ctx->dynamicBuffer));
    ctx->dynamicBuffer._priv     = upload;
    ctx->dynamicBuffer.device    = record->argumentBuffer.device;
    ctx->dynamicBuffer.sizeBytes = upload.length;
    if (@available(macOS 13.0, iOS 16.0, *)) {
      ctx->dynamicBuffer._gpuAddress = upload.gpuAddress;
    }
    gpuSetComputeBuffer(ctx->compute,
                        &ctx->dynamicBuffer,
                        ctx->dynamicOffset,
                        binding->binding);
  }
  if (ctx->dynamicRecord != record ||
      ctx->dynamicIndex != binding->arrayIndex) {
    ctx->valid = false;
    return;
  }

  addresses = ctx->dynamicBuffer._priv
                ? (uint64_t *)((uint8_t *)[(id<MTLBuffer>)ctx->dynamicBuffer._priv
                                            contents] + ctx->dynamicOffset)
                : NULL;
  if (!addresses) {
    ctx->valid = false;
    return;
  }
  addresses[binding->arrayIndex] = mt_bufferAddress(binding);
  ctx->dynamicIndex++;
  mt_useComputeBuffer(ctx, binding);
}

static void
mt_bindRenderBinding(void *ctx, const GPUBindGroupBindingView *binding) {
  MTBufferDescriptorArray *record;
  MTBindContext            *bind;
  uint32_t                  index;

  if (!ctx || !binding) {
    return;
  }
  bind  = ctx;
  index = binding->binding + binding->arrayIndex;
  if (!bind->valid) {
    return;
  }

  record = mt_bufferDescriptorArray(bind->native,
                                    binding->layoutEntryIndex);
  if (record) {
    mt_bindRenderArgumentBuffer(bind, record, binding);
    return;
  }

  if ((binding->visibility & GPU_SHADER_STAGE_VERTEX_BIT) != 0u) {
    if (binding->kind == GPUBindKindBuffer && binding->buffer) {
      gpuSetRenderVertexBuffer(bind->render,
                               binding->buffer,
                               binding->offset,
                               index);
    } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
      gpuSetRenderVertexTexture(bind->render, binding->textureView, index);
    } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
      gpuSetRenderVertexSampler(bind->render, binding->sampler, index);
    } else if (binding->kind == GPUBindKindAccelerationStructure &&
               binding->accelerationStructure) {
      gpuSetRenderVertexAccelerationStructure(
        bind->render,
        binding->accelerationStructure,
        index);
    }
  }
  if ((binding->visibility & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0u) {
    if (binding->kind == GPUBindKindBuffer && binding->buffer) {
      gpuSetRenderFragmentBuffer(bind->render,
                                 binding->buffer,
                                 binding->offset,
                                 index);
    } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
      gpuSetRenderFragmentTexture(bind->render, binding->textureView, index);
    } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
      gpuSetRenderFragmentSampler(bind->render, binding->sampler, index);
    } else if (binding->kind == GPUBindKindAccelerationStructure &&
               binding->accelerationStructure) {
      gpuSetRenderFragmentAccelerationStructure(
        bind->render,
        binding->accelerationStructure,
        index);
    }
  }
  if ((binding->visibility & GPU_SHADER_STAGE_TASK_BIT) != 0u) {
    if (binding->kind == GPUBindKindBuffer && binding->buffer) {
      gpuSetRenderTaskBuffer(bind->render,
                             binding->buffer,
                             binding->offset,
                             index);
    } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
      gpuSetRenderTaskTexture(bind->render, binding->textureView, index);
    } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
      gpuSetRenderTaskSampler(bind->render, binding->sampler, index);
    }
  }
  if ((binding->visibility & GPU_SHADER_STAGE_MESH_BIT) != 0u) {
    if (binding->kind == GPUBindKindBuffer && binding->buffer) {
      gpuSetRenderMeshBuffer(bind->render,
                             binding->buffer,
                             binding->offset,
                             index);
    } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
      gpuSetRenderMeshTexture(bind->render, binding->textureView, index);
    } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
      gpuSetRenderMeshSampler(bind->render, binding->sampler, index);
    }
  }
}

static void
mt_bindComputeBinding(void *ctx, const GPUBindGroupBindingView *binding) {
  MTBufferDescriptorArray *record;
  MTBindContext            *bind;
  uint32_t                  index;

  if (!ctx || !binding ||
      (binding->visibility & GPU_SHADER_STAGE_COMPUTE_BIT) == 0u) {
    return;
  }
  bind  = ctx;
  index = binding->binding + binding->arrayIndex;
  if (!bind->valid) {
    return;
  }

  record = mt_bufferDescriptorArray(bind->native,
                                    binding->layoutEntryIndex);
  if (record) {
    mt_bindComputeArgumentBuffer(bind, record, binding);
  } else if (binding->kind == GPUBindKindBuffer && binding->buffer) {
    gpuSetComputeBuffer(bind->compute,
                        binding->buffer,
                        binding->offset,
                        index);
  } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
    gpuSetComputeTexture(bind->compute, binding->textureView, index);
  } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
    gpuSetComputeSampler(bind->compute, binding->sampler, index);
  } else if (binding->kind == GPUBindKindAccelerationStructure &&
             binding->accelerationStructure) {
    gpuSetComputeAccelerationStructure(bind->compute,
                                       binding->accelerationStructure,
                                       index);
  }
}

static bool
mt_bindRenderGroup(GPURenderPassEncoder *pass,
                   GPUPipelineLayout    *pipelineLayout,
                   uint32_t              groupIndex,
                   GPUBindGroup         *group,
                   uint32_t              dynamicOffsetCount,
                   const uint32_t       *dynamicOffsets) {
  MTBindContext ctx;

  memset(&ctx, 0, sizeof(ctx));
  ctx.render = pass;
  ctx.native = group ? group->_native : NULL;
  ctx.valid  = true;
  return gpuForEachBindGroupBindingWithDynamicOffsets(pipelineLayout,
                                                       groupIndex,
                                                       group,
                                                       dynamicOffsetCount,
                                                       dynamicOffsets,
                                                       mt_bindRenderBinding,
                                                       &ctx) &&
         ctx.valid;
}

static bool
mt_bindComputeGroup(GPUComputePassEncoder *pass,
                    GPUPipelineLayout     *pipelineLayout,
                    uint32_t               groupIndex,
                    GPUBindGroup          *group,
                    uint32_t               dynamicOffsetCount,
                    const uint32_t        *dynamicOffsets) {
  MTBindContext ctx;

  memset(&ctx, 0, sizeof(ctx));
  ctx.compute = pass;
  ctx.native  = group ? group->_native : NULL;
  ctx.valid   = true;
  return gpuForEachBindGroupBindingWithDynamicOffsets(pipelineLayout,
                                                       groupIndex,
                                                       group,
                                                       dynamicOffsetCount,
                                                       dynamicOffsets,
                                                       mt_bindComputeBinding,
                                                       &ctx) &&
         ctx.valid;
}

GPU_HIDE
void
mt_initDescriptor(GPUApiDescriptor *api) {
  api->createBindGroup  = mt_createBindGroup;
  api->updateBindGroup  = mt_updateBindGroup;
  api->destroyBindGroup = mt_destroyBindGroup;
  api->bindRenderGroup  = mt_bindRenderGroup;
  api->bindComputeGroup = mt_bindComputeGroup;
}
