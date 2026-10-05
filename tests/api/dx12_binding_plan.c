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

#include "test.h"

enum {
  GPU_DX12_BINDING_UNIFORM_BYTES  = 512u,
  GPU_DX12_BINDING_DYNAMIC_OFFSET = 256u,
  GPU_DX12_BINDING_ENTRY_COUNT    = 5u,
  GPU_DX12_BINDING_GROUP_COUNT    = 2u
};

static const uint8_t textureColors[GPU_DX12_BINDING_GROUP_COUNT][4] = {
  {255u, 0u, 0u, 255u},
  {0u, 255u, 0u, 255u}
};

static const float constants[GPU_DX12_BINDING_GROUP_COUNT][4] = {
  {2.0f, 2.0f, 2.0f, 2.0f},
  {0.5f, 0.5f, 0.5f, 0.5f}
};

static const float inputs[GPU_DX12_BINDING_GROUP_COUNT][4] = {
  {1.0f, 2.0f, 3.0f, 4.0f},
  {5.0f, 6.0f, 7.0f, 8.0f}
};

static const float expected[GPU_DX12_BINDING_GROUP_COUNT][4] = {
  {3.0f, 4.0f, 6.0f, 9.0f},
  {2.5f, 4.0f, 3.5f, 5.0f}
};

static int
create_binding_texture(GPUDevice       *device,
                       GPUQueue        *queue,
                       const char      *label,
                       const uint8_t    color[4],
                       GPUTexture     **outTexture,
                       GPUTextureView **outView) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTextureWriteRegion    writeRegion = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 1u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;

  if (GPUCreateTexture(device, &textureInfo, outTexture) != GPU_OK
      || !*outTexture) {
    return 0;
  }

  writeRegion.width        = 1u;
  writeRegion.height       = 1u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = 4u;
  writeRegion.rowsPerImage = 1u;

  if (GPUQueueWriteTexture(queue,
                           *outTexture,
                           &writeRegion,
                           color,
                           4u) != GPU_OK) {
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;

  return GPUCreateTextureView(*outTexture, &viewInfo, outView) == GPU_OK
         && *outView;
}

static int
near_value(float value, float expected) {
  float difference;

  difference = value - expected;
  return difference > -0.001f && difference < 0.001f;
}

