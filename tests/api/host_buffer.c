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
#include "../../src/api/buffer_internal.h"

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

static int
expect_host_failure(GPUDevice                 *device,
                    const GPUBufferCreateInfo *info,
                    GPUResult                  expected) {
  GPUBuffer *buffer;
  GPUResult  result;

  buffer = (GPUBuffer *)(uintptr_t)1u;
  result = GPUCreateBuffer(device, info, &buffer);

  if (result != expected || buffer) {
    fprintf(stderr, "host buffer: expected=%d actual=%d output=%p\n", expected, result, (void *)buffer);

    if (result == GPU_OK)
      GPUDestroyBuffer(buffer);

    return 0;
  }

  return 1;
}

static int
read_host_alias(GPUQueue *queue, GPUBuffer *buffer, uint8_t *bytes) {
  uint8_t      saved[64];
  uint8_t      expected[64];
  const size_t sources[]      = {0u, 4u, 8u, 0u};
  const size_t destinations[] = {4u, 0u, 8u, 40u};
  const size_t sizes[]        = {32u, 32u, 16u, 16u};
  size_t       i;
  uint32_t     caseIndex;
  int          ok = 1;

  memcpy(saved, bytes, sizeof(saved));

  for (caseIndex = 0u; caseIndex < GPU_ARRAY_LEN(sources); caseIndex++) {
    for (i = 0u; i < sizeof(expected); i++) {
      bytes[i] = (uint8_t)(i * 17u + caseIndex * 31u);
    }

    /* snapshot the source before the overlapping destination is written. */
    memcpy(expected, bytes, sizeof(expected));
    memcpy(expected + destinations[caseIndex], bytes + sources[caseIndex], sizes[caseIndex]);

    if (GPUQueueReadBuffer(queue,
                           buffer,
                           sources[caseIndex],
                           bytes + destinations[caseIndex],
                           sizes[caseIndex]) != GPU_OK
        || memcmp(bytes, expected, sizeof(expected)) != 0) {
      fprintf(stderr, "host buffer alias read failed: case=%u\n", caseIndex);
      ok = 0;
      break;
    }
  }

  memcpy(bytes, saved, sizeof(saved));

  return ok;
}

static int
invalid_host_buffers(GPUDevice *device, GPUBufferCreateInfo *info, size_t page) {
  GPUMemoryRequirements       requirements;
  GPUSparseBufferRequirements sparse;
  GPUBufferHostMemoryEXT      original;
  GPUBufferHostMemoryEXT      duplicate;
  GPUBufferHostMemoryEXT     *host;
  uint64_t                   size;
  uint32_t                   i;

  host      = (GPUBufferHostMemoryEXT *)info->chain.pNext;
  original  = *host;
  duplicate = original;
  size      = info->sizeBytes;

  for (i = 0u; i < 12u; i++) {
    *host           = original;
    info->sizeBytes = size;

    switch (i) {
      case 0: host->chain.structSize = 0u; break;
      case 1: host->chain.structSize = sizeof(*host) - 1u; break;
      case 2: host->chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS; break;
      case 3: host->chain.pNext = host; break;
      case 4: host->pData = NULL; break;
      case 5: host->allocationSize = 0u; break;
      case 6: host->allocationSize = page - 1u; break;
      case 7: host->pData = (uint8_t *)original.pData + 1u; break;
      case 8: info->sizeBytes = page + 1u; break;
      case 9: host->allocationSize = UINT64_MAX; break;
      case 10: host->pData = (void *)(UINTPTR_MAX - page + 1u); break;
      case 11: host->chain.pNext = &duplicate; break;
    }

    if (!expect_host_failure(device, info, GPU_ERROR_INVALID_ARGUMENT)) {
      fprintf(stderr, "host buffer negative case=%u\n", i);
      return 0;
    }
  }

  *host           = original;
  info->sizeBytes = size;

  memset(&requirements, 0xff, sizeof(requirements));
  memset(&sparse, 0xff, sizeof(sparse));

  if (GPUGetBufferMemoryRequirements(device, info, &requirements) != GPU_ERROR_INVALID_ARGUMENT
      || requirements.sizeBytes != 0u || requirements.alignmentBytes != 0u
      || GPUGetSparseBufferRequirements(device, info, &sparse) != GPU_ERROR_INVALID_ARGUMENT
      || sparse.pageSizeBytes != 0u || sparse.tileCount != 0u) {
    return 0;
  }

  return 1;
}

size_t
gpu_test_host_page_size(void) {
#if defined(_WIN32)
  SYSTEM_INFO info;

  GetSystemInfo(&info);
  return info.dwPageSize;
#else
  long size;

  size = sysconf(_SC_PAGESIZE);
  return size > 0 ? (size_t)size : 0u;
#endif
}

