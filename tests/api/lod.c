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
#include "../../src/api/device_internal.h"
#include "../../src/api/texture_internal.h"

#include <math.h>

typedef struct LODCase {
  float    samplerMin;
  float    samplerMax;
  float    viewMin;
  uint32_t baseMip;
  bool     samplerClamp;
} LODCase;

static const LODCase cases[] = {
  {0.0f, 3.0f, 0.0f, 0u, false},
  {0.0f, 0.0f, 0.0f, 0u, true},
  {1.0f, 1.0f, 0.0f, 0u, true},
  {0.0f, 1.0f, 0.0f, 0u, true},
  {1.5f, 1.5f, 0.0f, 0u, true},
  {1.0f, 1.0f, 0.0f, 1u, true},
  {0.0f, 3.0f, 1.0f, 0u, false},
  {0.0f, 3.0f, 1.5f, 0u, false},
  {0.0f, 3.0f, 2.0f, 0u, false},
  {0.0f, 0.0f, 1.5f, 1u, true}
};

static const uint8_t colors[4][4] = {
  {255u,   0u,   0u, 255u},
  {  0u, 255u,   0u, 255u},
  {  0u,   0u, 255u, 255u},
  {255u, 255u, 255u, 255u}
};

static int
check_lod_validation(GPUDevice *device, GPUTexture *texture) {
  GPUDevice                disabledDevice;
  GPUTexture               disabledTexture;
  GPUSamplerCreateInfo     samplerInfo = {0};
  GPUSamplerLODClamp       samplerLOD  = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTextureViewMinLODEXT  viewLOD     = {0};
  GPUSampler              *sampler;
  GPUTextureView          *view;
  GPUResult                result;
  uint32_t                 i;

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.chain.pNext      = &samplerLOD;
  samplerInfo.desc.minFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.magFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_LINEAR;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.chain.pNext      = &viewLOD;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.mipLevelCount    = 4u;
  viewInfo.arrayLayerCount  = 1u;

  for (i = 0u; i < 8u; i++) {
    samplerLOD.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_LOD_CLAMP;
    samplerLOD.chain.structSize = sizeof(samplerLOD);
    samplerLOD.chain.pNext      = NULL;
    samplerLOD.minLOD           = 0.0f;
    samplerLOD.maxLOD           = 3.0f;

    switch (i) {
      case 0u: samplerLOD.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_MIN_LOD_EXT; break;
      case 1u: samplerLOD.chain.structSize--; break;
      case 2u: samplerLOD.chain.pNext = &samplerLOD; break;
      case 3u: samplerLOD.minLOD = -1.0f; break;
      case 4u: samplerLOD.minLOD = NAN; break;
      case 5u: samplerLOD.maxLOD = INFINITY; break;
      case 6u: samplerLOD.minLOD = 4.0f; break;
      case 7u: samplerLOD.maxLOD = NAN; break;
    }

    sampler = (GPUSampler *)(uintptr_t)1u;
    result  = GPUCreateSampler(device, &samplerInfo, false, &sampler);

    if (result != GPU_ERROR_INVALID_ARGUMENT || sampler) {
      fprintf(stderr, "lod sampler validation %u failed: %d\n", i, result);
      return 0;
    }

    viewLOD.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_MIN_LOD_EXT;
    viewLOD.chain.structSize = sizeof(viewLOD);
    viewLOD.chain.pNext      = NULL;
    viewLOD.minLOD           = 0.0f;

    switch (i) {
      case 0u: viewLOD.chain.sType = GPU_STRUCTURE_TYPE_SAMPLER_LOD_CLAMP; break;
      case 1u: viewLOD.chain.structSize--; break;
      case 2u: viewLOD.chain.pNext = &viewLOD; break;
      case 3u: viewLOD.minLOD = -1.0f; break;
      case 4u: viewLOD.minLOD = NAN; break;
      case 5u: viewLOD.minLOD = INFINITY; break;
      case 6u: viewLOD.minLOD = 3.5f; break;
      case 7u: viewLOD.minLOD = 4.0f; break;
    }

    view   = (GPUTextureView *)(uintptr_t)1u;
    result = GPUCreateTextureView(texture, &viewInfo, &view);

    if (result != GPU_ERROR_INVALID_ARGUMENT || view) {
      fprintf(stderr, "lod view validation %u failed: %d\n", i, result);
      return 0;
    }
  }

  viewLOD.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_MIN_LOD_EXT;
  viewLOD.chain.structSize = sizeof(viewLOD);
  viewLOD.chain.pNext      = NULL;
  viewLOD.minLOD           = 1.0f;
  view                     = NULL;
  result                   = GPUCreateTextureView(texture, &viewInfo, &view);

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_TEXTURE_VIEW_MIN_LOD)) {
    if (result != GPU_OK || !view)
      return 0;
  } else if (result != GPU_ERROR_UNSUPPORTED || view) {
    fprintf(stderr, "lod view accepted a disabled feature: %d\n", result);
    return 0;
  }

  GPUDestroyTextureView(view);

  disabledDevice                    = *device;
  disabledTexture                   = *texture;
  disabledDevice.enabledFeatureMask &= ~(UINT64_C(1) << GPU_FEATURE_TEXTURE_VIEW_MIN_LOD);
  disabledTexture.device            = &disabledDevice;
  view                              = NULL;

  if (GPUCreateTextureView(&disabledTexture, &viewInfo, &view) != GPU_ERROR_UNSUPPORTED || view) {
    fprintf(stderr, "lod view accepted a disabled feature\n");
    GPUDestroyTextureView(view);
    return 0;
  }

  return 1;
}

