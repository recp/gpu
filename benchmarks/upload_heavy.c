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

#include "api/device_internal.h"
#include "render.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  UPLOAD_FRAMES_IN_FLIGHT = 3,
  UPLOAD_ALIGNMENT        = 256
};

typedef struct UploadUniforms {
  float tint[4];
} UploadUniforms;

typedef struct UploadHeavy {
  BenchRender        *bench;
  GPURenderPipeline  *pipeline;
  GPUBindGroupLayout *layout;
  GPUBindGroup       *group;
  GPUBuffer          *transientBuffer;
  uint64_t            ringBytesPerFrame;
  uint64_t            expectedUsedBytes;
} UploadHeavy;

static const float uploadVertices[] = {
  -1.0f, -1.0f,
   3.0f, -1.0f,
  -1.0f,  3.0f
};

static bool
upload_createLayout(UploadHeavy *upload) {
  const GPUBindGroupLayoutEntry *entries;
  uint32_t                       entryCount;
  uint32_t                       layoutCount;

  GPUDestroyPipelineLayout(upload->bench->pipelineLayout);
  upload->bench->pipelineLayout = NULL;

  layoutCount = 0u;
  if (GPUCreateBindGroupLayoutsFromReflection(upload->bench->device,
                                               upload->bench->library,
                                               &layoutCount,
                                               NULL) != GPU_OK ||
      layoutCount != 1u) {
    return false;
  }
  if (GPUCreateBindGroupLayoutsFromReflection(upload->bench->device,
                                               upload->bench->library,
                                               &layoutCount,
                                               &upload->layout) != GPU_OK ||
      layoutCount != 1u || !upload->layout) {
    return false;
  }

  entries = GPUGetBindGroupLayoutEntries(upload->layout, &entryCount);
  if (!entries || entryCount != 1u || entries[0].binding != 0u ||
      entries[0].bindingType != GPU_BINDING_UNIFORM_BUFFER ||
      entries[0].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      entries[0].arrayCount != 1u || !entries[0].hasDynamicOffset) {
    return false;
  }

  return GPUCreatePipelineLayoutFromReflection(upload->bench->device,
                                                upload->bench->library,
                                                1u,
                                                &upload->layout,
                                                &upload->bench->pipelineLayout)
           == GPU_OK &&
         upload->bench->pipelineLayout != NULL;
}

static bool
upload_createResources(UploadHeavy *upload, uint32_t drawCount) {
  GPUTransientAllocatorConfig allocatorInfo;
  GPUTransientBufferSlice     initialSlice;
  GPUBindGroupCreateInfo      groupInfo;
  GPUBindGroupEntry           entry;

  memset(&allocatorInfo, 0, sizeof(allocatorInfo));
  memset(&initialSlice, 0, sizeof(initialSlice));
  memset(&groupInfo, 0, sizeof(groupInfo));
  memset(&entry, 0, sizeof(entry));

  upload->ringBytesPerFrame = (uint64_t)drawCount * UPLOAD_ALIGNMENT;
  if (upload->ringBytesPerFrame == 0u ||
      upload->ringBytesPerFrame > UINT32_MAX / UPLOAD_FRAMES_IN_FLIGHT) {
    return false;
  }
  allocatorInfo.chain.sType = GPU_STRUCTURE_TYPE_TRANSIENT_ALLOCATOR_CONFIG;
  allocatorInfo.chain.structSize = sizeof(allocatorInfo);
  allocatorInfo.ringBytesPerFrame = upload->ringBytesPerFrame;
  allocatorInfo.framesInFlight    = UPLOAD_FRAMES_IN_FLIGHT;
  allocatorInfo.chunkBytes        = 64u * 1024u;
  allocatorInfo.allowChunkFallback = false;
  if (GPUConfigureTransientAllocator(upload->bench->device,
                                     &allocatorInfo) != GPU_OK ||
      GPUAllocateTransientBuffer(upload->bench->device,
                                 GPU_BUFFER_USAGE_UNIFORM,
                                 sizeof(UploadUniforms),
                                 UPLOAD_ALIGNMENT,
                                 &initialSlice) != GPU_OK ||
      !initialSlice.buffer) {
    return false;
  }
  upload->transientBuffer = initialSlice.buffer;

  entry.binding       = 0u;
  entry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  entry.buffer.buffer = upload->transientBuffer;
  entry.buffer.size   = sizeof(UploadUniforms);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "upload-heavy-group";
  groupInfo.layout           = upload->layout;
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &entry;
  return GPUCreateBindGroup(upload->bench->device,
                            &groupInfo,
                            &upload->group) == GPU_OK &&
         upload->group != NULL;
}

