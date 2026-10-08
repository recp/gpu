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

#include "../api/test.h"
#include "../../src/backend/dx12/common.h"

#define REQUIRE(x) \
  do { \
    if (!(x)) { \
      fprintf(stderr, "half range line %d failed\n", __LINE__); \
      ok = 0; \
      goto cleanup; \
    } \
  } while (0)

_Static_assert(sizeof(GPUBufferBindingLayout) == 16u, "buffer binding layout grew");

static const uint32_t rawOffsets[] = {0u, 2u, 4u, 8u, 12u, 16u, 20u};

static const GPUBindingType rawTypes[] = {
  GPU_BINDING_READ_ONLY_STORAGE_BUFFER, GPU_BINDING_STORAGE_BUFFER
};

static const uint32_t counts[] = {1u, 3u, 63u, 64u};

static int
check_dynamic_order(GPUDevice *device) {
  GPUBuffer                    *buffers[3]      = {0};
  GPUBufferCreateInfo           bufferInfo      = {0};
  GPUBindGroupLayoutEntry       entries[2]      = {0};
  GPUBindGroupLayoutCreateInfo  layoutInfo      = {0};
  GPUBindGroupEntry             bindings[3]     = {0};
  GPUBindGroupCreateInfo        groupInfo       = {0};
  const uint32_t                valid[]         = {112u, 48u, 16u};
  const uint32_t                invalid[]       = {16u, 112u, 48u};
  const uint32_t                bufferIndices[] = {2u, 0u, 1u};
  GPUBindGroupLayout           *layout          = NULL;
  GPUBindGroup                 *group           = NULL;
  const GPUBindGroupDX12       *native;
  const DX12DynamicBufferRange *ranges;
  int                           ok = 0;
  uint32_t                      bufferIndex;
  uint32_t                      bindingIndex;
  uint32_t                      layoutIndex;
  uint32_t                      rangeIndex;
  uint32_t                      destroyIndex;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE;

  for (bufferIndex = 0u; bufferIndex < 3u; bufferIndex++) {
    bufferInfo.sizeBytes = 128u >> bufferIndex;

    if (GPUCreateBuffer(device, &bufferInfo, &buffers[bufferIndex]) != GPU_OK)
      goto cleanup;
  }

  for (bindingIndex = 0u; bindingIndex < 3u; bindingIndex++) {
    bindings[bindingIndex].bindingType   = GPU_BINDING_STORAGE_BUFFER;
    bindings[bindingIndex].binding       = bindingIndex == 1u ? 1u : 3u;
    bindings[bindingIndex].arrayIndex    = bindingIndex == 0u ? 1u : 0u;
    bindings[bindingIndex].buffer.buffer = buffers[bufferIndices[bindingIndex]];
    bindings[bindingIndex].buffer.size   = 16u;
  }

  /* runtime entries and layout entries deliberately use different orders. */

  for (layoutIndex = 0u; layoutIndex < 2u; layoutIndex++) {
    entries[layoutIndex].bindingType      = GPU_BINDING_STORAGE_BUFFER;
    entries[layoutIndex].visibility       = GPU_SHADER_STAGE_COMPUTE_BIT;
    entries[layoutIndex].binding          = layoutIndex ? 1u : 3u;
    entries[layoutIndex].arrayCount       = layoutIndex ? 1u : 2u;
    entries[layoutIndex].hasDynamicOffset = true;
  }

  layoutInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.entryCount       = 2u;
  layoutInfo.pEntries         = entries;

  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK)
    goto cleanup;

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = layout;
  groupInfo.entryCount       = 3u;
  groupInfo.pEntries         = bindings;

  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK
      || !(native = group->_native) || native->dynamicOffsetCount != 3u)
    goto cleanup;

  ranges      = (const DX12DynamicBufferRange *)(native->descriptorOffsets + native->entryCount);

  for (rangeIndex = 0u; rangeIndex < 3u; rangeIndex++) {
    if (ranges[rangeIndex].maxOffset != valid[rangeIndex] || ranges[rangeIndex].alignmentMask != 15u
        || ranges[rangeIndex].stride != 0u)
      goto cleanup;
  }

  if (!dx12_dynamicOffsetsValid(ranges, 3u, 3u, valid)
      || dx12_dynamicOffsetsValid(ranges, 3u, 3u, invalid))
    goto cleanup;

  puts("dynamic descriptor order passed");

  ok = 1;
cleanup:
  if (!ok)
    fprintf(stderr, "dynamic descriptor order failed\n");
  GPUDestroyBindGroup(group);
  GPUDestroyBindGroupLayout(layout);
  for (destroyIndex = 0u; destroyIndex < 3u; destroyIndex++)
    GPUDestroyBuffer(buffers[destroyIndex]);
  return ok;
}

