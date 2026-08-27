#include "api/device_internal.h"

#include <stdio.h>
#include <string.h>

static GPUAdapter adapter;
static uint32_t   enumerationCalls;

static GPUAdapter *
enumerate_adapters(GPUInstance * __restrict instance, uint32_t maxCount) {
  enumerationCalls++;
  if (enumerationCalls == 1u || maxCount == 0u) {
    return NULL;
  }

  memset(&adapter, 0, sizeof(adapter));
  adapter.inst = instance;
  return &adapter;
}

int
main(void) {
  GPUApi       api      = {0};
  GPUInstance  instance = {0};
  GPUAdapter  *result;
  GPUResult    status;
  uint32_t     count;

  api.device.getAvailableAdapters = enumerate_adapters;
  instance._api                   = &api;

  count  = 0u;
  status = GPUEnumerateAdapters(&instance, &count, NULL);
  if (status != GPU_OK || count != 0u || instance._adaptersEnumerated ||
      enumerationCalls != 1u) {
    fprintf(stderr, "failed adapter enumeration was cached\n");
    return 1;
  }

  status = GPUEnumerateAdapters(&instance, &count, NULL);
  if (status != GPU_OK || count != 1u || !instance._adaptersEnumerated ||
      enumerationCalls != 2u) {
    fprintf(stderr, "adapter enumeration retry failed\n");
    return 1;
  }

  result = NULL;
  count  = 1u;
  status = GPUEnumerateAdapters(&instance, &count, &result);
  if (status != GPU_OK || count != 1u || result != &adapter ||
      enumerationCalls != 2u) {
    fprintf(stderr, "successful adapter enumeration was not cached\n");
    return 1;
  }
  return 0;
}