int
gpu_test_lod(GPUDevice *baseDevice, const char *bytecodePath) {
  GPUDeviceCreateInfo          deviceInfo    = {0};
  GPUTextureCreateInfo         textureInfo   = {0};
  GPUTextureWriteRegion        region        = {0};
  GPUTextureViewCreateInfo     viewInfo      = {0};
  GPUTextureViewMinLODEXT      viewLOD       = {0};
  GPUSamplerCreateInfo         samplerInfo   = {0};
  GPUSamplerLODClamp           samplerLOD    = {0};
  GPUBufferCreateInfo          bufferInfo    = {0};
  GPUComputePipelineCreateInfo pipelineInfo  = {0};
  GPUBindGroupEntry            entries[3]    = {0};
  GPUBindGroupCreateInfo       groupInfo     = {0};
  GPUQueueSubmitInfo           submitInfo    = {0};
  GPUBufferBarrier             bufferBarrier = {0};
  GPUBarrierBatch              barrierBatch  = {0};
  uint8_t                      pixels[8][256];
  float                        values[2][4];
  GPUFeature                   features[] = {GPU_FEATURE_COMPUTE, GPU_FEATURE_TEXTURE_VIEW_MIN_LOD};
  GPUCommandBuffer            *commands[1];
  GPUDevice                   *device;
  GPUQueue                    *queue;
  GPUShaderLibrary            *library  = NULL;
  GPUShaderLayout             *layout   = NULL;
  GPUComputePipeline          *pipeline = NULL;
  GPUTexture                  *texture  = NULL;
  GPUTextureView              *view     = NULL;
  GPUSampler                  *sampler  = NULL;
  GPUBuffer                   *output   = NULL;
  GPUBindGroup                *group    = NULL;
  GPUCommandBuffer            *cmdb;
  GPUComputePassEncoder       *encoder;
  GPUFence                    *fence    = NULL;
  void                        *bytecode;
  const char                  *phase    = "queue/fixture";
  uint64_t                     bytecodeSize;
  float                        lod, fraction, expected;
  GPUResult                    result;
  uint32_t                     mip, width, x, y, i, sample, component, lower, upper;
  uint32_t                     checked = 0u;
  int                          ok      = 0;
  bool                         minLOD;

  if (!baseDevice || !bytecodePath)
    return 0;

  device = baseDevice;
  minLOD = GPUIsFeatureSupported(baseDevice->adapter, GPU_FEATURE_TEXTURE_VIEW_MIN_LOD);

  if (minLOD) {
    deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.chain.structSize      = sizeof(deviceInfo);
    deviceInfo.required.pFeatures    = features;
    deviceInfo.required.featureCount = 2u;

    if (GPUCreateDevice(baseDevice->adapter, &deviceInfo, &device) != GPU_OK || !device)
      return 0;
  }

  queue    = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);

  if (!queue || !bytecode)
    goto cleanup;

  phase = "shader/layout";

  result = GPUCreateShaderLibraryFromUSL(device, bytecode, bytecodeSize, &library);

  if (result != GPU_OK) {
    fprintf(stderr, "lod shader library failed: %d\n", result);
    goto cleanup;
  }

  if (GPUCreateShaderLayout(device, library, &layout) != GPU_OK)
    goto cleanup;

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.layout           = layout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "lod_cs";

  phase = "pipeline";

  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK)
    goto cleanup;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 8u;
  textureInfo.height           = 8u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 4u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;

  phase = "texture/validation";

  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK
      || !check_lod_validation(device, texture))
    goto cleanup;

  region.depth       = 1u;
  region.layerCount  = 1u;
  region.bytesPerRow = 256u;
  phase              = "upload";

  for (mip = 0u; mip < 4u; mip++) {
    width = 8u >> mip;

    for (y = 0u; y < width; y++) {
      for (x = 0u; x < width; x++) {
        memcpy(&pixels[y][x * 4u], colors[mip], 4u);
      }
    }

    region.width        = width;
    region.height       = width;
    region.mipLevel     = mip;
    region.rowsPerImage = width;

    if (GPUQueueWriteTexture(queue, texture, &region, pixels, 256u * width) != GPU_OK)
      goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(values);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC;

  phase = "output/fence";

  if (GPUCreateBuffer(device, &bufferInfo, &output) != GPU_OK
      || GPUCreateFence(device, NULL, &fence) != GPU_OK)
    goto cleanup;

  bufferBarrier.buffer            = output;
  bufferBarrier.srcAccess         = GPU_ACCESS_SHADER_WRITE;
  bufferBarrier.dstAccess         = GPU_ACCESS_TRANSFER_READ;
  bufferBarrier.sizeBytes         = sizeof(values);
  barrierBatch.srcStages          = GPU_STAGE_COMPUTE;
  barrierBatch.dstStages          = GPU_STAGE_TRANSFER;
  barrierBatch.bufferBarrierCount = 1u;
  barrierBatch.pBufferBarriers    = &bufferBarrier;

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.chain.pNext      = &viewLOD;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.arrayLayerCount  = 1u;
  viewLOD.chain.sType       = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_MIN_LOD_EXT;
  viewLOD.chain.structSize  = sizeof(viewLOD);

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.desc.minFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.magFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_LINEAR;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerLOD.chain.sType       = GPU_STRUCTURE_TYPE_SAMPLER_LOD_CLAMP;
  samplerLOD.chain.structSize  = sizeof(samplerLOD);

  entries[0].binding       = 0u;
  entries[0].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  entries[1].binding       = 1u;
  entries[1].bindingType   = GPU_BINDING_SAMPLER;
  entries[2].binding       = 2u;
  entries[2].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[2].buffer.buffer = output;
  entries[2].buffer.size   = sizeof(values);

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = layout->bindGroupLayouts[0];
  groupInfo.entryCount       = 3u;
  groupInfo.pEntries         = entries;

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = commands;
  submitInfo.commandBufferCount = 1u;
  submitInfo.fence              = fence;

  for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
    if (!minLOD && cases[i].viewMin != 0.0f)
      continue;

    viewInfo.baseMipLevel  = cases[i].baseMip;
    viewInfo.mipLevelCount = 4u - cases[i].baseMip;
    viewLOD.minLOD         = cases[i].viewMin;

    samplerInfo.chain.pNext = cases[i].samplerClamp ? &samplerLOD : NULL;
    samplerLOD.minLOD       = cases[i].samplerMin;
    samplerLOD.maxLOD       = cases[i].samplerMax;

    phase = "view/sampler";

    if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK
        || GPUCreateSampler(device, &samplerInfo, false, &sampler) != GPU_OK)
      goto cleanup;

    entries[0].textureView = view;
    entries[1].sampler     = sampler;

    phase = "bind/encode";

    result = GPUCreateBindGroup(device, &groupInfo, &group);

    if (result != GPU_OK) {
      fprintf(stderr, "lod bind group failed: %d\n", result);
      goto cleanup;
    }

    if (GPUAcquireCommandBuffer(queue, "lod-clamp", &cmdb) != GPU_OK
        || !(encoder = GPUBeginComputePass(cmdb, "lod-clamp")))
      goto cleanup;

    GPUBindComputePipeline(encoder, pipeline);
    GPUBindComputeGroup(encoder, 0u, group, 0u, NULL);
    GPUDispatch(encoder, 1u, 1u, 1u);
    GPUEndComputePass(encoder);
    GPUEncodeBarriers(cmdb, &barrierBatch);

    commands[0] = cmdb;

    phase = "submit/readback";

    if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK
        || GPUWaitFence(fence, UINT64_MAX) != GPU_OK
        || GPUQueueReadBuffer(queue, output, 0u, values, sizeof(values)) != GPU_OK)
      goto cleanup;

    for (sample = 0u; sample < 2u; sample++) {
      lod = sample == 0u ? 0.0f : 3.0f;

      if (cases[i].samplerClamp)
        lod = fminf(fmaxf(lod, cases[i].samplerMin), cases[i].samplerMax);

      lod      = fminf(fmaxf(lod + cases[i].baseMip, cases[i].viewMin), 3.0f);
      lower    = (uint32_t)lod;
      upper    = lower < 3u ? lower + 1u : lower;
      fraction = lod - (float)lower;

      for (component = 0u; component < 4u; component++) {
        expected = ((1.0f - fraction) * colors[lower][component]
                    + fraction * colors[upper][component]) / 255.0f;

        if (!isfinite(values[sample][component])
            || fabsf(values[sample][component] - expected) > 0.01f) {
          fprintf(stderr, "lod case %u sample %u component %u: %g != %g\n",
                  i, sample, component, values[sample][component], expected);
          goto cleanup;
        }
      }
    }

    GPUDestroyBindGroup(group);
    GPUDestroySampler(sampler);
    GPUDestroyTextureView(view);
    group   = NULL;
    sampler = NULL;
    view    = NULL;
    checked++;
  }

  printf("lod: %u native sampling cases; view minLOD %s\n", checked, minLOD ? "enabled" : "unsupported");
  ok = 1;

cleanup:
  if (!ok)
    fprintf(stderr, "lod failed during %s (completed %u cases)\n", phase, checked);

  GPUDestroyBindGroup(group);
  GPUDestroySampler(sampler);
  GPUDestroyTextureView(view);
  GPUDestroyFence(fence);
  GPUDestroyBuffer(output);
  GPUDestroyTexture(texture);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(layout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);

  if (device != baseDevice)
    GPUDestroyDevice(device);

  return ok;
}