int
gpu_test_host_buffer(GPUDevice *baseDevice) {
  GPUBufferHostMemoryEXT host        = {0};
  GPUBufferCreateInfo    info        = {0};
  GPUDeviceCreateInfo    deviceInfo  = {0};
  GPUDeviceCapabilities caps;
  const GPUFeature      features[]  = {GPU_FEATURE_BUFFER_HOST_MEMORY_EXT};
  uint32_t              source[]    = {2u, 4u, 6u, 8u};
  uint32_t              readback[4]  = {0};
  GPUDevice            *device      = NULL;
  GPUBuffer            *buffer      = NULL;
  GPUBuffer            *alias       = NULL;
  GPUQueue             *queue;
  uint8_t              *allocation  = NULL;
  uint8_t              *bytes;
  size_t                page;
  size_t                i;
  GPUResult             result;
  int                   ok = 0;

  host.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_HOST_MEMORY_EXT;
  host.chain.structSize = sizeof(host);
  host.pData            = source;
  host.allocationSize   = sizeof(source);

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.chain.pNext      = &host;
  info.sizeBytes        = sizeof(source);
  info.usage            = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_STORAGE;

  if (!expect_host_failure(baseDevice, &info, GPU_ERROR_UNSUPPORTED))
    return 0;

  deviceInfo.required.pFeatures    = features;
  deviceInfo.required.featureCount = GPU_ARRAY_LEN(features);
  result = gpu_test_create_device(baseDevice->adapter, &deviceInfo, &device);

  if (!GPUIsFeatureSupported(baseDevice->adapter, GPU_FEATURE_BUFFER_HOST_MEMORY_EXT)) {
    GPUDestroyDevice(device);
    return result == GPU_ERROR_UNSUPPORTED;
  }

  if (result != GPU_OK || !device
      || !GPUIsFeatureEnabled(device, GPU_FEATURE_BUFFER_HOST_MEMORY_EXT)
      || GPUGetDeviceCapabilities(device, &caps) != GPU_OK) {
    goto cleanup;
  }

  for (i = 0u; i < caps.enabled.featureCount; i++) {
    if (caps.enabled.pFeatures[i] == GPU_FEATURE_BUFFER_HOST_MEMORY_EXT)
      break;
  }

  if (i == caps.enabled.featureCount)
    goto cleanup;

  page = gpu_test_host_page_size();

  if (page == 0u || page > SIZE_MAX / 3u || !(allocation = malloc(page * 3u)))
    goto cleanup;

  bytes = (uint8_t *)(((uintptr_t)allocation + page - 1u) / page * page);
  memset(bytes, 0x5a, page);
  host.pData          = bytes;
  host.allocationSize = page;
  info.sizeBytes      = 64u;

  if (!invalid_host_buffers(device, &info, page)
      || GPUCreateBuffer(device, &info, &buffer) != GPU_OK || !buffer
      || GPUCreateBuffer(device, &info, &alias) != GPU_OK || !alias
      || buffer->_hostMemory != bytes || alias->_hostMemory != bytes
      || !buffer->_hostImported || buffer->sizeBytes != 64u
      || buffer->_allocationSize != page) {
    goto cleanup;
  }

  /* creation borrows storage, not the caller's descriptor. */
  host.pData          = NULL;
  host.allocationSize = 0u;
  queue               = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);

  if (!queue
      || GPUQueueWriteBuffer(queue, buffer, 0u, source, sizeof(source)) != GPU_OK
      || GPUQueueReadBuffer(queue, alias, 0u, readback, sizeof(readback)) != GPU_OK
      || memcmp(source, readback, sizeof(source)) != 0
      || memcmp(source, bytes, sizeof(source)) != 0
      || GPUQueueReadBuffer(queue, buffer, 64u, readback, sizeof(readback)) != GPU_ERROR_INVALID_ARGUMENT
      || GPUQueueWriteBuffer(queue, buffer, 60u, source, sizeof(source)) != GPU_ERROR_INVALID_ARGUMENT) {
    goto cleanup;
  }

  if (!read_host_alias(queue, alias, bytes))
    goto cleanup;

  GPUDestroyBuffer(alias);
  GPUDestroyBuffer(buffer);
  alias  = NULL;
  buffer = NULL;

  /* wrapper destruction must leave the caller's allocation untouched. */
  ok = memcmp(source, bytes, sizeof(source)) == 0;

  for (i = sizeof(source); i < page; i++) {
    ok = bytes[i] == 0x5a && ok;
  }

  printf("host buffer: two aliases, four alias reads,12 rejects, logical bounds and borrowed lifetime=%s\n",
         ok ? "pass" : "fail");

cleanup:
  GPUDestroyBuffer(alias);
  GPUDestroyBuffer(buffer);
  GPUDestroyDevice(device);
  free(allocation);
  return ok;
}
