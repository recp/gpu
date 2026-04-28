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
#define GPU_USL_BYTECODE_VERSION 2u
#define GPU_USL_BC_NO_STRING_OFFSET 0xFFFFFFFFu
#define GPU_USL_OPERAND_CONSTANT 0x80000000u
#define GPU_USL_OPERAND_STRING 0xC0000000u
#define GPU_USL_OPERAND_TYPE_MASK 0xC0000000u
#define GPU_USL_OPERAND_VALUE_MASK 0x3FFFFFFFu
/* Bytecode v2 maps opcodes as USLInstructionKind + 1; USL_INST_CALL is 20. */
#define GPU_USL_OPCODE_CALL 21u

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
  uint32_t block_labels_offset;
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

typedef struct {
  uint32_t type;
  uint8_t padding[4];
  union {
    float float_val;
    int32_t int_val;
    uint32_t uint_val;
    uint32_t string_offset;
  } value;
} GPUUSLBytecodeConstant;

typedef struct {
  uint32_t size;
  uint8_t opcode;
  uint8_t operand_count;
  uint16_t result_id;
  uint32_t result_type;
  uint32_t operands[];
} GPUUSLBytecodeInst;

_Static_assert(sizeof(GPUUSLBytecodeHeader) == 68, "USL bytecode header layout changed");
_Static_assert(sizeof(GPUUSLBytecodeFunction) == 52, "USL bytecode function layout changed");
_Static_assert(sizeof(GPUUSLBytecodeParam) == 16, "USL bytecode parameter layout changed");
_Static_assert(sizeof(GPUUSLBytecodeAttribute) == 8, "USL bytecode attribute layout changed");
_Static_assert(sizeof(GPUUSLBytecodeConstant) == 12, "USL bytecode constant layout changed");

typedef struct GPUBindGroupLayoutPriv {
  uint32_t count;
  GPUBindGroupLayoutEntry *entries;
} GPUBindGroupLayoutPriv;