int
gpu_test_dx12_binding_plan(GPUDevice *device, const char *bytecodePath) {
  GPUQueue                    *queue;
  GPUShaderLibrary            *library;
  GPUShaderLayout             *shaderLayout;
  GPUComputePipeline          *pipeline;
  GPUBindGroup                *groups[GPU_DX12_BINDING_GROUP_COUNT]        = {0};
  GPUBuffer                   *uniforms[GPU_DX12_BINDING_GROUP_COUNT]      = {0};
  GPUBuffer                   *inputBuffers[GPU_DX12_BINDING_GROUP_COUNT]  = {0};
  GPUBuffer                   *outputBuffers[GPU_DX12_BINDING_GROUP_COUNT] = {0};
  GPUTexture                  *textures[GPU_DX12_BINDING_GROUP_COUNT]      = {0};
  GPUTextureView              *views[GPU_DX12_BINDING_GROUP_COUNT]         = {0};
  GPUSampler                  *samplers[GPU_DX12_BINDING_GROUP_COUNT]      = {0};
  GPUCommandBuffer            *cmdb;
  GPUComputePassEncoder       *pass;
  GPUFence                    *fence;
  void                        *bytecode;
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo          bufferInfo    = {0};
  GPUSamplerCreateInfo         samplerInfo   = {0};
  GPUBindGroupEntry            entries[GPU_DX12_BINDING_ENTRY_COUNT] = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUBufferBarrier             bufferBarriers[GPU_DX12_BINDING_GROUP_COUNT] = {0};
  GPUBarrierBatch              barrierBatch = {0};
  GPUCommandBuffer            *submitBuffers[1];
  GPUQueueSubmitInfo           submitInfo = {0};
  float                        output[GPU_DX12_BINDING_GROUP_COUNT][4] = {0};
  uint32_t                     dynamicOffset;
  uint64_t                     bytecodeSize;
  int                          ok;
  uint32_t                     resourceIndex;
  uint32_t                     groupIndex;
  uint32_t                     barrierIndex;
  uint32_t                     readbackIndex;
  uint32_t                     component;
  uint32_t                     releaseIndex;

  if (!device || !bytecodePath) {
    return 0;
  }

  queue        = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  library      = NULL;
  shaderLayout = NULL;
  pipeline     = NULL;
  cmdb         = NULL;
  pass         = NULL;
  fence        = NULL;
  bytecodeSize = 0u;
  bytecode     = gpu_test_read_file(bytecodePath, &bytecodeSize);
  ok           = queue && bytecode;

  if (!ok) {
    fprintf(stderr, "DX12 binding-plan fixture setup failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK
      || !library
      || GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK
      || !shaderLayout
      || shaderLayout->bindGroupLayoutCount != GPU_DX12_BINDING_GROUP_COUNT
      || !shaderLayout->bindGroupLayouts
      || !shaderLayout->bindGroupLayouts[0]
      || !shaderLayout->bindGroupLayouts[1]
      || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "DX12 binding-plan shader layout failed\n");
    ok = 0;
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "api-dx12-binding-plan";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "dx12_binding_plan_cs";

  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK
      || !pipeline) {
    fprintf(stderr, "DX12 binding-plan pipeline failed\n");
    ok = 0;
    goto cleanup;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "api-dx12-binding-plan";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);

  for (resourceIndex = 0u; resourceIndex < GPU_DX12_BINDING_GROUP_COUNT; resourceIndex++) {
    bufferInfo.label     = "api-dx12-binding-plan-uniform";
    bufferInfo.sizeBytes = GPU_DX12_BINDING_UNIFORM_BYTES;
    bufferInfo.usage     = GPU_BUFFER_USAGE_UNIFORM |
                           GPU_BUFFER_USAGE_COPY_DST;

    if (GPUCreateBuffer(device, &bufferInfo, &uniforms[resourceIndex]) != GPU_OK
        || !uniforms[resourceIndex]
        || GPUQueueWriteBuffer(queue,
                               uniforms[resourceIndex],
                               GPU_DX12_BINDING_DYNAMIC_OFFSET,
                               constants[resourceIndex],
                               sizeof(constants[resourceIndex])) != GPU_OK) {
      fprintf(stderr, "DX12 binding-plan uniform setup failed\n");
      ok = 0;
      goto cleanup;
    }

    bufferInfo.label     = "api-dx12-binding-plan-input";
    bufferInfo.sizeBytes = sizeof(inputs[resourceIndex]);
    bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                           GPU_BUFFER_USAGE_COPY_DST;

    if (GPUCreateBuffer(device, &bufferInfo, &inputBuffers[resourceIndex]) != GPU_OK
        || !inputBuffers[resourceIndex]
        || GPUQueueWriteBuffer(queue,
                               inputBuffers[resourceIndex],
                               0u,
                               inputs[resourceIndex],
                               sizeof(inputs[resourceIndex])) != GPU_OK) {
      fprintf(stderr, "DX12 binding-plan input setup failed\n");
      ok = 0;
      goto cleanup;
    }

    bufferInfo.label     = "api-dx12-binding-plan-output";
    bufferInfo.sizeBytes = sizeof(output[resourceIndex]);
    bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                           GPU_BUFFER_USAGE_COPY_SRC |
                           GPU_BUFFER_USAGE_COPY_DST;

    if (GPUCreateBuffer(device, &bufferInfo, &outputBuffers[resourceIndex]) != GPU_OK
        || !outputBuffers[resourceIndex]
        || GPUQueueWriteBuffer(queue,
                               outputBuffers[resourceIndex],
                               0u,
                               output[resourceIndex],
                               sizeof(output[resourceIndex])) != GPU_OK
        || !create_binding_texture(device,
                                   queue,
                                   "api-dx12-binding-plan-texture",
                                   textureColors[resourceIndex],
                                   &textures[resourceIndex],
                                   &views[resourceIndex])
        || GPUCreateSampler(device,
                            &samplerInfo,
                            false,
                            &samplers[resourceIndex]) != GPU_OK
        || !samplers[resourceIndex]) {
      fprintf(stderr, "DX12 binding-plan resource setup failed\n");
      ok = 0;
      goto cleanup;
    }

    memset(entries, 0, sizeof(entries));
    entries[0].binding       = 0u;
    entries[0].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
    entries[0].buffer.buffer = uniforms[resourceIndex];
    entries[0].buffer.size   = sizeof(constants[resourceIndex]);
    entries[1].binding       = 1u;
    entries[1].bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
    entries[1].buffer.buffer = inputBuffers[resourceIndex];
    entries[1].buffer.size   = sizeof(inputs[resourceIndex]);
    entries[2].binding       = 2u;
    entries[2].bindingType   = GPU_BINDING_STORAGE_BUFFER;
    entries[2].buffer.buffer = outputBuffers[resourceIndex];
    entries[2].buffer.size   = sizeof(output[resourceIndex]);
    entries[3].binding       = 3u;
    entries[3].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
    entries[3].textureView   = views[resourceIndex];
    entries[4].binding       = 4u;
    entries[4].bindingType   = GPU_BINDING_SAMPLER;
    entries[4].sampler       = samplers[resourceIndex];

    groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
    groupInfo.chain.structSize = sizeof(groupInfo);
    groupInfo.label            = "api-dx12-binding-plan";
    groupInfo.layout           = shaderLayout->bindGroupLayouts[resourceIndex];
    groupInfo.entryCount       = GPU_DX12_BINDING_ENTRY_COUNT;
    groupInfo.pEntries         = entries;

    if (GPUCreateBindGroup(device, &groupInfo, &groups[resourceIndex]) != GPU_OK
        || !groups[resourceIndex]) {
      fprintf(stderr, "DX12 binding-plan group setup failed\n");
      ok = 0;
      goto cleanup;
    }
  }

  if (GPUAcquireCommandBuffer(queue,
                              "api-dx12-binding-plan",
                              &cmdb) != GPU_OK
      || !cmdb
      || !(pass = GPUBeginComputePass(cmdb, "api-dx12-binding-plan"))) {
    fprintf(stderr, "DX12 binding-plan command setup failed\n");
    ok = 0;
    goto cleanup;
  }

  dynamicOffset = GPU_DX12_BINDING_DYNAMIC_OFFSET;
  GPUBindComputePipeline(pass, pipeline);

  for (groupIndex = 0u; groupIndex < GPU_DX12_BINDING_GROUP_COUNT; groupIndex++) {
    GPUBindComputeGroup(pass, groupIndex, groups[groupIndex], 1u, &dynamicOffset);
  }

  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  for (barrierIndex = 0u; barrierIndex < GPU_DX12_BINDING_GROUP_COUNT; barrierIndex++) {
    bufferBarriers[barrierIndex].buffer    = outputBuffers[barrierIndex];
    bufferBarriers[barrierIndex].srcAccess = GPU_ACCESS_SHADER_WRITE;
    bufferBarriers[barrierIndex].dstAccess = GPU_ACCESS_TRANSFER_READ;
    bufferBarriers[barrierIndex].sizeBytes = sizeof(output[barrierIndex]);
  }

  barrierBatch.srcStages          = GPU_STAGE_COMPUTE;
  barrierBatch.dstStages          = GPU_STAGE_TRANSFER;
  barrierBatch.bufferBarrierCount = GPU_DX12_BINDING_GROUP_COUNT;
  barrierBatch.pBufferBarriers    = bufferBarriers;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "DX12 binding-plan fence failed\n");
    ok = 0;
    goto cleanup;
  }

  submitBuffers[0]              = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = submitBuffers;
  submitInfo.fence              = fence;

  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK
      || GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "DX12 binding-plan submission failed\n");
    cmdb = NULL;
    ok   = 0;
    goto cleanup;
  }

  cmdb = NULL;

  for (readbackIndex = 0u; readbackIndex < GPU_DX12_BINDING_GROUP_COUNT; readbackIndex++) {
    if (GPUQueueReadBuffer(queue,
                           outputBuffers[readbackIndex],
                           0u,
                           output[readbackIndex],
                           sizeof(output[readbackIndex])) != GPU_OK) {
      fprintf(stderr, "DX12 binding-plan readback failed\n");
      ok = 0;
      goto cleanup;
    }

    for (component = 0u; component < 4u; component++) {
      if (!near_value(output[readbackIndex][component], expected[readbackIndex][component])) {
        fprintf(stderr,
                "DX12 binding-plan mismatch at %u,%u: %.3f != %.3f\n",
                readbackIndex,
                component,
                output[readbackIndex][component],
                expected[readbackIndex][component]);
        ok = 0;
        goto cleanup;
      }
    }
  }

  ok = 1;

cleanup:

  if (pass) {
    GPUEndComputePass(pass);
  }

  GPUDestroyFence(fence);

  for (releaseIndex = 0u; releaseIndex < GPU_DX12_BINDING_GROUP_COUNT; releaseIndex++) {
    GPUDestroyBindGroup(groups[releaseIndex]);
    GPUDestroySampler(samplers[releaseIndex]);
    GPUDestroyTextureView(views[releaseIndex]);
    GPUDestroyTexture(textures[releaseIndex]);
    GPUDestroyBuffer(outputBuffers[releaseIndex]);
    GPUDestroyBuffer(inputBuffers[releaseIndex]);
    GPUDestroyBuffer(uniforms[releaseIndex]);
  }

  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  return ok;
}
