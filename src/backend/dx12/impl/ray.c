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
dx12_rayFillAABB(
  D3D12_RAYTRACING_GEOMETRY_DESC               *dst,
  const GPUAccelerationStructureAABBGeometryEXT *src) {
  GPUBufferDX12 *buffer;

  buffer = src->buffer->_priv;
  memset(dst, 0, sizeof(*dst));
  dst->Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
  dst->Flags =
    (src->flags & GPU_ACCELERATION_STRUCTURE_GEOMETRY_NON_OPAQUE_BIT_EXT) == 0u
      ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
      : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
  dst->AABBs.AABBCount             = src->count;
  dst->AABBs.AABBs.StartAddress    = buffer->gpuAddress + src->offset;
  dst->AABBs.AABBs.StrideInBytes   = src->stride;
}

static void
dx12_rayFillGeometry(
  D3D12_RAYTRACING_GEOMETRY_DESC             *dst,
  const GPUAccelerationStructureGeometryEXT  *src) {
  if (src->type == GPU_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_EXT) {
    dx12_rayFillAABB(dst, &src->aabbs);
  } else {
    dx12_rayFillTriangle(dst, &src->triangles);
  }
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
    dx12_rayFillGeometry(&geometries[i],
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
    const GPUAccelerationStructureGeometryEXT *source;

    source = &info->bottomLevel.pGeometries[i];
    if (source->type == GPU_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_EXT) {
      GPUBufferDX12 *buffer;

      buffer = source->aabbs.buffer->_priv;
      if (!buffer || !buffer->gpuAddress ||
          !dx12_transitionBuffer(
            commandList,
            buffer,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
        return false;
      }
    } else {
      GPUBufferDX12 *vertex;
      GPUBufferDX12 *index;

      vertex = source->triangles.vertexBuffer->_priv;
      index  = source->triangles.indexBuffer
                 ? source->triangles.indexBuffer->_priv
                 : NULL;
      if (!vertex || !vertex->gpuAddress ||
          !dx12_transitionBuffer(
            commandList,
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
    }
    dx12_rayFillGeometry(&native->geometries[i], source);
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
    instances[i].InstanceContributionToHitGroupIndex =
      source->hitGroupOffset;
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

typedef struct DX12RayHitGroupNames {
  wchar_t *closestHit;
  wchar_t *anyHit;
  wchar_t *intersection;
} DX12RayHitGroupNames;

static wchar_t *
dx12_rayWide(const char *text) {
  wchar_t *result;
  int      count;

  if (!text || !text[0]) {
    return NULL;
  }
  count = MultiByteToWideChar(CP_UTF8,
                              MB_ERR_INVALID_CHARS,
                              text,
                              -1,
                              NULL,
                              0);
  if (count <= 0 || (size_t)count > SIZE_MAX / sizeof(*result)) {
    return NULL;
  }
  result = malloc((size_t)count * sizeof(*result));
  if (!result || MultiByteToWideChar(CP_UTF8,
                                     MB_ERR_INVALID_CHARS,
                                     text,
                                     -1,
                                     result,
                                     count) != count) {
    free(result);
    return NULL;
  }
  return result;
}

static wchar_t *
dx12_rayHitGroupName(uint32_t index) {
  wchar_t name[64];
  wchar_t *result;
  int     count;

  count = swprintf(name,
                   GPU_ARRAY_LEN(name),
                   L"gpu_hit_group_%u",
                   index);
  if (count <= 0 || count >= (int)GPU_ARRAY_LEN(name)) {
    return NULL;
  }
  result = malloc(((size_t)count + 1u) * sizeof(*result));
  if (result) {
    memcpy(result, name, ((size_t)count + 1u) * sizeof(*result));
  }
  return result;
}

static void
dx12_rayFreeHitNames(DX12RayHitGroupNames *names, uint32_t count) {
  if (!names) {
    return;
  }
  for (uint32_t i = 0u; i < count; i++) {
    free(names[i].intersection);
    free(names[i].anyHit);
    free(names[i].closestHit);
  }
  free(names);
}

static void
dx12_rayDestroyPipelineState(GPURayTracingPipelineDX12 *native) {
  if (!native) {
    return;
  }
  if (native->groupExports) {
    for (uint32_t i = 0u; i < native->groupCount; i++) {
      free(native->groupExports[i]);
    }
  }
  if (native->properties) {
    native->properties->lpVtbl->Release(native->properties);
  }
  if (native->stateObject) {
    native->stateObject->lpVtbl->Release(native->stateObject);
  }
  if (native->rootSignature) {
    native->rootSignature->lpVtbl->Release(native->rootSignature);
  }
  free(native->groupExports);
  free(native);
}

static GPUResult
dx12_createRayTracingPipeline(GPUDevice                                *device,
                              const GPURayTracingPipelineCreateInfoEXT *info,
                              GPURayTracingPipelineEXT                 *pipeline) {
  GPUDeviceDX12                    *deviceDX12;
  GPUShaderLibraryDX12             *library;
  GPURayTracingPipelineDX12        *native;
  DX12RayHitGroupNames             *hitNames;
  D3D12_HIT_GROUP_DESC             *hitGroups;
  D3D12_STATE_SUBOBJECT            *subobjects;
  D3D12_DXIL_LIBRARY_DESC           libraryDesc = {0};
  D3D12_SHADER_BYTECODE             bytecode = {0};
  D3D12_RAYTRACING_SHADER_CONFIG    shaderConfig = {0};
  D3D12_GLOBAL_ROOT_SIGNATURE       globalRoot = {0};
  D3D12_RAYTRACING_PIPELINE_CONFIG  pipelineConfig = {0};
  D3D12_STATE_OBJECT_DESC           stateDesc = {0};
  DX12ShaderCode                    libraryCode = {0};
  uint64_t                          rootKey[2];
  uint32_t                          hitCount;
  uint32_t                          subobjectCount;
  uint32_t                          cursor;
  HRESULT                           result;

  deviceDX12 = device ? device->_priv : NULL;
  library    = info && info->library ? info->library->_priv : NULL;
  if (!deviceDX12 || !deviceDX12->rayTracingPipeline ||
      !deviceDX12->d3dDevice5 || !library || !info || !pipeline ||
      info->groupCount > UINT32_MAX - 4u ||
      info->groupCount > SIZE_MAX / sizeof(*native->groupExports) ||
      !dx12_compileRayLibrary(deviceDX12, library, &libraryCode)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  hitCount = 0u;
  for (uint32_t i = 0u; i < info->groupCount; i++) {
    hitCount += info->pGroups[i].type !=
                GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT;
  }
  subobjectCount = hitCount + 4u;
  native         = calloc(1, sizeof(*native));
  hitNames       = calloc(hitCount, sizeof(*hitNames));
  hitGroups      = calloc(hitCount, sizeof(*hitGroups));
  subobjects     = calloc(subobjectCount, sizeof(*subobjects));
  if (native) {
    native->groupExports = calloc(info->groupCount,
                                  sizeof(*native->groupExports));
  }
  if (!native || (hitCount > 0u && (!hitNames || !hitGroups)) ||
      !subobjects || !native->groupExports) {
    free(subobjects);
    free(hitGroups);
    dx12_rayFreeHitNames(hitNames, hitCount);
    dx12_rayDestroyPipelineState(native);
    dx12_freeShaderCode(&libraryCode);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->groupCount = info->groupCount;

  cursor = 0u;
  for (uint32_t i = 0u; i < info->groupCount; i++) {
    const GPURayTracingShaderGroupEXT *src;
    D3D12_HIT_GROUP_DESC              *dst;

    src = &info->pGroups[i];
    if (src->type == GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT) {
      native->groupExports[i] = dx12_rayWide(src->generalEntry);
      if (!native->groupExports[i]) {
        goto out_of_memory;
      }
      continue;
    }

    dst = &hitGroups[cursor];
    native->groupExports[i] = dx12_rayHitGroupName(i);
    hitNames[cursor].closestHit = dx12_rayWide(src->closestHitEntry);
    hitNames[cursor].anyHit = dx12_rayWide(src->anyHitEntry);
    hitNames[cursor].intersection = dx12_rayWide(src->intersectionEntry);
    if (!native->groupExports[i] ||
        (src->closestHitEntry && !hitNames[cursor].closestHit) ||
        (src->anyHitEntry && !hitNames[cursor].anyHit) ||
        (src->intersectionEntry && !hitNames[cursor].intersection)) {
      goto out_of_memory;
    }
    dst->HitGroupExport          = native->groupExports[i];
    dst->ClosestHitShaderImport  = hitNames[cursor].closestHit;
    dst->AnyHitShaderImport      = hitNames[cursor].anyHit;
    dst->IntersectionShaderImport = hitNames[cursor].intersection;
    dst->Type = src->type == GPU_RAY_TRACING_SHADER_GROUP_PROCEDURAL_HIT_EXT
                  ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE
                  : D3D12_HIT_GROUP_TYPE_TRIANGLES;
    cursor++;
  }

  if (dx12_createShaderRootSignature(device,
                                     info->layout,
                                     info->library,
                                     &native->rootSignature,
                                     rootKey) != GPU_OK) {
    goto backend_failure;
  }
  GPU__UNUSED(rootKey);
  bytecode.pShaderBytecode = libraryCode.data;
  bytecode.BytecodeLength  = libraryCode.size;
  libraryDesc.DXILLibrary  = bytecode;
  libraryDesc.NumExports   = 0u;
  libraryDesc.pExports     = NULL;

  cursor = 0u;
  subobjects[cursor].Type  = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  subobjects[cursor++].pDesc = &libraryDesc;
  for (uint32_t i = 0u; i < hitCount; i++) {
    subobjects[cursor].Type  = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[cursor++].pDesc = &hitGroups[i];
  }
  shaderConfig.MaxPayloadSizeInBytes      = info->maxPayloadSizeBytes;
  shaderConfig.MaxAttributeSizeInBytes    = info->maxHitAttributeSizeBytes;
  subobjects[cursor].Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
  subobjects[cursor++].pDesc = &shaderConfig;
  globalRoot.pGlobalRootSignature = native->rootSignature;
  subobjects[cursor].Type  = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
  subobjects[cursor++].pDesc = &globalRoot;
  pipelineConfig.MaxTraceRecursionDepth = info->maxRecursionDepth;
  subobjects[cursor].Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
  subobjects[cursor++].pDesc = &pipelineConfig;

  stateDesc.Type          = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  stateDesc.NumSubobjects = cursor;
  stateDesc.pSubobjects   = subobjects;
  result = deviceDX12->d3dDevice5->lpVtbl->CreateStateObject(
    deviceDX12->d3dDevice5,
    &stateDesc,
    &IID_ID3D12StateObject,
    (void **)&native->stateObject
  );
  if (FAILED(result) || !native->stateObject ||
      FAILED(native->stateObject->lpVtbl->QueryInterface(
        native->stateObject,
        &IID_ID3D12StateObjectProperties,
        (void **)&native->properties
      )) || !native->properties) {
    goto backend_failure;
  }

  free(subobjects);
  free(hitGroups);
  dx12_rayFreeHitNames(hitNames, hitCount);
  dx12_freeShaderCode(&libraryCode);
  pipeline->_priv = native;
  return GPU_OK;

out_of_memory:
  result = E_OUTOFMEMORY;
  goto fail;
backend_failure:
  result = E_FAIL;
fail:
  free(subobjects);
  free(hitGroups);
  dx12_rayFreeHitNames(hitNames, hitCount);
  dx12_rayDestroyPipelineState(native);
  dx12_freeShaderCode(&libraryCode);
  return result == E_OUTOFMEMORY ? GPU_ERROR_OUT_OF_MEMORY
                                : GPU_ERROR_BACKEND_FAILURE;
}

static void
dx12_destroyRayTracingPipeline(GPURayTracingPipelineEXT *pipeline) {
  GPURayTracingPipelineDX12 *native;

  native = pipeline ? pipeline->_priv : NULL;
  dx12_rayDestroyPipelineState(native);
  if (pipeline) {
    pipeline->_priv = NULL;
  }
}

static void
dx12_rayDestroyShaderTableState(GPUShaderTableDX12 *native) {
  if (!native) {
    return;
  }
  if (native->resource) {
    native->resource->lpVtbl->Release(native->resource);
  }
  free(native);
}

static bool
dx12_rayTableSection(uint64_t *cursor,
                     uint64_t  stride,
                     uint32_t  count,
                     uint64_t *outOffset,
                     uint64_t *outSize) {
  uint64_t offset;
  uint64_t size;

  offset = dx12_rayAlign(*cursor,
                         D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
  if ((*cursor != 0u && offset == 0u) ||
      (count > 0u && stride > UINT64_MAX / count)) {
    return false;
  }
  size = stride * count;
  if (offset > UINT64_MAX - size) {
    return false;
  }
  *outOffset = offset;
  *outSize   = size;
  *cursor    = offset + size;
  return true;
}

static bool
dx12_rayCopyIdentifiers(uint8_t                         *dst,
                        ID3D12StateObjectProperties     *properties,
                        wchar_t * const                 *groupExports,
                        const GPUShaderTableRecordEXT   *records,
                        uint32_t                         count,
                        uint64_t                         stride) {
  for (uint32_t i = 0u; i < count; i++) {
    const void *identifier;

    identifier = properties->lpVtbl->GetShaderIdentifier(
      properties,
      groupExports[records[i].groupIndex]
    );
    if (!identifier) {
      return false;
    }
    memcpy(dst + stride * i,
           identifier,
           D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  }
  return true;
}

static GPUResult
dx12_createShaderTable(GPUDevice                         *device,
                       const GPUShaderTableCreateInfoEXT *info,
                       GPUShaderTableEXT                 *table) {
  GPUDeviceDX12              *deviceDX12;
  GPURayTracingPipelineDX12  *pipeline;
  GPUShaderTableDX12         *native;
  D3D12_HEAP_PROPERTIES       heap = {0};
  D3D12_RESOURCE_DESC         desc = {0};
  D3D12_RANGE                 readRange = {0};
  D3D12_GPU_VIRTUAL_ADDRESS   address;
  uint8_t                    *mapped;
  uint64_t                    rayGenerationOffset;
  uint64_t                    missOffset;
  uint64_t                    hitOffset;
  uint64_t                    callableOffset;
  uint64_t                    rayGenerationSize;
  uint64_t                    missSize;
  uint64_t                    hitSize;
  uint64_t                    callableSize;
  uint64_t                    tableSize;
  uint64_t                    stride;
  HRESULT                     result;

  deviceDX12 = device ? device->_priv : NULL;
  pipeline   = info && info->pipeline ? info->pipeline->_priv : NULL;
  stride     = dx12_rayAlign(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
                            D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
  tableSize  = 0u;
  if (!deviceDX12 || !deviceDX12->rayTracingPipeline || !pipeline || !table ||
      !pipeline->properties || !stride ||
      !dx12_rayTableSection(&tableSize,
                            stride,
                            1u,
                            &rayGenerationOffset,
                            &rayGenerationSize) ||
      !dx12_rayTableSection(&tableSize,
                            stride,
                            info->missRecordCount,
                            &missOffset,
                            &missSize) ||
      !dx12_rayTableSection(&tableSize,
                            stride,
                            info->hitGroupRecordCount,
                            &hitOffset,
                            &hitSize) ||
      !dx12_rayTableSection(&tableSize,
                            stride,
                            info->callableRecordCount,
                            &callableOffset,
                            &callableSize)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  heap.Type             = D3D12_HEAP_TYPE_UPLOAD;
  heap.CreationNodeMask = 1u;
  heap.VisibleNodeMask  = 1u;
  desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width            = tableSize;
  desc.Height           = 1u;
  desc.DepthOrArraySize = 1u;
  desc.MipLevels        = 1u;
  desc.SampleDesc.Count = 1u;
  desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  result = deviceDX12->d3dDevice->lpVtbl->CreateCommittedResource(
    deviceDX12->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_NONE,
    &desc,
    D3D12_RESOURCE_STATE_GENERIC_READ,
    NULL,
    &IID_ID3D12Resource,
    (void **)&native->resource
  );
  if (FAILED(result) || !native->resource ||
      FAILED(native->resource->lpVtbl->Map(native->resource,
                                           0u,
                                           &readRange,
                                           (void **)&mapped)) || !mapped) {
    dx12_rayDestroyShaderTableState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  memset(mapped, 0, (size_t)tableSize);
  if (!dx12_rayCopyIdentifiers(mapped + rayGenerationOffset,
                               pipeline->properties,
                               pipeline->groupExports,
                               info->pRayGenerationRecord,
                               1u,
                               stride) ||
      !dx12_rayCopyIdentifiers(mapped + missOffset,
                               pipeline->properties,
                               pipeline->groupExports,
                               info->pMissRecords,
                               info->missRecordCount,
                               stride) ||
      !dx12_rayCopyIdentifiers(mapped + hitOffset,
                               pipeline->properties,
                               pipeline->groupExports,
                               info->pHitGroupRecords,
                               info->hitGroupRecordCount,
                               stride) ||
      !dx12_rayCopyIdentifiers(mapped + callableOffset,
                               pipeline->properties,
                               pipeline->groupExports,
                               info->pCallableRecords,
                               info->callableRecordCount,
                               stride)) {
    native->resource->lpVtbl->Unmap(native->resource, 0u, NULL);
    dx12_rayDestroyShaderTableState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  native->resource->lpVtbl->Unmap(native->resource, 0u, NULL);
  address = native->resource->lpVtbl->GetGPUVirtualAddress(native->resource);
  if (!address) {
    dx12_rayDestroyShaderTableState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->rayGeneration.StartAddress = address + rayGenerationOffset;
  native->rayGeneration.SizeInBytes  = rayGenerationSize;
  if (missSize > 0u) {
    native->miss.StartAddress  = address + missOffset;
    native->miss.SizeInBytes   = missSize;
    native->miss.StrideInBytes = stride;
  }
  if (hitSize > 0u) {
    native->hit.StartAddress  = address + hitOffset;
    native->hit.SizeInBytes   = hitSize;
    native->hit.StrideInBytes = stride;
  }
  if (callableSize > 0u) {
    native->callable.StartAddress  = address + callableOffset;
    native->callable.SizeInBytes   = callableSize;
    native->callable.StrideInBytes = stride;
  }
  dx12_raySetName(native->resource, info->label);
  table->_priv = native;
  return GPU_OK;
}

static void
dx12_destroyShaderTable(GPUShaderTableEXT *table) {
  GPUShaderTableDX12 *native;

  native = table ? table->_priv : NULL;
  dx12_rayDestroyShaderTableState(native);
  if (table) {
    table->_priv = NULL;
  }
}

static GPURayTracingPassEncoderEXT *
dx12_beginRayTracingPass(GPUCommandBuffer *cmdb, const char *label) {
  GPUCommandBufferDX12        *command;
  GPURayTracingPassEncoderEXT *pass;
  GPURayTracingEncoderDX12    *native;
  GPUDevice                   *device;

  command = cmdb ? cmdb->_priv : NULL;
  device  = gpuCommandBufferDevice(cmdb);
  if (!command || !command->owner || !command->commandList ||
      !command->commandList5 || !device ||
      (command->owner->type != D3D12_COMMAND_LIST_TYPE_DIRECT &&
       command->owner->type != D3D12_COMMAND_LIST_TYPE_COMPUTE)) {
    return NULL;
  }

  pass   = &command->rayTracingEncoder;
  native = &command->rayTracingState;
  memset(pass, 0, sizeof(*pass));
  memset(native, 0, sizeof(*native));
  native->device           = device->_priv;
  native->commandList      = command->commandList;
  native->commandList5     = command->commandList5;
  native->debugEventActive = dx12_beginDebugEvent(device,
                                                  native->commandList,
                                                  label);
  pass->_priv = native;
  return pass;
}

static void
dx12_bindRayTracingPipeline(GPURayTracingPassEncoderEXT *pass,
                            GPURayTracingPipelineEXT    *pipeline) {
  GPURayTracingEncoderDX12  *native;
  GPURayTracingPipelineDX12 *pipelineDX12;

  native       = pass ? pass->_priv : NULL;
  pipelineDX12 = pipeline ? pipeline->_priv : NULL;
  if (!native || !native->commandList || !native->commandList5 ||
      !pipelineDX12 || !pipelineDX12->stateObject ||
      !pipelineDX12->rootSignature) {
    return;
  }

  native->commandList5->lpVtbl->SetPipelineState1(native->commandList5,
                                                   pipelineDX12->stateObject);
  native->commandList->lpVtbl->SetComputeRootSignature(
    native->commandList,
    pipelineDX12->rootSignature
  );
  native->rootSignature = pipelineDX12->rootSignature;
}

static void
dx12_dispatchRays(GPURayTracingPassEncoderEXT *pass,
                  GPUShaderTableEXT           *table,
                  uint32_t                     width,
                  uint32_t                     height,
                  uint32_t                     depth) {
  GPURayTracingEncoderDX12 *native;
  GPUShaderTableDX12       *tableDX12;
  D3D12_DISPATCH_RAYS_DESC  desc = {0};

  native    = pass ? pass->_priv : NULL;
  tableDX12 = table ? table->_priv : NULL;
  if (!native || !native->commandList5 || !tableDX12) {
    return;
  }
  desc.RayGenerationShaderRecord = tableDX12->rayGeneration;
  desc.MissShaderTable            = tableDX12->miss;
  desc.HitGroupTable              = tableDX12->hit;
  desc.CallableShaderTable        = tableDX12->callable;
  desc.Width                      = width;
  desc.Height                     = height;
  desc.Depth                      = depth;
  native->commandList5->lpVtbl->DispatchRays(native->commandList5, &desc);
}

static void
dx12_endRayTracingPass(GPURayTracingPassEncoderEXT *pass) {
  GPURayTracingEncoderDX12 *native;

  native = pass ? pass->_priv : NULL;
  if (!native) {
    return;
  }
  if (native->debugEventActive) {
    dx12_endDebugEvent(pass->device, native->commandList);
  }
  native->device           = NULL;
  native->commandList      = NULL;
  native->commandList5     = NULL;
  native->rootSignature    = NULL;
  native->resourceHeap     = NULL;
  native->samplerHeap      = NULL;
  native->debugEventActive = false;
}

GPU_HIDE
void
dx12_initRayTracing(GPUApiRayTracing *api) {
  api->createPipeline     = dx12_createRayTracingPipeline;
  api->destroyPipeline    = dx12_destroyRayTracingPipeline;
  api->createShaderTable  = dx12_createShaderTable;
  api->destroyShaderTable = dx12_destroyShaderTable;
  api->beginPass          = dx12_beginRayTracingPass;
  api->bindPipeline       = dx12_bindRayTracingPipeline;
  api->bindGroup          = dx12_bindRayTracingGroup;
  api->dispatch           = dx12_dispatchRays;
  api->endPass            = dx12_endRayTracingPass;
}
