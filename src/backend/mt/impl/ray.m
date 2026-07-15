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

static GPUAccelerationStructureMT *
mt_rayStructure(GPUAccelerationStructureEXT *structure) {
  return structure ? structure->_priv : NULL;
}

static MTRayQueryEncoder *
mt_rayEncoder(GPUAccelerationStructurePassEncoderEXT *pass) {
  return pass ? pass->_priv : NULL;
}

static id<MTLBuffer>
mt_rayBuffer(GPUBuffer *buffer) {
  return buffer ? (id<MTLBuffer>)buffer->_priv : nil;
}

static id<MTLAccelerationStructure>
mt_rayNativeStructure(GPUAccelerationStructureEXT *structure) {
  GPUAccelerationStructureMT *native;

  native = mt_rayStructure(structure);
  return native ? native->structure : nil;
}

static MTLAccelerationStructureUsage
mt_rayUsage(GPUAccelerationStructureBuildFlagsEXT flags) {
  MTLAccelerationStructureUsage usage;

  usage = MTLAccelerationStructureUsageNone;
  if ((flags & GPU_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_EXT) != 0u) {
    usage |= MTLAccelerationStructureUsageRefit;
  }
  if ((flags & GPU_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_EXT) != 0u) {
    usage |= MTLAccelerationStructureUsagePreferFastBuild;
  }
#if defined(__MAC_26_0) && defined(__IPHONE_26_0)
  if ((flags & GPU_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_EXT) != 0u) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      usage |= MTLAccelerationStructureUsagePreferFastIntersection;
    }
  }
#endif
  return usage;
}

static MTLAccelerationStructureInstanceOptions
mt_rayInstanceOptions(GPUAccelerationStructureInstanceFlagsEXT flags) {
  MTLAccelerationStructureInstanceOptions options;

  options = MTLAccelerationStructureInstanceOptionTriangleFrontFacingWindingCounterClockwise;
  if ((flags & GPU_ACCELERATION_STRUCTURE_INSTANCE_DISABLE_CULL_BIT_EXT) != 0u) {
    options |= MTLAccelerationStructureInstanceOptionDisableTriangleCulling;
  }
  if ((flags & GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_OPAQUE_BIT_EXT) != 0u) {
    options |= MTLAccelerationStructureInstanceOptionOpaque;
  }
  if ((flags &
       GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_NON_OPAQUE_BIT_EXT) != 0u) {
    options |= MTLAccelerationStructureInstanceOptionNonOpaque;
  }
  return options;
}

static void
mt_rayTransform(MTLPackedFloat4x3 *dst, const float src[3][4]) {
  for (uint32_t column = 0u; column < 4u; column++) {
    for (uint32_t row = 0u; row < 3u; row++) {
      dst->columns[column].elements[row] = src[row][column];
    }
  }
}

static MTLAccelerationStructureTriangleGeometryDescriptor *
mt_rayClassicTriangle(
  const GPUAccelerationStructureTriangleGeometryEXT *geometry) {
  MTLAccelerationStructureTriangleGeometryDescriptor *descriptor;

  descriptor = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
  descriptor.vertexBuffer       = mt_rayBuffer(geometry->vertexBuffer);
  descriptor.vertexBufferOffset = (NSUInteger)geometry->vertexOffset;
  descriptor.vertexStride       = geometry->vertexStride;
  descriptor.vertexFormat       = MTLAttributeFormatFloat3;
  descriptor.opaque =
    (geometry->flags &
     GPU_ACCELERATION_STRUCTURE_GEOMETRY_NON_OPAQUE_BIT_EXT) == 0u;
  if (geometry->indexBuffer) {
    descriptor.indexBuffer       = mt_rayBuffer(geometry->indexBuffer);
    descriptor.indexBufferOffset = (NSUInteger)geometry->indexOffset;
    descriptor.indexType         = geometry->indexType == GPU_INDEX_TYPE_UINT32
                                     ? MTLIndexTypeUInt32
                                     : MTLIndexTypeUInt16;
    descriptor.triangleCount     = geometry->indexCount / 3u;
  } else {
    descriptor.indexBuffer   = nil;
    descriptor.triangleCount = geometry->vertexCount / 3u;
  }
  return descriptor;
}

