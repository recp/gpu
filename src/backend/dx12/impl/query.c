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
#include "../../../api/query_internal.h"

#include <stddef.h>

typedef struct GPUQuerySetDX12 {
  ID3D12QueryHeap *heap;
} GPUQuerySetDX12;

#define DX12__ASSERT_PIPESTAT_FIELD(GPU_FIELD, DX12_FIELD)                   \
  _Static_assert(offsetof(GPUPipelineStatisticsResult, GPU_FIELD) ==         \
                   offsetof(D3D12_QUERY_DATA_PIPELINE_STATISTICS,            \
                            DX12_FIELD),                                     \
                 #GPU_FIELD " must match Direct3D 12")

DX12__ASSERT_PIPESTAT_FIELD(inputAssemblyVertices, IAVertices);
DX12__ASSERT_PIPESTAT_FIELD(inputAssemblyPrimitives, IAPrimitives);
DX12__ASSERT_PIPESTAT_FIELD(vertexShaderInvocations, VSInvocations);
DX12__ASSERT_PIPESTAT_FIELD(geometryShaderInvocations, GSInvocations);
DX12__ASSERT_PIPESTAT_FIELD(geometryShaderPrimitives, GSPrimitives);
DX12__ASSERT_PIPESTAT_FIELD(clippingInvocations, CInvocations);
DX12__ASSERT_PIPESTAT_FIELD(clippingPrimitives, CPrimitives);
DX12__ASSERT_PIPESTAT_FIELD(fragmentShaderInvocations, PSInvocations);
DX12__ASSERT_PIPESTAT_FIELD(tessControlShaderPatches, HSInvocations);
DX12__ASSERT_PIPESTAT_FIELD(tessEvaluationShaderInvocations, DSInvocations);
DX12__ASSERT_PIPESTAT_FIELD(computeShaderInvocations, CSInvocations);

#undef DX12__ASSERT_PIPESTAT_FIELD

_Static_assert(sizeof(GPUPipelineStatisticsResult) ==
                 sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS),
               "pipeline statistics result must match Direct3D 12");

static void
dx12__setQueryName(ID3D12QueryHeap *heap, const char *label) {
  wchar_t name[256];

  if (!heap || !label || label[0] == '\0' ||
      MultiByteToWideChar(CP_UTF8,
                          MB_ERR_INVALID_CHARS,
                          label,
                          -1,
                          name,
                          (int)GPU_ARRAY_LEN(name)) <= 0) {
    return;
  }

  (void)heap->lpVtbl->SetName(heap, name);
}

static void
dx12__transitionQueryBuffer(GPUCommandBufferDX12 *command,
                            GPUBufferDX12        *buffer,
                            D3D12_RESOURCE_STATES state) {
  D3D12_RESOURCE_BARRIER barrier = {0};

  if (!command || !command->commandList || !buffer || !buffer->resource ||
      buffer->state == state) {
    return;
  }

  barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource   = buffer->resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = buffer->state;
  barrier.Transition.StateAfter  = state;
  command->commandList->lpVtbl->ResourceBarrier(command->commandList,
                                                 1u,
                                                 &barrier);
  buffer->state = state;
}

