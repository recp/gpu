/*
 * Copyright (C) 2020 Recep Aslantas
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

#include "../../common.h"

#define GPU_USL_BYTECODE_MAGIC   0x554C5342u
#define GPU_USL_BYTECODE_VERSION 1u
#define GPU_USL_BC_NO_STRING_OFFSET 0xFFFFFFFFu

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t function_count;
  uint32_t struct_count;
  uint32_t string_table_offset;
  uint32_t string_table_size;
  uint32_t constant_pool_offset;
  uint32_t constant_pool_size;
  uint32_t generic_export_offset;
  uint32_t generic_export_count;
  uint32_t import_table_offset;
  uint32_t import_count;
  uint32_t spec_constant_offset;
  uint32_t spec_constant_count;
  uint32_t source_blob_offset;
  uint32_t source_blob_size;
  uint32_t data_size;
} GPUUSLBytecodeHeader;

typedef struct {
  uint32_t name_offset;
  uint32_t return_type;
  uint32_t function_type;
  uint32_t flags;
  uint32_t param_count;
  uint32_t params_offset;
  uint32_t block_inst_counts_offset;
  uint32_t block_count;
  uint32_t inst_count;
  uint32_t data_offset;
  uint32_t attribute_count;
  uint32_t attributes_offset;
} GPUUSLBytecodeFunction;

typedef struct {
  uint32_t name_offset;
  uint32_t type_id;
  uint32_t attribute_count;
  uint32_t attributes_offset;
} GPUUSLBytecodeParam;

typedef struct {
  uint32_t name_offset;
  uint32_t value_offset;
} GPUUSLBytecodeAttribute;

typedef struct GPUBindGroupLayoutPriv {
  uint32_t count;
  GPUBindGroupLayoutEntry *entries;
} GPUBindGroupLayoutPriv;

typedef struct GPUBindGroupBindingPriv {
  uint32_t binding;
  GPUBindKind kind;
  GPUBuffer *buffer;
  GPUTexture *texture;
  GPUSampler *sampler;
  size_t offset;
} GPUBindGroupBindingPriv;

typedef struct GPUBindGroupPriv {
  GPUBindGroupLayout *layout;
  uint32_t count;
  GPUBindGroupBindingPriv *bindings;
} GPUBindGroupPriv;

static GPUBindGroupLayoutPriv *
gpu_layoutPriv(GPUBindGroupLayout *layout) {
  return layout ? layout->_priv : NULL;
}

static GPUBindGroupPriv *
gpu_groupPriv(GPUBindGroup *group) {
  return group ? group->_priv : NULL;
}

static int
gpu_usl_bytecodeRangeOk(const GPUUSLBytecodeHeader *header,
                        uint64_t bytecodeSize,
                        uint64_t offset,
                        uint64_t size) {
  if (!header) {
    return 0;
  }

  if (offset > header->data_size || size > header->data_size) {
    return 0;
  }

  if (offset + size > header->data_size) {
    return 0;
  }

  if (offset + size > bytecodeSize) {
    return 0;
  }

  return 1;
}

static const char *
gpu_usl_bytecodeStringAt(const uint8_t *data,
                         const GPUUSLBytecodeHeader *header,
                         uint64_t bytecodeSize,
                         uint32_t relativeOffset) {
  const char *base;
  const char *value;
  uint64_t remain;

  if (!data || !header || header->string_table_size == 0) {
    return NULL;
  }

  if (relativeOffset >= header->string_table_size) {
    return NULL;
  }

  if (!gpu_usl_bytecodeRangeOk(header,
                               bytecodeSize,
                               header->string_table_offset,
                               header->string_table_size)) {
    return NULL;
  }

  base = (const char *)(data + header->string_table_offset);
  value = base + relativeOffset;
  remain = header->string_table_size - relativeOffset;
  if (!memchr(value, '\0', (size_t)remain)) {
    return NULL;
  }

  return value;
}

static int
gpu_usl_stageFromFunctionType(uint32_t functionType, GPUBindStage *outStage) {
  if (!outStage) {
    return 0;
  }

  switch (functionType) {
    case 1:
      *outStage = GPUBindStageVertex;
      return 1;
    case 2:
      *outStage = GPUBindStageFragment;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_appendLayoutEntry(GPUBindGroupLayoutEntry **entries,
                      uint32_t *count,
                      GPUBindStage stage,
                      GPUBindKind kind,
                      uint32_t binding) {
  GPUBindGroupLayoutEntry *grown;

  if (!entries || !count) {
    return 0;
  }

  grown = realloc(*entries, (*count + 1u) * sizeof(**entries));
  if (!grown) {
    return 0;
  }

  *entries = grown;
  grown[*count].stage = stage;
  grown[*count].kind = kind;
  grown[*count].binding = binding;
  (*count)++;
  return 1;
}

static int
gpu_bindKindFromAttributeName(const char *attrName, GPUBindKind *outKind) {
  if (!attrName || !outKind) {
    return 0;
  }

  if (strcmp(attrName, "buffer") == 0) {
    *outKind = GPUBindKindBuffer;
    return 1;
  }

  if (strcmp(attrName, "texture") == 0) {
    *outKind = GPUBindKindTexture;
    return 1;
  }

  if (strcmp(attrName, "sampler") == 0) {
    *outKind = GPUBindKindSampler;
    return 1;
  }

  return 0;
}

static const GPUBindGroupBindingPriv *
gpu_findBinding(const GPUBindGroupPriv *group,
                uint32_t binding,
                GPUBindKind kind) {
  uint32_t i;

  if (!group) {
    return NULL;
  }

  for (i = 0; i < group->count; i++) {
    if (group->bindings[i].binding == binding &&
        group->bindings[i].kind == kind) {
      return &group->bindings[i];
    }
  }

  return NULL;
}

GPU_EXPORT
GPUDescriptorPool*
GPUCreateDescriptorPool(GPUDevice *__restrict device) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->descriptor.createDescriptorPool(api, device);
}

GPU_EXPORT
int
GPUCreateBindGroupLayout(const GPUBindGroupLayoutEntry *entries,
                         uint32_t count,
                         GPUBindGroupLayout **outLayout) {
  GPUBindGroupLayout *layout;
  GPUBindGroupLayoutPriv *priv;

  if (!entries || count == 0 || !outLayout) {
    return -1;
  }

  layout = calloc(1, sizeof(*layout));
  priv = calloc(1, sizeof(*priv));
  if (!layout || !priv) {
    free(layout);
    free(priv);
    return -2;
  }

  priv->entries = calloc(count, sizeof(*priv->entries));
  if (!priv->entries) {
    free(priv);
    free(layout);
    return -3;
  }

  memcpy(priv->entries, entries, count * sizeof(*entries));
  priv->count = count;
  layout->_priv = priv;
  *outLayout = layout;
  return 0;
}

GPU_EXPORT
int
GPUCreateBindGroupLayoutFromUSLBytecode(const void *bytecodeData,
                                        uint64_t bytecodeSize,
                                        const char *entryPointName,
                                        GPUBindGroupLayout **outLayout) {
  const uint8_t *data;
  const GPUUSLBytecodeHeader *header;
  GPUBindGroupLayoutEntry *entries;
  GPUBindStage stage;
  uint32_t offset;
  uint32_t entryCount;
  int rc;

  if (!bytecodeData || bytecodeSize < sizeof(GPUUSLBytecodeHeader) ||
      !entryPointName || !outLayout) {
    return -1;
  }

  data = (const uint8_t *)bytecodeData;
  header = (const GPUUSLBytecodeHeader *)bytecodeData;
  if (header->magic != GPU_USL_BYTECODE_MAGIC ||
      header->version != GPU_USL_BYTECODE_VERSION) {
    return -2;
  }

  if (!gpu_usl_bytecodeRangeOk(header,
                               bytecodeSize,
                               sizeof(GPUUSLBytecodeHeader),
                               sizeof(GPUUSLBytecodeFunction))) {
    return -3;
  }

  entries = NULL;
  entryCount = 0;
  offset = (uint32_t)sizeof(GPUUSLBytecodeHeader);

  for (uint32_t entryIndex = 0; entryIndex < header->function_count; entryIndex++) {
    const GPUUSLBytecodeFunction *fn;
    const GPUUSLBytecodeParam *params;
    const char *fnName;
    uint32_t paramIndex;
    uint32_t instOffset;

    if (!gpu_usl_bytecodeRangeOk(header,
                                 bytecodeSize,
                                 offset,
                                 sizeof(GPUUSLBytecodeFunction))) {
      free(entries);
      return -12;
    }

    fn = (const GPUUSLBytecodeFunction *)(data + offset);
    fnName = gpu_usl_bytecodeStringAt(data, header, bytecodeSize, fn->name_offset);
    if (!fnName || strcmp(fnName, entryPointName) != 0) {
      goto next_function;
    }

    if (!gpu_usl_stageFromFunctionType(fn->function_type, &stage)) {
      free(entries);
      return -4;
    }

    if (!gpu_usl_bytecodeRangeOk(header,
                                 bytecodeSize,
                                 fn->params_offset,
                                 (uint64_t)fn->param_count * sizeof(GPUUSLBytecodeParam))) {
      free(entries);
      return -5;
    }

    params = (const GPUUSLBytecodeParam *)(data + fn->params_offset);
    for (paramIndex = 0; paramIndex < fn->param_count; paramIndex++) {
      const GPUUSLBytecodeParam *param;
      const GPUUSLBytecodeAttribute *attrs;
      uint32_t attrIndex;

      param = &params[paramIndex];
      if (param->attribute_count == 0) {
        continue;
      }

      if (!gpu_usl_bytecodeRangeOk(header,
                                   bytecodeSize,
                                   param->attributes_offset,
                                   (uint64_t)param->attribute_count * sizeof(GPUUSLBytecodeAttribute))) {
        free(entries);
        return -6;
      }

      attrs = (const GPUUSLBytecodeAttribute *)(data + param->attributes_offset);
      for (attrIndex = 0; attrIndex < param->attribute_count; attrIndex++) {
        const char *attrName;
        const char *attrValue;
        GPUBindKind kind;
        char *endptr;
        unsigned long binding;

        attrName = gpu_usl_bytecodeStringAt(data, header, bytecodeSize, attrs[attrIndex].name_offset);
        if (!gpu_bindKindFromAttributeName(attrName, &kind)) {
          continue;
        }

        if (attrs[attrIndex].value_offset == GPU_USL_BC_NO_STRING_OFFSET) {
          free(entries);
          return -7;
        }

        attrValue = gpu_usl_bytecodeStringAt(data, header, bytecodeSize, attrs[attrIndex].value_offset);
        if (!attrValue) {
          free(entries);
          return -8;
        }

        binding = strtoul(attrValue, &endptr, 10);
        if (!endptr || *endptr != '\0') {
          free(entries);
          return -9;
        }

        if (!gpu_appendLayoutEntry(&entries, &entryCount, stage, kind, (uint32_t)binding)) {
          free(entries);
          return -10;
        }
      }
    }

    break;

  next_function:
    instOffset = fn->data_offset;
    for (uint32_t instIndex = 0; instIndex < fn->inst_count; instIndex++) {
      uint32_t instSize;

      if (!gpu_usl_bytecodeRangeOk(header, bytecodeSize, instOffset, sizeof(uint32_t))) {
        free(entries);
        return -13;
      }

      memcpy(&instSize, data + instOffset, sizeof(uint32_t));
      if (instSize < sizeof(uint32_t) * 2u ||
          !gpu_usl_bytecodeRangeOk(header, bytecodeSize, instOffset, instSize)) {
        free(entries);
        return -14;
      }

      instOffset += instSize;
    }
    offset = instOffset;
  }

  if (entryCount == 0) {
    free(entries);
    return -11;
  }

  rc = GPUCreateBindGroupLayout(entries, entryCount, outLayout);
  free(entries);
  return rc;
}

GPU_EXPORT
const GPUBindGroupLayoutEntry *
GPUGetBindGroupLayoutEntries(GPUBindGroupLayout *layout, uint32_t *outCount) {
  GPUBindGroupLayoutPriv *priv;

  priv = gpu_layoutPriv(layout);
  if (outCount) {
    *outCount = priv ? priv->count : 0;
  }

  return priv ? priv->entries : NULL;
}

GPU_EXPORT
void
GPUDestroyBindGroupLayout(GPUBindGroupLayout *layout) {
  GPUBindGroupLayoutPriv *priv;

  if (!layout) {
    return;
  }

  priv = gpu_layoutPriv(layout);
  if (priv) {
    free(priv->entries);
    free(priv);
  }

  free(layout);
}

GPU_EXPORT
int
GPUCreateBindGroup(GPUBindGroupLayout *layout,
                   const GPUBindGroupEntry *entries,
                   uint32_t count,
                   GPUBindGroup **outGroup) {
  GPUBindGroup *group;
  GPUBindGroupPriv *priv;
  uint32_t i;

  if (!layout || !entries || count == 0 || !outGroup) {
    return -1;
  }

  group = calloc(1, sizeof(*group));
  priv = calloc(1, sizeof(*priv));
  if (!group || !priv) {
    free(group);
    free(priv);
    return -2;
  }

  priv->bindings = calloc(count, sizeof(*priv->bindings));
  if (!priv->bindings) {
    free(priv);
    free(group);
    return -3;
  }

  for (i = 0; i < count; i++) {
    priv->bindings[i].binding = entries[i].binding;
    priv->bindings[i].kind = entries[i].kind;
    priv->bindings[i].buffer = entries[i].buffer;
    priv->bindings[i].texture = entries[i].texture;
    priv->bindings[i].sampler = entries[i].sampler;
    priv->bindings[i].offset = entries[i].offset;
  }

  priv->layout = layout;
  priv->count = count;
  group->_priv = priv;
  *outGroup = group;
  return 0;
}

GPU_EXPORT
void
GPUDestroyBindGroup(GPUBindGroup *group) {
  GPUBindGroupPriv *priv;

  if (!group) {
    return;
  }

  priv = gpu_groupPriv(group);
  if (priv) {
    free(priv->bindings);
    free(priv);
  }

  free(group);
}

GPU_EXPORT
void
GPUBindRenderGroup(GPURenderCommandEncoder *rce, GPUBindGroup *group) {
  const GPUBindGroupBindingPriv *binding;
  GPUBindGroupLayoutPriv *layout;
  GPUBindGroupPriv *priv;
  uint32_t i;

  if (!rce || !group) {
    return;
  }

  priv = gpu_groupPriv(group);
  layout = gpu_layoutPriv(priv ? priv->layout : NULL);
  if (!priv || !layout) {
    return;
  }

  for (i = 0; i < layout->count; i++) {
    binding = gpu_findBinding(priv,
                              layout->entries[i].binding,
                              layout->entries[i].kind);
    if (!binding) {
      continue;
    }

    switch (layout->entries[i].stage) {
      case GPUBindStageVertex:
        if (layout->entries[i].kind == GPUBindKindBuffer && binding->buffer) {
          GPUSetVertexBuffer(rce,
                             binding->buffer,
                             binding->offset,
                             layout->entries[i].binding);
        }
        break;
      case GPUBindStageFragment:
        if (layout->entries[i].kind == GPUBindKindBuffer && binding->buffer) {
          GPUSetFragmentBuffer(rce,
                               binding->buffer,
                               binding->offset,
                               layout->entries[i].binding);
        } else if (layout->entries[i].kind == GPUBindKindTexture && binding->texture) {
          GPUSetFragmentTexture(rce,
                                binding->texture,
                                layout->entries[i].binding);
        } else if (layout->entries[i].kind == GPUBindKindSampler && binding->sampler) {
          GPUSetFragmentSampler(rce,
                                binding->sampler,
                                layout->entries[i].binding);
        }
        break;
      default:
        break;
    }
  }
}
