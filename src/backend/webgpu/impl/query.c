/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

#if defined(__EMSCRIPTEN__)
#  include <emscripten/emscripten.h>

EM_JS(void,
      webgpu_writeTimestampJS,
      (uintptr_t encoderHandle, uintptr_t querySetHandle, uint32_t queryIndex), {
  const encoder = WebGPU.getJsObject(encoderHandle);
  const querySet = WebGPU.getJsObject(querySetHandle);
  const pass = encoder.beginComputePass({
    timestampWrites: {
      querySet,
      beginningOfPassWriteIndex: queryIndex
    }
  });
  pass.end();
});
#endif

static WGPUQueryType
webgpu_queryType(GPUQueryType type) {
  static const WGPUQueryType types[] = {
    [GPU_QUERY_TIMESTAMP] = WGPUQueryType_Timestamp,
    [GPU_QUERY_OCCLUSION] = WGPUQueryType_Occlusion
  };

  return (uint32_t)type < GPU_ARRAY_LEN(types)
           ? types[type]
           : WGPUQueryType_Force32;
}

static GPUResult
webgpu_createQuerySet(GPUDevice                   *device,
                      const GPUQuerySetCreateInfo *info,
                      GPUQuerySet                 *set) {
  WGPUQuerySetDescriptor descriptor = WGPU_QUERY_SET_DESCRIPTOR_INIT;
  GPUDeviceWebGPU       *native;

  native = gpu_webgpuDevice(device);
  if (!native || !native->device || !info || !set) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  descriptor.type = webgpu_queryType(info->type);
  if (descriptor.type == WGPUQueryType_Force32) {
    return GPU_ERROR_UNSUPPORTED;
  }
  descriptor.label = gpu_webgpuString(info->label);
  descriptor.count = info->count;
  set->_priv = wgpuDeviceCreateQuerySet(native->device, &descriptor);
  return set->_priv ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

static void
webgpu_destroyQuerySet(GPUQuerySet *set) {
  WGPUQuerySet native;

  native = set ? set->_priv : NULL;
  if (native) {
    wgpuQuerySetDestroy(native);
    wgpuQuerySetRelease(native);
    set->_priv = NULL;
  }
}

static void
webgpu_writeTimestamp(GPUCommandBuffer *cmdb,
                      GPUQuerySet      *set,
                      uint32_t          queryIndex) {
  GPUCommandWebGPU *command;

  command = gpu_webgpuCommand(cmdb);
  if (command && command->encoder && set && set->_priv) {
#if defined(__EMSCRIPTEN__)
    webgpu_writeTimestampJS((uintptr_t)command->encoder,
                            (uintptr_t)set->_priv,
                            queryIndex);
#else
    wgpuCommandEncoderWriteTimestamp(command->encoder,
                                     set->_priv,
                                     queryIndex);
#endif
  }
}

static void
webgpu_beginOcclusionQuery(GPURenderPassEncoder *pass,
                           GPUQuerySet          *set,
                           uint32_t              queryIndex) {
  GPUCommandWebGPU *command;

  command = pass ? pass->_priv : NULL;
  if (command && command->renderEncoder && set && set->_priv) {
    wgpuRenderPassEncoderBeginOcclusionQuery(command->renderEncoder,
                                              queryIndex);
  }
}

static void
webgpu_endOcclusionQuery(GPURenderPassEncoder *pass,
                         GPUQuerySet          *set,
                         uint32_t              queryIndex) {
  GPUCommandWebGPU *command;

  GPU__UNUSED(set);
  GPU__UNUSED(queryIndex);
  command = pass ? pass->_priv : NULL;
  if (command && command->renderEncoder) {
    wgpuRenderPassEncoderEndOcclusionQuery(command->renderEncoder);
  }
}

static WGPUBuffer
webgpu_queryScratch(GPUCommandWebGPU *command, uint64_t sizeBytes) {
  WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
  GPUDeviceWebGPU     *device;

  /* WebGPU query sets contain at most 8192 64-bit results. */
  if (sizeBytes > GPU_WEBGPU_QUERY_RESOLVE_CAPACITY) {
    return NULL;
  }
  if (command->queryResolveScratch) {
    return command->queryResolveScratch;
  }

  device = gpu_webgpuDevice(gpuCommandBufferDevice(&command->command));
  if (!device || !device->device) {
    return NULL;
  }
  descriptor.label = gpu_webgpuString("gpu-webgpu-query-resolve");
  descriptor.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
  descriptor.size  = GPU_WEBGPU_QUERY_RESOLVE_CAPACITY;
  command->queryResolveScratch = wgpuDeviceCreateBuffer(device->device,
                                                         &descriptor);
  return command->queryResolveScratch;
}

static void
webgpu_resolveQuerySet(GPUCommandBuffer *cmdb,
                       GPUQuerySet      *set,
                       uint32_t          firstQuery,
                       uint32_t          queryCount,
                       GPUBuffer        *dstBuffer,
                       uint64_t          dstOffset) {
  GPUCommandWebGPU *command;
  WGPUBuffer        destination;
  uint64_t          resultBytes;

  command = gpu_webgpuCommand(cmdb);
  if (!command || !command->encoder || !set || !set->_priv ||
      !dstBuffer || !dstBuffer->_priv) {
    return;
  }

  resultBytes = (uint64_t)queryCount * sizeof(uint64_t);
  destination = dstBuffer->_priv;
  if ((dstOffset & 255u) != 0u) {
    destination = webgpu_queryScratch(command, resultBytes);
    if (!destination) {
      return;
    }
  }

  wgpuCommandEncoderResolveQuerySet(command->encoder,
                                    set->_priv,
                                    firstQuery,
                                    queryCount,
                                    destination,
                                    destination == dstBuffer->_priv
                                      ? dstOffset
                                      : 0u);
  if (destination != dstBuffer->_priv) {
    wgpuCommandEncoderCopyBufferToBuffer(command->encoder,
                                         destination,
                                         0u,
                                         dstBuffer->_priv,
                                         dstOffset,
                                         resultBytes);
  }
}

void
webgpu_initQuery(GPUApiCommandBuffer *api) {
  api->createQuerySet      = webgpu_createQuerySet;
  api->destroyQuerySet     = webgpu_destroyQuerySet;
  api->writeTimestamp      = webgpu_writeTimestamp;
  api->beginOcclusionQuery = webgpu_beginOcclusionQuery;
  api->endOcclusionQuery   = webgpu_endOcclusionQuery;
  api->resolveQuerySet     = webgpu_resolveQuerySet;
}
