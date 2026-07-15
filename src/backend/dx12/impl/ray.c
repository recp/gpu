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
#include "../impl.h"

enum {
  GPU_DX12_RAY_STACK_GEOMETRY_COUNT = 8u
};

static GPUAccelerationStructureDX12 *
dx12_rayStructure(GPUAccelerationStructureEXT *structure) {
  return structure ? structure->_priv : NULL;
}

static GPUAccelerationStructureEncoderDX12 *
dx12_rayEncoder(GPUAccelerationStructurePassEncoderEXT *pass) {
  return pass ? pass->_priv : NULL;
}

static D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE
dx12_rayType(GPUAccelerationStructureTypeEXT type) {
  return type == GPU_ACCELERATION_STRUCTURE_TOP_LEVEL_EXT
           ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL
           : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
}

static D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS
dx12_rayBuildFlags(GPUAccelerationStructureBuildFlagsEXT flags,
                   bool                                  update) {
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS result;

  result = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
  if ((flags & GPU_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_EXT) != 0u) {
    result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
  }
  if ((flags & GPU_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_EXT) != 0u) {
    result |=
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  }
  if ((flags & GPU_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_EXT) != 0u) {
    result |=
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
  }
  if (update) {
    result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
  }
  return result;
}

static D3D12_RAYTRACING_INSTANCE_FLAGS
dx12_rayInstanceFlags(GPUAccelerationStructureInstanceFlagsEXT flags) {
  D3D12_RAYTRACING_INSTANCE_FLAGS result;

  result = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
  if ((flags & GPU_ACCELERATION_STRUCTURE_INSTANCE_DISABLE_CULL_BIT_EXT) != 0u) {
    result |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
  }
  if ((flags & GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_OPAQUE_BIT_EXT) != 0u) {
    result |= D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
  }
  if ((flags &
       GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_NON_OPAQUE_BIT_EXT) != 0u) {
    result |= D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
  }
  return result;
}

static uint64_t
dx12_rayAlign(uint64_t value, uint64_t alignment) {
  if (value > UINT64_MAX - (alignment - 1u)) {
    return 0u;
  }
  return (value + alignment - 1u) & ~(alignment - 1u);
}

static void
dx12_rayFillTriangle(
  D3D12_RAYTRACING_GEOMETRY_DESC                 *dst,
  const GPUAccelerationStructureTriangleGeometryEXT *src) {
  GPUBufferDX12 *vertex;
  GPUBufferDX12 *index;

  vertex = src->vertexBuffer->_priv;
  index  = src->indexBuffer ? src->indexBuffer->_priv : NULL;
  memset(dst, 0, sizeof(*dst));
  dst->Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  dst->Flags =
    (src->flags &
     GPU_ACCELERATION_STRUCTURE_GEOMETRY_NON_OPAQUE_BIT_EXT) == 0u
      ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
      : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
  dst->Triangles.Transform3x4 = 0u;
  dst->Triangles.IndexFormat = index
                                 ? (src->indexType == GPU_INDEX_TYPE_UINT32
                                      ? DXGI_FORMAT_R32_UINT
                                      : DXGI_FORMAT_R16_UINT)
                                 : DXGI_FORMAT_UNKNOWN;
  dst->Triangles.VertexFormat             = DXGI_FORMAT_R32G32B32_FLOAT;
  dst->Triangles.IndexCount               = src->indexCount;
  dst->Triangles.VertexCount              = src->vertexCount;
  dst->Triangles.IndexBuffer              = index
                                               ? index->gpuAddress +
                                                   src->indexOffset
                                               : 0u;
  dst->Triangles.VertexBuffer.StartAddress = vertex->gpuAddress +
                                              src->vertexOffset;
  dst->Triangles.VertexBuffer.StrideInBytes = src->vertexStride;
}

