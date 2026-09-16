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

_Static_assert(sizeof(GPUBufferBindingLayout) == 16u, "buffer binding layout grew");

static int
check_dynamic_order(GPUDevice *device) {
  GPUBuffer                    *buffers[3] = {0};
  GPUBindGroupLayout            *layout = NULL;
  GPUBindGroup                  *group = NULL;
  const GPUBindGroupDX12        *native;
  const DX12DynamicBufferRange  *ranges;
  GPUBufferCreateInfo            bufferInfo = {0};
  GPUBindGroupLayoutEntry         entries[2] = {0};
  GPUBindGroupLayoutCreateInfo    layoutInfo = {0};
  GPUBindGroupEntry               bindings[3] = {0};
  GPUBindGroupCreateInfo          groupInfo = {0};
  const uint32_t                 valid[] = {112u, 48u, 16u};
  const uint32_t                 invalid[] = {16u, 112u, 48u};
  const uint32_t                 bufferIndices[] = {2u, 0u, 1u};
  int                            ok = 0;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE;
  for (uint32_t i = 0u; i < 3u; i++) {
    bufferInfo.sizeBytes = 128u >> i;
    if (GPUCreateBuffer(device, &bufferInfo, &buffers[i]) != GPU_OK) goto cleanup;
  }
  for (uint32_t i = 0u; i < 3u; i++) {
    bindings[i].bindingType   = GPU_BINDING_STORAGE_BUFFER;
    bindings[i].binding       = i == 1u ? 1u : 3u;
    bindings[i].arrayIndex    = i == 0u ? 1u : 0u;
    bindings[i].buffer.buffer = buffers[bufferIndices[i]];
    bindings[i].buffer.size   = 16u;
  }
  /* Runtime entries and layout entries deliberately use different orders. */
  for (uint32_t i = 0u; i < 2u; i++) {
    entries[i].bindingType      = GPU_BINDING_STORAGE_BUFFER;
    entries[i].visibility       = GPU_SHADER_STAGE_COMPUTE_BIT;
    entries[i].binding          = i ? 1u : 3u;
    entries[i].arrayCount       = i ? 1u : 2u;
    entries[i].hasDynamicOffset = true;
  }
  layoutInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.entryCount       = 2u;
  layoutInfo.pEntries         = entries;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK) goto cleanup;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = layout;
  groupInfo.entryCount       = 3u;
  groupInfo.pEntries         = bindings;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK ||
      !(native = group->_native) || native->dynamicOffsetCount != 3u) goto cleanup;
  ranges = (const DX12DynamicBufferRange *)(native->descriptorOffsets + native->entryCount);
  for (uint32_t i = 0u; i < 3u; i++) {
    if (ranges[i].maxOffset != valid[i] || ranges[i].alignmentMask != 15u ||
        ranges[i].stride != 0u) goto cleanup;
  }
  if (!dx12_dynamicOffsetsValid(ranges, 3u, 3u, valid) ||
      dx12_dynamicOffsetsValid(ranges, 3u, 3u, invalid)) goto cleanup;
  puts("dynamic descriptor order passed");
  ok = 1;
cleanup:
  if (!ok) fprintf(stderr, "dynamic descriptor order failed\n");
  GPUDestroyBindGroup(group);
  GPUDestroyBindGroupLayout(layout);
  for (uint32_t i = 0u; i < 3u; i++) GPUDestroyBuffer(buffers[i]);
  return ok;
}