static MTLAccelerationStructureDescriptor *
mt_rayClassicDescriptor(const GPUAccelerationStructureBuildInfoEXT *info) {
  if (info->type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT) {
    MTLPrimitiveAccelerationStructureDescriptor *descriptor;
    NSMutableArray                              *geometries;

    geometries = [NSMutableArray
      arrayWithCapacity:info->bottomLevel.geometryCount];
    for (uint32_t i = 0u; i < info->bottomLevel.geometryCount; i++) {
      [geometries addObject:
        mt_rayClassicTriangle(&info->bottomLevel.pGeometries[i])];
    }
    descriptor = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
    descriptor.geometryDescriptors = geometries;
    descriptor.usage = mt_rayUsage(info->flags);
    return descriptor;
  }

  {
    MTLInstanceAccelerationStructureDescriptor *descriptor;
    NSMutableArray                             *instances;

    instances = [NSMutableArray
      arrayWithCapacity:info->topLevel.instanceCount];
    for (uint32_t i = 0u; i < info->topLevel.instanceCount; i++) {
      [instances addObject:
        mt_rayNativeStructure(info->topLevel.pInstances[i].structure)];
    }
    descriptor = [MTLInstanceAccelerationStructureDescriptor descriptor];
    descriptor.instancedAccelerationStructures = instances;
    descriptor.instanceCount                   = info->topLevel.instanceCount;
    descriptor.instanceDescriptorType =
      MTLAccelerationStructureInstanceDescriptorTypeDefault;
    descriptor.usage = mt_rayUsage(info->flags);
    return descriptor;
  }
}

static bool
mt_rayEnsureInstanceBuffer(GPUDeviceMT                 *device,
                           GPUAccelerationStructureMT  *native,
                           uint64_t                     sizeBytes,
                           const char                  *label) {
  id<MTLBuffer> buffer;
  uint64_t      capacity;

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
  if (capacity > NSUIntegerMax) {
    return false;
  }

  buffer = [device->device newBufferWithLength:(NSUInteger)capacity
                                       options:MTLResourceStorageModeShared];
  if (!buffer) {
    return false;
  }
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (label && label[0] != '\0') {
    buffer.label = [NSString stringWithUTF8String:label];
  }
#else
  GPU__UNUSED(label);
#endif
  [native->instanceBuffer release];
  native->instanceBuffer   = buffer;
  native->instanceCapacity = capacity;
  return true;
}

static bool
mt_rayPrepareClassicBLAS(
  GPUAccelerationStructureMT                 *native,
  const GPUAccelerationStructureBuildInfoEXT *info) {
  MTLPrimitiveAccelerationStructureDescriptor *descriptor;

  if (!native->classicGeometry) {
    native->classicGeometry = [[NSMutableArray alloc]
      initWithCapacity:info->bottomLevel.geometryCount];
  }
  while (native->classicGeometry.count < info->bottomLevel.geometryCount) {
    [native->classicGeometry addObject:
      [MTLAccelerationStructureTriangleGeometryDescriptor descriptor]];
  }
  while (native->classicGeometry.count > info->bottomLevel.geometryCount) {
    [native->classicGeometry removeLastObject];
  }
  for (uint32_t i = 0u; i < info->bottomLevel.geometryCount; i++) {
    MTLAccelerationStructureTriangleGeometryDescriptor *geometry;
    const GPUAccelerationStructureTriangleGeometryEXT  *source;

    source   = &info->bottomLevel.pGeometries[i];
    geometry = native->classicGeometry[i];
    geometry.vertexBuffer       = mt_rayBuffer(source->vertexBuffer);
    geometry.vertexBufferOffset = (NSUInteger)source->vertexOffset;
    geometry.vertexStride       = source->vertexStride;
    geometry.vertexFormat       = MTLAttributeFormatFloat3;
    geometry.opaque =
      (source->flags &
       GPU_ACCELERATION_STRUCTURE_GEOMETRY_NON_OPAQUE_BIT_EXT) == 0u;
    if (source->indexBuffer) {
      geometry.indexBuffer       = mt_rayBuffer(source->indexBuffer);
      geometry.indexBufferOffset = (NSUInteger)source->indexOffset;
      geometry.indexType         = source->indexType == GPU_INDEX_TYPE_UINT32
                                     ? MTLIndexTypeUInt32
                                     : MTLIndexTypeUInt16;
      geometry.triangleCount     = source->indexCount / 3u;
    } else {
      geometry.indexBuffer       = nil;
      geometry.indexBufferOffset = 0u;
      geometry.triangleCount     = source->vertexCount / 3u;
    }
  }

  if (!native->classicDescriptor) {
    native->classicDescriptor =
      [[MTLPrimitiveAccelerationStructureDescriptor descriptor] retain];
  }
  descriptor = (MTLPrimitiveAccelerationStructureDescriptor *)
    native->classicDescriptor;
  descriptor.geometryDescriptors = native->classicGeometry;
  descriptor.usage               = mt_rayUsage(info->flags);
  return true;
}