static void
dx12_rayFillInputs(
  const GPUAccelerationStructureBuildInfoEXT       *info,
  D3D12_RAYTRACING_GEOMETRY_DESC                   *geometries,
  D3D12_GPU_VIRTUAL_ADDRESS                         instanceAddress,
  bool                                              update,
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *outInputs) {
  memset(outInputs, 0, sizeof(*outInputs));
  outInputs->Type        = dx12_rayType(info->type);
  outInputs->Flags       = dx12_rayBuildFlags(info->flags, update);
  outInputs->DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  if (info->type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT) {
    outInputs->NumDescs       = info->bottomLevel.geometryCount;
    outInputs->pGeometryDescs = geometries;
  } else {
    outInputs->NumDescs      = info->topLevel.instanceCount;
    outInputs->InstanceDescs = instanceAddress;
  }
}

GPU_HIDE
GPUResult
dx12_getAccelerationStructureSizes(
  GPUDevice                                    *device,
  const GPUAccelerationStructureBuildInfoEXT  *info,
  GPUAccelerationStructureSizesEXT            *outSizes) {
  GPUDeviceDX12 *deviceDX12;
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes = {0};
  D3D12_RAYTRACING_GEOMETRY_DESC stackGeometries[
    GPU_DX12_RAY_STACK_GEOMETRY_COUNT];
  D3D12_RAYTRACING_GEOMETRY_DESC *geometries;
  uint32_t                         geometryCount;
  bool                             heap;

  deviceDX12 = device ? device->_priv : NULL;
  if (!deviceDX12 || !deviceDX12->rayQuery || !deviceDX12->d3dDevice5) {
    return GPU_ERROR_UNSUPPORTED;
  }

  geometryCount = info->type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT
                    ? info->bottomLevel.geometryCount
                    : 0u;
  heap       = geometryCount > GPU_DX12_RAY_STACK_GEOMETRY_COUNT;
  geometries = heap ? calloc(geometryCount, sizeof(*geometries))
                    : stackGeometries;
  if (geometryCount > 0u && !geometries) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  for (uint32_t i = 0u; i < geometryCount; i++) {
    dx12_rayFillTriangle(&geometries[i],
                         &info->bottomLevel.pGeometries[i]);
  }

  dx12_rayFillInputs(info, geometries, 0u, false, &inputs);
  deviceDX12->d3dDevice5->lpVtbl
    ->GetRaytracingAccelerationStructurePrebuildInfo(deviceDX12->d3dDevice5,
                                                     &inputs,
                                                     &sizes);
  if (heap) {
    free(geometries);
  }
  outSizes->accelerationStructureSize = sizes.ResultDataMaxSizeInBytes;
  outSizes->buildScratchSize          = sizes.ScratchDataSizeInBytes;
  outSizes->updateScratchSize         = sizes.UpdateScratchDataSizeInBytes;
  return sizes.ResultDataMaxSizeInBytes > 0u
           ? GPU_OK
           : GPU_ERROR_BACKEND_FAILURE;
}

#if GPU_BUILD_WITH_DEBUG_MARKERS
static void
dx12_raySetName(ID3D12Resource *resource, const char *label) {
  wchar_t name[256];

  if (!resource || !label || label[0] == '\0' ||
      MultiByteToWideChar(CP_UTF8,
                          MB_ERR_INVALID_CHARS,
                          label,
                          -1,
                          name,
                          (int)GPU_ARRAY_LEN(name)) <= 0) {
    return;
  }
  (void)resource->lpVtbl->SetName(resource, name);
}
#else
#  define dx12_raySetName(resource, label) ((void)0)
#endif

static void
dx12_rayDestroyState(GPUAccelerationStructureDX12 *native) {
  if (!native) {
    return;
  }
  if (native->instanceBuffer) {
    if (native->instanceMapped) {
      native->instanceBuffer->lpVtbl->Unmap(native->instanceBuffer, 0u, NULL);
    }
    native->instanceBuffer->lpVtbl->Release(native->instanceBuffer);
  }
  if (native->resource) {
    native->resource->lpVtbl->Release(native->resource);
  }
  free(native->geometries);
  free(native);
}