GPU_HIDE
GPUResult
dx12_createQuerySet(GPUDevice                  *device,
                    const GPUQuerySetCreateInfo *info,
                    GPUQuerySet                *set) {
  GPUDeviceDX12         *deviceDX12;
  GPUQuerySetDX12       *native;
  D3D12_QUERY_HEAP_DESC  desc = {0};
  HRESULT                result;

  deviceDX12 = device ? device->_priv : NULL;
  if (!deviceDX12 || !deviceDX12->d3dDevice || !info || !set ||
      (info->type != GPU_QUERY_OCCLUSION &&
       info->type != GPU_QUERY_PIPELINE_STATISTICS)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  desc.Type     = info->type == GPU_QUERY_OCCLUSION
                    ? D3D12_QUERY_HEAP_TYPE_OCCLUSION
                    : D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
  desc.Count    = info->count;
  desc.NodeMask = 0u;
  result = deviceDX12->d3dDevice->lpVtbl->CreateQueryHeap(
    deviceDX12->d3dDevice,
    &desc,
    &IID_ID3D12QueryHeap,
    (void **)&native->heap
  );
  if (FAILED(result) || !native->heap) {
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  dx12__setQueryName(native->heap, info->label);
  set->_priv = native;
  return GPU_OK;
}

GPU_HIDE
void
dx12_beginOcclusionQuery(GPURenderPassEncoder *pass,
                         GPUQuerySet          *set,
                         uint32_t              queryIndex) {
  GPURenderEncoderDX12 *encoder;
  GPUQuerySetDX12      *native;

  encoder = pass ? pass->_priv : NULL;
  native  = set ? set->_priv : NULL;
  if (!encoder || !encoder->commandList || !native || !native->heap) {
    return;
  }

  encoder->commandList->lpVtbl->BeginQuery(
    encoder->commandList,
    native->heap,
    D3D12_QUERY_TYPE_BINARY_OCCLUSION,
    queryIndex
  );
}

GPU_HIDE
void
dx12_endOcclusionQuery(GPURenderPassEncoder *pass,
                       GPUQuerySet          *set,
                       uint32_t              queryIndex) {
  GPURenderEncoderDX12 *encoder;
  GPUQuerySetDX12      *native;

  encoder = pass ? pass->_priv : NULL;
  native  = set ? set->_priv : NULL;
  if (!encoder || !encoder->commandList || !native || !native->heap) {
    return;
  }

  encoder->commandList->lpVtbl->EndQuery(
    encoder->commandList,
    native->heap,
    D3D12_QUERY_TYPE_BINARY_OCCLUSION,
    queryIndex
  );
}

GPU_HIDE
void
dx12_destroyQuerySet(GPUQuerySet *set) {
  GPUQuerySetDX12 *native;

  native = set ? set->_priv : NULL;
  if (!native) {
    return;
  }

  if (native->heap) {
    native->heap->lpVtbl->Release(native->heap);
  }
  free(native);
  set->_priv = NULL;
}

GPU_HIDE
void
dx12_beginPipelineStatisticsQuery(GPUCommandBuffer *cmdb,
                                  GPUQuerySet      *set,
                                  uint32_t          queryIndex) {
  GPUCommandBufferDX12 *command;
  GPUQuerySetDX12      *native;

  command = cmdb ? cmdb->_priv : NULL;
  native  = set ? set->_priv : NULL;
  if (!command || !command->commandList || !native || !native->heap) {
    return;
  }

  command->commandList->lpVtbl->BeginQuery(
    command->commandList,
    native->heap,
    D3D12_QUERY_TYPE_PIPELINE_STATISTICS,
    queryIndex
  );
}

GPU_HIDE
void
dx12_endPipelineStatisticsQuery(GPUCommandBuffer *cmdb,
                                GPUQuerySet      *set,
                                uint32_t          queryIndex) {
  GPUCommandBufferDX12 *command;
  GPUQuerySetDX12      *native;

  command = cmdb ? cmdb->_priv : NULL;
  native  = set ? set->_priv : NULL;
  if (!command || !command->commandList || !native || !native->heap) {
    return;
  }

  command->commandList->lpVtbl->EndQuery(
    command->commandList,
    native->heap,
    D3D12_QUERY_TYPE_PIPELINE_STATISTICS,
    queryIndex
  );
}

GPU_HIDE
void
dx12_resolveQuerySet(GPUCommandBuffer *cmdb,
                     GPUQuerySet      *set,
                     uint32_t          firstQuery,
                     uint32_t          queryCount,
                     GPUBuffer        *dstBuffer,
                     uint64_t          dstOffset) {
  GPUCommandBufferDX12 *command;
  GPUQuerySetDX12      *native;
  GPUBufferDX12        *buffer;
  D3D12_QUERY_TYPE      queryType;
  D3D12_RESOURCE_STATES previousState;

  command = cmdb ? cmdb->_priv : NULL;
  native  = set ? set->_priv : NULL;
  buffer  = dstBuffer ? dstBuffer->_priv : NULL;
  if (!command || !command->commandList || !native || !native->heap ||
      !buffer || !buffer->resource || !buffer->defaultHeap) {
    if (dstBuffer && dstBuffer->device) {
      gpuDeviceRecordValidationError(
        dstBuffer->device,
        "Direct3D 12 query resolve requires a GPU-local destination buffer"
      );
    }
    return;
  }
  if (set->type == GPU_QUERY_OCCLUSION) {
    queryType = D3D12_QUERY_TYPE_BINARY_OCCLUSION;
  } else if (set->type == GPU_QUERY_PIPELINE_STATISTICS) {
    queryType = D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
  } else {
    return;
  }

  previousState = buffer->state;
  dx12__transitionQueryBuffer(command,
                              buffer,
                              D3D12_RESOURCE_STATE_COPY_DEST);
  command->commandList->lpVtbl->ResolveQueryData(
    command->commandList,
    native->heap,
    queryType,
    firstQuery,
    queryCount,
    buffer->resource,
    dstOffset
  );
  dx12__transitionQueryBuffer(command, buffer, previousState);
}

GPU_HIDE
void
dx12_initQuery(GPUApiCommandBuffer *api) {
  api->createQuerySet               = dx12_createQuerySet;
  api->destroyQuerySet              = dx12_destroyQuerySet;
  api->beginOcclusionQuery          = dx12_beginOcclusionQuery;
  api->endOcclusionQuery            = dx12_endOcclusionQuery;
  api->beginPipelineStatisticsQuery = dx12_beginPipelineStatisticsQuery;
  api->endPipelineStatisticsQuery   = dx12_endPipelineStatisticsQuery;
  api->resolveQuerySet              = dx12_resolveQuerySet;
}
