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

#include "api/constants_internal.h"
#include "api/device_internal.h"

static const uint32_t kinds[] = {
  USL_RUNTIME_TYPE_BOOL, USL_RUNTIME_TYPE_I32, USL_RUNTIME_TYPE_U32, USL_RUNTIME_TYPE_F32
};

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

/* only normalization is linked; this backend entry is never called. */
GPUShaderFunction*
shaderFunction(GPUShaderLibrary *library, const char *name) {
  (void)library;
  (void)name;
  return NULL;
}

int
main(void) {
  uint32_t count;
  uint32_t pattern;

  for (count = 0u; count <= USL_RUNTIME_MAX_SPEC_CONSTANTS; count++) {
    for (pattern = 0u; pattern < 4u; pattern++) {
      if (!checkLookup(count, pattern)) {
        return 1;
      }
    }
  }

  puts("constant metadata/lookup: 516 count/order cases passed");
  return 0;
}