static bool
mt_rayPrepareClassicTLAS(
  GPUDeviceMT                                  *device,
  GPUAccelerationStructureMT                  *native,
  const GPUAccelerationStructureBuildInfoEXT  *info) {
  MTLInstanceAccelerationStructureDescriptor *descriptor;
  MTLAccelerationStructureInstanceDescriptor *instances;
  uint64_t                                    sizeBytes;

  sizeBytes = (uint64_t)info->topLevel.instanceCount * sizeof(*instances);
  if (!mt_rayEnsureInstanceBuffer(device,
                                  native,
                                  sizeBytes,
                                  "gpu-ray-instance-buffer")) {
    return false;
  }
  if (!native->classicInstances) {
    native->classicInstances = [[NSMutableArray alloc]
      initWithCapacity:info->topLevel.instanceCount];
  }
  while (native->classicInstances.count < info->topLevel.instanceCount) {
    [native->classicInstances addObject:[NSNull null]];
  }
  while (native->classicInstances.count > info->topLevel.instanceCount) {
    [native->classicInstances removeLastObject];
  }

  instances = (MTLAccelerationStructureInstanceDescriptor *)
    native->instanceBuffer.contents;
  for (uint32_t i = 0u; i < info->topLevel.instanceCount; i++) {
    const GPUAccelerationStructureInstanceEXT *source;

    source = &info->topLevel.pInstances[i];
    mt_rayTransform(&instances[i].transformationMatrix, source->transform);
    instances[i].options = mt_rayInstanceOptions(source->flags);
    instances[i].mask = source->mask ? source->mask : 0xffu;
    instances[i].intersectionFunctionTableOffset = 0u;
    instances[i].accelerationStructureIndex       = i;
    native->classicInstances[i] = mt_rayNativeStructure(source->structure);
  }

  if (!native->classicDescriptor) {
    native->classicDescriptor =
      [[MTLInstanceAccelerationStructureDescriptor descriptor] retain];
  }
  descriptor = (MTLInstanceAccelerationStructureDescriptor *)
    native->classicDescriptor;
  descriptor.instanceDescriptorBuffer       = native->instanceBuffer;
  descriptor.instanceDescriptorBufferOffset = 0u;
  descriptor.instanceDescriptorStride       = sizeof(*instances);
  descriptor.instanceCount                  = info->topLevel.instanceCount;
  descriptor.instancedAccelerationStructures = native->classicInstances;
  descriptor.instanceDescriptorType =
    MTLAccelerationStructureInstanceDescriptorTypeDefault;
  descriptor.usage = mt_rayUsage(info->flags);
  return true;
}

#if MT_HAS_METAL4
static MTL4BufferRange
mt_rayRange(GPUBuffer *buffer, uint64_t offset) {
  MTL4BufferRange range;

  range.bufferAddress = buffer ? buffer->_gpuAddress + offset : 0u;
  range.length = buffer && offset <= buffer->sizeBytes
                   ? buffer->sizeBytes - offset
                   : 0u;
  return range;
}