typedef struct GPUBindGroupBindingPriv {
  GPUBindStage stage;
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

  if (offset > header->data_size || size > header->data_size - offset) {
    return 0;
  }

  if (offset > bytecodeSize || size > bytecodeSize - offset) {
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
    case 3:
      *outStage = GPUBindStageCompute;
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
  size_t nextCount;

  if (!entries || !count) {
    return 0;
  }

  if (*count == UINT32_MAX) {
    return 0;
  }

  nextCount = (size_t)*count + 1u;
  if (nextCount > SIZE_MAX / sizeof(**entries)) {
    return 0;
  }

  grown = realloc(*entries, nextCount * sizeof(**entries));
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
gpu_bindStageIsValid(GPUBindStage stage) {
  return stage == GPUBindStageVertex ||
         stage == GPUBindStageFragment ||
         stage == GPUBindStageCompute;
}

static int
gpu_bindKindIsValid(GPUBindKind kind) {
  return kind == GPUBindKindBuffer ||
         kind == GPUBindKindTexture ||
         kind == GPUBindKindSampler;
}

static int
gpu_layoutEntryDuplicateExists(const GPUBindGroupLayoutEntry *entries,
                               uint32_t count,
                               GPUBindStage stage,
                               GPUBindKind kind,
                               uint32_t binding) {
  uint32_t i;

  if (!entries) {
    return 0;
  }

  for (i = 0; i < count; i++) {
    if (entries[i].stage == stage &&
        entries[i].kind == kind &&
        entries[i].binding == binding) {
      return 1;
    }
  }

  return 0;
}

static int
gpu_validateLayoutEntries(const GPUBindGroupLayoutEntry *entries,
                          uint32_t count) {
  uint32_t i;

  if (!entries && count > 0) {
    return 0;
  }

  for (i = 0; i < count; i++) {
    if (!gpu_bindStageIsValid(entries[i].stage) ||
        !gpu_bindKindIsValid(entries[i].kind) ||
        gpu_layoutEntryDuplicateExists(entries,
                                       i,
                                       entries[i].stage,
                                       entries[i].kind,
                                       entries[i].binding)) {
      return 0;
    }
  }

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

static int
gpu_usl_resourceKindFromBindKind(GPUBindKind kind, GPUUSLResourceKind *outKind) {
  if (!outKind) {
    return 0;
  }

  switch (kind) {
    case GPUBindKindBuffer:
      *outKind = GPUUSLResourceKindBuffer;
      return 1;
    case GPUBindKindTexture:
      *outKind = GPUUSLResourceKindTexture;
      return 1;
    case GPUBindKindSampler:
      *outKind = GPUUSLResourceKindSampler;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_usl_bindKindFromResourceKind(GPUUSLResourceKind kind, GPUBindKind *outKind) {
  if (!outKind) {
    return 0;
  }

  switch (kind) {
    case GPUUSLResourceKindBuffer:
      *outKind = GPUBindKindBuffer;
      return 1;
    case GPUUSLResourceKindTexture:
      *outKind = GPUBindKindTexture;
      return 1;
    case GPUUSLResourceKindSampler:
      *outKind = GPUBindKindSampler;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_usl_parseU32(const char *value, uint32_t *outValue) {
  char *endptr;
  unsigned long parsed;

  if (!value || !outValue) {
    return 0;
  }

  endptr = NULL;
  parsed = strtoul(value, &endptr, 10);
  if (!endptr || endptr == value || *endptr != '\0' || parsed > UINT32_MAX) {
    return 0;
  }

  *outValue = (uint32_t)parsed;
  return 1;
}

static int
gpu_usl_instRecordIsValid(const GPUUSLBytecodeInst *inst) {
  uint64_t expectedSize;

  if (!inst || inst->size < sizeof(GPUUSLBytecodeInst)) {
    return 0;
  }

  expectedSize = sizeof(GPUUSLBytecodeInst) + (uint64_t)inst->operand_count * sizeof(uint32_t);
  return expectedSize == inst->size;
}

static int
gpu_usl_appendResourceBinding(GPUUSLResourceBindingDesc **items,
                              uint32_t *count,
                              GPUUSLStage stage,
                              GPUUSLResourceKind kind,
                              uint32_t binding) {
  GPUUSLResourceBindingDesc *grown;
  size_t nextCount;

  if (!items || !count) {
    return 0;
  }

  for (uint32_t i = 0; i < *count; i++) {
    if ((*items)[i].stage == stage &&
        (*items)[i].kind == kind &&
        (*items)[i].binding == binding) {
      return 1;
    }
  }

  if (*count == UINT32_MAX) {
    return 0;
  }

  nextCount = (size_t)*count + 1u;
  if (nextCount > SIZE_MAX / sizeof(**items)) {
    return 0;
  }

  grown = realloc(*items, nextCount * sizeof(**items));
  if (!grown) {
    return 0;
  }

  *items = grown;
  grown[*count].stage = stage;
  grown[*count].kind = kind;
  grown[*count].binding = binding;
  (*count)++;
  return 1;
}

static int
gpu_usl_collectResourceBindings(const uint8_t *data,
                                const GPUUSLBytecodeHeader *header,
                                uint64_t bytecodeSize,
                                const GPUUSLBytecodeFunction *fn,
                                GPUUSLStage stage,
                                GPUUSLResourceBindingDesc **outBindings,
                                uint32_t *outCount) {
  const GPUUSLBytecodeParam *params;

  if (!data || !header || !fn || !outBindings || !outCount) {
    return 0;
  }

  if (!gpu_usl_bytecodeRangeOk(header,
                               bytecodeSize,
                               fn->params_offset,
                               (uint64_t)fn->param_count * sizeof(GPUUSLBytecodeParam))) {
    return 0;
  }

  params = (const GPUUSLBytecodeParam *)(data + fn->params_offset);
  for (uint32_t paramIndex = 0; paramIndex < fn->param_count; paramIndex++) {
    const GPUUSLBytecodeParam *param;
    const GPUUSLBytecodeAttribute *attrs;

    param = &params[paramIndex];
    if (param->attribute_count == 0) {
      continue;
    }

    if (!gpu_usl_bytecodeRangeOk(header,
                                 bytecodeSize,
                                 param->attributes_offset,
                                 (uint64_t)param->attribute_count * sizeof(GPUUSLBytecodeAttribute))) {
      return 0;
    }

    attrs = (const GPUUSLBytecodeAttribute *)(data + param->attributes_offset);
    for (uint32_t attrIndex = 0; attrIndex < param->attribute_count; attrIndex++) {
      const char *attrName;
      const char *attrValue;
      GPUBindKind bindKind;
      GPUUSLResourceKind resourceKind;
      uint32_t binding;

      attrName = gpu_usl_bytecodeStringAt(data, header, bytecodeSize, attrs[attrIndex].name_offset);
      if (!gpu_bindKindFromAttributeName(attrName, &bindKind)) {
        continue;
      }

      if (attrs[attrIndex].value_offset == GPU_USL_BC_NO_STRING_OFFSET) {
        return 0;
      }

      attrValue = gpu_usl_bytecodeStringAt(data,
                                           header,
                                           bytecodeSize,
                                           attrs[attrIndex].value_offset);
      if (!gpu_usl_parseU32(attrValue, &binding) ||
          !gpu_usl_resourceKindFromBindKind(bindKind, &resourceKind)) {
        return 0;
      }

      if (!gpu_usl_appendResourceBinding(outBindings,
                                         outCount,
                                         stage,
                                         resourceKind,
                                         binding)) {
        return 0;
      }
    }
  }

  return 1;
}

static int
gpu_usl_nextFunctionOffset(const uint8_t *data,
                           const GPUUSLBytecodeHeader *header,
                           uint64_t bytecodeSize,
                           const GPUUSLBytecodeFunction *fn,
                           uint32_t *outOffset) {
  uint32_t instOffset;

  if (!data || !header || !fn || !outOffset) {
    return 0;
  }

  instOffset = fn->data_offset;
  for (uint32_t instIndex = 0; instIndex < fn->inst_count; instIndex++) {
    uint32_t instSize;

    if (!gpu_usl_bytecodeRangeOk(header, bytecodeSize, instOffset, sizeof(uint32_t))) {
      return 0;
    }

    memcpy(&instSize, data + instOffset, sizeof(uint32_t));
    if (instSize < sizeof(GPUUSLBytecodeInst) ||
        !gpu_usl_bytecodeRangeOk(header, bytecodeSize, instOffset, instSize) ||
        !gpu_usl_instRecordIsValid((const GPUUSLBytecodeInst *)(data + instOffset)) ||
        instSize > UINT32_MAX - instOffset) {
      return 0;
    }

    instOffset += instSize;
  }

  *outOffset = instOffset;
  return 1;
}

static int
gpu_usl_findFunction(const uint8_t *data,
                     const GPUUSLBytecodeHeader *header,
                     uint64_t bytecodeSize,
                     const char *entryPointName,
                     const GPUUSLBytecodeFunction **outFn) {
  uint32_t offset;

  if (!data || !header || !entryPointName || !outFn) {
    return 0;
  }

  offset = (uint32_t)sizeof(GPUUSLBytecodeHeader);
  for (uint32_t entryIndex = 0; entryIndex < header->function_count; entryIndex++) {
    const GPUUSLBytecodeFunction *fn;
    const char *fnName;

    if (!gpu_usl_bytecodeRangeOk(header,
                                 bytecodeSize,
                                 offset,
                                 sizeof(GPUUSLBytecodeFunction))) {
      return 0;
    }

    fn = (const GPUUSLBytecodeFunction *)(data + offset);
    fnName = gpu_usl_bytecodeStringAt(data, header, bytecodeSize, fn->name_offset);
    if (fnName && strcmp(fnName, entryPointName) == 0) {
      *outFn = fn;
      return 1;
    }

    if (!gpu_usl_nextFunctionOffset(data, header, bytecodeSize, fn, &offset)) {
      return 0;
    }
  }

  return 0;
}

static int
gpu_usl_functionIndex(const uint8_t *data,
                      const GPUUSLBytecodeHeader *header,
                      uint64_t bytecodeSize,
                      const GPUUSLBytecodeFunction *target,
                      uint32_t *outIndex) {
  uint32_t offset;

  if (!data || !header || !target || !outIndex) {
    return 0;
  }

  offset = (uint32_t)sizeof(GPUUSLBytecodeHeader);
  for (uint32_t i = 0; i < header->function_count; i++) {
    const GPUUSLBytecodeFunction *fn;

    if (!gpu_usl_bytecodeRangeOk(header,
                                 bytecodeSize,
                                 offset,
                                 sizeof(GPUUSLBytecodeFunction))) {
      return 0;
    }

    fn = (const GPUUSLBytecodeFunction *)(data + offset);
    if (fn == target) {
      *outIndex = i;
      return 1;
    }

    if (!gpu_usl_nextFunctionOffset(data, header, bytecodeSize, fn, &offset)) {
      return 0;
    }
  }

  return 0;
}

static int
gpu_usl_staticSamplerEqual(const GPUUSLStaticSamplerDesc *a,
                           const GPUUSLStaticSamplerDesc *b) {
  return a && b &&
         a->minFilter == b->minFilter &&
         a->magFilter == b->magFilter &&
         a->mipFilter == b->mipFilter &&
         a->addressMode == b->addressMode &&
         a->coordSpace == b->coordSpace &&
         a->compareFunc == b->compareFunc &&
         a->hasCompare == b->hasCompare &&
         a->maxAnisotropy == b->maxAnisotropy;
}

GPU_EXPORT
int
GPUUSLStaticSamplerDescIsValid(const GPUUSLStaticSamplerDesc *desc) {
  if (!desc) {
    return 0;
  }

  if (desc->minFilter > GPUUSLSamplerFilterLinear ||
      desc->magFilter > GPUUSLSamplerFilterLinear ||
      desc->mipFilter > GPUUSLSamplerFilterLinear) {
    return 0;
  }

  if (desc->addressMode > GPUUSLSamplerAddressClampToBorder ||
      desc->coordSpace > GPUUSLSamplerCoordPixel ||
      desc->compareFunc > GPUUSLSamplerCompareAlways ||
      desc->hasCompare > 1u ||
      desc->maxAnisotropy > 255u) {
    return 0;
  }

  return 1;
}

static GPUUSLStaticSamplerDesc
gpu_usl_unpackStaticSampler(uint32_t packed) {
  GPUUSLStaticSamplerDesc desc;

  memset(&desc, 0, sizeof(desc));
  desc.minFilter = packed & 0x7u;
  desc.magFilter = (packed >> 3u) & 0x7u;
  desc.mipFilter = (packed >> 6u) & 0x7u;
  desc.addressMode = (packed >> 9u) & 0x7u;
  desc.coordSpace = (packed >> 12u) & 0x3u;
  desc.compareFunc = (packed >> 14u) & 0xFu;
  desc.hasCompare = (packed >> 18u) & 0x1u;
  desc.maxAnisotropy = (packed >> 19u) & 0xFFu;
  return desc;
}

static int
gpu_usl_appendStaticSampler(GPUUSLStaticSamplerDesc **items,
                            uint32_t *count,
                            GPUUSLStaticSamplerDesc desc) {
  GPUUSLStaticSamplerDesc *grown;
  size_t nextCount;

  if (!items || !count) {
    return 0;
  }

  if (!GPUUSLStaticSamplerDescIsValid(&desc)) {
    return 0;
  }

  for (uint32_t i = 0; i < *count; i++) {
    if (gpu_usl_staticSamplerEqual(&(*items)[i], &desc)) {
      return 1;
    }
  }

  if (*count == UINT32_MAX) {
    return 0;
  }

  nextCount = (size_t)*count + 1u;
  if (nextCount > SIZE_MAX / sizeof(**items)) {
    return 0;
  }

  grown = realloc(*items, nextCount * sizeof(**items));
  if (!grown) {
    return 0;
  }

  *items = grown;
  desc.logicalIndex = *count;
  grown[*count] = desc;
  (*count)++;
  return 1;
}

static int
gpu_usl_parseWorkgroupSize(const char *value, uint32_t outSize[3]) {
  const char *p;
  uint32_t parsed[3] = {1u, 1u, 1u};
  uint32_t count = 0;

  if (!value || !outSize) {
    return 0;
  }

  p = value;
  while (*p && count < 3u) {
    char *endptr = NULL;
    unsigned long v;

    v = strtoul(p, &endptr, 10);
    if (!endptr || endptr == p || v == 0 || v > UINT32_MAX) {
      return 0;
    }

    parsed[count++] = (uint32_t)v;
    if (*endptr == '\0') {
      p = endptr;
      break;
    }
    if (*endptr != ',') {
      return 0;
    }
    p = endptr + 1;
  }

  if (count == 0 || *p != '\0') {
    return 0;
  }

  outSize[0] = parsed[0];
  outSize[1] = parsed[1];
  outSize[2] = parsed[2];
  return 1;
}

static int
gpu_usl_readFunctionWorkgroupSize(const uint8_t *data,
                                  const GPUUSLBytecodeHeader *header,
                                  uint64_t bytecodeSize,
                                  const GPUUSLBytecodeFunction *fn,
                                  uint32_t outSize[3]) {
  const GPUUSLBytecodeAttribute *attrs;

  if (!data || !header || !fn || !outSize || fn->attribute_count == 0) {
    return 0;
  }

  if (!gpu_usl_bytecodeRangeOk(header,
                               bytecodeSize,
                               fn->attributes_offset,
                               (uint64_t)fn->attribute_count * sizeof(GPUUSLBytecodeAttribute))) {
    return 0;
  }

  attrs = (const GPUUSLBytecodeAttribute *)(data + fn->attributes_offset);
  for (uint32_t i = 0; i < fn->attribute_count; i++) {
    const char *name;
    const char *value;

    name = gpu_usl_bytecodeStringAt(data, header, bytecodeSize, attrs[i].name_offset);
    if (!name || strcmp(name, "workgroup_size") != 0) {
      continue;
    }

    if (attrs[i].value_offset == GPU_USL_BC_NO_STRING_OFFSET) {
      return 0;
    }

    value = gpu_usl_bytecodeStringAt(data, header, bytecodeSize, attrs[i].value_offset);
    return gpu_usl_parseWorkgroupSize(value, outSize);
  }

  return 0;
}

static int
gpu_usl_collectStaticSamplers(const uint8_t *data,
                              const GPUUSLBytecodeHeader *header,
                              uint64_t bytecodeSize,
                              const GPUUSLBytecodeFunction *fn,
                              uint8_t *visited,
                              GPUUSLStaticSamplerDesc **outSamplers,
                              uint32_t *outCount) {
  const GPUUSLBytecodeConstant *constants;
  uint32_t instOffset;
  uint32_t fnIndex;

  if (!data || !header || !fn || !visited || !outSamplers || !outCount) {
    return 0;
  }

  if (!gpu_usl_functionIndex(data, header, bytecodeSize, fn, &fnIndex)) {
    return 0;
  }
  if (visited[fnIndex]) {
    return 1;
  }
  visited[fnIndex] = 1u;

  if (header->constant_pool_size == 0) {
    constants = NULL;
  } else if (!gpu_usl_bytecodeRangeOk(header,
                                      bytecodeSize,
                                      header->constant_pool_offset,
                                      (uint64_t)header->constant_pool_size *
                                        sizeof(GPUUSLBytecodeConstant))) {
    return 0;
  } else {
    constants = (const GPUUSLBytecodeConstant *)(data + header->constant_pool_offset);
  }

  instOffset = fn->data_offset;
  for (uint32_t instIndex = 0; instIndex < fn->inst_count; instIndex++) {
    const GPUUSLBytecodeInst *inst;

    if (!gpu_usl_bytecodeRangeOk(header, bytecodeSize, instOffset, sizeof(GPUUSLBytecodeInst))) {
      free(*outSamplers);
      *outSamplers = NULL;
      *outCount = 0;
      return 0;
    }

    inst = (const GPUUSLBytecodeInst *)(data + instOffset);
    if (!gpu_usl_instRecordIsValid(inst) ||
        !gpu_usl_bytecodeRangeOk(header, bytecodeSize, instOffset, inst->size)) {
      free(*outSamplers);
      *outSamplers = NULL;
      *outCount = 0;
      return 0;
    }

    for (uint32_t opIndex = 0; opIndex < inst->operand_count; opIndex++) {
      uint32_t operand = inst->operands[opIndex];
      uint32_t constIndex;
      GPUUSLStaticSamplerDesc desc;

      if ((operand & GPU_USL_OPERAND_TYPE_MASK) != GPU_USL_OPERAND_CONSTANT) {
        continue;
      }

      constIndex = operand & GPU_USL_OPERAND_VALUE_MASK;
      if (constIndex >= header->constant_pool_size ||
          constants[constIndex].type != 3u) {
        continue;
      }

      desc = gpu_usl_unpackStaticSampler(constants[constIndex].value.uint_val);
      if (!gpu_usl_appendStaticSampler(outSamplers, outCount, desc)) {
        free(*outSamplers);
        *outSamplers = NULL;
        *outCount = 0;
        return 0;
      }
    }

    if (inst->opcode == GPU_USL_OPCODE_CALL && inst->operand_count > 0) {
      uint32_t calleeOperand = inst->operands[0];
      uint32_t constIndex;
      const char *calleeName;
      const GPUUSLBytecodeFunction *calleeFn;

      if ((calleeOperand & GPU_USL_OPERAND_TYPE_MASK) == GPU_USL_OPERAND_STRING) {
        constIndex = calleeOperand & GPU_USL_OPERAND_VALUE_MASK;
        if (constants &&
            constIndex < header->constant_pool_size &&
            constants[constIndex].type == 2u) {
          calleeName = gpu_usl_bytecodeStringAt(data,
                                                header,
                                                bytecodeSize,
                                                constants[constIndex].value.string_offset);
          if (calleeName &&
              gpu_usl_findFunction(data, header, bytecodeSize, calleeName, &calleeFn)) {
            if (!gpu_usl_collectStaticSamplers(data,
                                               header,
                                               bytecodeSize,
                                               calleeFn,
                                               visited,
                                               outSamplers,
                                               outCount)) {
              free(*outSamplers);
              *outSamplers = NULL;
              *outCount = 0;
              return 0;
            }
          }
        }
      }
    }

    if (inst->size > UINT32_MAX - instOffset) {
      free(*outSamplers);
      *outSamplers = NULL;
      *outCount = 0;
      return 0;
    }

    instOffset += inst->size;
  }

  return 1;
}

static const GPUBindGroupEntry *
gpu_findBindGroupEntry(const GPUBindGroupEntry *entries,
                       uint32_t count,
                       GPUBindStage stage,
                       uint32_t binding,
                       GPUBindKind kind) {
  uint32_t i;

  if (!entries) {
    return NULL;
  }

  for (i = 0; i < count; i++) {
    if (entries[i].stage == stage &&
        entries[i].binding == binding &&
        entries[i].kind == kind) {
      return &entries[i];
    }
  }

  return NULL;
}

static int
gpu_bindGroupEntryHasResource(const GPUBindGroupEntry *entry) {
  if (!entry) {
    return 0;
  }

  switch (entry->kind) {
    case GPUBindKindBuffer:
      return entry->buffer != NULL;
    case GPUBindKindTexture:
      return entry->texture != NULL;
    case GPUBindKindSampler:
      return entry->sampler != NULL;
    default:
      return 0;
  }
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

  if (!outLayout) {
    return -1;
  }

  *outLayout = NULL;
  if (!gpu_validateLayoutEntries(entries, count)) {
    return -1;
  }

  layout = calloc(1, sizeof(*layout));
  priv = calloc(1, sizeof(*priv));
  if (!layout || !priv) {
    free(layout);
    free(priv);
    return -2;
  }

  if ((size_t)count > SIZE_MAX / sizeof(*priv->entries)) {
    free(priv);
    free(layout);
    return -3;
  }

  if (count > 0) {
    priv->entries = calloc(count, sizeof(*priv->entries));
    if (!priv->entries) {
      free(priv);
      free(layout);
      return -3;
    }

    memcpy(priv->entries, entries, count * sizeof(*entries));
  }

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
  GPUUSLResourceBindingDesc *bindings;
  GPUBindStage stage;
  uint32_t offset;
  uint32_t entryCount;
  uint32_t bindingCount;
  int foundEntry;
  int rc;

  if (!outLayout) {
    return -1;
  }

  *outLayout = NULL;
  if (!bytecodeData || bytecodeSize < sizeof(GPUUSLBytecodeHeader) ||
      !entryPointName) {
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
  bindings = NULL;
  entryCount = 0;
  bindingCount = 0;
  foundEntry = 0;
  offset = (uint32_t)sizeof(GPUUSLBytecodeHeader);

  for (uint32_t entryIndex = 0; entryIndex < header->function_count; entryIndex++) {
    const GPUUSLBytecodeFunction *fn;
    const char *fnName;

    if (!gpu_usl_bytecodeRangeOk(header,
                                 bytecodeSize,
                                 offset,
                                 sizeof(GPUUSLBytecodeFunction))) {
      free(bindings);
      free(entries);
      return -12;
    }

    fn = (const GPUUSLBytecodeFunction *)(data + offset);
    fnName = gpu_usl_bytecodeStringAt(data, header, bytecodeSize, fn->name_offset);
    if (!fnName || strcmp(fnName, entryPointName) != 0) {
      goto next_function;
    }

    foundEntry = 1;
    if (!gpu_usl_stageFromFunctionType(fn->function_type, &stage)) {
      free(bindings);
      free(entries);
      return -4;
    }

    if (!gpu_usl_collectResourceBindings(data,
                                         header,
                                         bytecodeSize,
                                         fn,
                                         (GPUUSLStage)stage,
                                         &bindings,
                                         &bindingCount)) {
      free(bindings);
      free(entries);
      return -5;
    }

    for (uint32_t i = 0; i < bindingCount; i++) {
      GPUBindKind bindKind;

      if (!gpu_usl_bindKindFromResourceKind(bindings[i].kind, &bindKind)) {
        free(bindings);
        free(entries);
        return -6;
      }

      if (!gpu_appendLayoutEntry(&entries,
                                 &entryCount,
                                 stage,
                                 bindKind,
                                 bindings[i].binding)) {
        free(bindings);
        free(entries);
        return -10;
      }
    }

    break;

  next_function:
    if (!gpu_usl_nextFunctionOffset(data, header, bytecodeSize, fn, &offset)) {
      free(bindings);
      free(entries);
      return -13;
    }
  }

  if (!foundEntry) {
    free(bindings);
    free(entries);
    return -11;
  }

  rc = GPUCreateBindGroupLayout(entries, entryCount, outLayout);
  free(bindings);
  free(entries);
  return rc;
}

GPU_EXPORT
int
GPUReflectUSLBytecodeEntry(const void *bytecodeData,
                           uint64_t bytecodeSize,
                           const char *entryPointName,
                           GPUUSLEntryReflection **outReflection) {
  const uint8_t *data;
  const GPUUSLBytecodeHeader *header;
  const GPUUSLBytecodeFunction *fn;
  GPUUSLEntryReflection *reflection;
  uint8_t *visited;
  GPUBindStage stage;

  if (!outReflection) {
    return -1;
  }

  *outReflection = NULL;
  if (!bytecodeData || bytecodeSize < sizeof(GPUUSLBytecodeHeader) ||
      !entryPointName) {
    return -1;
  }

  data = (const uint8_t *)bytecodeData;
  header = (const GPUUSLBytecodeHeader *)bytecodeData;
  if (header->magic != GPU_USL_BYTECODE_MAGIC ||
      header->version != GPU_USL_BYTECODE_VERSION) {
    return -2;
  }

  if (!gpu_usl_findFunction(data, header, bytecodeSize, entryPointName, &fn)) {
    return -3;
  }

  if (!gpu_usl_stageFromFunctionType(fn->function_type, &stage)) {
    return -4;
  }

  reflection = calloc(1, sizeof(*reflection));
  if (!reflection) {
    return -5;
  }

  reflection->stage = (GPUUSLStage)stage;
  if (stage == GPUBindStageCompute) {
    if (!gpu_usl_readFunctionWorkgroupSize(data,
                                           header,
                                           bytecodeSize,
                                           fn,
                                           reflection->workgroupSize)) {
      free(reflection);
      return -6;
    }
  }

  if (!gpu_usl_collectResourceBindings(data,
                                       header,
                                       bytecodeSize,
                                       fn,
                                       (GPUUSLStage)stage,
                                       &reflection->resourceBindings,
                                       &reflection->resourceBindingCount)) {
    free(reflection);
    return -7;
  }

  visited = calloc(header->function_count ? header->function_count : 1u, sizeof(*visited));
  if (!visited) {
    free(reflection->resourceBindings);
    free(reflection);
    return -8;
  }

  if (!gpu_usl_collectStaticSamplers(data,
                                     header,
                                     bytecodeSize,
                                     fn,
                                     visited,
                                     &reflection->staticSamplers,
                                     &reflection->staticSamplerCount)) {
    free(visited);
    free(reflection->resourceBindings);
    free(reflection);
    return -9;
  }

  free(visited);
  *outReflection = reflection;
  return 0;
}

GPU_EXPORT
void
GPUFreeUSLEntryReflection(GPUUSLEntryReflection *reflection) {
  if (!reflection) {
    return;
  }

  free(reflection->resourceBindings);
  free(reflection->staticSamplers);
  free(reflection);
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
  GPUBindGroupLayoutPriv *layoutPriv;
  uint32_t i;

  if (!outGroup) {
    return -1;
  }

  *outGroup = NULL;
  if (!layout || (!entries && count > 0)) {
    return -1;
  }

  layoutPriv = gpu_layoutPriv(layout);
  if (!layoutPriv || count != layoutPriv->count) {
    return -2;
  }

  group = calloc(1, sizeof(*group));
  priv = calloc(1, sizeof(*priv));
  if (!group || !priv) {
    free(group);
    free(priv);
    return -3;
  }

  if ((size_t)layoutPriv->count > SIZE_MAX / sizeof(*priv->bindings)) {
    free(priv);
    free(group);
    return -4;
  }

  if (layoutPriv->count > 0) {
    priv->bindings = calloc(layoutPriv->count, sizeof(*priv->bindings));
    if (!priv->bindings) {
      free(priv);
      free(group);
      return -5;
    }
  }

  for (i = 0; i < layoutPriv->count; i++) {
    const GPUBindGroupEntry *entry;

    entry = gpu_findBindGroupEntry(entries,
                                   count,
                                   layoutPriv->entries[i].stage,
                                   layoutPriv->entries[i].binding,
                                   layoutPriv->entries[i].kind);
    if (!gpu_bindGroupEntryHasResource(entry)) {
      free(priv->bindings);
      free(priv);
      free(group);
      return -6;
    }

    priv->bindings[i].stage = entry->stage;
    priv->bindings[i].binding = entry->binding;
    priv->bindings[i].kind = entry->kind;
    priv->bindings[i].buffer = entry->buffer;
    priv->bindings[i].texture = entry->texture;
    priv->bindings[i].sampler = entry->sampler;
    priv->bindings[i].offset = entry->offset;
  }

  priv->layout = layout;
  priv->count = layoutPriv->count;
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
  if (!priv || !layout || priv->count < layout->count) {
    return;
  }

  for (i = 0; i < layout->count; i++) {
    binding = &priv->bindings[i];

    switch (layout->entries[i].stage) {
      case GPUBindStageVertex:
        if (layout->entries[i].kind == GPUBindKindBuffer && binding->buffer) {
          GPUSetVertexBuffer(rce,
                             binding->buffer,
                             binding->offset,
                             layout->entries[i].binding);
        } else if (layout->entries[i].kind == GPUBindKindTexture && binding->texture) {
          GPUSetVertexTexture(rce,
                              binding->texture,
                              layout->entries[i].binding);
        } else if (layout->entries[i].kind == GPUBindKindSampler && binding->sampler) {
          GPUSetVertexSampler(rce,
                              binding->sampler,
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