static bool
upload_encode(GPURenderPassEncoder *pass,
              uint32_t              drawCount,
              void                 *userData) {
  UploadHeavy *upload;

  upload = userData;
  gpuDeviceAdvanceFrameSlot(upload->bench->device);
  GPUBindRenderPipeline(pass, upload->pipeline);
  for (uint32_t draw = 0u; draw < drawCount; draw++) {
    GPUTransientBufferSlice uniformSlice;
    GPUTransientBufferSlice vertexSlice;
    GPUBufferBinding        vertexBinding;
    UploadUniforms          uniforms;
    uint64_t                frameOffset;
    uint32_t                dynamicOffset;

    if (GPUAllocateTransientBuffer(upload->bench->device,
                                   GPU_BUFFER_USAGE_UNIFORM,
                                   sizeof(uniforms),
                                   UPLOAD_ALIGNMENT,
                                   &uniformSlice) != GPU_OK ||
        GPUAllocateTransientBuffer(upload->bench->device,
                                   GPU_BUFFER_USAGE_VERTEX,
                                   sizeof(uploadVertices),
                                   16u,
                                   &vertexSlice) != GPU_OK ||
        uniformSlice.buffer != upload->transientBuffer ||
        vertexSlice.buffer != upload->transientBuffer ||
        uniformSlice.offset > UINT32_MAX) {
      return false;
    }

    uniforms.tint[0] = (draw & 1u) ? 0.2f : 1.0f;
    uniforms.tint[1] = (draw & 2u) ? 1.0f : 0.3f;
    uniforms.tint[2] = (draw & 4u) ? 0.4f : 1.0f;
    uniforms.tint[3] = 1.0f;
    memcpy(uniformSlice.cpuPtr, &uniforms, sizeof(uniforms));
    memcpy(vertexSlice.cpuPtr, uploadVertices, sizeof(uploadVertices));

    dynamicOffset        = (uint32_t)uniformSlice.offset;
    vertexBinding.buffer = vertexSlice.buffer;
    vertexBinding.offset = vertexSlice.offset;
    GPUBindVertexBuffers(pass, 0u, 1u, &vertexBinding);
    GPUBindRenderGroup(pass, 0u, upload->group, 1u, &dynamicOffset);
    GPUDraw(pass, 3u, 1u, 0u, 0u);

    frameOffset = vertexSlice.offset % upload->ringBytesPerFrame;
    upload->expectedUsedBytes = frameOffset + vertexSlice.sizeBytes;
  }
  return true;
}

static void
upload_cleanup(UploadHeavy *upload) {
  GPUDestroyRenderPipeline(upload->pipeline);
  GPUDestroyBindGroup(upload->group);
  GPUDestroyPipelineLayout(upload->bench->pipelineLayout);
  upload->bench->pipelineLayout = NULL;
  GPUDestroyBindGroupLayout(upload->layout);
  bench_renderCleanup(upload->bench);
}

int
main(int argc, char *argv[]) {
  BenchRenderConfig config;
  BenchRender       bench;
  BenchPipelineInfo pipelineInfo;
  BenchSceneMetrics metrics;
  GPUAllocatorStats allocatorStats;
  UploadHeavy       upload;
  uint64_t          bytesPerFrame;
  bool              ok;

  memset(&bench, 0, sizeof(bench));
  memset(&pipelineInfo, 0, sizeof(pipelineInfo));
  memset(&metrics, 0, sizeof(metrics));
  memset(&allocatorStats, 0, sizeof(allocatorStats));
  memset(&upload, 0, sizeof(upload));
  upload.bench = &bench;
  if (!bench_renderConfig(argc, argv, &config) ||
      !bench_renderInit(&bench, &config, 1u, 1u) ||
      !upload_createLayout(&upload) ||
      !upload_createResources(&upload, config.drawCount)) {
    upload_cleanup(&upload);
    return EXIT_FAILURE;
  }

  pipelineInfo.label         = "upload-heavy-pipeline";
  pipelineInfo.vertexEntry   = "tri_vs";
  pipelineInfo.fragmentEntry = "tri_fs";
  pipelineInfo.frontFace     = GPU_FRONT_FACE_CCW;
  pipelineInfo.vertexInput   = true;
  if (!bench_renderPipeline(&bench, &pipelineInfo, &upload.pipeline)) {
    fprintf(stderr, "failed to create upload-heavy pipeline\n");
    upload_cleanup(&upload);
    return EXIT_FAILURE;
  }

  ok = bench_renderRun(&bench,
                       &config,
                       upload_encode,
                       &upload,
                       &metrics);
  if (ok) {
    ok = GPUGetAllocatorStats(bench.device, &allocatorStats) == GPU_OK;
  }
  if (ok) {
    bytesPerFrame = (uint64_t)config.drawCount *
                    (sizeof(UploadUniforms) + sizeof(uploadVertices));
    bench_renderPrint("upload heavy", &bench, &config, &metrics);
    printf("upload/frame: %" PRIu64 " bytes, ring used: %" PRIu64
           "/%" PRIu64 ", high-water: %" PRIu64 ", fallbacks: %" PRIu64
           "\n",
           bytesPerFrame,
           allocatorStats.ringUsedBytes,
           upload.ringBytesPerFrame,
           allocatorStats.ringHighWaterBytes,
           allocatorStats.uploadStallCount);
    ok = bench_renderMetricsPass(&metrics) &&
         allocatorStats.ringUsedBytes == upload.expectedUsedBytes &&
         allocatorStats.ringHighWaterBytes == upload.expectedUsedBytes &&
         allocatorStats.uploadStallCount == 0u;
  }

  bench_renderFreeMetrics(&metrics);
  upload_cleanup(&upload);
  if (!ok) {
    fprintf(stderr, "upload-heavy benchmark failed\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
