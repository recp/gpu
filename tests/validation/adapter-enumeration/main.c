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

#include <stdio.h>
#include <string.h>

static GPUAdapter adapter;
static uint32_t   enumerationCalls;

static GPUAdapter*
enumerate_adapters(GPUInstance *__restrict instance, uint32_t maxCount) {
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
  Api         api      = {0};
  GPUInstance instance = {0};
  GPUAdapter *result;
  GPUResult   status;
  uint32_t    count;

  api.device.getAvailableAdapters = enumerate_adapters;
  instance._api                   = &api;

  count  = 0u;
  status = GPUEnumerateAdapters(&instance, &count, NULL);

  if (status != GPU_OK || count != 0u || instance._adaptersEnumerated
      || enumerationCalls != 1u) {
    fprintf(stderr, "failed adapter enumeration was cached\n");
    return 1;
  }

  status = GPUEnumerateAdapters(&instance, &count, NULL);

  if (status != GPU_OK || count != 1u || !instance._adaptersEnumerated
      || enumerationCalls != 2u) {
    fprintf(stderr, "adapter enumeration retry failed\n");
    return 1;
  }

  result = NULL;
  count  = 1u;
  status = GPUEnumerateAdapters(&instance, &count, &result);

  if (status != GPU_OK || count != 1u || result != &adapter
      || enumerationCalls != 2u) {
    fprintf(stderr, "successful adapter enumeration was not cached\n");
    return 1;
  }

  return 0;
}