static int
check_raw_alignment(GPUDevice *device) {
  GPUBufferCreateInfo          bufferInfo  = {0};
  GPUBindGroupLayoutEntry      entry       = {0};
  GPUBindGroupLayoutCreateInfo layoutInfo  = {0};
  GPUBindGroupCreateInfo       groupInfo   = {0};
  GPUBindGroupEntry            bindings[2] = {0};
  GPUBuffer                   *buffer      = NULL;
  GPUBindGroupLayout          *layout      = NULL;
  GPUBindGroup                *group       = NULL;
  uint32_t                     checked     = 0u;
  int                          ok          = 0;
  uint32_t                     kind;
  uint32_t                     count;
  uint32_t                     i;
  GPUResult                    result;
  uint32_t                     j;
  bool                         valid;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = 32u;
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE;

  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK)
    goto cleanup;

  entry.visibility            = GPU_SHADER_STAGE_COMPUTE_BIT;
  entry.buffer.strideBytes    = 2u;
  entry.buffer.minBindingSize = 2u;
  entry.buffer.byteAddress    = true;
  layoutInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.entryCount       = 1u;
  layoutInfo.pEntries         = &entry;
  groupInfo.chain.sType       = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize  = sizeof(groupInfo);
  groupInfo.pEntries          = bindings;

  for (kind = 0u; kind < GPU_ARRAY_LEN(rawTypes); kind++) {
    entry.bindingType = rawTypes[kind];

    for (count = 1u; count <= 2u; count++) {
      entry.arrayCount = count;

      if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK)
        goto cleanup;

      groupInfo.layout     = layout;
      groupInfo.entryCount = count;

      for (i = 0u; i < GPU_ARRAY_LEN(rawOffsets); i++) {
        valid = rawOffsets[i] % 16u == 0u;

        for (j = 0u; j < count; j++) {
          bindings[j].bindingType   = rawTypes[kind];
          bindings[j].arrayIndex    = j;
          bindings[j].buffer.buffer = buffer;
          bindings[j].buffer.offset = rawOffsets[i];
          bindings[j].buffer.size   = 6u;
        }

        result = GPUCreateBindGroup(device, &groupInfo, &group);

        if (result != (valid ? GPU_OK : GPU_ERROR_UNSUPPORTED)
            || (group != NULL) != valid) {
          fprintf(stderr, "raw alignment mismatch type=%u count=%u offset=%u result=%d\n",
                  (unsigned)rawTypes[kind], count, rawOffsets[i], (int)result);
          goto cleanup;
        }

        GPUDestroyBindGroup(group);
        group = NULL;
        checked++;
      }

      GPUDestroyBindGroupLayout(layout);
      layout = NULL;
    }
  }

  printf("raw root/table alignment: %u cases passed\n", checked);

  ok = 1;
cleanup:
  GPUDestroyBindGroup(group);
  GPUDestroyBindGroupLayout(layout);
  GPUDestroyBuffer(buffer);
  return ok;
}