GPU_HIDE
GPUResult
dx12_createAccelerationStructure(
  GPUDevice                                    *device,
  const GPUAccelerationStructureCreateInfoEXT *info,
  GPUAccelerationStructureEXT                 *structure) {
  GPUDeviceDX12                    *deviceDX12;
  GPUAccelerationStructureDX12    *native;
  D3D12_HEAP_PROPERTIES            heap = {0};
  D3D12_RESOURCE_DESC              desc = {0};
  uint64_t                         sizeBytes;
  HRESULT                          result;

  deviceDX12 = device ? device->_priv : NULL;
  if (!deviceDX12 || !deviceDX12->rayQuery || !deviceDX12->d3dDevice5) {
    return GPU_ERROR_UNSUPPORTED;
  }
  sizeBytes = dx12_rayAlign(
    info->sizeBytes,
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT
  );
  if (sizeBytes == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->device          = deviceDX12;
  heap.Type               = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask   = 1u;
  heap.VisibleNodeMask    = 1u;
  desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width              = sizeBytes;
  desc.Height             = 1u;
  desc.DepthOrArraySize   = 1u;
  desc.MipLevels          = 1u;
  desc.SampleDesc.Count   = 1u;
  desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  result = deviceDX12->d3dDevice->lpVtbl->CreateCommittedResource(
    deviceDX12->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_NONE,
    &desc,
    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
    NULL,
    &IID_ID3D12Resource,
    (void **)&native->resource
  );
  if (FAILED(result) || !native->resource) {
    dx12_rayDestroyState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  native->address = native->resource->lpVtbl->GetGPUVirtualAddress(
    native->resource
  );
  if (!native->address) {
    dx12_rayDestroyState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  dx12_raySetName(native->resource, info->label);
  structure->_priv = native;
  return GPU_OK;
}

GPU_HIDE
void
dx12_destroyAccelerationStructure(GPUAccelerationStructureEXT *structure) {
  GPUAccelerationStructureDX12 *native;

  native = dx12_rayStructure(structure);
  dx12_rayDestroyState(native);
  if (structure) {
    structure->_priv = NULL;
  }
}

static bool
dx12_rayEnsureGeometryCapacity(GPUAccelerationStructureDX12 *native,
                               uint32_t                        count) {
  D3D12_RAYTRACING_GEOMETRY_DESC *geometries;
  uint32_t                         capacity;

  if (count <= native->geometryCapacity) {
    return true;
  }
  capacity = native->geometryCapacity ? native->geometryCapacity : 4u;
  while (capacity < count) {
    if (capacity > UINT32_MAX / 2u) {
      capacity = count;
      break;
    }
    capacity *= 2u;
  }
  geometries = realloc(native->geometries,
                       (size_t)capacity * sizeof(*geometries));
  if (!geometries) {
    return false;
  }
  native->geometries       = geometries;
  native->geometryCapacity = capacity;
  return true;
}

static bool
dx12_rayEnsureInstanceBuffer(GPUDeviceDX12                   *device,
                             GPUAccelerationStructureDX12   *native,
                             uint64_t                         sizeBytes) {
  ID3D12Resource                *buffer;
  D3D12_HEAP_PROPERTIES          heap = {0};
  D3D12_RESOURCE_DESC            desc = {0};
  D3D12_RANGE                    readRange = {0};
  D3D12_GPU_VIRTUAL_ADDRESS      address;
  void                          *mapped;
  uint64_t                       capacity;
  HRESULT                        result;

  if (sizeBytes <= native->instanceCapacity && native->instanceBuffer) {
    return true;
  }
  capacity = native->instanceCapacity ? native->instanceCapacity : 256u;
  while (capacity < sizeBytes) {
    if (capacity > UINT64_MAX / 2u) {
      capacity = sizeBytes;
      break;
    }
    capacity *= 2u;
  }
  capacity = dx12_rayAlign(capacity,
                           D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT);
  if (!capacity) {
    return false;
  }

  buffer                  = NULL;
  mapped                  = NULL;
  heap.Type               = D3D12_HEAP_TYPE_UPLOAD;
  heap.CreationNodeMask   = 1u;
  heap.VisibleNodeMask    = 1u;
  desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width              = capacity;
  desc.Height             = 1u;
  desc.DepthOrArraySize   = 1u;
  desc.MipLevels          = 1u;
  desc.SampleDesc.Count   = 1u;
  desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  result = device->d3dDevice->lpVtbl->CreateCommittedResource(
    device->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_NONE,
    &desc,
    D3D12_RESOURCE_STATE_GENERIC_READ,
    NULL,
    &IID_ID3D12Resource,
    (void **)&buffer
  );
  if (FAILED(result) || !buffer ||
      FAILED(buffer->lpVtbl->Map(buffer, 0u, &readRange, &mapped)) ||
      !mapped || !(address = buffer->lpVtbl->GetGPUVirtualAddress(buffer))) {
    if (buffer) {
      if (mapped) {
        buffer->lpVtbl->Unmap(buffer, 0u, NULL);
      }
      buffer->lpVtbl->Release(buffer);
    }
    return false;
  }

  dx12_raySetName(buffer, "gpu-ray-instance-buffer");
  if (native->instanceBuffer) {
    if (native->instanceMapped) {
      native->instanceBuffer->lpVtbl->Unmap(native->instanceBuffer, 0u, NULL);
    }
    native->instanceBuffer->lpVtbl->Release(native->instanceBuffer);
  }
  native->instanceBuffer   = buffer;
  native->instanceMapped   = mapped;
  native->instanceAddress  = address;
  native->instanceCapacity = capacity;
  return true;
}

static bool
dx12_rayPrepareBLAS(
  ID3D12GraphicsCommandList                    *commandList,
  GPUAccelerationStructureDX12                *native,
  const GPUAccelerationStructureBuildInfoEXT  *info) {
  if (!dx12_rayEnsureGeometryCapacity(native,
                                      info->bottomLevel.geometryCount)) {
    return false;
  }

  for (uint32_t i = 0u; i < info->bottomLevel.geometryCount; i++) {
    const GPUAccelerationStructureTriangleGeometryEXT *source;
    GPUBufferDX12                                     *vertex;
    GPUBufferDX12                                     *index;

    source = &info->bottomLevel.pGeometries[i];
    vertex = source->vertexBuffer->_priv;
    index  = source->indexBuffer ? source->indexBuffer->_priv : NULL;
    if (!vertex || !vertex->gpuAddress ||
        !dx12_transitionBuffer(commandList,
                               vertex,
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
        (index &&
         (!index->gpuAddress ||
          !dx12_transitionBuffer(
            commandList,
            index,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)))) {
      return false;
    }
    dx12_rayFillTriangle(&native->geometries[i], source);
  }
  return true;
}

static bool
dx12_rayPrepareTLAS(
  GPUDeviceDX12                                *device,
  GPUAccelerationStructureDX12                *native,
  const GPUAccelerationStructureBuildInfoEXT  *info) {
  D3D12_RAYTRACING_INSTANCE_DESC *instances;
  uint64_t                        sizeBytes;

  sizeBytes = (uint64_t)info->topLevel.instanceCount *
              sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
  if (!dx12_rayEnsureInstanceBuffer(device, native, sizeBytes)) {
    return false;
  }

  instances = native->instanceMapped;
  memset(instances, 0, (size_t)sizeBytes);
  for (uint32_t i = 0u; i < info->topLevel.instanceCount; i++) {
    const GPUAccelerationStructureInstanceEXT *source;
    GPUAccelerationStructureDX12              *structure;

    source    = &info->topLevel.pInstances[i];
    structure = dx12_rayStructure(source->structure);
    if (!structure || !structure->address) {
      return false;
    }
    memcpy(instances[i].Transform,
           source->transform,
           sizeof(instances[i].Transform));
    instances[i].InstanceID                          = 0u;
    instances[i].InstanceMask                        = source->mask
                                                        ? source->mask
                                                        : 0xffu;
    instances[i].InstanceContributionToHitGroupIndex = 0u;
    instances[i].Flags = dx12_rayInstanceFlags(source->flags);
    instances[i].AccelerationStructure = structure->address;
  }
  return true;
}

GPU_HIDE
GPUAccelerationStructurePassEncoderEXT *
dx12_beginAccelerationStructurePass(GPUCommandBuffer *cmdb,
                                    const char       *label) {
  GPUCommandBufferDX12                     *command;
  GPUAccelerationStructurePassEncoderEXT   *pass;
  GPUAccelerationStructureEncoderDX12      *native;
  GPUDevice                                *device;

  command = cmdb ? cmdb->_priv : NULL;
  device  = gpuCommandBufferDevice(cmdb);
  if (!command || !command->owner || !command->commandList ||
      !command->commandList5 || !device ||
      (command->owner->type != D3D12_COMMAND_LIST_TYPE_DIRECT &&
       command->owner->type != D3D12_COMMAND_LIST_TYPE_COMPUTE)) {
    return NULL;
  }

  pass   = &command->rayQueryEncoder;
  native = &command->rayQueryState;
  memset(pass, 0, sizeof(*pass));
  memset(native, 0, sizeof(*native));
  native->commandList      = command->commandList;
  native->commandList5     = command->commandList5;
  native->debugEventActive = dx12_beginDebugEvent(device,
                                                   native->commandList,
                                                   label);
  pass->_priv = native;
  return pass;
}

GPU_HIDE
GPUResult
dx12_buildAccelerationStructure(
  GPUAccelerationStructurePassEncoderEXT     *pass,
  GPUAccelerationStructureEXT                *dst,
  const GPUAccelerationStructureBuildInfoEXT *info,
  GPUBuffer                                   *scratchBuffer,
  uint64_t                                     scratchOffset) {
  GPUAccelerationStructureEncoderDX12 *encoder;
  GPUAccelerationStructureDX12        *native;
  GPUAccelerationStructureDX12        *source;
  GPUBufferDX12                        *scratch;
  GPUDeviceDX12                        *device;
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {0};
  D3D12_RESOURCE_BARRIER                barrier = {0};
  D3D12_GPU_VIRTUAL_ADDRESS             scratchAddress;
  bool                                  prepared;

  encoder = dx12_rayEncoder(pass);
  native  = dx12_rayStructure(dst);
  source  = info->source ? dx12_rayStructure(info->source) : NULL;
  scratch = scratchBuffer ? scratchBuffer->_priv : NULL;
  device  = pass && pass->device ? pass->device->_priv : NULL;
  if (!encoder || !encoder->commandList || !encoder->commandList5 ||
      !native || !native->resource || !native->address || !scratch ||
      !scratch->resource || !scratch->gpuAddress || !device ||
      !device->rayQuery) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (scratchOffset > UINT64_MAX - scratch->gpuAddress) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  scratchAddress = scratch->gpuAddress + scratchOffset;
  if ((scratchAddress &
       (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1u)) != 0u ||
      !dx12_transitionBuffer(encoder->commandList,
                             scratch,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  prepared = info->type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT
               ? dx12_rayPrepareBLAS(encoder->commandList, native, info)
               : dx12_rayPrepareTLAS(device, native, info);
  if (!prepared) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  dx12_rayFillInputs(info,
                     native->geometries,
                     native->instanceAddress,
                     info->mode == GPU_ACCELERATION_STRUCTURE_UPDATE_EXT,
                     &build.Inputs);
  build.DestAccelerationStructureData    = native->address;
  build.SourceAccelerationStructureData  = source ? source->address : 0u;
  build.ScratchAccelerationStructureData = scratchAddress;
  encoder->commandList5->lpVtbl->BuildRaytracingAccelerationStructure(
    encoder->commandList5,
    &build,
    0u,
    NULL
  );

  barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.UAV.pResource = native->resource;
  encoder->commandList->lpVtbl->ResourceBarrier(encoder->commandList,
                                                 1u,
                                                 &barrier);
  return GPU_OK;
}

GPU_HIDE
void
dx12_endAccelerationStructurePass(
  GPUAccelerationStructurePassEncoderEXT *pass) {
  GPUAccelerationStructureEncoderDX12 *native;

  native = dx12_rayEncoder(pass);
  if (!native) {
    return;
  }
  if (native->debugEventActive) {
    dx12_endDebugEvent(pass->device, native->commandList);
  }
  native->commandList      = NULL;
  native->commandList5     = NULL;
  native->debugEventActive = false;
}

GPU_HIDE
void
dx12_initRayQuery(GPUApiRayQuery *api) {
  api->getSizes  = dx12_getAccelerationStructureSizes;
  api->create    = dx12_createAccelerationStructure;
  api->destroy   = dx12_destroyAccelerationStructure;
  api->beginPass = dx12_beginAccelerationStructurePass;
  api->build     = dx12_buildAccelerationStructure;
  api->endPass   = dx12_endAccelerationStructurePass;
}