static bool
mt_rayPrepareModernBLAS(
  GPUAccelerationStructureMT                 *native,
  const GPUAccelerationStructureBuildInfoEXT *info) {
  MTL4PrimitiveAccelerationStructureDescriptor *descriptor;

  if (!native->modernGeometry) {
    native->modernGeometry = [[NSMutableArray alloc]
      initWithCapacity:info->bottomLevel.geometryCount];
  }
  while (native->modernGeometry.count < info->bottomLevel.geometryCount) {
    MTL4AccelerationStructureTriangleGeometryDescriptor *geometry;

    geometry = [MTL4AccelerationStructureTriangleGeometryDescriptor new];
    [native->modernGeometry addObject:geometry];
    [geometry release];
  }
  while (native->modernGeometry.count > info->bottomLevel.geometryCount) {
    [native->modernGeometry removeLastObject];
  }
  for (uint32_t i = 0u; i < info->bottomLevel.geometryCount; i++) {
    MTL4AccelerationStructureTriangleGeometryDescriptor *geometry;
    const GPUAccelerationStructureTriangleGeometryEXT   *source;

    source   = &info->bottomLevel.pGeometries[i];
    geometry = native->modernGeometry[i];
    geometry.vertexBuffer = mt_rayRange(source->vertexBuffer,
                                        source->vertexOffset);
    geometry.vertexStride = source->vertexStride;
    geometry.vertexFormat = MTLAttributeFormatFloat3;
    geometry.opaque =
      (source->flags &
       GPU_ACCELERATION_STRUCTURE_GEOMETRY_NON_OPAQUE_BIT_EXT) == 0u;
    if (source->indexBuffer) {
      geometry.indexBuffer = mt_rayRange(source->indexBuffer,
                                         source->indexOffset);
      geometry.indexType = source->indexType == GPU_INDEX_TYPE_UINT32
                             ? MTLIndexTypeUInt32
                             : MTLIndexTypeUInt16;
      geometry.triangleCount = source->indexCount / 3u;
    } else {
      geometry.indexBuffer   = (MTL4BufferRange){0};
      geometry.triangleCount = source->vertexCount / 3u;
    }
  }
  if (!native->modernDescriptor) {
    native->modernDescriptor =
      [MTL4PrimitiveAccelerationStructureDescriptor new];
  }
  descriptor = native->modernDescriptor;
  descriptor.geometryDescriptors = native->modernGeometry;
  descriptor.usage               = mt_rayUsage(info->flags);
  return true;
}

static bool
mt_rayPrepareModernTLAS(
  GPUDeviceMT                                  *device,
  GPUAccelerationStructureMT                  *native,
  const GPUAccelerationStructureBuildInfoEXT  *info) {
  MTL4InstanceAccelerationStructureDescriptor      *descriptor;
  MTLIndirectAccelerationStructureInstanceDescriptor *instances;
  uint64_t                                           sizeBytes;

  sizeBytes = (uint64_t)info->topLevel.instanceCount * sizeof(*instances);
  if (!mt_rayEnsureInstanceBuffer(device,
                                  native,
                                  sizeBytes,
                                  "gpu-ray-indirect-instance-buffer")) {
    return false;
  }
  instances = (MTLIndirectAccelerationStructureInstanceDescriptor *)
    native->instanceBuffer.contents;
  for (uint32_t i = 0u; i < info->topLevel.instanceCount; i++) {
    const GPUAccelerationStructureInstanceEXT *source;
    id<MTLAccelerationStructure>               structure;

    source    = &info->topLevel.pInstances[i];
    structure = mt_rayNativeStructure(source->structure);
    mt_rayTransform(&instances[i].transformationMatrix, source->transform);
    instances[i].options = mt_rayInstanceOptions(source->flags);
    instances[i].mask = source->mask ? source->mask : 0xffu;
    instances[i].intersectionFunctionTableOffset = 0u;
    instances[i].userID = i;
    instances[i].accelerationStructureID = structure.gpuResourceID;
  }

  if (!native->modernDescriptor) {
    native->modernDescriptor =
      [MTL4InstanceAccelerationStructureDescriptor new];
  }
  descriptor = native->modernDescriptor;
  descriptor.instanceDescriptorBuffer = (MTL4BufferRange){
    .bufferAddress = native->instanceBuffer.gpuAddress,
    .length        = native->instanceCapacity
  };
  descriptor.instanceDescriptorStride = sizeof(*instances);
  descriptor.instanceCount            = info->topLevel.instanceCount;
  descriptor.instanceDescriptorType =
    MTLAccelerationStructureInstanceDescriptorTypeIndirect;
  descriptor.usage = mt_rayUsage(info->flags);
  return true;
}
#endif