static int
run_range(GPUDevice          *device,
          GPUBindGroupLayout *layout,
          GPUComputePipeline *pipeline,
          uint32_t            count,
          uint32_t            offset,
          uint32_t            suffix,
          bool                placed,
          bool                root,
          bool                dynamic,
          bool                raw) {
  GPUQueue              *queue;
  GPUBuffer             *buffers[4] = {0};
  GPUHeap               *heaps[2]   = {0};
  GPUBindGroup          *group      = NULL;
  GPUCommandBuffer      *cmdb       = NULL;
  GPUComputePassEncoder *pass       = NULL;
  GPUFence              *fence      = NULL;
  GPUBufferCreateInfo    bufferInfo = {0};
  GPUHeapCreateInfo      heapInfo   = {0};
  GPUMemoryRequirements  memory     = {0};
  GPUBindGroupEntry      entries[4] = {0};
  GPUBindGroupCreateInfo groupInfo  = {0};
  GPUQueueSubmitInfo     submitInfo = {0};
  uint16_t               initial[2][80];
  uint16_t               actual[80];
  float                  output[2]         = {0};
  uint32_t               bytes;
  uint32_t               size;
  uint32_t               dynamicOffsets[2] = {offset, offset};
  int                    ok                = 0;
  uint32_t               uploadIndex;
  uint32_t               j;
  uint32_t               readbackIndex;
  uint32_t               bufferIndex;
  uint32_t               heapIndex;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  bytes = count * 2u;
  size  = offset + bytes + suffix;

  REQUIRE(queue && count > 0u && size <= sizeof(actual));
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  bufferInfo.sizeBytes        = size;

  for (uploadIndex = 0u; uploadIndex < 2u; uploadIndex++) {
    memset(initial[uploadIndex], 0x5a, size);

    for (j = 0u; j < count; j++) {
      initial[uploadIndex][offset / 2u + j] = uploadIndex ? 0x4000u : 0x3c00u;
    }

    if (placed) {
      REQUIRE(GPUGetBufferMemoryRequirements(device, &bufferInfo, &memory) == GPU_OK);
      heapInfo.chain.sType       = GPU_STRUCTURE_TYPE_HEAP_CREATE_INFO;
      heapInfo.chain.structSize  = sizeof(heapInfo);
      heapInfo.sizeBytes         = memory.sizeBytes;
      heapInfo.compatibilityMask = memory.compatibilityMask;
      REQUIRE(GPUCreateHeap(device, &heapInfo, &heaps[uploadIndex]) == GPU_OK);
      REQUIRE(GPUCreatePlacedBuffer(device, &bufferInfo, heaps[uploadIndex], 0u, &buffers[uploadIndex]) == GPU_OK);
    } else {
      REQUIRE(GPUCreateBuffer(device, &bufferInfo, &buffers[uploadIndex]) == GPU_OK);
    }

    REQUIRE(GPUQueueWriteBuffer(queue, buffers[uploadIndex], 0u, initial[uploadIndex], size) == GPU_OK);
    entries[uploadIndex].binding       = root ? uploadIndex : 0u;
    entries[uploadIndex].arrayIndex    = root ? 0u : uploadIndex;
    entries[uploadIndex].bindingType   = root && uploadIndex == 0u
                                           ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                           : GPU_BINDING_STORAGE_BUFFER;
    entries[uploadIndex].buffer.buffer = buffers[uploadIndex];
    entries[uploadIndex].buffer.offset = dynamic ? 0u : offset;
    entries[uploadIndex].buffer.size   = bytes;
  }

  bufferInfo.usage     = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  bufferInfo.sizeBytes = sizeof(count);
  REQUIRE(GPUCreateBuffer(device, &bufferInfo, &buffers[2]) == GPU_OK);
  REQUIRE(GPUQueueWriteBuffer(queue, buffers[2], 0u, &count, sizeof(count)) == GPU_OK);
  entries[2].binding       = 2u;
  entries[2].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  entries[2].buffer.buffer = buffers[2];
  entries[2].buffer.size   = sizeof(count);

  bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  bufferInfo.sizeBytes = sizeof(output);
  REQUIRE(GPUCreateBuffer(device, &bufferInfo, &buffers[3]) == GPU_OK);
  REQUIRE(GPUQueueWriteBuffer(queue, buffers[3], 0u, output, sizeof(output)) == GPU_OK);
  entries[3].binding       = 3u;
  entries[3].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[3].buffer.buffer = buffers[3];
  entries[3].buffer.size   = sizeof(output);

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = layout;
  groupInfo.entryCount       = 4u;
  groupInfo.pEntries         = entries;
  REQUIRE(GPUCreateBindGroup(device, &groupInfo, &group) == GPU_OK);
  REQUIRE(GPUAcquireCommandBuffer(queue, "half-buffer-range", &cmdb) == GPU_OK);
  REQUIRE((pass = GPUBeginComputePass(cmdb, "half-buffer-range")) != NULL);
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass,
                      0u,
                      group,
                      dynamic ? 2u : 0u,
                      dynamic ? dynamicOffsets : NULL);
  REQUIRE(pass->_boundGroups[0] == group);

  if (dynamic) {
    REQUIRE(pass->_boundDynamicOffsets[0][0] == offset && pass->_boundDynamicOffsets[0][1] == offset);

    if (raw && suffix >= 16u) {
      /* reject the second native offset after a different, valid first one.
         a failed bind must not leave partially updated native root state. */
      uint32_t invalidOffsets[2] = {offset + 16u, offset + 2u};

      GPUBindComputeGroup(pass, 0u, group, 2u, invalidOffsets);
      REQUIRE(pass->_boundDynamicOffsets[0][0] == offset && pass->_boundDynamicOffsets[0][1] == offset);
      GPUBindComputeGroup(pass, 0u, group, 2u, dynamicOffsets);
    }
  }

  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;
  REQUIRE(GPUCreateFence(device, NULL, &fence) == GPU_OK);
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  REQUIRE(GPUQueueSubmit(queue, &submitInfo) == GPU_OK);
  cmdb = NULL;
  REQUIRE(GPUWaitFence(fence, UINT64_C(30000000000)) == GPU_OK);
  REQUIRE(GPUQueueReadBuffer(queue, buffers[3], 0u, output, sizeof(output)) == GPU_OK);
  REQUIRE(GPUQueueReadBuffer(queue, buffers[0], size, actual, 1u) == GPU_ERROR_INVALID_ARGUMENT);
  REQUIRE(GPUQueueWriteBuffer(queue, buffers[0], size, actual, 1u) == GPU_ERROR_INVALID_ARGUMENT);

  ok = 1;

  if (output[0] != 1.0f || output[1] != 2.0f) {
    fprintf(stderr, "half read mismatch count=%u offset=%u suffix=%u: %.9g %.9g\n",
            count, offset, suffix, output[0], output[1]);
    ok = 0;
  }

  for (readbackIndex = 0u; readbackIndex < 2u; readbackIndex++) {
    if (!root || readbackIndex != 0u) {
      initial[readbackIndex][offset / 2u + count - 1u] = readbackIndex ? 0x4080u : 0x3e00u;
    }

    REQUIRE(GPUQueueReadBuffer(queue, buffers[readbackIndex], 0u, actual, size) == GPU_OK);

    if (memcmp(initial[readbackIndex], actual, size)) {
      fprintf(stderr, "half write/guard mismatch count=%u offset=%u suffix=%u buffer=%u\n",
              count, offset, suffix, readbackIndex);
      ok = 0;
    }
  }