static int
check_raw_alignment(GPUDevice *device) {
  static const uint32_t offsets[] = {0u, 2u, 4u, 8u, 12u, 16u, 20u};
  static const GPUBindingType types[] = {
    GPU_BINDING_READ_ONLY_STORAGE_BUFFER, GPU_BINDING_STORAGE_BUFFER
  };
  GPUBuffer                    *buffer = NULL;
  GPUBindGroupLayout            *layout = NULL;
  GPUBindGroup                  *group = NULL;
  GPUBufferCreateInfo            bufferInfo = {0};
  GPUBindGroupLayoutEntry         entry = {0};
  GPUBindGroupLayoutCreateInfo    layoutInfo = {0};
  GPUBindGroupCreateInfo          groupInfo = {0};
  GPUBindGroupEntry               bindings[2] = {0};
  uint32_t                       checked = 0u;
  int                            ok = 0;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = 32u;
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK) goto cleanup;
  entry.visibility           = GPU_SHADER_STAGE_COMPUTE_BIT;
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
  for (uint32_t kind = 0u; kind < GPU_ARRAY_LEN(types); kind++) {
    entry.bindingType = types[kind];
    for (uint32_t count = 1u; count <= 2u; count++) {
      entry.arrayCount = count;
      if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK) goto cleanup;
      groupInfo.layout     = layout;
      groupInfo.entryCount = count;
      for (uint32_t i = 0u; i < GPU_ARRAY_LEN(offsets); i++) {
        GPUResult result;
        bool      valid = offsets[i] % 16u == 0u;

        for (uint32_t j = 0u; j < count; j++) {
          bindings[j].bindingType   = types[kind];
          bindings[j].arrayIndex    = j;
          bindings[j].buffer.buffer = buffer;
          bindings[j].buffer.offset = offsets[i];
          bindings[j].buffer.size   = 6u;
        }
        result = GPUCreateBindGroup(device, &groupInfo, &group);
        if (result != (valid ? GPU_OK : GPU_ERROR_UNSUPPORTED) ||
            (group != NULL) != valid) {
          fprintf(stderr, "raw alignment mismatch type=%u count=%u offset=%u result=%d\n",
                  (unsigned)types[kind], count, offsets[i], (int)result);
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
run_range(GPUDevice *device, GPUBindGroupLayout *layout,
           GPUComputePipeline *pipeline, uint32_t count,
           uint32_t offset, uint32_t suffix,
           bool placed, bool root, bool dynamic, bool raw) {
  GPUQueue              *queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  GPUBuffer             *buffers[4] = {0};
  GPUHeap               *heaps[2] = {0};
  GPUBindGroup          *group = NULL;
  GPUCommandBuffer      *cmdb = NULL;
  GPUComputePassEncoder *pass = NULL;
  GPUFence              *fence = NULL;
  GPUBufferCreateInfo    bufferInfo = {0};
  GPUHeapCreateInfo      heapInfo = {0};
  GPUMemoryRequirements  memory = {0};
  GPUBindGroupEntry      entries[4] = {0};
  GPUBindGroupCreateInfo groupInfo = {0};
  GPUQueueSubmitInfo     submitInfo = {0};
  uint16_t              initial[2][80], actual[80];
  float                 output[2] = {0};
  uint32_t              bytes = count * 2u, size = offset + bytes + suffix;
  uint32_t              dynamicOffsets[2] = {offset, offset};
  int                   ok = 0;

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "half range line %d failed\n", __LINE__); ok = 0; goto cleanup; } } while (0)
  REQUIRE(queue && count > 0u && size <= sizeof(actual));
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  bufferInfo.sizeBytes        = size;
  for (uint32_t i = 0u; i < 2u; i++) {
    memset(initial[i], 0x5a, size);
    for (uint32_t j = 0u; j < count; j++) initial[i][offset / 2u + j] = i ? 0x4000u : 0x3c00u;
    if (placed) {
      REQUIRE(GPUGetBufferMemoryRequirements(device, &bufferInfo, &memory) == GPU_OK);
      heapInfo.chain.sType      = GPU_STRUCTURE_TYPE_HEAP_CREATE_INFO;
      heapInfo.chain.structSize = sizeof(heapInfo);
      heapInfo.sizeBytes         = memory.sizeBytes;
      heapInfo.compatibilityMask = memory.compatibilityMask;
      REQUIRE(GPUCreateHeap(device, &heapInfo, &heaps[i]) == GPU_OK);
      REQUIRE(GPUCreatePlacedBuffer(device, &bufferInfo, heaps[i], 0u, &buffers[i]) == GPU_OK);
    } else {
      REQUIRE(GPUCreateBuffer(device, &bufferInfo, &buffers[i]) == GPU_OK);
    }
    REQUIRE(GPUQueueWriteBuffer(queue, buffers[i], 0u, initial[i], size) == GPU_OK);
    entries[i].binding       = root ? i : 0u;
    entries[i].arrayIndex    = root ? 0u : i;
    entries[i].bindingType   = root && i == 0u
                                ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                : GPU_BINDING_STORAGE_BUFFER;
    entries[i].buffer.buffer = buffers[i];
    entries[i].buffer.offset = dynamic ? 0u : offset;
    entries[i].buffer.size   = bytes;
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
  GPUBindComputeGroup(pass, 0u, group, dynamic ? 2u : 0u,
                      dynamic ? dynamicOffsets : NULL);
  REQUIRE(pass->_boundGroups[0] == group);
  if (dynamic) {
    REQUIRE(pass->_boundDynamicOffsets[0][0] == offset &&
            pass->_boundDynamicOffsets[0][1] == offset);
    if (raw && suffix >= 16u) {
      /* Reject the second native offset after a different, valid first one.
         A failed bind must not leave partially updated native root state. */
      uint32_t invalidOffsets[2] = {offset + 16u, offset + 2u};

      GPUBindComputeGroup(pass, 0u, group, 2u, invalidOffsets);
      REQUIRE(pass->_boundDynamicOffsets[0][0] == offset &&
              pass->_boundDynamicOffsets[0][1] == offset);
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
  for (uint32_t i = 0u; i < 2u; i++) {
    if (!root || i != 0u) {
      initial[i][offset / 2u + count - 1u] = i ? 0x4080u : 0x3e00u;
    }
    REQUIRE(GPUQueueReadBuffer(queue, buffers[i], 0u, actual, size) == GPU_OK);
    if (memcmp(initial[i], actual, size)) {
      fprintf(stderr, "half write/guard mismatch count=%u offset=%u suffix=%u buffer=%u\n",
              count, offset, suffix, i);
      ok = 0;
    }
  }
cleanup:
  if (pass) GPUEndComputePass(pass);
  if (cmdb) GPUDiscardCommandBuffer(cmdb);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  for (uint32_t i = 0u; i < 4u; i++) GPUDestroyBuffer(buffers[i]);
  for (uint32_t i = 0u; i < 2u; i++) GPUDestroyHeap(heaps[i]);
  return ok;
#undef REQUIRE
}

int
main(int argc, char **argv) {
  static const uint32_t  counts[] = {1u, 3u, 63u, 64u};
  GPUInstance          *instance = NULL;
  GPUAdapter           *adapter = NULL;
  GPUDevice            *device = NULL;
  GPUShaderLibrary     *library = NULL;
  GPUShaderLayout      *layout = NULL;
  GPUComputePipeline   *pipeline = NULL;
  GPUBindGroupLayout   *clonedGroup = NULL;
  GPUPipelineLayout    *clonedLayout = NULL;
  const GPUBindGroupLayoutEntry *entries;
  void                 *artifact = NULL;
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUDeviceCreateInfo   deviceInfo = {0};
  GPUShaderLibraryCreateInfo libraryInfo = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBindGroupLayoutCreateInfo groupInfo = {0};
  GPUPipelineLayoutCreateInfo layoutInfo = {0};
  GPUFeature            features[3] = {GPU_FEATURE_DESCRIPTOR_INDEXING};
  uint64_t              artifactSize = 0u;
  uint32_t              passed = 0u, failed = 0u, entryCount, featureCount = 1u;
  int                   result = 1;
  bool                  native16 = false, placed = false, expectedRaw;
  bool                  root = false, dynamic = false, hlsl = false;

  if (argc < 2 || argc > 6) return 2;
  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "native16") == 0 && !native16) native16 = true;
    else if (strcmp(argv[i], "hlsl") == 0 && !native16) { native16 = true; hlsl = true; }
    else if (strcmp(argv[i], "placed") == 0 && !placed) placed = true;
    else if (strcmp(argv[i], "root") == 0 && !root) root = true;
    else if (strcmp(argv[i], "dynamic") == 0 && !dynamic) dynamic = true;
    else return 2;
  }
  if (root && !dynamic) return 2;
  expectedRaw = !native16 || (root && hlsl);
  if (native16) features[featureCount++] = GPU_FEATURE_SHADER_F16;
  if (placed) features[featureCount++] = GPU_FEATURE_PLACED_RESOURCES;
  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK ||
      gpu_test_request_adapter(instance, &adapter) != GPU_OK) goto cleanup;
  for (uint32_t i = 0u; i < featureCount; i++) {
    if (!GPUIsFeatureSupported(adapter, features[i])) { result = 77; goto cleanup; }
  }
  deviceInfo.chain.sType      = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize = sizeof(deviceInfo);
  deviceInfo.required.pFeatures = features;
  deviceInfo.required.featureCount = featureCount;
  if (gpu_test_create_device(adapter, &deviceInfo, &device) != GPU_OK) goto cleanup;
  if (!check_dynamic_order(device)) goto cleanup;
  if (!check_raw_alignment(device)) goto cleanup;
  artifact = gpu_test_read_file(argv[1], &artifactSize);
  if (!artifact) goto cleanup;
  libraryInfo.chain.sType      = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  libraryInfo.chain.structSize = sizeof(libraryInfo);
  libraryInfo.sourceKind      = GPU_SHADER_SOURCE_USL_BYTECODE;
  libraryInfo.sourceData      = artifact;
  libraryInfo.sourceSize      = artifactSize;
  libraryInfo.disableDiskCache = true;
  if (GPUCreateShaderLibrary(device, &libraryInfo, &library) != GPU_OK ||
      GPUCreateShaderLayout(device, library, &layout) != GPU_OK ||
      !layout || layout->bindGroupLayoutCount != 1u ||
      !layout->bindGroupLayouts || !layout->bindGroupLayouts[0] ||
      !layout->pipelineLayout) goto cleanup;
  entries = GPUGetBindGroupLayoutEntries(layout->bindGroupLayouts[0], &entryCount);
  if (!entries || entryCount != (root ? 4u : 3u)) goto cleanup;
  for (uint32_t i = 0u; i < (root ? 2u : 1u); i++) {
    if (entries[i].binding != i || entries[i].arrayCount != (root ? 1u : 2u) ||
        entries[i].buffer.strideBytes != 2u || entries[i].buffer.minBindingSize != 2u ||
        entries[i].buffer.byteAddress != expectedRaw ||
        entries[i].hasDynamicOffset != dynamic ||
        entries[i].bindingType != (root && i == 0u
                                    ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                    : GPU_BINDING_STORAGE_BUFFER)) goto cleanup;
  }
  printf("half reflected stride=%u minimum=%llu descriptors=%u\n", entries[0].buffer.strideBytes,
         (unsigned long long)entries[0].buffer.minBindingSize, entries[0].arrayCount);
  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.layout           = layout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "half_buffer_cs";
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.entryCount       = entryCount;
  groupInfo.pEntries         = entries;
  if (GPUCreateBindGroupLayout(device, &groupInfo, &clonedGroup) != GPU_OK) goto cleanup;
  layoutInfo.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.bindGroupLayoutCount = 1u;
  layoutInfo.ppBindGroupLayouts   = &clonedGroup;
  if (GPUCreatePipelineLayout(device, &layoutInfo, &clonedLayout) != GPU_OK) goto cleanup;
  pipelineInfo.layout = clonedLayout;
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK) goto cleanup;
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(counts); i++) {
    for (uint32_t offset = 0u; offset <= 16u; offset += 16u) {
      for (uint32_t suffix = 0u; suffix <= 16u; suffix += 16u) {
        int ok = run_range(device, clonedGroup, pipeline, counts[i], offset, suffix,
                           placed, root, dynamic, expectedRaw);
        printf("%s count=%u offset=%u suffix=%u placed=%u root=%u dynamic=%u\n",
               ok ? "PASS" : "FAIL", counts[i], offset, suffix,
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
