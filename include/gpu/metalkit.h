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

#ifndef gpu_metalkit_helpers_h
#define gpu_metalkit_helpers_h
#if defined(__APPLE__) && defined(__OBJC__)
#ifdef __cplusplus
extern "C" {
#endif

#import <MetalKit/MetalKit.h>

GPU_INLINE
void
GPUDrawIndexedMTKSubMesh(GPURenderCommandEncoder *rce,
                         MTKSubmesh              *submesh) {
  GPUDrawIndexed(rce,
                 (GPUPrimitiveType)submesh.primitiveType,
                 (uint32_t)submesh.indexCount,
                 (GPUIndexType)submesh.indexType,
                 (GPUBuffer *)submesh.indexBuffer.buffer,
                 (uint32_t)submesh.indexBuffer.offset);
}

GPU_INLINE
void
GPUDrawIndexedMTKMesh(GPURenderCommandEncoder *rce, MTKMesh *mesh) {
  for(MTKSubmesh *submesh in mesh.submeshes)
    GPUDrawIndexedMTKSubMesh(rce, submesh);
}

GPU_INLINE
void
GPUSetVertexBuffersFromMTKMeshBuffers(GPURenderCommandEncoder  *rce,
                                      NSArray<MTKMeshBuffer *> *buffers) {
  MTKMeshBuffer *buffer;
  for (uint32_t i = 0; i < buffers.count; i++) {
    buffer = buffers[i];
    if ((NSNull*)buffer != [NSNull null])
      GPUSetVertexBuffer(rce, (GPUBuffer *)buffer.buffer, buffer.offset, i);
  }
}

GPU_INLINE
void
GPULoadAndDrawMTKMesh(GPURenderCommandEncoder *rce, MTKMesh *mesh) {
  GPUSetVertexBuffersFromMTKMeshBuffers(rce, mesh.vertexBuffers);
  GPUDrawIndexedMTKMesh(rce, mesh);
  
  
  //  GPUSetVertexBuffersFromMTKMeshBuffers()
  //  GPU_SetVertexBuffersFromMTKMeshBuffers()
  //  gpuSetVertexBuffersFromMTKMeshBuffers()
  //  gpu_setVertexBuffersFromMTKMeshBuffers()
  //  gpuSetVertexBuffersFromMTKMeshBuffers()
  //  gpu_set_vertexbuffers_from_mtkmeshbuffers()
  //  gpu_mtk_meshbuffer_set_vertexbuffers()
}
#ifdef __cplusplus
}
#endif
#endif
#endif /* gpu_metalkit_helpers_h */