GPU_HIDE
GPUResult
mt_getAccelerationStructureSizes(
  GPUDevice                                    *device,
  const GPUAccelerationStructureBuildInfoEXT  *info,
  GPUAccelerationStructureSizesEXT            *outSizes) {
  GPUDeviceMT                       *deviceMT;
  MTLAccelerationStructureDescriptor *descriptor;
  MTLAccelerationStructureSizes      sizes;

  deviceMT   = device->_priv;
  descriptor = mt_rayClassicDescriptor(info);
  if (!deviceMT || !descriptor) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  sizes = [deviceMT->device
    accelerationStructureSizesWithDescriptor:descriptor];
  outSizes->accelerationStructureSize = sizes.accelerationStructureSize;
  outSizes->buildScratchSize          = sizes.buildScratchBufferSize;
  outSizes->updateScratchSize         = sizes.refitScratchBufferSize;
  return outSizes->accelerationStructureSize > 0u
           ? GPU_OK
           : GPU_ERROR_BACKEND_FAILURE;
}

GPU_HIDE
GPUResult
mt_createAccelerationStructure(
  GPUDevice                                    *device,
  const GPUAccelerationStructureCreateInfoEXT *info,
  GPUAccelerationStructureEXT                 *structure) {
  GPUAccelerationStructureMT *native;
  GPUDeviceMT                  *deviceMT;

  deviceMT = device->_priv;
  if (!deviceMT || info->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->structure = [deviceMT->device
    newAccelerationStructureWithSize:(NSUInteger)info->sizeBytes];
  if (!native->structure) {
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (info->label && info->label[0] != '\0') {
    native->structure.label = [NSString stringWithUTF8String:info->label];
  }
#endif
  structure->_priv = native;
  return GPU_OK;
}

GPU_HIDE
void
mt_destroyAccelerationStructure(GPUAccelerationStructureEXT *structure) {
  GPUAccelerationStructureMT *native;

  native = mt_rayStructure(structure);
  if (!native) {
    return;
  }
  [native->modernDescriptor release];
  [native->classicDescriptor release];
  [native->classicInstances release];
  [native->modernGeometry release];
  [native->classicGeometry release];
  [native->instanceBuffer release];
  [native->structure release];
  free(native);
  structure->_priv = NULL;
}

GPU_HIDE
GPUAccelerationStructurePassEncoderEXT *
mt_beginAccelerationStructurePass(GPUCommandBuffer *cmdb, const char *label) {
  MTCommandBuffer                           *command;
  MTRayQueryEncoder                        *native;
  GPUAccelerationStructurePassEncoderEXT   *pass;

  command = mt_commandBuffer(cmdb);
  if (!command) {
    return NULL;
  }
  pass   = &command->rayQueryEncoder;
  native = &command->rayQueryState;
  memset(pass, 0, sizeof(*pass));
  memset(native, 0, sizeof(*native));

#if MT_HAS_METAL4
  if (command->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      native->modern = [command->modern computeCommandEncoder];
      mt_applyPendingBarrier(cmdb, native->modern);
    }
  } else
#endif
  {
    native->classic = [command->classic accelerationStructureCommandEncoder];
  }
  if (!native->classic && !native->modern) {
    return NULL;
  }
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (label && label[0] != '\0') {
    NSString *nativeLabel;

    nativeLabel = [NSString stringWithUTF8String:label];
    native->classic.label = nativeLabel;
#if MT_HAS_METAL4
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [(id<MTL4ComputeCommandEncoder>)native->modern setLabel:nativeLabel];
    }
#endif
  }
#else
  GPU__UNUSED(label);
#endif
  pass->_priv = native;
  return pass;
}

static void
mt_rayUseBuildResources(
  GPUAccelerationStructurePassEncoderEXT     *pass,
  GPUAccelerationStructureEXT                *dst,
  const GPUAccelerationStructureBuildInfoEXT *info,
  GPUBuffer                                   *scratchBuffer) {
  mt_useAllocation(pass->cmdb, mt_rayBuffer(scratchBuffer));
  mt_useAllocation(pass->cmdb, mt_rayNativeStructure(dst));
  if (info->source) {
    mt_useAllocation(pass->cmdb, mt_rayNativeStructure(info->source));
  }
  if (info->type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT) {
    for (uint32_t i = 0u; i < info->bottomLevel.geometryCount; i++) {
      mt_useAllocation(pass->cmdb,
                       mt_rayBuffer(info->bottomLevel.pGeometries[i].vertexBuffer));
      mt_useAllocation(pass->cmdb,
                       mt_rayBuffer(info->bottomLevel.pGeometries[i].indexBuffer));
    }
  } else {
    GPUAccelerationStructureMT *native;

    native = mt_rayStructure(dst);
    mt_useAllocation(pass->cmdb, native ? native->instanceBuffer : nil);
    for (uint32_t i = 0u; i < info->topLevel.instanceCount; i++) {
      mt_useAllocation(
        pass->cmdb,
        mt_rayNativeStructure(info->topLevel.pInstances[i].structure));
    }
  }
}

