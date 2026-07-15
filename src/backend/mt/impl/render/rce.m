/*
 * Copyright (C) 2020 Recep Aslantas
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

#include "../../common.h"
#include "../../../../api/render/pipeline_internal.h"

static MTRenderEncoder *
mt_renderEncoder(GPURenderCommandEncoder *rce) {
  return rce ? rce->_priv : NULL;
}

static id<MTLBuffer>
mt_nativeBuffer(GPUBuffer *buffer) {
  return buffer ? (id<MTLBuffer>)buffer->_priv : nil;
}

GPU_HIDE
GPURenderCommandEncoder *
mt_renderCommandEncoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass) {
  MTCommandBuffer         *commandState;
  GPURenderCommandEncoder *enc;
  MTRenderEncoder         *nativeState;
  MTRenderPass            *nativePass;

  if (!cmdb || !pass || !pass->_priv) {
    return NULL;
  }

  nativePass = pass->_priv;
  commandState = mt_commandBuffer(cmdb);
  if (!commandState) {
    return NULL;
  }
  enc = &commandState->renderEncoder;
  nativeState = &commandState->renderState;
  memset(enc, 0, sizeof(*enc));
  memset(nativeState, 0, sizeof(*nativeState));

#if MT_HAS_METAL4
  if (mt_commandBufferIsModern(cmdb)) {
    if (!nativePass->modern ||
        !mt_prepareArgumentState(cmdb,
                                 &commandState->vertexArguments,
                                 gpuDeviceDebugLabel(
                                   gpuCommandBufferDevice(cmdb),
                                   "gpu-metal4-vertex-arguments")) ||
        !mt_prepareArgumentState(cmdb,
                                 &commandState->fragmentArguments,
                                 gpuDeviceDebugLabel(
                                   gpuCommandBufferDevice(cmdb),
                                   "gpu-metal4-fragment-arguments"))) {
      return NULL;
    }

    if (@available(macOS 26.0, iOS 26.0, *)) {
      nativeState->modern = [commandState->modern
        renderCommandEncoderWithDescriptor:nativePass->modern];
      mt_applyPendingBarrier(cmdb, nativeState->modern);
      nativeState->vertexArguments = &commandState->vertexArguments;
      nativeState->fragmentArguments = &commandState->fragmentArguments;
      [nativeState->modern setArgumentTable:nativeState->vertexArguments->table
                                   atStages:MTLRenderStageVertex];
      [nativeState->modern setArgumentTable:nativeState->fragmentArguments->table
                                   atStages:MTLRenderStageFragment];
    }
  } else
#endif
  {
    nativeState->classic = [mt_classicCommandBuffer(cmdb)
      renderCommandEncoderWithDescriptor:nativePass->classic];
  }

  if (!nativeState->classic && !nativeState->modern) {
    return NULL;
  }
  nativeState->width  = nativePass->width;
  nativeState->height = nativePass->height;
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (gpuDeviceDebugMarkersEnabled(gpuCommandBufferDevice(cmdb)) &&
      pass->label && pass->label[0] != '\0') {
    NSString *label = [NSString stringWithUTF8String:pass->label];

    nativeState->classic.label = label;
#if MT_HAS_METAL4
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [(id<MTL4RenderCommandEncoder>)nativeState->modern setLabel:label];
    }
#endif
  }
#endif

  enc->_priv = nativeState;
  enc->_primitiveType = GPUPrimitiveTypeTriangle;
  return enc;
}

GPU_HIDE
void
mt_frontFace(GPURenderCommandEncoder *rce, GPUFrontFace frontFace) {
  MTRenderEncoder *native;
  MTLWinding       mtWinding;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  mtWinding = frontFace == GPU_FRONT_FACE_CW ?
    MTLWindingClockwise : MTLWindingCounterClockwise;
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern setFrontFacingWinding:mtWinding];
    }
    return;
  }
#endif
  [native->classic setFrontFacingWinding:mtWinding];
}

GPU_HIDE
void
mt_cullMode(GPURenderCommandEncoder *rce, GPUCullMode mode) {
  MTRenderEncoder *native;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern setCullMode:(MTLCullMode)mode];
    }
    return;
  }
#endif
  [native->classic setCullMode:(MTLCullMode)mode];
}

GPU_HIDE
void
mt_setRenderPipelineState(GPURenderCommandEncoder *rce,
                          GPURenderPipelineState  *pipelineState) {
  MTRenderEncoder       *native;
  MTRenderPipelineState *state;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  state = pipelineState ? pipelineState->_priv : NULL;
  if (!state || !state->render || !state->depthStencil) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      if (state->mesh && !native->meshArguments) {
        MTCommandBuffer *commandState = mt_commandBuffer(rce->_cmdb);

        if (!mt_prepareArgumentState(
              rce->_cmdb,
              &commandState->meshArguments,
              gpuDeviceDebugLabel(gpuCommandBufferDevice(rce->_cmdb),
                                  "gpu-metal4-mesh-arguments"))) {
          return;
        }
        native->meshArguments = &commandState->meshArguments;
        [native->modern setArgumentTable:native->meshArguments->table
                                 atStages:MTLRenderStageMesh];
      }
      if (state->task && !native->taskArguments) {
        MTCommandBuffer *commandState = mt_commandBuffer(rce->_cmdb);

        if (!mt_prepareArgumentState(
              rce->_cmdb,
              &commandState->taskArguments,
              gpuDeviceDebugLabel(gpuCommandBufferDevice(rce->_cmdb),
                                  "gpu-metal4-task-arguments"))) {
          return;
        }
        native->taskArguments = &commandState->taskArguments;
        [native->modern setArgumentTable:native->taskArguments->table
                                 atStages:MTLRenderStageObject];
      }
      [native->modern setRenderPipelineState:state->render];
      [native->modern setDepthStencilState:state->depthStencil];
    }
    return;
  }
#endif
  [native->classic setRenderPipelineState:state->render];
  [native->classic setDepthStencilState:state->depthStencil];
}

GPU_HIDE
void
mt_viewport(GPURenderCommandEncoder *rce, const GPUViewport *viewport) {
  MTRenderEncoder *native;
  MTLViewport      vp;

  vp.originX = viewport->x;
  vp.originY = viewport->y;
  vp.width   = viewport->width;
  vp.height  = viewport->height;
  vp.znear   = viewport->minDepth;
  vp.zfar    = viewport->maxDepth;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern setViewport:vp];
    }
    return;
  }
#endif
  [native->classic setViewport:vp];
}

static void
mt_scissorAxis(int32_t     origin,
               uint32_t    extent,
               uint32_t    limit,
               NSUInteger *outOrigin,
               NSUInteger *outExtent) {
  uint64_t clipped;

  if (origin < 0) {
    clipped = (uint64_t)-(int64_t)origin;
    extent  = clipped >= extent ? 0u : extent - (uint32_t)clipped;
    origin  = 0;
  }

  *outOrigin = (NSUInteger)origin;
  if ((uint32_t)origin >= limit) {
    *outOrigin = limit;
    *outExtent = 0u;
    return;
  }

  *outExtent = extent > limit - (uint32_t)origin
                 ? limit - (uint32_t)origin
                 : extent;
}

GPU_HIDE
void
mt_scissor(GPURenderCommandEncoder *rce, const GPUScissorRect *scissor) {
  MTRenderEncoder *native;
  MTLScissorRect   rect;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  mt_scissorAxis(scissor->x,
                 scissor->width,
                 native->width,
                 &rect.x,
                 &rect.width);
  mt_scissorAxis(scissor->y,
                 scissor->height,
                 native->height,
                 &rect.y,
                 &rect.height);
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern setScissorRect:rect];
    }
    return;
  }
#endif
  [native->classic setScissorRect:rect];
}

GPU_HIDE
void
mt_blendConstant(GPURenderCommandEncoder *rce, const float rgba[4]) {
  MTRenderEncoder *native;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern setBlendColorRed:rgba[0]
                                 green:rgba[1]
                                  blue:rgba[2]
                                 alpha:rgba[3]];
    }
    return;
  }
#endif
  [native->classic setBlendColorRed:rgba[0]
                              green:rgba[1]
                               blue:rgba[2]
                              alpha:rgba[3]];
}

GPU_HIDE
void
mt_stencilReference(GPURenderCommandEncoder *rce, uint32_t reference) {
  MTRenderEncoder *native;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern setStencilReferenceValue:reference];
    }
    return;
  }
#endif
  [native->classic setStencilReferenceValue:reference];
}

GPU_HIDE
void
mt_renderPushConstants(GPURenderCommandEncoder *rce,
                       GPUShaderStageFlags       stages,
                       const void               *data,
                       uint32_t                  sizeBytes) {
  MTRenderEncoder *native;

  if (!rce || !data || sizeBytes == 0u) {
    return;
  }

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    uint64_t address;

    if (!mt_uploadConstants(rce->_cmdb, data, sizeBytes, &address)) {
      return;
    }
    if ((stages & GPU_SHADER_STAGE_VERTEX_BIT) != 0u) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        [(id<MTL4ArgumentTable>)native->vertexArguments->table
          setAddress:address
             atIndex:MT_PUSH_CONSTANT_INDEX];
      }
      native->vertexArguments->bufferMask |= 1u << MT_PUSH_CONSTANT_INDEX;
    }
    if ((stages & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0u) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        [(id<MTL4ArgumentTable>)native->fragmentArguments->table
          setAddress:address
             atIndex:MT_PUSH_CONSTANT_INDEX];
      }
      native->fragmentArguments->bufferMask |= 1u << MT_PUSH_CONSTANT_INDEX;
    }
    if ((stages & GPU_SHADER_STAGE_TASK_BIT) != 0u && native->taskArguments) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        [(id<MTL4ArgumentTable>)native->taskArguments->table
          setAddress:address
             atIndex:MT_PUSH_CONSTANT_INDEX];
      }
      native->taskArguments->bufferMask |= 1u << MT_PUSH_CONSTANT_INDEX;
    }
    if ((stages & GPU_SHADER_STAGE_MESH_BIT) != 0u && native->meshArguments) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        [(id<MTL4ArgumentTable>)native->meshArguments->table
          setAddress:address
             atIndex:MT_PUSH_CONSTANT_INDEX];
      }
      native->meshArguments->bufferMask |= 1u << MT_PUSH_CONSTANT_INDEX;
    }
    return;
  }
#endif

  if ((stages & GPU_SHADER_STAGE_VERTEX_BIT) != 0u) {
    [native->classic setVertexBytes:data
                             length:(NSUInteger)sizeBytes
                            atIndex:MT_PUSH_CONSTANT_INDEX];
  }
  if ((stages & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0u) {
    [native->classic setFragmentBytes:data
                               length:(NSUInteger)sizeBytes
                              atIndex:MT_PUSH_CONSTANT_INDEX];
  }
  if (@available(macOS 13.0, iOS 16.0, *)) {
    if ((stages & GPU_SHADER_STAGE_TASK_BIT) != 0u) {
      [native->classic setObjectBytes:data
                              length:(NSUInteger)sizeBytes
                             atIndex:MT_PUSH_CONSTANT_INDEX];
    }
    if ((stages & GPU_SHADER_STAGE_MESH_BIT) != 0u) {
      [native->classic setMeshBytes:data
                            length:(NSUInteger)sizeBytes
                           atIndex:MT_PUSH_CONSTANT_INDEX];
    }
  }
}

GPU_HIDE
void
mt_vertexBytes(GPURenderCommandEncoder *rce,
               void                    *bytes,
               size_t                   length,
               uint32_t                 index) {
  MTRenderEncoder *native;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    uint64_t address;

    if (length <= UINT32_MAX &&
        mt_uploadConstants(rce->_cmdb, bytes, (uint32_t)length, &address) &&
        index < MT_ARGUMENT_BUFFER_COUNT) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        [(id<MTL4ArgumentTable>)native->vertexArguments->table
          setAddress:address
             atIndex:index];
      }
      native->vertexArguments->bufferMask |= 1u << index;
    }
    return;
  }
#endif
  [native->classic setVertexBytes:bytes length:length atIndex:index];
}

GPU_HIDE
void
mt_vertexBuffer(GPURenderCommandEncoder *rce,
                GPUBuffer               *buffer,
                uint64_t                 offset,
                uint32_t                 index) {
  MTRenderEncoder *native;
  id<MTLBuffer>    nativeBuffer;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  nativeBuffer = mt_nativeBuffer(buffer);
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentBuffer(rce->_cmdb,
                         native->vertexArguments,
                         buffer,
                         offset,
                         index);
    return;
  }
#endif
  [native->classic setVertexBuffer:nativeBuffer
                            offset:(NSUInteger)offset
                           atIndex:index];
}

GPU_HIDE
void
mt_vertexInputBuffer(GPURenderCommandEncoder *rce,
                     GPUBuffer               *buffer,
                     uint64_t                 offset,
                     uint32_t                 index) {
  index = mt_vertexBufferIndex(index);
  if (index != UINT32_MAX) {
    mt_vertexBuffer(rce, buffer, offset, index);
  }
}

GPU_HIDE
void
mt_rceSetVertexTexture(GPURenderCommandEncoder *rce,
                       GPUTextureView          *view,
                       uint32_t                 index) {
  MTRenderEncoder *native;
  id<MTLTexture>   texture;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  texture = view ? (id<MTLTexture>)view->_priv : nil;
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentTexture(rce->_cmdb, native->vertexArguments, view, index);
    return;
  }
#endif
  [native->classic setVertexTexture:texture atIndex:index];
}

GPU_HIDE
void
mt_rceSetVertexSampler(GPURenderCommandEncoder *rce,
                       GPUSampler              *sampler,
                       uint32_t                 index) {
  MTRenderEncoder    *native;
  id<MTLSamplerState> samplerState;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  samplerState = sampler ? (id<MTLSamplerState>)sampler->_priv : nil;
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentSampler(native->vertexArguments, sampler, index);
    return;
  }
#endif
  [native->classic setVertexSamplerState:samplerState atIndex:index];
}

GPU_HIDE
void
mt_rceSetVertexAccelerationStructure(
  GPURenderCommandEncoder     *rce,
  GPUAccelerationStructureEXT *structure,
  uint32_t                     index) {
  GPUAccelerationStructureMT *ray;
  MTRenderEncoder             *native;

  native = mt_renderEncoder(rce);
  ray    = structure ? structure->_priv : NULL;
  if (!native || !ray || !ray->structure) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentAccelerationStructure(rce->_cmdb,
                                        native->vertexArguments,
                                        structure,
                                        index);
    return;
  }
#endif
  if (@available(macOS 12.0, iOS 15.0, *)) {
    [native->classic setVertexAccelerationStructure:ray->structure
                                      atBufferIndex:index];
  }
}

GPU_HIDE
void
mt_taskBuffer(GPURenderCommandEncoder *rce,
              GPUBuffer               *buffer,
              uint64_t                 offset,
              uint32_t                 index) {
  MTRenderEncoder *native;
  id<MTLBuffer>    nativeBuffer;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  nativeBuffer = mt_nativeBuffer(buffer);
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentBuffer(rce->_cmdb,
                         native->taskArguments,
                         buffer,
                         offset,
                         index);
    return;
  }
#endif
  if (@available(macOS 13.0, iOS 16.0, *)) {
    [native->classic setObjectBuffer:nativeBuffer
                              offset:(NSUInteger)offset
                             atIndex:index];
  }
}

GPU_HIDE
void
mt_rceSetTaskTexture(GPURenderCommandEncoder *rce,
                     GPUTextureView          *view,
                     uint32_t                 index) {
  MTRenderEncoder *native;
  id<MTLTexture>   texture;

  native  = mt_renderEncoder(rce);
  texture = view ? (id<MTLTexture>)view->_priv : nil;
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentTexture(rce->_cmdb, native->taskArguments, view, index);
    return;
  }
#endif
  if (@available(macOS 13.0, iOS 16.0, *)) {
    [native->classic setObjectTexture:texture atIndex:index];
  }
}

GPU_HIDE
void
mt_rceSetTaskSampler(GPURenderCommandEncoder *rce,
                     GPUSampler              *sampler,
                     uint32_t                 index) {
  MTRenderEncoder     *native;
  id<MTLSamplerState>  samplerState;

  native       = mt_renderEncoder(rce);
  samplerState = sampler ? (id<MTLSamplerState>)sampler->_priv : nil;
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentSampler(native->taskArguments, sampler, index);
    return;
  }
#endif
  if (@available(macOS 13.0, iOS 16.0, *)) {
    [native->classic setObjectSamplerState:samplerState atIndex:index];
  }
}

GPU_HIDE
void
mt_meshBuffer(GPURenderCommandEncoder *rce,
              GPUBuffer               *buffer,
              uint64_t                 offset,
              uint32_t                 index) {
  MTRenderEncoder *native;
  id<MTLBuffer>    nativeBuffer;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  nativeBuffer = mt_nativeBuffer(buffer);
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentBuffer(rce->_cmdb,
                         native->meshArguments,
                         buffer,
                         offset,
                         index);
    return;
  }
#endif
  if (@available(macOS 13.0, iOS 16.0, *)) {
    [native->classic setMeshBuffer:nativeBuffer
                            offset:(NSUInteger)offset
                           atIndex:index];
  }
}

GPU_HIDE
void
mt_rceSetMeshTexture(GPURenderCommandEncoder *rce,
                     GPUTextureView          *view,
                     uint32_t                 index) {
  MTRenderEncoder *native;
  id<MTLTexture>   texture;

  native  = mt_renderEncoder(rce);
  texture = view ? (id<MTLTexture>)view->_priv : nil;
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentTexture(rce->_cmdb, native->meshArguments, view, index);
    return;
  }
#endif
  if (@available(macOS 13.0, iOS 16.0, *)) {
    [native->classic setMeshTexture:texture atIndex:index];
  }
}

GPU_HIDE
void
mt_rceSetMeshSampler(GPURenderCommandEncoder *rce,
                     GPUSampler              *sampler,
                     uint32_t                 index) {
  MTRenderEncoder     *native;
  id<MTLSamplerState>  samplerState;

  native       = mt_renderEncoder(rce);
  samplerState = sampler ? (id<MTLSamplerState>)sampler->_priv : nil;
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentSampler(native->meshArguments, sampler, index);
    return;
  }
#endif
  if (@available(macOS 13.0, iOS 16.0, *)) {
    [native->classic setMeshSamplerState:samplerState atIndex:index];
  }
}

GPU_HIDE
void
mt_fragmentBuffer(GPURenderCommandEncoder *rce,
                  GPUBuffer               *buffer,
                  uint64_t                 offset,
                  uint32_t                 index) {
  MTRenderEncoder *native;
  id<MTLBuffer>    nativeBuffer;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  nativeBuffer = mt_nativeBuffer(buffer);
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentBuffer(rce->_cmdb,
                         native->fragmentArguments,
                         buffer,
                         offset,
                         index);
    return;
  }
#endif
  [native->classic setFragmentBuffer:nativeBuffer
                              offset:(NSUInteger)offset
                             atIndex:index];
}

GPU_HIDE
void
mt_rceSetFragmentTexture(GPURenderCommandEncoder *rce,
                         GPUTextureView           *view,
                         uint32_t                 index) {
  MTRenderEncoder *native;
  id<MTLTexture>   texture;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  texture = view ? (id<MTLTexture>)view->_priv : nil;
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentTexture(rce->_cmdb, native->fragmentArguments, view, index);
    return;
  }
#endif
  [native->classic setFragmentTexture:texture atIndex:index];
}

GPU_HIDE
void
mt_rceSetFragmentSampler(GPURenderCommandEncoder *rce,
                         GPUSampler              *sampler,
                         uint32_t                 index) {
  MTRenderEncoder     *native;
  id<MTLSamplerState>  samplerState;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  samplerState = sampler ? (id<MTLSamplerState>)sampler->_priv : nil;
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentSampler(native->fragmentArguments, sampler, index);
    return;
  }
#endif
  [native->classic setFragmentSamplerState:samplerState atIndex:index];
}

GPU_HIDE
void
mt_rceSetFragmentAccelerationStructure(
  GPURenderCommandEncoder     *rce,
  GPUAccelerationStructureEXT *structure,
  uint32_t                     index) {
  GPUAccelerationStructureMT *ray;
  MTRenderEncoder             *native;

  native = mt_renderEncoder(rce);
  ray    = structure ? structure->_priv : NULL;
  if (!native || !ray || !ray->structure) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    mt_setArgumentAccelerationStructure(rce->_cmdb,
                                        native->fragmentArguments,
                                        structure,
                                        index);
    return;
  }
#endif
  if (@available(macOS 12.0, iOS 15.0, *)) {
    [native->classic setFragmentAccelerationStructure:ray->structure
                                        atBufferIndex:index];
  }
}

GPU_HIDE
void
mt_drawPrimitives(GPURenderCommandEncoder *rce,
                  GPUPrimitiveType         type,
                  size_t                   start,
                  size_t                   count,
                  uint32_t                 instanceCount,
                  uint32_t                 firstInstance) {
  MTRenderEncoder *native;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern drawPrimitives:(MTLPrimitiveType)type
                         vertexStart:start
                         vertexCount:count
                       instanceCount:instanceCount
                        baseInstance:firstInstance];
    }
    return;
  }
#endif
  [native->classic drawPrimitives:(MTLPrimitiveType)type
                      vertexStart:start
                      vertexCount:count
                    instanceCount:instanceCount
                     baseInstance:firstInstance];
}

GPU_HIDE
void
mt_drawIndexedPrims(GPURenderCommandEncoder *rce,
                    uint32_t                 indexCount,
                    uint32_t                 instanceCount,
                    uint32_t                 firstIndex,
                    int32_t                  vertexOffset,
                    uint32_t                 firstInstance) {
  MTRenderEncoder *native;
  id<MTLBuffer>    indexBuffer;
  uint64_t         indexSize;
  uint64_t         indexOffset;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  indexBuffer = mt_nativeBuffer(rce->_indexBuffer);
  indexSize = rce->_indexType == GPU_INDEX_TYPE_UINT32
                ? 4u
                : 2u;
  indexOffset = rce->_indexBufferOffset + (uint64_t)firstIndex * indexSize;
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      mt_useAllocation(rce->_cmdb, indexBuffer);
      [native->modern drawIndexedPrimitives:(MTLPrimitiveType)rce->_primitiveType
                                 indexCount:indexCount
                                  indexType:(MTLIndexType)rce->_indexType
                                indexBuffer:indexBuffer.gpuAddress + indexOffset
                          indexBufferLength:indexBuffer.length - (NSUInteger)indexOffset
                              instanceCount:instanceCount
                                 baseVertex:vertexOffset
                               baseInstance:firstInstance];
    }
    return;
  }
#endif
  [native->classic drawIndexedPrimitives:(MTLPrimitiveType)rce->_primitiveType
                              indexCount:indexCount
                               indexType:(MTLIndexType)rce->_indexType
                             indexBuffer:indexBuffer
                       indexBufferOffset:(NSUInteger)indexOffset
                           instanceCount:instanceCount
                              baseVertex:vertexOffset
                            baseInstance:firstInstance];
}

GPU_HIDE
void
mt_drawMesh(GPURenderCommandEncoder *rce,
            uint32_t                 groupCountX,
            uint32_t                 groupCountY,
            uint32_t                 groupCountZ,
            const uint32_t           taskWorkgroupSize[3],
            const uint32_t           meshWorkgroupSize[3]) {
  MTRenderEncoder *native;
  MTLSize          groups;
  MTLSize          taskThreads;
  MTLSize          meshThreads;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }

  groups = MTLSizeMake(groupCountX, groupCountY, groupCountZ);
  taskThreads = MTLSizeMake(taskWorkgroupSize[0],
                            taskWorkgroupSize[1],
                            taskWorkgroupSize[2]);
  meshThreads = MTLSizeMake(meshWorkgroupSize[0],
                            meshWorkgroupSize[1],
                            meshWorkgroupSize[2]);
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern drawMeshThreadgroups:groups
               threadsPerObjectThreadgroup:taskThreads
                 threadsPerMeshThreadgroup:meshThreads];
    }
    return;
  }
#endif
  if (@available(macOS 13.0, iOS 16.0, *)) {
    [native->classic drawMeshThreadgroups:groups
              threadsPerObjectThreadgroup:taskThreads
                threadsPerMeshThreadgroup:meshThreads];
  }
}

GPU_HIDE
void
mt_drawPrimitivesIndirect(GPURenderCommandEncoder *rce,
                          GPUPrimitiveType         type,
                          GPUBuffer               *argsBuffer,
                          uint64_t                 argsOffset) {
  MTRenderEncoder *native;
  id<MTLBuffer>    args;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  args = mt_nativeBuffer(argsBuffer);
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      mt_useAllocation(rce->_cmdb, args);
      [native->modern drawPrimitives:(MTLPrimitiveType)type
                      indirectBuffer:args.gpuAddress + argsOffset];
    }
    return;
  }
#endif
  [native->classic drawPrimitives:(MTLPrimitiveType)type
                    indirectBuffer:args
              indirectBufferOffset:(NSUInteger)argsOffset];
}

GPU_HIDE
void
mt_drawIndexedPrimsIndirect(GPURenderCommandEncoder *rce,
                            GPUBuffer               *argsBuffer,
                            uint64_t                 argsOffset) {
  MTRenderEncoder *native;
  id<MTLBuffer>    indexBuffer;
  id<MTLBuffer>    args;

  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
  indexBuffer = mt_nativeBuffer(rce->_indexBuffer);
  args = mt_nativeBuffer(argsBuffer);
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      mt_useAllocation(rce->_cmdb, indexBuffer);
      mt_useAllocation(rce->_cmdb, args);
      [native->modern drawIndexedPrimitives:(MTLPrimitiveType)rce->_primitiveType
                                  indexType:(MTLIndexType)rce->_indexType
                                indexBuffer:indexBuffer.gpuAddress + rce->_indexBufferOffset
                          indexBufferLength:indexBuffer.length - (NSUInteger)rce->_indexBufferOffset
                             indirectBuffer:args.gpuAddress + argsOffset];
    }
    return;
  }
#endif
  [native->classic drawIndexedPrimitives:(MTLPrimitiveType)rce->_primitiveType
                               indexType:(MTLIndexType)rce->_indexType
                             indexBuffer:indexBuffer
                       indexBufferOffset:(NSUInteger)rce->_indexBufferOffset
                          indirectBuffer:args
                    indirectBufferOffset:(NSUInteger)argsOffset];
}

GPU_HIDE
void
mt_endEncoding(GPURenderCommandEncoder *rce) {
  MTRenderEncoder *native;

  if (!rce) {
    return;
  }
  native = mt_renderEncoder(rce);
  if (!native) {
    return;
  }
#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern endEncoding];
    }
  } else
#endif
  {
    [native->classic endEncoding];
  }
  native->classic = nil;
  native->modern = nil;
}

GPU_HIDE
void
mt_initRCE(GPUApiRCE *api) {
  api->renderCommandEncoder     = mt_renderCommandEncoder;
  api->frontFace                = mt_frontFace;
  api->cullMode                 = mt_cullMode;
  api->setRenderPipelineState   = mt_setRenderPipelineState;
  api->viewport                 = mt_viewport;
  api->scissor                  = mt_scissor;
  api->blendConstant            = mt_blendConstant;
  api->stencilReference         = mt_stencilReference;
  api->pushConstants            = mt_renderPushConstants;
  api->vertexBytes              = mt_vertexBytes;
  api->vertexBuffer             = mt_vertexBuffer;
  api->vertexInputBuffer        = mt_vertexInputBuffer;
  api->setVertexTexture         = mt_rceSetVertexTexture;
  api->setVertexSampler         = mt_rceSetVertexSampler;
  api->setVertexAccelerationStructure =
    mt_rceSetVertexAccelerationStructure;
  api->taskBuffer               = mt_taskBuffer;
  api->setTaskTexture           = mt_rceSetTaskTexture;
  api->setTaskSampler           = mt_rceSetTaskSampler;
  api->meshBuffer               = mt_meshBuffer;
  api->setMeshTexture           = mt_rceSetMeshTexture;
  api->setMeshSampler           = mt_rceSetMeshSampler;
  api->fragmentBuffer           = mt_fragmentBuffer;
  api->setFragmentTexture       = mt_rceSetFragmentTexture;
  api->setFragmentSampler       = mt_rceSetFragmentSampler;
  api->setFragmentAccelerationStructure =
    mt_rceSetFragmentAccelerationStructure;
  api->drawPrimitives           = mt_drawPrimitives;
  api->drawIndexedPrims         = mt_drawIndexedPrims;
  api->drawMesh                 = mt_drawMesh;
  api->drawPrimitivesIndirect   = mt_drawPrimitivesIndirect;
  api->drawIndexedPrimsIndirect = mt_drawIndexedPrimsIndirect;
  api->endEncoding              = mt_endEncoding;
}
