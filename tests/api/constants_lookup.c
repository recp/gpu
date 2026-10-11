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

#undef calloc
#undef free

#include "api/constants_internal.h"
#include "api/device_internal.h"

#include <math.h>

static const uint32_t kinds[] = {
  USL_RUNTIME_TYPE_BOOL, USL_RUNTIME_TYPE_I32, USL_RUNTIME_TYPE_U32, USL_RUNTIME_TYPE_F32
};

static uint32_t allocationCalls;
static uint32_t releaseCalls;
static bool     failAllocation;

static GPUConstant
constantValue(const USLRuntimeSpecConstant *metadata) {
  GPUConstant value = {0};

  value.id   = metadata->function_constant_id;
  value.type = (GPUConstantType)(GPU_CONSTANT_BOOL + metadata->default_uint % 4u);

  switch (value.type) {
    case GPU_CONSTANT_BOOL: value.value.boolean = true; break;
    case GPU_CONSTANT_I32: value.value.i32 = -7; break;
    case GPU_CONSTANT_U32: value.value.u32 = UINT32_MAX; break;
    case GPU_CONSTANT_F32: value.value.f32 = -0.0f; break;
    default: break;
  }

  return value;
}

static int
checkLookup(uint32_t count, uint32_t pattern) {
  GPUConstant            values[USL_RUNTIME_MAX_SPEC_CONSTANTS] = {0};
  GPUConstant            expected;
  GPUPreparedConstants   prepared;
  GPUPipelineConstants   constants = {0};
  GPUShaderLibrary       library   = {0};
  GPUDevice              device    = {0};
  GPUApi                 api       = {0};
  USLRuntimeSpecConstant *metadata;
  USLRuntimeSpecConstant *original;
  uint32_t                i;
  uint32_t                j;
  uint32_t                n;
  uint32_t                index;
  uint32_t                checks;
  uint32_t                missing;
  int                     ok;
  bool                    dense;

  ok     = 0;
  checks = 0u;

  if (!(metadata = calloc(count ? count : 1u, sizeof(*metadata)))) {
    return 0;
  }

  if (!(original = calloc(count ? count : 1u, sizeof(*original)))) {
    free(metadata);
    return 0;
  }

  for (i = 0u; i < count; i++) {
    metadata[i].function_constant_id = pattern == 0u ? i
                                      : pattern == 1u ? count - i - 1u
                                      : pattern == 2u ? UINT32_MAX - i * 3u
                                                      : (count - i - 1u) / 2u;

    metadata[i].type.kind    = kinds[i % 4u];
    metadata[i].default_uint = i;
    metadata[i].has_default  = 1u;
    snprintf(metadata[i].name, sizeof(metadata[i].name), "constant_%u", i);
  }

  memcpy(original, metadata, (size_t)count * sizeof(*metadata));

  api.backend            = GPU_BACKEND_METAL;
  library._api           = &api;
  library._device        = &device;
  library._metadata      = metadata;
  library._constants     = metadata;
  library._constantCount = count;
  indexConstantMetadata(&library);

  dense = true;

  for (i = 0u; i < count; i++) {
    if (original[i].function_constant_id != i) {
      dense = false;
    }
  }

  if (library._constantIdsDense != dense
      || memcmp(original, metadata, (size_t)count * sizeof(*metadata)) != 0) {
    goto cleanup;
  }

  constants.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS;
  constants.chain.structSize = sizeof(constants);
  constants.pConstants       = values;

  for (i = 0u; i < count; i++) {
    index = count - i - 1u;

    for (j = 0u; j < count; j++) {
      if (original[j].function_constant_id == original[index].function_constant_id) {
        break;
      }
    }

    for (n = 0u; n < constants.constantCount; n++) {
      if (values[n].id == original[j].function_constant_id) {
        break;
      }
    }

    if (n != constants.constantCount) {
      continue;
    }

    values[constants.constantCount++] = constantValue(&original[j]);

    if (prepareConstants(&library, &constants.chain, true, &prepared) != GPU_OK) {
      goto cleanup;
    }

    for (n = 0u; n < constants.constantCount; n++) {
      for (j = 0u; j < count; j++) {
        if (original[j].function_constant_id == prepared.values[n].id) {
          break;
        }
      }

      if (j == count) {
        free(prepared.values);
        goto cleanup;
      }

      expected = constantValue(&original[j]);

      if (memcmp(&expected, &prepared.values[n], sizeof(expected)) != 0
          || (n > 0u && prepared.values[n - 1u].id >= prepared.values[n].id)) {
        free(prepared.values);
        goto cleanup;
      }
    }

    free(prepared.values);
    checks++;
  }

  if (count > 0u) {
    missing      = pattern == 2u ? 1u : count + 1u;
    values[0].id = missing;

    if (prepareConstants(&library, &constants.chain, true, &prepared) != GPU_ERROR_INVALID_ARGUMENT) {
      free(prepared.values);
      goto cleanup;
    }

    values[0]      = constantValue(&original[0]);
    values[0].type = values[0].type == GPU_CONSTANT_BOOL ? GPU_CONSTANT_U32 : GPU_CONSTANT_BOOL;

    if (prepareConstants(&library, &constants.chain, true, &prepared) != GPU_ERROR_INVALID_ARGUMENT) {
      free(prepared.values);
      goto cleanup;
    }

    if (count > 1u) {
      values[0]               = constantValue(&original[0]);
      values[1]               = values[0];
      constants.constantCount = 2u;

      if (prepareConstants(&library, &constants.chain, true, &prepared) != GPU_ERROR_INVALID_ARGUMENT) {
        free(prepared.values);
        goto cleanup;
      }
    }
  }

  constants.constantCount = 0u;

  if (prepareConstants(&library, &constants.chain, true, &prepared) != GPU_OK
      || prepared.values || prepared.chain) {
    free(prepared.values);
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (!ok) {
    fprintf(stderr, "constant lookup count=%u pattern=%u checks=%u\n", count, pattern, checks);
  }

  free(original);
  free(metadata);
  return ok;
}

static int
checkNormalization(uint32_t count, uint32_t pattern, uint32_t order) {
  GPUConstant            values[USL_RUNTIME_MAX_SPEC_CONSTANTS];
  GPUConstant            original[USL_RUNTIME_MAX_SPEC_CONSTANTS];
  GPUConstant            expected[USL_RUNTIME_MAX_SPEC_CONSTANTS] = {0};
  GPUConstant            value;
  GPUPreparedConstants   prepared;
  GPUPipelineConstants   constants = {0};
  GPUShaderLibrary       library   = {0};
  GPUDevice              device    = {0};
  GPUApi                 api       = {0};
  USLRuntimeSpecConstant *metadata;
  uint32_t                i;
  uint32_t                index;
  GPUResult               result;
  int                     ok = 0;
  bool                    duplicate;

  if (!(metadata = calloc(count ? count : 1u, sizeof(*metadata)))) {
    return 0;
  }

  memset(values, 0xa5, sizeof(values));

  for (i = 0u; i < count; i++) {
    metadata[i].function_constant_id = pattern ? UINT32_MAX - i * 3u : i;
    metadata[i].type.kind            = kinds[i % 4u];
    metadata[i].default_uint         = i;
  }

  for (i = 0u; i < count; i++) {
    index = order == 1u ? count - 1u - i : order == 2u ? (i + count / 2u) % count : i;

    if (order == 3u && count > 1u && i >= count - 2u) {
      index = 2u * count - 3u - i;
    }

    value          = constantValue(&metadata[index]);
    values[i].id   = value.id;
    values[i].type = value.type;

    switch (value.type) {
      case GPU_CONSTANT_BOOL: values[i].value.boolean = value.value.boolean; break;
      case GPU_CONSTANT_I32:  values[i].value.i32 = value.value.i32; break;
      case GPU_CONSTANT_U32:  values[i].value.u32 = value.value.u32; break;
      case GPU_CONSTANT_F32:  values[i].value.f32 = value.value.f32; break;
      default: goto cleanup;
    }

    expected[i] = constantValue(&metadata[pattern ? count - 1u - i : i]);
  }

  duplicate = count > 1u && order >= 4u;

  if (duplicate) {
    values[count - 1u] = values[order == 4u ? count - 2u : 0u];
  }

  memcpy(original, values, sizeof(values));

  api.backend            = GPU_BACKEND_METAL;
  library._api           = &api;
  library._device        = &device;
  library._metadata      = metadata;
  library._constants     = metadata;
  library._constantCount = count;
  indexConstantMetadata(&library);

  constants.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS;
  constants.chain.structSize = sizeof(constants);
  constants.pConstants       = values;
  constants.constantCount    = count;

  allocationCalls = releaseCalls = 0u;

  result = prepareConstants(&library, &constants.chain, true, &prepared);

  ok = result == (duplicate ? GPU_ERROR_INVALID_ARGUMENT : GPU_OK)
       && allocationCalls == (uint32_t)(count != 0u)
       && releaseCalls == (uint32_t)duplicate
       && memcmp(values, original, sizeof(values)) == 0;

  if (count && !duplicate) {
    ok = ok && prepared.values && prepared.values != values
         && prepared.constants.constantCount == count
         && prepared.constants.pConstants == prepared.values
         && prepared.chain == &prepared.constants.chain && !prepared.constants.chain.pNext
         && memcmp(prepared.values, expected, (size_t)count * sizeof(*expected)) == 0;
    free(prepared.values);
  } else {
    ok = ok && !prepared.values && !prepared.chain && !prepared.constants.pConstants;
  }

  if (count) {
    failAllocation  = true;
    allocationCalls = releaseCalls = 0u;

    result = prepareConstants(&library, &constants.chain, true, &prepared);
    failAllocation = false;

    ok = ok && result == GPU_ERROR_OUT_OF_MEMORY && !prepared.values && !prepared.chain
         && !prepared.constants.pConstants && allocationCalls == 1u && releaseCalls == 0u;
  }

cleanup:
  if (!ok) {
    fprintf(stderr, "normalization count=%u metadata=%u order=%u\n", count, pattern, order);
  }

  free(metadata);
  return ok;
}

static int
checkAllocationErrors(void) {
  USLRuntimeSpecConstant metadata  = {0};
  GPUConstant           value     = {0};
  GPUPreparedConstants  prepared;
  GPUPipelineConstants  constants = {0};
  GPUShaderLibrary      library   = {0};
  GPUDevice             device    = {0};
  GPUApi                api       = {0};
  uint32_t              i;
  GPUResult             result;
  GPUResult             expected;

  api.backend            = GPU_BACKEND_METAL;
  library._api           = &api;
  library._device        = &device;
  library._metadata      = &metadata;
  library._constants     = &metadata;
  library._constantCount = 1u;
  indexConstantMetadata(&library);

  constants.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS;
  constants.chain.structSize = sizeof(constants);
  constants.pConstants       = &value;
  constants.constantCount    = 1u;

  for (i = 0u; i < 8u; i++) {
    metadata.type.kind = USL_RUNTIME_TYPE_BOOL;
    value.id           = 0u;
    value.type         = GPU_CONSTANT_BOOL;
    expected           = GPU_ERROR_INVALID_ARGUMENT;

    switch (i) {
      case 0u: value.id = 1u; break;
      case 1u: value.type = GPU_CONSTANT_U32; break;
      case 2u: value.type = (GPUConstantType)0u; break;
      case 3u:
      case 4u:
        metadata.type.kind = USL_RUNTIME_TYPE_F32;
        value.type         = GPU_CONSTANT_F32;
        value.value.f32    = i == 3u ? NAN : INFINITY;
        break;
      case 5u:
      case 7u:
        metadata.type.kind = USL_RUNTIME_TYPE_F64;
        value.type         = GPU_CONSTANT_F64;
        value.value.f64    = i == 5u ? NAN : 1.0;
        expected           = i == 5u ? GPU_ERROR_INVALID_ARGUMENT : GPU_ERROR_UNSUPPORTED;
        break;
      case 6u:
        metadata.type.kind = USL_RUNTIME_TYPE_I64;
        value.type         = GPU_CONSTANT_I64;
        expected           = GPU_ERROR_UNSUPPORTED;
        break;
    }

    failAllocation  = true;
    allocationCalls = releaseCalls = 0u;

    result = prepareConstants(&library, &constants.chain, true, &prepared);
    failAllocation = false;

    if (result != expected || prepared.values || prepared.chain || allocationCalls || releaseCalls) {
      return 0;
    }
  }

  return 1;
}

/* only normalization is linked; this backend entry is never called. */
GPUShaderFunction*
shaderFunction(GPUShaderLibrary *library, const char *name) {
  (void)library;
  (void)name;
  return NULL;
}

void*
constantTestCalloc(size_t count, size_t size) {
  allocationCalls++;
  return failAllocation ? NULL : calloc(count, size);
}

void
constantTestFree(void *data) {
  releaseCalls++;
  free(data);
}

int
main(void) {
  uint32_t count;
  uint32_t pattern;
  uint32_t order;

  for (count = 0u; count <= USL_RUNTIME_MAX_SPEC_CONSTANTS; count++) {
    for (pattern = 0u; pattern < 4u; pattern++) {
      if (!checkLookup(count, pattern)) {
        return 1;
      }
    }
  }

  for (count = 0u; count <= USL_RUNTIME_MAX_SPEC_CONSTANTS; count++) {
    for (pattern = 0u; pattern < 2u; pattern++) {
      for (order = 0u; order < 6u; order++) {
        if (!checkNormalization(count, pattern, order)) {
          return 1;
        }
      }
    }
  }

  if (!checkAllocationErrors()) {
    return 1;
  }

  puts("constant metadata/lookup: 516 count/order cases passed");
  puts("normalization: 1548 order/byte/owner cases, 1536 allocation faults and 8 early errors passed");

  return 0;
}