cleanup:
  if (pass)
    GPUEndComputePass(pass);
  if (cmdb)
    GPUDiscardCommandBuffer(cmdb);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  for (bufferIndex = 0u; bufferIndex < 4u; bufferIndex++)
    GPUDestroyBuffer(buffers[bufferIndex]);
  for (heapIndex = 0u; heapIndex < 2u; heapIndex++)
    GPUDestroyHeap(heaps[heapIndex]);
  return ok;
#undef REQUIRE
}

int
main(int argc, char **argv) {
  GPUInstanceCreateInfo          instanceInfo = {0};
  GPUDeviceCreateInfo            deviceInfo   = {0};
  GPUShaderLibraryCreateInfo     libraryInfo  = {0};
  GPUComputePipelineCreateInfo   pipelineInfo = {0};
  GPUBindGroupLayoutCreateInfo   groupInfo    = {0};
  GPUPipelineLayoutCreateInfo    layoutInfo   = {0};
  GPUFeature                     features[3]  = {GPU_FEATURE_DESCRIPTOR_INDEXING};
  GPUInstance                   *instance     = NULL;
  GPUAdapter                    *adapter      = NULL;
  GPUDevice                     *device       = NULL;
  GPUShaderLibrary              *library      = NULL;
  GPUShaderLayout               *layout       = NULL;
  GPUComputePipeline            *pipeline     = NULL;
  GPUBindGroupLayout            *clonedGroup  = NULL;
  GPUPipelineLayout             *clonedLayout = NULL;
  const GPUBindGroupLayoutEntry *entries;
  void                          *artifact     = NULL;
  uint64_t                       artifactSize = 0u;
  uint32_t                       passed       = 0u;
  uint32_t                       failed       = 0u;
  uint32_t                       entryCount;
  uint32_t                       featureCount = 1u;
  int                            result       = 1;
  int                            optionIndex;
  uint32_t                       featureIndex;
  uint32_t                       entryIndex;
  uint32_t                       countIndex;
  uint32_t                       offset;
  uint32_t                       suffix;
  int                            ok;
  bool                           native16 = false;
  bool                           placed   = false;
  bool                           expectedRaw;
  bool                           root    = false;
  bool                           dynamic = false;
  bool                           hlsl    = false;

  if (argc < 2 || argc > 6)
    return 2;

  for (optionIndex = 2; optionIndex < argc; optionIndex++) {
    if (strcmp(argv[optionIndex], "native16") == 0 && !native16)
      native16 = true;
    else if (strcmp(argv[optionIndex], "hlsl") == 0 && !native16) {
      native16 = true;
      hlsl     = true;
    } else if (strcmp(argv[optionIndex], "placed") == 0 && !placed)
      placed = true;
    else if (strcmp(argv[optionIndex], "root") == 0 && !root)
      root = true;
    else if (strcmp(argv[optionIndex], "dynamic") == 0 && !dynamic)
      dynamic = true;
    else
      return 2;
  }

  if (root && !dynamic)
    return 2;

  expectedRaw = !native16 || (root && hlsl);

  if (native16)
    features[featureCount++] = GPU_FEATURE_SHADER_F16;

  if (placed)
    features[featureCount++] = GPU_FEATURE_PLACED_RESOURCES;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;

  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK
      || gpu_test_request_adapter(instance, &adapter) != GPU_OK)
    goto cleanup;

  for (featureIndex = 0u; featureIndex < featureCount; featureIndex++) {
    if (!GPUIsFeatureSupported(adapter, features[featureIndex])) {
      result = 77;
      goto cleanup;
    }
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = features;
  deviceInfo.required.featureCount = featureCount;

  if (gpu_test_create_device(adapter, &deviceInfo, &device) != GPU_OK)
    goto cleanup;

  if (!check_dynamic_order(device))
    goto cleanup;

  if (!check_raw_alignment(device))
    goto cleanup;

  if (!(artifact = gpu_test_read_file(argv[1], &artifactSize)))
    goto cleanup;

  libraryInfo.chain.sType      = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  libraryInfo.chain.structSize = sizeof(libraryInfo);
  libraryInfo.sourceKind       = GPU_SHADER_SOURCE_USL_BYTECODE;
  libraryInfo.sourceData       = artifact;
  libraryInfo.sourceSize       = artifactSize;
  libraryInfo.disableDiskCache = true;

  if (GPUCreateShaderLibrary(device, &libraryInfo, &library) != GPU_OK
      || GPUCreateShaderLayout(device, library, &layout) != GPU_OK
      || !layout || layout->bindGroupLayoutCount != 1u
      || !layout->bindGroupLayouts || !layout->bindGroupLayouts[0]
      || !layout->pipelineLayout)
    goto cleanup;

  if (!(entries = GPUGetBindGroupLayoutEntries(layout->bindGroupLayouts[0],
                                               &entryCount)) || entryCount != (root ? 4u : 3u))
    goto cleanup;

  for (entryIndex = 0u; entryIndex < (root ? 2u : 1u); entryIndex++) {
    if (entries[entryIndex].binding != entryIndex || entries[entryIndex].arrayCount != (root ? 1u : 2u)
        || entries[entryIndex].buffer.strideBytes != 2u || entries[entryIndex].buffer.minBindingSize != 2u
        || entries[entryIndex].buffer.byteAddress != expectedRaw
        || entries[entryIndex].hasDynamicOffset != dynamic
        || entries[entryIndex].bindingType != (root && entryIndex == 0u
                                                 ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                                 : GPU_BINDING_STORAGE_BUFFER))
      goto cleanup;
  }

  printf("half reflected stride=%u minimum=%llu descriptors=%u\n", entries[0].buffer.strideBytes,
         (unsigned long long)entries[0].buffer.minBindingSize, entries[0].arrayCount);

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.layout           = layout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "half_buffer_cs";
  groupInfo.chain.sType         = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  groupInfo.chain.structSize    = sizeof(groupInfo);
  groupInfo.entryCount          = entryCount;
  groupInfo.pEntries            = entries;

  if (GPUCreateBindGroupLayout(device, &groupInfo, &clonedGroup) != GPU_OK)
    goto cleanup;

  layoutInfo.chain.sType          = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize     = sizeof(layoutInfo);
  layoutInfo.bindGroupLayoutCount = 1u;
  layoutInfo.ppBindGroupLayouts   = &clonedGroup;

  if (GPUCreatePipelineLayout(device, &layoutInfo, &clonedLayout) != GPU_OK)
    goto cleanup;

  pipelineInfo.layout = clonedLayout;

  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK)
    goto cleanup;

  for (countIndex = 0u; countIndex < GPU_ARRAY_LEN(counts); countIndex++) {
    for (offset = 0u; offset <= 16u; offset += 16u) {
      for (suffix = 0u; suffix <= 16u; suffix += 16u) {
        ok = run_range(device,
                       clonedGroup,
                       pipeline,
                       counts[countIndex],
                       offset,
                       suffix,
                       placed,
                       root,
                       dynamic,
                       expectedRaw);
        printf("%s count=%u offset=%u suffix=%u placed=%u root=%u dynamic=%u\n",
               ok ? "PASS" : "FAIL", counts[countIndex], offset, suffix,
               (unsigned)placed, (unsigned)root, (unsigned)dynamic);
        passed += ok; failed += !ok;
      }
    }
  }

  printf("passed=%u failed=%u\n", passed, failed);
  result = failed || !passed;
cleanup:
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyPipelineLayout(clonedLayout);
  GPUDestroyBindGroupLayout(clonedGroup);
  GPUDestroyShaderLayout(layout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);
  return result;
}