GPU_HIDE
GPUResult
mt_buildAccelerationStructure(
  GPUAccelerationStructurePassEncoderEXT     *pass,
  GPUAccelerationStructureEXT                *dst,
  const GPUAccelerationStructureBuildInfoEXT *info,
  GPUBuffer                                   *scratchBuffer,
  uint64_t                                     scratchOffset) {
  GPUAccelerationStructureMT *native;
  GPUDeviceMT                  *device;
  MTRayQueryEncoder            *encoder;

  native  = mt_rayStructure(dst);
  device  = pass && pass->device ? pass->device->_priv : NULL;
  encoder = mt_rayEncoder(pass);
  if (!native || !device || !encoder) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

#if MT_HAS_METAL4
  if (encoder->modern) {
    MTL4AccelerationStructureDescriptor *descriptor;
    MTL4BufferRange                      scratch;

    if (@available(macOS 26.0, iOS 26.0, *)) {
      bool prepared;

      prepared = info->type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT
                   ? mt_rayPrepareModernBLAS(native, info)
                   : mt_rayPrepareModernTLAS(device, native, info);
      if (!prepared) {
        return GPU_ERROR_OUT_OF_MEMORY;
      }
      descriptor = native->modernDescriptor;
      scratch    = mt_rayRange(scratchBuffer, scratchOffset);
      mt_rayUseBuildResources(pass, dst, info, scratchBuffer);
      if (info->mode == GPU_ACCELERATION_STRUCTURE_UPDATE_EXT) {
        [(id<MTL4ComputeCommandEncoder>)encoder->modern
          refitAccelerationStructure:mt_rayNativeStructure(info->source)
                              descriptor:descriptor
                             destination:native->structure
                           scratchBuffer:scratch];
      } else {
        [(id<MTL4ComputeCommandEncoder>)encoder->modern
          buildAccelerationStructure:native->structure
                          descriptor:descriptor
                       scratchBuffer:scratch];
      }
      return GPU_OK;
    }
  }
#endif

  {
    bool prepared;

    prepared = info->type == GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT
                 ? mt_rayPrepareClassicBLAS(native, info)
                 : mt_rayPrepareClassicTLAS(device, native, info);
    if (!prepared) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    if (info->mode == GPU_ACCELERATION_STRUCTURE_UPDATE_EXT) {
      [encoder->classic
        refitAccelerationStructure:mt_rayNativeStructure(info->source)
                          descriptor:native->classicDescriptor
                         destination:native->structure
                       scratchBuffer:mt_rayBuffer(scratchBuffer)
                 scratchBufferOffset:(NSUInteger)scratchOffset];
    } else {
      [encoder->classic
        buildAccelerationStructure:native->structure
                        descriptor:native->classicDescriptor
                     scratchBuffer:mt_rayBuffer(scratchBuffer)
               scratchBufferOffset:(NSUInteger)scratchOffset];
    }
  }
  return GPU_OK;
}

GPU_HIDE
void
mt_endAccelerationStructurePass(
  GPUAccelerationStructurePassEncoderEXT *pass) {
  MTRayQueryEncoder *native;

  native = mt_rayEncoder(pass);
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [(id<MTL4ComputeCommandEncoder>)native->modern endEncoding];
    }
  } else
#endif
  {
    [native->classic endEncoding];
  }
  native->classic = nil;
  native->modern  = nil;
}

GPU_HIDE
void
mt_initRayQuery(GPUApiRayQuery *api) {
  api->getSizes  = mt_getAccelerationStructureSizes;
  api->create    = mt_createAccelerationStructure;
  api->destroy   = mt_destroyAccelerationStructure;
  api->beginPass = mt_beginAccelerationStructurePass;
  api->build     = mt_buildAccelerationStructure;
  api->endPass   = mt_endAccelerationStructurePass;
}
