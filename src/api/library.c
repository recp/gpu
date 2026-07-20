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

#include "../common.h"
#include "device_internal.h"
#include "library_internal.h"
#include "usl_target.h"

typedef struct GPUShaderEntryInfo {
  char     *name;
  char     *nodeName;
  char     *payloadType;
  uint64_t  nameHash;
  uint32_t  stage;
  uint32_t  runtimeStage;
  uint32_t  workgroupSize[3];
  uint32_t  meshTopology;
  uint32_t  meshMaxVertices;
  uint32_t  meshMaxPrimitives;
  uint32_t  resourceStart;
  uint32_t  resourceCount;
  uint32_t  payloadSizeBytes;
  uint32_t  rayPayloadSizeBytes;
  uint32_t  hitAttributeSizeBytes;
  uint32_t  callableDataSizeBytes;
  uint32_t  nodeIndex;
  uint32_t  nodeLaunch;
  uint32_t  nameLength;
  bool      nodeProgramEntry;
} GPUShaderEntryInfo;

typedef struct GPUShaderEntryInfoList {
  uint32_t count;
  GPUShaderEntryInfo entries[];
} GPUShaderEntryInfoList;

typedef struct GPUShaderResourceBindingInfo {
  uint32_t groupIndex;
  uint32_t binding;
  GPUBindingType bindingType;
  uint32_t backendBinding;
} GPUShaderResourceBindingInfo;

typedef struct GPUShaderResourceBindingInfoList {
  uint32_t count;
  GPUShaderResourceBindingInfo entries[];
} GPUShaderResourceBindingInfoList;

static void
gpu_clearShaderMetadata(GPUShaderLibrary *library) {
  if (!library) {
    return;
  }

  free(library->_metadata);
  library->_metadata         = NULL;
  library->_entryInfo        = NULL;
  library->_entryResources   = NULL;
  library->_resourceBindings = NULL;
  library->_staticSamplers   = NULL;
  memset(&library->_reflection, 0, sizeof(library->_reflection));
}

static void
gpu_clearShaderReflection(GPUShaderReflection *reflection) {
  if (!reflection) {
    return;
  }

  free((void *)reflection->pResources);
  memset(reflection, 0, sizeof(*reflection));
}

static int
gpu_uslRuntimeInfoIsUsable(const USRuntimeInfo *runtimeInfo) {
  return runtimeInfo &&
         runtimeInfo->abi_version == USL_RUNTIME_INFO_VERSION;
}

static int
gpu_shaderVisibilityFromUSLStage(uint32_t stage,
                                 GPUShaderStageFlags *outVisibility);

static int
gpu_subgroupOperationsFromUSL(
  uint32_t                          uslOperations,
  GPUBackendSubgroupOperationFlags *outOperations) {
  const uint32_t knownOperations =
    USL_RUNTIME_SUBGROUP_OPERATION_BASIC |
    USL_RUNTIME_SUBGROUP_OPERATION_SHUFFLE |
    USL_RUNTIME_SUBGROUP_OPERATION_SHUFFLE_RELATIVE;
  GPUBackendSubgroupOperationFlags operations;

  if (!outOperations || (uslOperations & ~knownOperations) != 0u) {
    return 0;
  }

  operations = 0u;
  if ((uslOperations & USL_RUNTIME_SUBGROUP_OPERATION_BASIC) != 0u) {
    operations |= GPU_BACKEND_SUBGROUP_OPERATION_BASIC_BIT;
  }
  if ((uslOperations & USL_RUNTIME_SUBGROUP_OPERATION_SHUFFLE) != 0u) {
    operations |= GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_BIT;
  }
  if ((uslOperations &
       USL_RUNTIME_SUBGROUP_OPERATION_SHUFFLE_RELATIVE) != 0u) {
    operations |= GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_RELATIVE_BIT;
  }
  *outOperations = operations;
  return 1;
}

static int
gpu_subgroupMatrixComponentFromUSL(
  uint32_t                                elementKind,
  GPUSubgroupMatrixComponentTypeEXT      *outType) {
  if (!outType) {
    return 0;
  }

  switch (elementKind) {
    case USL_RUNTIME_ELEM_I8:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_I8_EXT;
      return 1;
    case USL_RUNTIME_ELEM_I16:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_I16_EXT;
      return 1;
    case USL_RUNTIME_ELEM_I32:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_I32_EXT;
      return 1;
    case USL_RUNTIME_ELEM_I64:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_I64_EXT;
      return 1;
    case USL_RUNTIME_ELEM_U8:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_U8_EXT;
      return 1;
    case USL_RUNTIME_ELEM_U16:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_U16_EXT;
      return 1;
    case USL_RUNTIME_ELEM_U32:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_U32_EXT;
      return 1;
    case USL_RUNTIME_ELEM_U64:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_U64_EXT;
      return 1;
    case USL_RUNTIME_ELEM_F16:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT;
      return 1;
    case USL_RUNTIME_ELEM_F32:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT;
      return 1;
    case USL_RUNTIME_ELEM_F64:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_F64_EXT;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_subgroupMatrixPropertyMatches(
  const GPUSubgroupMatrixPropertiesEXT    *property,
  const USLRuntimeSubgroupMatrixRequirement *requirement) {
  GPUSubgroupMatrixComponentTypeEXT type;
  GPUShaderStageFlags                stage;

  if (!property || !requirement || requirement->flags != 0u ||
      requirement->scope != USL_RUNTIME_SUBGROUP_MATRIX_SCOPE_SUBGROUP ||
      property->scope != GPU_SUBGROUP_MATRIX_SCOPE_SUBGROUP_EXT ||
      property->saturatingAccumulation ||
      !gpu_shaderVisibilityFromUSLStage(requirement->stage, &stage) ||
      (property->stages & stage) == 0u) {
    return 0;
  }

  if (requirement->operation == USL_RUNTIME_SUBGROUP_MATRIX_OPERATION_MAD) {
    GPUSubgroupMatrixComponentTypeEXT aType;
    GPUSubgroupMatrixComponentTypeEXT bType;
    GPUSubgroupMatrixComponentTypeEXT cType;
    GPUSubgroupMatrixComponentTypeEXT resultType;

    return gpu_subgroupMatrixComponentFromUSL(requirement->a_element_kind,
                                              &aType) &&
           gpu_subgroupMatrixComponentFromUSL(requirement->b_element_kind,
                                              &bType) &&
           gpu_subgroupMatrixComponentFromUSL(requirement->c_element_kind,
                                              &cType) &&
           gpu_subgroupMatrixComponentFromUSL(
             requirement->result_element_kind,
             &resultType) &&
           property->m == requirement->m &&
           property->n == requirement->n &&
           property->k == requirement->k &&
           property->aType == aType && property->bType == bType &&
           property->cType == cType && property->resultType == resultType;
  }

  if (requirement->operation != USL_RUNTIME_SUBGROUP_MATRIX_OPERATION_LOAD &&
      requirement->operation != USL_RUNTIME_SUBGROUP_MATRIX_OPERATION_STORE &&
      requirement->operation != USL_RUNTIME_SUBGROUP_MATRIX_OPERATION_FILL) {
    return 0;
  }
  if (!gpu_subgroupMatrixComponentFromUSL(requirement->element_kind, &type)) {
    return 0;
  }

  switch (requirement->use) {
    case USL_RUNTIME_SUBGROUP_MATRIX_USE_A:
      return requirement->rows == property->m &&
             requirement->cols == property->k && type == property->aType;
    case USL_RUNTIME_SUBGROUP_MATRIX_USE_B:
      return requirement->rows == property->k &&
             requirement->cols == property->n && type == property->bType;
    case USL_RUNTIME_SUBGROUP_MATRIX_USE_ACCUMULATOR:
      return requirement->rows == property->m &&
             requirement->cols == property->n &&
             type == (requirement->operation ==
                        USL_RUNTIME_SUBGROUP_MATRIX_OPERATION_STORE
                       ? property->resultType
                       : property->cType);
    default:
      return 0;
  }
}

static int
gpu_subgroupMatrixRequirementsEnabled(const GPUDevice      *device,
                                      const USRuntimeInfo *runtimeInfo) {
  GPUSubgroupMatrixPropertiesEXT *properties;
  GPUResult                       result;
  size_t                          propertyBytes;
  uint32_t                        propertyCount;
  int                             enabled;

  if (!runtimeInfo || runtimeInfo->subgroup_matrix_requirement_count == 0u) {
    return 1;
  }
  if (!device || !device->adapter ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_SUBGROUP_MATRIX) ||
      runtimeInfo->subgroup_matrix_requirement_count >
        USL_RUNTIME_MAX_SUBGROUP_MATRIX_REQUIREMENTS ||
      !runtimeInfo->subgroup_matrix_requirements) {
    return 0;
  }

  propertyCount = 0u;
  if (GPUGetSubgroupMatrixPropertiesEXT(device->adapter,
                                        &propertyCount,
                                        NULL) != GPU_OK ||
      propertyCount == 0u ||
      SIZE_MAX / propertyCount < sizeof(*properties)) {
    return 0;
  }
  propertyBytes = (size_t)propertyCount * sizeof(*properties);
  properties = malloc(propertyBytes);
  if (!properties) {
    return 0;
  }
  result = GPUGetSubgroupMatrixPropertiesEXT(device->adapter,
                                             &propertyCount,
                                             properties);
  if (result != GPU_OK) {
    free(properties);
    return 0;
  }

  enabled = 1;
  for (uint32_t i = 0u;
       enabled && i < runtimeInfo->subgroup_matrix_requirement_count;
       i++) {
    int matched;

    matched = 0;
    for (uint32_t j = 0u; j < propertyCount; j++) {
      if (gpu_subgroupMatrixPropertyMatches(
            &properties[j],
            &runtimeInfo->subgroup_matrix_requirements[i])) {
        matched = 1;
        break;
      }
    }
    enabled = matched;
  }
  free(properties);
  return enabled;
}

static int
gpu_subgroupRequirementsEnabled(const GPUDevice      *device,
                                const USRuntimeInfo *runtimeInfo) {
  GPUBackendSubgroupOperationFlags operations;
  GPUShaderStageFlags               stage;
  GPUApi                            *api;

  if (!device || !runtimeInfo || !(api = gpuDeviceApi(device))) {
    return 0;
  }

  for (uint32_t i = 0u; i < runtimeInfo->entry_point_count; i++) {
    const USLRuntimeEntryPoint *entry;

    entry = &runtimeInfo->entry_points[i];
    if (entry->subgroup_operation_flags ==
        USL_RUNTIME_SUBGROUP_OPERATION_NONE) {
      continue;
    }
    if (!api->device.supportsSubgroupOperations ||
        !gpu_shaderVisibilityFromUSLStage(entry->stage, &stage) ||
        !gpu_subgroupOperationsFromUSL(entry->subgroup_operation_flags,
                                       &operations) ||
        !api->device.supportsSubgroupOperations(device->adapter,
                                                stage,
                                                operations)) {
      return 0;
    }
  }

  return 1;
}

static int
gpu_shaderRequirementsEnabled(const GPUDevice      *device,
                              const USRuntimeInfo *runtimeInfo) {
  uint32_t flags;

  flags = USL_BYTECODE_RUNTIME_INFO_FLAG_ENTRY_OVERFLOW |
          USL_BYTECODE_RUNTIME_INFO_FLAG_CAPABILITY_REQUIREMENT_OVERFLOW |
          USL_BYTECODE_RUNTIME_INFO_FLAG_SUBGROUP_MATRIX_REQUIREMENT_OVERFLOW;
  if (!device || !gpu_uslRuntimeInfoIsUsable(runtimeInfo) ||
      (runtimeInfo->flags & flags) != 0u ||
      runtimeInfo->entry_point_count > USL_RUNTIME_MAX_ENTRY_POINTS ||
      runtimeInfo->capability_requirement_count >
        USL_RUNTIME_MAX_CAPABILITY_REQUIREMENTS) {
    return 0;
  }

  for (uint32_t i = 0u;
       i < runtimeInfo->capability_requirement_count;
       i++) {
    const USLRuntimeCapabilityRequirement *requirement;

    requirement = &runtimeInfo->capability_requirements[i];
    if ((requirement->flags &
         USL_RUNTIME_CAPABILITY_REQUIREMENT_FLAG_ATOM_OVERFLOW) != 0u ||
        requirement->atom_count != requirement->atom_total_count ||
        requirement->atom_count >
          USL_RUNTIME_MAX_CAPABILITY_REQUIREMENT_ATOMS) {
      return 0;
    }
  }

  return gpu_subgroupRequirementsEnabled(device, runtimeInfo) &&
         gpu_subgroupMatrixRequirementsEnabled(device, runtimeInfo);
}

static int
gpu_shaderVisibilityFromUSLStage(uint32_t stage, GPUShaderStageFlags *outVisibility) {
  if (!outVisibility) {
    return 0;
  }

  switch (stage) {
    case USL_RUNTIME_STAGE_VERTEX:
      *outVisibility = GPU_SHADER_STAGE_VERTEX_BIT;
      return 1;
    case USL_RUNTIME_STAGE_FRAGMENT:
      *outVisibility = GPU_SHADER_STAGE_FRAGMENT_BIT;
      return 1;
    case USL_RUNTIME_STAGE_COMPUTE:
    case USL_RUNTIME_STAGE_NODE:
      *outVisibility = GPU_SHADER_STAGE_COMPUTE_BIT;
      return 1;
    case USL_RUNTIME_STAGE_TASK:
      *outVisibility = GPU_SHADER_STAGE_TASK_BIT;
      return 1;
    case USL_RUNTIME_STAGE_MESH:
      *outVisibility = GPU_SHADER_STAGE_MESH_BIT;
      return 1;
    case USL_RUNTIME_STAGE_RAY_GENERATION:
      *outVisibility = GPU_SHADER_STAGE_RAY_GENERATION_BIT;
      return 1;
    case USL_RUNTIME_STAGE_MISS:
      *outVisibility = GPU_SHADER_STAGE_MISS_BIT;
      return 1;
    case USL_RUNTIME_STAGE_CLOSEST_HIT:
      *outVisibility = GPU_SHADER_STAGE_CLOSEST_HIT_BIT;
      return 1;
    case USL_RUNTIME_STAGE_ANY_HIT:
      *outVisibility = GPU_SHADER_STAGE_ANY_HIT_BIT;
      return 1;
    case USL_RUNTIME_STAGE_INTERSECTION:
      *outVisibility = GPU_SHADER_STAGE_INTERSECTION_BIT;
      return 1;
    case USL_RUNTIME_STAGE_CALLABLE:
      *outVisibility = GPU_SHADER_STAGE_CALLABLE_BIT;
      return 1;
    default:
      return 0;
  }
}

static uint64_t
gpu_shaderNameHash(const char *name, size_t length) {
  uint64_t hash = UINT64_C(1469598103934665603);

  for (size_t i = 0u; i < length; i++) {
    hash ^= (uint8_t)name[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static const GPUShaderEntryInfo *
gpu_findShaderEntry(const GPUShaderLibrary *library, const char *entryPoint) {
  const GPUShaderEntryInfoList *list;
  uint64_t hash;
  size_t length;

  if (!library || !entryPoint || !library->_entryInfo) {
    return NULL;
  }

  length = strlen(entryPoint);
  if (length > UINT32_MAX) {
    return NULL;
  }
  hash = gpu_shaderNameHash(entryPoint, length);
  list = library->_entryInfo;
  for (uint32_t i = 0u; i < list->count; i++) {
    const GPUShaderEntryInfo *entry = &list->entries[i];

    if (entry->nameHash == hash &&
        entry->nameLength == (uint32_t)length &&
        memcmp(entry->name, entryPoint, length) == 0) {
      return entry;
    }
  }
  return NULL;
}

GPU_HIDE
int
gpuGetShaderLibraryWorkgroupSize(const GPUShaderLibrary *library,
                                 const char               *entryPoint,
                                 GPUShaderStageFlags       stage,
                                 uint32_t                  outSize[3]) {
  const GPUShaderEntryInfo *entry;

  if (outSize) {
    outSize[0] = 1u;
    outSize[1] = 1u;
    outSize[2] = 1u;
  }
  if (!outSize) {
    return 0;
  }

  entry = gpu_findShaderEntry(library, entryPoint);
  if (!entry || entry->stage != stage ||
      (stage == GPU_SHADER_STAGE_COMPUTE_BIT &&
       entry->runtimeStage != USL_RUNTIME_STAGE_COMPUTE)) {
    return 0;
  }

  outSize[0] = entry->workgroupSize[0];
  outSize[1] = entry->workgroupSize[1];
  outSize[2] = entry->workgroupSize[2];
  return 1;
}

GPU_HIDE
int
gpuGetShaderLibraryComputeWorkgroupSize(const GPUShaderLibrary *library,
                                        const char *entryPoint,
                                        uint32_t outSize[3]) {
  return gpuGetShaderLibraryWorkgroupSize(library,
                                          entryPoint,
                                          GPU_SHADER_STAGE_COMPUTE_BIT,
                                          outSize);
}

GPU_HIDE
int
gpuGetShaderLibraryMeshOutputInfo(const GPUShaderLibrary *library,
                                  const char               *entryPoint,
                                  uint32_t                 *outTopology,
                                  uint32_t                 *outMaxVertices,
                                  uint32_t                 *outMaxPrimitives) {
  const GPUShaderEntryInfo *entry;

  if (outTopology) {
    *outTopology = USL_RUNTIME_MESH_TOPOLOGY_UNKNOWN;
  }
  if (outMaxVertices) {
    *outMaxVertices = 0u;
  }
  if (outMaxPrimitives) {
    *outMaxPrimitives = 0u;
  }
  if (!outTopology || !outMaxVertices || !outMaxPrimitives) {
    return 0;
  }

  entry = gpu_findShaderEntry(library, entryPoint);
  if (!entry || entry->stage != GPU_SHADER_STAGE_MESH_BIT) {
    return 0;
  }

  *outTopology      = entry->meshTopology;
  *outMaxVertices   = entry->meshMaxVertices;
  *outMaxPrimitives = entry->meshMaxPrimitives;
  return 1;
}

GPU_HIDE
int
gpuGetShaderLibraryEntryStage(const GPUShaderLibrary *library,
                              const char *entryPoint,
                              GPUShaderStageFlags *outStage) {
  const GPUShaderEntryInfo *entry;

  if (outStage) {
    *outStage = 0u;
  }
  if (!outStage) {
    return 0;
  }

  entry = gpu_findShaderEntry(library, entryPoint);
  if (!entry) {
    return 0;
  }

  *outStage = entry->stage;
  return 1;
}

static int
gpu_executionGraphEntryInfo(const GPUShaderEntryInfo             *entry,
                            GPUShaderExecutionGraphEntryInfo     *outEntry) {
  if (!entry || !outEntry || entry->runtimeStage != USL_RUNTIME_STAGE_NODE) {
    return 0;
  }

  outEntry->entryPoint      = entry->name;
  outEntry->nodeName        = entry->nodeName ? entry->nodeName : entry->name;
  outEntry->nodeIndex       = entry->nodeIndex;
  outEntry->recordSizeBytes = entry->payloadSizeBytes;
  outEntry->nodeLaunch      = entry->nodeLaunch;
  outEntry->programEntry    = entry->nodeProgramEntry;
  return 1;
}

GPU_HIDE
int
gpuGetShaderLibraryExecutionGraphEntry(
  const GPUShaderLibrary                 *library,
  const char                             *entryPoint,
  GPUShaderExecutionGraphEntryInfo       *outEntry) {
  if (outEntry) {
    memset(outEntry, 0, sizeof(*outEntry));
  }
  return outEntry && gpu_executionGraphEntryInfo(
                       gpu_findShaderEntry(library, entryPoint),
                       outEntry
                     );
}

GPU_HIDE
uint32_t
gpuGetShaderLibraryExecutionGraphEntryCount(const GPUShaderLibrary *library) {
  const GPUShaderEntryInfoList *list;
  uint32_t                      count;

  list = library ? library->_entryInfo : NULL;
  if (!list) {
    return 0u;
  }
  count = 0u;
  for (uint32_t i = 0u; i < list->count; i++) {
    count += list->entries[i].runtimeStage == USL_RUNTIME_STAGE_NODE;
  }
  return count;
}

GPU_HIDE
int
gpuGetShaderLibraryExecutionGraphEntryAt(
  const GPUShaderLibrary                 *library,
  uint32_t                                index,
  GPUShaderExecutionGraphEntryInfo       *outEntry) {
  const GPUShaderEntryInfoList *list;

  if (outEntry) {
    memset(outEntry, 0, sizeof(*outEntry));
  }
  list = library ? library->_entryInfo : NULL;
  if (!list || !outEntry) {
    return 0;
  }
  for (uint32_t i = 0u; i < list->count; i++) {
    const GPUShaderEntryInfo *entry;

    entry = &list->entries[i];
    if (entry->runtimeStage != USL_RUNTIME_STAGE_NODE) {
      continue;
    }
    if (index == 0u) {
      return gpu_executionGraphEntryInfo(entry, outEntry);
    }
    index--;
  }
  return 0;
}

GPU_HIDE
int
gpuGetShaderLibraryPayloadInfo(const GPUShaderLibrary *library,
                               const char               *entryPoint,
                               GPUShaderStageFlags       stage,
                               uint32_t                 *outSizeBytes,
                               const char              **outType) {
  const GPUShaderEntryInfo *entry;

  if (outSizeBytes) {
    *outSizeBytes = 0u;
  }
  if (outType) {
    *outType = NULL;
  }
  if (!outSizeBytes || !outType) {
    return 0;
  }

  entry = gpu_findShaderEntry(library, entryPoint);
  if (!entry || entry->stage != stage) {
    return 0;
  }

  *outSizeBytes = entry->payloadSizeBytes;
  *outType      = entry->payloadType;
  return 1;
}

GPU_HIDE
int
gpuGetShaderLibraryRayInterfaceInfo(const GPUShaderLibrary *library,
                                    const char               *entryPoint,
                                    GPUShaderStageFlags       stage,
                                    uint32_t                 *outPayloadSizeBytes,
                                    uint32_t                 *outHitAttributeSizeBytes,
                                    uint32_t                 *outCallableDataSizeBytes) {
  const GPUShaderEntryInfo *entry;

  if (outPayloadSizeBytes) {
    *outPayloadSizeBytes = 0u;
  }
  if (outHitAttributeSizeBytes) {
    *outHitAttributeSizeBytes = 0u;
  }
  if (outCallableDataSizeBytes) {
    *outCallableDataSizeBytes = 0u;
  }
  if (!outPayloadSizeBytes || !outHitAttributeSizeBytes ||
      !outCallableDataSizeBytes) {
    return 0;
  }

  entry = gpu_findShaderEntry(library, entryPoint);
  if (!entry || entry->stage != stage) {
    return 0;
  }

  *outPayloadSizeBytes      = entry->rayPayloadSizeBytes;
  *outHitAttributeSizeBytes = entry->hitAttributeSizeBytes;
  *outCallableDataSizeBytes = entry->callableDataSizeBytes;
  return 1;
}

GPU_HIDE
int
gpuShaderLibraryHasEntryResourceInfo(const GPUShaderLibrary *library) {
  return library && library->_entryInfo;
}

GPU_HIDE
const GPUShaderReflection *
gpuShaderReflectionView(const GPUShaderLibrary *library) {
  return library ? &library->_reflection : NULL;
}

GPU_HIDE
int
gpuShaderEntryView(const GPUShaderLibrary *library,
                   const char *entryPoint,
                   GPUShaderStageFlags *outStage,
                   GPUShaderReflection *outReflection) {
  const GPUShaderResourceReflection *resources;
  const GPUShaderEntryInfo *entry;

  if (!library || !entryPoint || !outStage || !outReflection ||
      !library->_entryInfo) {
    return 0;
  }

  memset(outReflection, 0, sizeof(*outReflection));
  *outStage = 0u;
  resources = library->_entryResources;
  entry = gpu_findShaderEntry(library, entryPoint);
  if (!entry) {
    return 0;
  }

  *outStage = entry->stage;
  outReflection->resourceCount = entry->resourceCount;
  outReflection->pResources = entry->resourceCount > 0u
                                ? resources + entry->resourceStart
                                : NULL;
  outReflection->pushConstantSizeBytes =
    library->_reflection.pushConstantSizeBytes;
  return 1;
}

GPU_HIDE
int
gpuGetShaderResourceBackendBinding(const GPUShaderLibrary *library,
                                   const GPUShaderResourceReflection *resource,
                                   uint32_t *outBinding) {
  const GPUShaderResourceBindingInfoList *list;

  if (!library || !resource || !outBinding) {
    return 0;
  }

  list = library->_resourceBindings;
  if (!list) {
    return 0;
  }

  for (uint32_t i = 0; i < list->count; i++) {
    const GPUShaderResourceBindingInfo *entry = &list->entries[i];

    if (entry->groupIndex == resource->groupIndex &&
        entry->binding == resource->binding &&
        entry->bindingType == resource->bindingType) {
      *outBinding = entry->backendBinding;
      return 1;
    }
  }

  return 0;
}

GPU_HIDE
const GPUShaderStaticSamplerInfo *
gpuGetShaderLibraryStaticSamplers(const GPUShaderLibrary *library,
                                  uint32_t *outCount) {
  if (outCount) {
    *outCount = library && library->_staticSamplers
                  ? library->_staticSamplers->count
                  : 0u;
  }
  return library && library->_staticSamplers
           ? library->_staticSamplers->items
           : NULL;
}

static int
gpu_bindingTypeFromUSLResource(const USLRuntimeResource *resource,
                               GPUBindingType *outType) {
  uint32_t typeKind;

  if (!resource || !outType) {
    return 0;
  }

  typeKind = resource->type.kind == USL_RUNTIME_TYPE_ARRAY
               ? resource->type.array_element_kind
               : resource->type.kind;

  switch (resource->kind) {
    case USL_RUNTIME_RESOURCE_BUFFER:
      if (resource->access != USL_RUNTIME_IMAGE_ACCESS_READ) {
        *outType = GPU_BINDING_STORAGE_BUFFER;
      } else if (resource->type.kind == USL_RUNTIME_TYPE_ARRAY) {
        *outType = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
      } else {
        *outType = GPU_BINDING_UNIFORM_BUFFER;
      }
      return 1;
    case USL_RUNTIME_RESOURCE_TEXTURE:
      *outType = GPU_BINDING_SAMPLED_TEXTURE;
      return 1;
    case USL_RUNTIME_RESOURCE_IMAGE:
      *outType = resource->access == USL_RUNTIME_IMAGE_ACCESS_READ &&
                 typeKind == USL_RUNTIME_TYPE_TEXTURE
                   ? GPU_BINDING_SAMPLED_TEXTURE
                   : GPU_BINDING_STORAGE_TEXTURE;
      return 1;
    case USL_RUNTIME_RESOURCE_SAMPLER:
      *outType = GPU_BINDING_SAMPLER;
      return 1;
    case USL_RUNTIME_RESOURCE_ACCELERATION_STRUCTURE:
      *outType = GPU_BINDING_ACCELERATION_STRUCTURE;
      return 1;
    case USL_RUNTIME_RESOURCE_SAMPLER_FEEDBACK:
      *outType = GPU_BINDING_SAMPLER_FEEDBACK_EXT;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_textureViewTypeFromUSL(uint32_t source, GPUTextureViewType *outType) {
  if (!outType) {
    return 0;
  }

  switch (source) {
    case USL_RUNTIME_TEXTURE_DIM_1D:
      *outType = GPU_TEXTURE_VIEW_1D;
      return 1;
    case USL_RUNTIME_TEXTURE_DIM_1D_ARRAY:
      *outType = GPU_TEXTURE_VIEW_1D_ARRAY;
      return 1;
    case USL_RUNTIME_TEXTURE_DIM_2D:
    case USL_RUNTIME_TEXTURE_DIM_2D_MS:
      *outType = GPU_TEXTURE_VIEW_2D;
      return 1;
    case USL_RUNTIME_TEXTURE_DIM_2D_ARRAY:
    case USL_RUNTIME_TEXTURE_DIM_2D_MS_ARRAY:
      *outType = GPU_TEXTURE_VIEW_2D_ARRAY;
      return 1;
    case USL_RUNTIME_TEXTURE_DIM_CUBE:
      *outType = GPU_TEXTURE_VIEW_CUBE;
      return 1;
    case USL_RUNTIME_TEXTURE_DIM_CUBE_ARRAY:
      *outType = GPU_TEXTURE_VIEW_CUBE_ARRAY;
      return 1;
    case USL_RUNTIME_TEXTURE_DIM_3D:
      *outType = GPU_TEXTURE_VIEW_3D;
      return 1;
    default:
      return 0;
  }
}

static GPUTextureSampleType
gpu_textureSampleTypeFromUSL(const USLRuntimeTypeDesc *type) {
  if (type &&
      (type->texture_content == USL_RUNTIME_TEXTURE_CONTENT_DEPTH ||
       type->texture_content == USL_RUNTIME_TEXTURE_CONTENT_DEPTH_STENCIL)) {
    return GPU_TEXTURE_SAMPLE_TYPE_DEPTH;
  }

  switch (type ? type->elem_kind : USL_RUNTIME_ELEM_UNKNOWN) {
    case USL_RUNTIME_ELEM_I8:
    case USL_RUNTIME_ELEM_I16:
    case USL_RUNTIME_ELEM_I32:
    case USL_RUNTIME_ELEM_I64:
      return GPU_TEXTURE_SAMPLE_TYPE_SINT;
    case USL_RUNTIME_ELEM_BOOL:
    case USL_RUNTIME_ELEM_U8:
    case USL_RUNTIME_ELEM_U16:
    case USL_RUNTIME_ELEM_U32:
    case USL_RUNTIME_ELEM_U64:
      return GPU_TEXTURE_SAMPLE_TYPE_UINT;
    default:
      return GPU_TEXTURE_SAMPLE_TYPE_FLOAT;
  }
}

static GPUStorageTextureAccess
gpu_storageTextureAccessFromUSL(uint32_t access) {
  switch (access) {
    case USL_RUNTIME_IMAGE_ACCESS_READ:
      return GPU_STORAGE_TEXTURE_ACCESS_READ_ONLY;
    case USL_RUNTIME_IMAGE_ACCESS_READ_WRITE:
      return GPU_STORAGE_TEXTURE_ACCESS_READ_WRITE;
    default:
      return GPU_STORAGE_TEXTURE_ACCESS_WRITE_ONLY;
  }
}

static GPUFormat
gpu_storageTextureFormatFromUSL(uint32_t format) {
  static const GPUFormat formats[USL_RUNTIME_TEXEL_FORMAT_COUNT] = {
    [USL_RUNTIME_TEXEL_FORMAT_UNKNOWN]       = GPU_FORMAT_UNDEFINED,
    [USL_RUNTIME_TEXEL_FORMAT_R8_UNORM]      = GPU_FORMAT_R8_UNORM,
    [USL_RUNTIME_TEXEL_FORMAT_R8_SNORM]      = GPU_FORMAT_R8_SNORM,
    [USL_RUNTIME_TEXEL_FORMAT_R8_UINT]       = GPU_FORMAT_R8_UINT,
    [USL_RUNTIME_TEXEL_FORMAT_R8_SINT]       = GPU_FORMAT_R8_SINT,
    [USL_RUNTIME_TEXEL_FORMAT_R16_UNORM]     = GPU_FORMAT_R16_UNORM,
    [USL_RUNTIME_TEXEL_FORMAT_R16_SNORM]     = GPU_FORMAT_R16_SNORM,
    [USL_RUNTIME_TEXEL_FORMAT_R16_UINT]      = GPU_FORMAT_R16_UINT,
    [USL_RUNTIME_TEXEL_FORMAT_R16_SINT]      = GPU_FORMAT_R16_SINT,
    [USL_RUNTIME_TEXEL_FORMAT_R16_FLOAT]     = GPU_FORMAT_R16_FLOAT,
    [USL_RUNTIME_TEXEL_FORMAT_R32_UINT]      = GPU_FORMAT_R32_UINT,
    [USL_RUNTIME_TEXEL_FORMAT_R32_SINT]      = GPU_FORMAT_R32_SINT,
    [USL_RUNTIME_TEXEL_FORMAT_R32_FLOAT]     = GPU_FORMAT_R32_FLOAT,
    [USL_RUNTIME_TEXEL_FORMAT_RG8_UNORM]     = GPU_FORMAT_RG8_UNORM,
    [USL_RUNTIME_TEXEL_FORMAT_RG8_SNORM]     = GPU_FORMAT_RG8_SNORM,
    [USL_RUNTIME_TEXEL_FORMAT_RG8_UINT]      = GPU_FORMAT_RG8_UINT,
    [USL_RUNTIME_TEXEL_FORMAT_RG8_SINT]      = GPU_FORMAT_RG8_SINT,
    [USL_RUNTIME_TEXEL_FORMAT_RG16_UNORM]    = GPU_FORMAT_RG16_UNORM,
    [USL_RUNTIME_TEXEL_FORMAT_RG16_SNORM]    = GPU_FORMAT_RG16_SNORM,
    [USL_RUNTIME_TEXEL_FORMAT_RG16_UINT]     = GPU_FORMAT_RG16_UINT,
    [USL_RUNTIME_TEXEL_FORMAT_RG16_SINT]     = GPU_FORMAT_RG16_SINT,
    [USL_RUNTIME_TEXEL_FORMAT_RG16_FLOAT]    = GPU_FORMAT_RG16_FLOAT,
    [USL_RUNTIME_TEXEL_FORMAT_RG32_UINT]     = GPU_FORMAT_RG32_UINT,
    [USL_RUNTIME_TEXEL_FORMAT_RG32_SINT]     = GPU_FORMAT_RG32_SINT,
    [USL_RUNTIME_TEXEL_FORMAT_RG32_FLOAT]    = GPU_FORMAT_RG32_FLOAT,
    [USL_RUNTIME_TEXEL_FORMAT_RGBA8_UNORM]   = GPU_FORMAT_RGBA8_UNORM,
    [USL_RUNTIME_TEXEL_FORMAT_RGBA8_SNORM]   = GPU_FORMAT_RGBA8_SNORM,
    [USL_RUNTIME_TEXEL_FORMAT_RGBA8_UINT]    = GPU_FORMAT_RGBA8_UINT,
    [USL_RUNTIME_TEXEL_FORMAT_RGBA8_SINT]    = GPU_FORMAT_RGBA8_SINT,
    [USL_RUNTIME_TEXEL_FORMAT_BGRA8_UNORM]   = GPU_FORMAT_BGRA8_UNORM,
    [USL_RUNTIME_TEXEL_FORMAT_RGB10A2_UNORM] = GPU_FORMAT_RGB10A2_UNORM,
    [USL_RUNTIME_TEXEL_FORMAT_RGB10A2_UINT]  = GPU_FORMAT_RGB10A2_UINT,
    [USL_RUNTIME_TEXEL_FORMAT_RG11B10_UFLOAT] = GPU_FORMAT_RG11B10_UFLOAT,
    [USL_RUNTIME_TEXEL_FORMAT_RGBA16_UNORM]  = GPU_FORMAT_RGBA16_UNORM,
    [USL_RUNTIME_TEXEL_FORMAT_RGBA16_SNORM]  = GPU_FORMAT_RGBA16_SNORM,
    [USL_RUNTIME_TEXEL_FORMAT_RGBA16_UINT]   = GPU_FORMAT_RGBA16_UINT,
    [USL_RUNTIME_TEXEL_FORMAT_RGBA16_SINT]   = GPU_FORMAT_RGBA16_SINT,
    [USL_RUNTIME_TEXEL_FORMAT_RGBA16_FLOAT]  = GPU_FORMAT_RGBA16_FLOAT,
    [USL_RUNTIME_TEXEL_FORMAT_RGBA32_UINT]   = GPU_FORMAT_RGBA32_UINT,
    [USL_RUNTIME_TEXEL_FORMAT_RGBA32_SINT]   = GPU_FORMAT_RGBA32_SINT,
    [USL_RUNTIME_TEXEL_FORMAT_RGBA32_FLOAT]  = GPU_FORMAT_RGBA32_FLOAT
  };

  return format < GPU_ARRAY_LEN(formats)
           ? formats[format]
           : GPU_FORMAT_UNDEFINED;
}

static int
gpu_bindingLayoutFromUSLResource(const USLRuntimeResource     *resource,
                                 GPUBindingType                bindingType,
                                 GPUShaderResourceReflection  *out) {
  if (!resource || !out) {
    return 0;
  }

  switch (bindingType) {
    case GPU_BINDING_SAMPLED_TEXTURE:
      if (!gpu_textureViewTypeFromUSL(resource->type.texture_dim,
                                      &out->sampledTexture.viewType)) {
        return 0;
      }
      out->sampledTexture.sampleType =
        gpu_textureSampleTypeFromUSL(&resource->type);
      out->sampledTexture.multisampled = resource->type.is_multisampled != 0u;
      return 1;
    case GPU_BINDING_STORAGE_TEXTURE:
      if (!gpu_textureViewTypeFromUSL(resource->type.texture_dim,
                                      &out->storageTexture.viewType)) {
        return 0;
      }
      out->storageTexture.format =
        gpu_storageTextureFormatFromUSL(resource->type.texel_format);
      out->storageTexture.access =
        gpu_storageTextureAccessFromUSL(resource->type.image_access);
      return 1;
    case GPU_BINDING_SAMPLER:
      out->sampler.type = resource->type.sampler_kind ==
                            USL_RUNTIME_SAMPLER_COMPARISON
                            ? GPU_SAMPLER_BINDING_COMPARISON
                            : GPU_SAMPLER_BINDING_FILTERING;
      return 1;
    default:
      return 1;
  }
}

static int
gpu_shaderResourceLayoutEqual(const GPUShaderResourceReflection *a,
                              const GPUShaderResourceReflection *b) {
  if (!a || !b || a->bindingType != b->bindingType) {
    return 0;
  }

  switch (a->bindingType) {
    case GPU_BINDING_SAMPLED_TEXTURE:
      return a->sampledTexture.viewType == b->sampledTexture.viewType &&
             a->sampledTexture.sampleType == b->sampledTexture.sampleType &&
             a->sampledTexture.multisampled == b->sampledTexture.multisampled;
    case GPU_BINDING_STORAGE_TEXTURE:
      return a->storageTexture.viewType == b->storageTexture.viewType &&
             a->storageTexture.format == b->storageTexture.format &&
             a->storageTexture.access == b->storageTexture.access;
    case GPU_BINDING_SAMPLER:
      return a->sampler.type == b->sampler.type;
    default:
      return 1;
  }
}

static int
gpu_shaderPublicBindingFromUSLResource(const USLRuntimeResource *resource,
                                       uint32_t *outGroupIndex,
                                       uint32_t *outBinding) {
  if (!resource || !outGroupIndex || !outBinding || resource->binding < 0) {
    return 0;
  }

  *outGroupIndex = resource->group;
  *outBinding = (uint32_t)resource->binding;
  return 1;
}

static int
gpu_shaderBackendBindingFromUSLResource(GPUBackend backend,
                                        const USLRuntimeResource *resource,
                                        uint32_t *outBinding) {
  if (!resource || !outBinding || resource->binding < 0) {
    return 0;
  }

  switch (backend) {
    case GPU_BACKEND_METAL:
      *outBinding = resource->metal_index >= 0
                      ? (uint32_t)resource->metal_index
                      : (uint32_t)resource->binding;
      break;
    case GPU_BACKEND_VULKAN:
      *outBinding = resource->spirv_binding >= 0
                      ? (uint32_t)resource->spirv_binding
                      : (uint32_t)resource->binding;
      break;
    case GPU_BACKEND_DX12:
      *outBinding = resource->hlsl_index >= 0
                      ? (uint32_t)resource->hlsl_index
                      : (uint32_t)resource->binding;
      break;
    default:
      *outBinding = (uint32_t)resource->binding;
      break;
  }
  return 1;
}

static int
gpu_staticSamplerDescEqual(const GPUStaticSamplerDesc *a,
                           const GPUStaticSamplerDesc *b) {
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

GPU_HIDE
int
gpuStaticSamplerDescIsValid(const GPUStaticSamplerDesc *desc) {
  if (!desc) {
    return 0;
  }

  if (desc->minFilter > USL_RUNTIME_FILTER_LINEAR ||
      desc->magFilter > USL_RUNTIME_FILTER_LINEAR ||
      desc->mipFilter > USL_RUNTIME_FILTER_LINEAR) {
    return 0;
  }

  if (desc->addressMode > USL_RUNTIME_ADDRESS_CLAMP_TO_BORDER ||
      desc->coordSpace > USL_RUNTIME_COORD_PIXEL ||
      desc->compareFunc > USL_RUNTIME_COMPARE_ALWAYS ||
      desc->hasCompare > 1u ||
      desc->maxAnisotropy > 255u) {
    return 0;
  }

  return 1;
}

static int
gpu_reserveMetadata(size_t *totalSize,
                    size_t alignment,
                    size_t headerSize,
                    size_t itemCount,
                    size_t itemSize,
                    size_t *outOffset) {
  size_t alignedSize;
  size_t blockSize;

  if (!totalSize || !outOffset || alignment == 0u || itemSize == 0u ||
      (alignment & (alignment - 1u)) != 0u ||
      itemCount > (SIZE_MAX - headerSize) / itemSize) {
    return 0;
  }

  blockSize = headerSize + itemCount * itemSize;
  if (blockSize == 0u) {
    *outOffset = SIZE_MAX;
    return 1;
  }
  if (*totalSize > SIZE_MAX - (alignment - 1u)) {
    return 0;
  }

  alignedSize = (*totalSize + alignment - 1u) & ~(alignment - 1u);
  if (alignedSize > SIZE_MAX - blockSize) {
    return 0;
  }

  *outOffset = alignedSize;
  *totalSize = alignedSize + blockSize;
  return 1;
}

static int
gpu_runtimeTextSize(const char text[USL_RUNTIME_NAME_TEXT_MAX],
                    size_t *outSize) {
  if (!text || !outSize) {
    return 0;
  }

  for (size_t i = 0u; i < USL_RUNTIME_NAME_TEXT_MAX; i++) {
    if (text[i] == '\0') {
      *outSize = i + 1u;
      return i > 0u;
    }
  }
  return 0;
}

static char *
gpu_storeMetadataText(char **cursor, const char *text, size_t size) {
  char *stored;

  stored = *cursor;
  memcpy(stored, text, size);
  *cursor += size;
  return stored;
}

static int
gpu_setShaderLibraryMetadata(GPUShaderLibrary *library,
                             const USReflection *usReflection) {
  const USRuntimeInfo *runtimeInfo;
  GPUShaderResourceReflection *entryResources;
  GPUShaderResourceBindingInfoList *resourceBindings;
  GPUShaderStaticSamplerInfoList *staticSamplers;
  GPUShaderResourceReflection *resources;
  GPUShaderEntryInfoList *entryInfo;
  GPUBackend backend;
  uint8_t *metadata;
  char *textCursor;
  size_t entryInfoOffset;
  size_t entryResourceOffset;
  size_t resourceBindingOffset;
  size_t resourceOffset;
  size_t staticSamplerOffset;
  size_t textOffset;
  size_t textSize;
  size_t totalSize;
  uint32_t usedResourceCount;
  uint32_t entryResourceCount;
  uint32_t resourceCount;
  uint32_t flags;

  if (!library || !usReflection ||
      usReflection->abi_version != US_REFLECTION_VERSION) {
    return 0;
  }

  runtimeInfo = &usReflection->runtime;
  flags = USL_BYTECODE_RUNTIME_INFO_FLAG_ENTRY_OVERFLOW |
          USL_BYTECODE_RUNTIME_INFO_FLAG_RESOURCE_OVERFLOW |
          USL_BYTECODE_RUNTIME_INFO_FLAG_STATIC_SAMPLER_OVERFLOW;
  if (!gpu_uslRuntimeInfoIsUsable(runtimeInfo) ||
      (runtimeInfo->flags & flags) != 0u ||
      runtimeInfo->entry_point_count > USL_RUNTIME_MAX_ENTRY_POINTS ||
      runtimeInfo->resource_count > USL_RUNTIME_MAX_RESOURCES ||
      runtimeInfo->static_sampler_count > USL_RUNTIME_MAX_STATIC_SAMPLERS) {
    return 0;
  }

  textSize          = 0u;
  usedResourceCount = 0u;
  for (uint32_t i = 0u; i < runtimeInfo->entry_point_count; i++) {
    const USLRuntimeEntryPoint *entry;
    size_t nodeNameSize;
    size_t payloadTypeSize;
    size_t size;

    entry = &runtimeInfo->entry_points[i];
    if (!gpu_runtimeTextSize(entry->name, &size) ||
        textSize > SIZE_MAX - size) {
      return 0;
    }
    textSize += size;
    if (entry->stage == USL_RUNTIME_STAGE_NODE && entry->node_id[0] != '\0') {
      if (!gpu_runtimeTextSize(entry->node_id, &nodeNameSize) ||
          textSize > SIZE_MAX - nodeNameSize) {
        return 0;
      }
      textSize += nodeNameSize;
    }
    if (entry->payload_size_bytes > 0u) {
      if (!gpu_runtimeTextSize(entry->payload_type, &payloadTypeSize) ||
          textSize > SIZE_MAX - payloadTypeSize) {
        return 0;
      }
      textSize += payloadTypeSize;
    }
  }
  for (uint32_t i = 0u; i < runtimeInfo->resource_count; i++) {
    const USLRuntimeResource *resource;
    size_t paramSize;

    resource = &runtimeInfo->resources[i];
    if (!resource->used) {
      continue;
    }
    if (resource->entry_index >= runtimeInfo->entry_point_count ||
        !gpu_runtimeTextSize(resource->param, &paramSize) ||
        textSize > SIZE_MAX - paramSize) {
      return 0;
    }
    textSize += paramSize;
    usedResourceCount++;
  }

  totalSize = 0u;
  if (!gpu_reserveMetadata(&totalSize,
                           _Alignof(GPUShaderEntryInfoList),
                           sizeof(*entryInfo),
                           runtimeInfo->entry_point_count,
                           sizeof(entryInfo->entries[0]),
                           &entryInfoOffset) ||
      !gpu_reserveMetadata(&totalSize,
                           _Alignof(GPUShaderResourceReflection),
                           0u,
                           usedResourceCount,
                           sizeof(entryResources[0]),
                           &entryResourceOffset) ||
      !gpu_reserveMetadata(&totalSize,
                           _Alignof(GPUShaderResourceBindingInfoList),
                           sizeof(*resourceBindings),
                           usedResourceCount,
                           sizeof(resourceBindings->entries[0]),
                           &resourceBindingOffset) ||
      !gpu_reserveMetadata(&totalSize,
                           _Alignof(GPUShaderResourceReflection),
                           0u,
                           usedResourceCount,
                           sizeof(resources[0]),
                           &resourceOffset) ||
      !gpu_reserveMetadata(&totalSize,
                           _Alignof(GPUShaderStaticSamplerInfoList),
                           sizeof(*staticSamplers),
                           runtimeInfo->static_sampler_count,
                           sizeof(staticSamplers->items[0]),
                           &staticSamplerOffset) ||
      !gpu_reserveMetadata(&totalSize,
                           _Alignof(char),
                           0u,
                           textSize,
                           sizeof(char),
                           &textOffset)) {
    return 0;
  }

  metadata = calloc(1, totalSize ? totalSize : 1u);
  if (!metadata) {
    return 0;
  }

  entryInfo = entryInfoOffset != SIZE_MAX
                ? (GPUShaderEntryInfoList *)(metadata + entryInfoOffset)
                : NULL;
  entryResources = entryResourceOffset != SIZE_MAX
                     ? (GPUShaderResourceReflection *)(metadata +
                                                       entryResourceOffset)
                     : NULL;
  resourceBindings = resourceBindingOffset != SIZE_MAX
                       ? (GPUShaderResourceBindingInfoList *)(metadata +
                                                              resourceBindingOffset)
                       : NULL;
  resources = resourceOffset != SIZE_MAX
                ? (GPUShaderResourceReflection *)(metadata + resourceOffset)
                : NULL;
  staticSamplers = staticSamplerOffset != SIZE_MAX
                     ? (GPUShaderStaticSamplerInfoList *)(metadata +
                                                          staticSamplerOffset)
                     : NULL;
  textCursor = textOffset != SIZE_MAX ? (char *)(metadata + textOffset) : NULL;
  entryResourceCount = 0u;
  resourceCount      = 0u;

  for (uint32_t i = 0u; i < runtimeInfo->entry_point_count; i++) {
    const USLRuntimeEntryPoint *src;
    GPUShaderEntryInfo *dst;
    GPUShaderStageFlags stage;
    size_t nameSize;

    src = &runtimeInfo->entry_points[i];
    if (!gpu_shaderVisibilityFromUSLStage(src->stage, &stage) ||
        !gpu_runtimeTextSize(src->name, &nameSize)) {
      free(metadata);
      return 0;
    }

    dst                       = &entryInfo->entries[entryInfo->count++];
    dst->name                 = gpu_storeMetadataText(&textCursor,
                                                      src->name,
                                                      nameSize);
    dst->nameHash             = gpu_shaderNameHash(src->name, nameSize - 1u);
    dst->nameLength           = (uint32_t)(nameSize - 1u);
    dst->stage                 = stage;
    dst->runtimeStage          = src->stage;
    dst->meshTopology          = src->mesh_topology;
    dst->meshMaxVertices       = src->mesh_max_vertices;
    dst->meshMaxPrimitives     = src->mesh_max_primitives;
    dst->payloadSizeBytes      = src->payload_size_bytes;
    dst->rayPayloadSizeBytes   = src->ray_payload_size_bytes;
    dst->hitAttributeSizeBytes = src->hit_attribute_size_bytes;
    dst->callableDataSizeBytes = src->callable_data_size_bytes;
    dst->nodeIndex             = src->node_index;
    dst->nodeLaunch            = src->node_launch;
    dst->nodeProgramEntry      = src->node_is_program_entry != 0u;
    if (src->stage == USL_RUNTIME_STAGE_NODE && src->node_id[0] != '\0') {
      size_t nodeNameSize;

      if (!gpu_runtimeTextSize(src->node_id, &nodeNameSize)) {
        free(metadata);
        return 0;
      }
      dst->nodeName = gpu_storeMetadataText(&textCursor,
                                            src->node_id,
                                            nodeNameSize);
    }
    if (src->payload_size_bytes > 0u) {
      size_t payloadTypeSize;

      if (!gpu_runtimeTextSize(src->payload_type, &payloadTypeSize)) {
        free(metadata);
        return 0;
      }
      dst->payloadType = gpu_storeMetadataText(&textCursor,
                                               src->payload_type,
                                               payloadTypeSize);
    }
    dst->workgroupSize[0] = src->workgroup_size[0]
                              ? src->workgroup_size[0] : 1u;
    dst->workgroupSize[1] = src->workgroup_size[1]
                              ? src->workgroup_size[1] : 1u;
    dst->workgroupSize[2] = src->workgroup_size[2]
                              ? src->workgroup_size[2] : 1u;
  }

  for (uint32_t i = 0u; i < runtimeInfo->resource_count; i++) {
    const USLRuntimeResource *src = &runtimeInfo->resources[i];

    if (src->used) {
      entryInfo->entries[src->entry_index].resourceCount++;
    }
  }
  for (uint32_t i = 0u; i < entryInfo->count; i++) {
    GPUShaderEntryInfo *entry = &entryInfo->entries[i];
    uint32_t capacity = entry->resourceCount;

    entry->resourceStart = entryResourceCount;
    entry->resourceCount = 0u;
    entryResourceCount  += capacity;
  }
  if (entryResourceCount != usedResourceCount) {
    free(metadata);
    return 0;
  }

  backend = library->_api ? library->_api->backend : GPU_BACKEND_DEFAULT;
  for (uint32_t i = 0u; i < runtimeInfo->resource_count; i++) {
    const USLRuntimeResource *src;
    GPUShaderEntryInfo *shaderEntry;
    GPUShaderResourceReflection *canonical;
    GPUShaderResourceReflection *entryResource;
    GPUShaderResourceReflection resourceLayout;
    GPUShaderResourceBindingInfo *bindingInfo;
    GPUShaderStageFlags visibility;
    GPUBindingType bindingType;
    uint32_t groupIndex;
    uint32_t binding;
    uint32_t backendBinding;
    uint32_t arrayCount;
    size_t paramSize;

    src = &runtimeInfo->resources[i];
    if (!src->used) {
      continue;
    }
    shaderEntry = &entryInfo->entries[src->entry_index];
    arrayCount  = src->descriptor_count;
    memset(&resourceLayout, 0, sizeof(resourceLayout));
    if (arrayCount == 0u ||
        !gpu_shaderVisibilityFromUSLStage(src->stage, &visibility) ||
        visibility != shaderEntry->stage ||
        !gpu_bindingTypeFromUSLResource(src, &bindingType) ||
        !gpu_shaderPublicBindingFromUSLResource(src,
                                                &groupIndex,
                                                &binding) ||
        !gpu_shaderBackendBindingFromUSLResource(backend,
                                                 src,
                                                 &backendBinding) ||
        !gpu_runtimeTextSize(src->param, &paramSize)) {
      free(metadata);
      return 0;
    }
    resourceLayout.bindingType = bindingType;
    if (!gpu_bindingLayoutFromUSLResource(src,
                                          bindingType,
                                          &resourceLayout)) {
      free(metadata);
      return 0;
    }

    bindingInfo = NULL;
    for (uint32_t j = 0u; j < resourceBindings->count; j++) {
      GPUShaderResourceBindingInfo *item = &resourceBindings->entries[j];

      if (item->groupIndex == groupIndex &&
          item->binding == binding &&
          item->bindingType == bindingType) {
        bindingInfo = item;
        break;
      }
    }
    if (bindingInfo) {
      if (bindingInfo->backendBinding != backendBinding) {
        free(metadata);
        return 0;
      }
    } else {
      bindingInfo = &resourceBindings->entries[resourceBindings->count++];
      bindingInfo->groupIndex     = groupIndex;
      bindingInfo->binding        = binding;
      bindingInfo->bindingType    = bindingType;
      bindingInfo->backendBinding = backendBinding;
    }

    canonical = NULL;
    for (uint32_t j = 0u; j < resourceCount; j++) {
      GPUShaderResourceReflection *item = &resources[j];

      if (item->groupIndex == groupIndex &&
          item->binding == binding &&
          item->bindingType == bindingType) {
        canonical = item;
        break;
      }
    }
    if (canonical) {
      if (canonical->arrayCount != arrayCount ||
          !gpu_shaderResourceLayoutEqual(canonical, &resourceLayout)) {
        free(metadata);
        return 0;
      }
      canonical->visibility       |= visibility;
      canonical->hasDynamicOffset  = canonical->hasDynamicOffset ||
                                     src->dynamic_offset != 0u;
    } else {
      canonical = &resources[resourceCount++];
      *canonical = resourceLayout;
      canonical->name = gpu_storeMetadataText(&textCursor,
                                              src->param,
                                              paramSize);
      canonical->groupIndex       = groupIndex;
      canonical->binding          = binding;
      canonical->bindingType      = bindingType;
      canonical->visibility       = visibility;
      canonical->arrayCount       = arrayCount;
      canonical->hasDynamicOffset = src->dynamic_offset != 0u;
    }

    entryResource = NULL;
    for (uint32_t j = 0u; j < shaderEntry->resourceCount; j++) {
      GPUShaderResourceReflection *item;

      item = &entryResources[shaderEntry->resourceStart + j];
      if (item->groupIndex == groupIndex &&
          item->binding == binding &&
          item->bindingType == bindingType) {
        entryResource = item;
        break;
      }
    }
    if (entryResource) {
      if (entryResource->arrayCount != arrayCount) {
        free(metadata);
        return 0;
      }
      entryResource->visibility |= visibility;
      entryResource->hasDynamicOffset = entryResource->hasDynamicOffset ||
                                        src->dynamic_offset != 0u;
    } else {
      entryResource = &entryResources[shaderEntry->resourceStart +
                                      shaderEntry->resourceCount++];
      *entryResource                  = *canonical;
      entryResource->visibility       = visibility;
      entryResource->hasDynamicOffset = src->dynamic_offset != 0u;
    }
  }

  for (uint32_t i = 0u; i < runtimeInfo->static_sampler_count; i++) {
    const USLRuntimeStaticSampler *src;
    GPUShaderStaticSamplerInfo item;
    GPUShaderStageFlags visibility;
    uint32_t duplicate;

    src = &runtimeInfo->static_samplers[i];
    if (!gpu_shaderVisibilityFromUSLStage(src->stage, &visibility)) {
      free(metadata);
      return 0;
    }

    memset(&item, 0, sizeof(item));
    item.desc.logicalIndex  = src->id;
    item.desc.minFilter     = src->min_filter;
    item.desc.magFilter     = src->mag_filter;
    item.desc.mipFilter     = src->mip_filter;
    item.desc.addressMode   = src->address_mode;
    item.desc.coordSpace    = src->coord_space;
    item.desc.compareFunc   = src->compare_func;
    item.desc.hasCompare    = src->has_compare;
    item.desc.maxAnisotropy = src->max_anisotropy;
    item.visibility         = visibility;
    item.hlslIndex          = src->hlsl_index >= 0
                                ? (uint32_t)src->hlsl_index
                                : UINT32_MAX;
    item.spirvGroup         = src->spirv_set;
    item.spirvBinding = src->spirv_binding >= 0
                          ? (uint32_t)src->spirv_binding
                          : UINT32_MAX;
    if (!gpuStaticSamplerDescIsValid(&item.desc)) {
      free(metadata);
      return 0;
    }

    duplicate = UINT32_MAX;
    for (uint32_t j = 0u; j < staticSamplers->count; j++) {
      if (staticSamplers->items[j].hlslIndex == item.hlslIndex &&
          staticSamplers->items[j].spirvGroup == item.spirvGroup &&
          staticSamplers->items[j].spirvBinding == item.spirvBinding &&
          gpu_staticSamplerDescEqual(&staticSamplers->items[j].desc,
                                     &item.desc)) {
        duplicate = j;
        break;
      }
    }
    if (duplicate != UINT32_MAX) {
      staticSamplers->items[duplicate].visibility |= visibility;
      continue;
    }
    staticSamplers->items[staticSamplers->count++] = item;
  }

  gpu_clearShaderMetadata(library);
  library->_metadata         = metadata;
  library->_entryInfo        = entryInfo;
  library->_entryResources   = entryResources;
  library->_resourceBindings = resourceBindings;
  library->_staticSamplers   = staticSamplers;
  library->_reflection.resourceCount = resourceCount;
  library->_reflection.pResources = resources;
  return 1;
}

static GPUResult
gpu_copyShaderReflection(const GPUShaderReflection *src,
                         GPUShaderReflection *dst) {
  const GPUShaderResourceReflection *srcResources;
  GPUShaderResourceReflection *dstResources;
  uint8_t *storage;
  char *text;
  size_t resourceBytes;
  size_t textBytes;

  if (!src || !dst) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(dst, 0, sizeof(*dst));
  dst->pushConstantSizeBytes = src->pushConstantSizeBytes;
  if (src->resourceCount == 0u) {
    return GPU_OK;
  }
  if (!src->pResources ||
      (size_t)src->resourceCount > SIZE_MAX / sizeof(*dstResources)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  srcResources  = src->pResources;
  resourceBytes = (size_t)src->resourceCount * sizeof(*dstResources);
  textBytes     = 0u;
  for (uint32_t i = 0u; i < src->resourceCount; i++) {
    const char *name = srcResources[i].name ? srcResources[i].name : "";
    size_t size = strlen(name) + 1u;

    if (textBytes > SIZE_MAX - size) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    textBytes += size;
  }
  if (resourceBytes > SIZE_MAX - textBytes) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  storage = calloc(1, resourceBytes + textBytes);
  if (!storage) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  dstResources = (GPUShaderResourceReflection *)storage;
  text         = (char *)(storage + resourceBytes);
  for (uint32_t i = 0u; i < src->resourceCount; i++) {
    const char *name = srcResources[i].name ? srcResources[i].name : "";
    size_t size = strlen(name) + 1u;

    dstResources[i] = srcResources[i];
    dstResources[i].name = text;
    memcpy(text, name, size);
    text += size;
  }

  dst->resourceCount = src->resourceCount;
  dst->pResources = dstResources;
  return GPU_OK;
}

GPU_HIDE
GPUShaderFunction*
gpuShaderFunction(GPUShaderLibrary *lib, const char *name) {
  GPUApi *api;

  if (!lib || !name || !(api = lib->_api) || !api->library.newFunction)
    return NULL;

  return api->library.newFunction(lib, name);
}

GPU_HIDE
void
gpuDestroyShaderFunction(GPUShaderLibrary  *library,
                         GPUShaderFunction *function) {
  GPUApi *api;

  if (!library || !function || !(api = library->_api) ||
      !api->library.destroyFunction) {
    return;
  }
  api->library.destroyFunction(function);
}

static GPUResult
gpu_createShaderLibraryFromUSLImpl(GPUDevice *device,
                                   const void *bytecodeData,
                                   uint64_t bytecodeSize,
                                   bool disableDiskCache,
                                   GPUShaderLibrary **outLibrary);

static GPUResult
gpu_createShaderLibraryFromBackendText(GPUDevice        *device,
                                       const void       *sourceData,
                                       uint64_t          sourceSize,
                                       uint32_t          defineCount,
                                       GPUShaderLibrary **outLibrary) {
  GPUApi *api;

  if (!(api = gpuDeviceApi(device))) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (!api->library.newLibraryWithSource || defineCount > 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!sourceData || sourceSize == 0u ||
      sourceSize > (uint64_t)SIZE_MAX) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  *outLibrary = api->library.newLibraryWithSource(device,
                                                   sourceData,
                                                   sourceSize);

  if (*outLibrary) {
    (*outLibrary)->_api    = api;
    (*outLibrary)->_device = device;
  }

  return *outLibrary ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
gpu_createShaderLibraryFromMSLText(GPUDevice *device,
                                   const GPUShaderLibraryCreateInfo *info,
                                   GPUShaderLibrary **outLibrary) {
  GPUApi *api;

  api = gpuDeviceApi(device);
  if (!api || api->backend != GPU_BACKEND_METAL) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return gpu_createShaderLibraryFromBackendText(device,
                                                info->sourceData,
                                                info->sourceSize,
                                                info->defineCount,
                                                outLibrary);
}

static GPUResult
gpu_createShaderLibraryFromWGSLText(GPUDevice *device,
                                    const GPUShaderLibraryCreateInfo *info,
                                    GPUShaderLibrary **outLibrary) {
  GPUApi *api;

  api = gpuDeviceApi(device);
  if (!api || api->backend != GPU_BACKEND_WEBGPU) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return gpu_createShaderLibraryFromBackendText(device,
                                                info->sourceData,
                                                info->sourceSize,
                                                info->defineCount,
                                                outLibrary);
}

static GPUResult
gpu_createShaderLibraryFromBinary(GPUDevice *device,
                                  const GPUShaderLibraryCreateInfo *info,
                                  GPUShaderLibrary **outLibrary) {
  GPUApi *api;

  if (!(api = gpuDeviceApi(device))) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (!api->library.newLibraryWithBinary || info->defineCount > 0u ||
      info->sourceSize > (uint64_t)SIZE_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLibrary = api->library.newLibraryWithBinary(device,
                                                   info->sourceData,
                                                   info->sourceSize);
  if (*outLibrary) {
    (*outLibrary)->_api    = api;
    (*outLibrary)->_device = device;
  }
  return *outLibrary ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

GPU_EXPORT
GPUResult
GPUCreateShaderLibrary(GPUDevice *device,
                       const GPUShaderLibraryCreateInfo *info,
                       GPUShaderLibrary **outLibrary) {
  if (!outLibrary) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLibrary = NULL;
  if (!device || !info || !info->sourceData || info->sourceSize == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0u &&
      info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  switch (info->sourceKind) {
    case GPU_SHADER_SOURCE_MSL_TEXT:
      return gpu_createShaderLibraryFromMSLText(device, info, outLibrary);
    case GPU_SHADER_SOURCE_WGSL_TEXT:
      return gpu_createShaderLibraryFromWGSLText(device, info, outLibrary);
    case GPU_SHADER_SOURCE_USL_BYTECODE:
      return gpu_createShaderLibraryFromUSLImpl(device,
                                                info->sourceData,
                                                info->sourceSize,
                                                info->disableDiskCache,
                                                outLibrary);
    case GPU_SHADER_SOURCE_USL_TEXT:
      return GPU_ERROR_INVALID_ARGUMENT;
    case GPU_SHADER_SOURCE_SPIRV_BINARY:
      return gpu_createShaderLibraryFromBinary(device, info, outLibrary);
    default:
      return GPU_ERROR_INVALID_ARGUMENT;
  }
}

static GPUResult
gpu_createShaderLibraryFromUSLImpl(GPUDevice *device,
                                   const void *bytecodeData,
                                   uint64_t bytecodeSize,
                                   bool disableDiskCache,
                                   GPUShaderLibrary **outLibrary) {
  GPUShaderLibraryCreateInfo info = {0};
  GPUApi                   *api;
  USCompileOutput          *compileOutput;
  USLCompileOptions         compileOptions;
  USLTargetSpec             target;
  USLCapabilityAtomDesc     targetAtoms[14];
  USCompileInput            compileInput;
  const char               *payloadSource;
  GPUResult                 rc;
  uint32_t                  targetAtomCount;
  uint32_t                  encoding;

  if (!device || !bytecodeData || !outLibrary) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outLibrary = NULL;

  if (!(api = gpuDeviceApi(device))) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (!gpu_uslDefaultTarget(api->backend, &target)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  targetAtomCount = 0u;
  if (api->backend == GPU_BACKEND_VULKAN) {
    if (device->uslUntypedPointers) {
      target.profile = USL_TARGET_PROFILE_VULKAN_1_4;
    } else if (GPUIsFeatureEnabled(device, GPU_FEATURE_EXECUTION_GRAPH)) {
      target.profile = USL_TARGET_PROFILE_VULKAN_1_3;
    } else if (GPUIsFeatureEnabled(device, GPU_FEATURE_ATOMIC64) ||
        GPUIsFeatureEnabled(device, GPU_FEATURE_SHADER_F16)) {
      target.profile = USL_TARGET_PROFILE_VULKAN_1_2;
    }
    if (GPUIsFeatureEnabled(device, GPU_FEATURE_SHADER_F16)) {
      if (us_cap_atom_init(
            &targetAtoms[targetAtomCount++],
            USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
            USL_SEMANTIC_FEATURE_ID_SHADER_F16,
            0u,
            0u) != USLOk) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    } else if (GPUIsFeatureEnabled(device, GPU_FEATURE_SUBGROUPS)) {
      target.profile = USL_TARGET_PROFILE_VULKAN_1_1;
    }
    if (GPUIsFeatureEnabled(device, GPU_FEATURE_SUBGROUPS)) {
      if (us_cap_atom_init(
            &targetAtoms[targetAtomCount++],
            USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
            USL_SEMANTIC_FEATURE_ID_SUBGROUP,
            0u,
            0u) != USLOk) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
    if (GPUIsFeatureEnabled(device, GPU_FEATURE_SHADER_SUBGROUP_CLOCK)) {
      if (us_cap_atom_init(
            &targetAtoms[targetAtomCount++],
            USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
            USL_SEMANTIC_FEATURE_ID_SHADER_SUBGROUP_CLOCK,
            0u,
            0u) != USLOk) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
    if (GPUIsFeatureEnabled(device, GPU_FEATURE_SHADER_DEVICE_CLOCK)) {
      if (us_cap_atom_init(
            &targetAtoms[targetAtomCount++],
            USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
            USL_SEMANTIC_FEATURE_ID_SHADER_DEVICE_CLOCK,
            0u,
            0u) != USLOk) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
    if (GPUIsFeatureEnabled(device,
                            GPU_FEATURE_COMPUTE_DERIVATIVES_QUADS)) {
      if (us_cap_atom_init(
            &targetAtoms[targetAtomCount++],
            USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
            USL_SEMANTIC_FEATURE_ID_COMPUTE_DERIVATIVES_QUADS,
            0u,
            0u) != USLOk) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
    if (GPUIsFeatureEnabled(device,
                            GPU_FEATURE_COMPUTE_DERIVATIVES_LINEAR)) {
      if (us_cap_atom_init(
            &targetAtoms[targetAtomCount++],
            USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
            USL_SEMANTIC_FEATURE_ID_COMPUTE_DERIVATIVES_LINEAR,
            0u,
            0u) != USLOk) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
    if (device->uslUntypedPointers) {
      if (us_cap_atom_init(
            &targetAtoms[targetAtomCount++],
            USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
            USL_SEMANTIC_FEATURE_ID_UNTYPED_POINTERS,
            0u,
            0u) != USLOk) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
  } else if (api->backend == GPU_BACKEND_DX12) {
    if (GPUIsFeatureEnabled(device, GPU_FEATURE_SUBGROUP_MATRIX)) {
      target.profile = USL_TARGET_PROFILE_HLSL_SM_6_10;
    } else if (GPUIsFeatureEnabled(device, GPU_FEATURE_EXECUTION_GRAPH)) {
      target.profile = USL_TARGET_PROFILE_HLSL_SM_6_8;
    } else if (GPUIsFeatureEnabled(device, GPU_FEATURE_ATOMIC64)) {
      target.profile = USL_TARGET_PROFILE_HLSL_SM_6_6;
    } else if (GPUIsFeatureEnabled(device, GPU_FEATURE_RAY_QUERY) ||
               GPUIsFeatureEnabled(device, GPU_FEATURE_SAMPLER_FEEDBACK)) {
      target.profile = USL_TARGET_PROFILE_HLSL_SM_6_5;
    } else if (GPUIsFeatureEnabled(device,
                                   GPU_FEATURE_RAY_TRACING_PIPELINE)) {
      target.profile = USL_TARGET_PROFILE_HLSL_SM_6_3;
    } else if (GPUIsFeatureEnabled(device, GPU_FEATURE_SHADER_F16)) {
      target.profile = USL_TARGET_PROFILE_HLSL_SM_6_2;
    } else if (GPUIsFeatureEnabled(device,
                                   GPU_FEATURE_DESCRIPTOR_INDEXING)) {
      target.profile = USL_TARGET_PROFILE_HLSL_SM_6_0;
    }
    if (GPUIsFeatureEnabled(device, GPU_FEATURE_SHADER_F16)) {
      if (us_cap_atom_init(
            &targetAtoms[targetAtomCount++],
            USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
            USL_SEMANTIC_FEATURE_ID_SHADER_F16,
            0u,
            0u) != USLOk) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
    if (GPUIsFeatureEnabled(device, GPU_FEATURE_SUBGROUPS)) {
      if (us_cap_atom_init(
            &targetAtoms[targetAtomCount++],
            USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
            USL_SEMANTIC_FEATURE_ID_SUBGROUP,
            0u,
            0u) != USLOk) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
  } else if (api->backend == GPU_BACKEND_METAL) {
    if (GPUIsFeatureEnabled(device, GPU_FEATURE_SUBGROUPS)) {
      if (us_cap_atom_init(
            &targetAtoms[targetAtomCount++],
            USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
            USL_SEMANTIC_FEATURE_ID_SUBGROUP,
            0u,
            0u) != USLOk) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
    if (GPUIsFeatureEnabled(device, GPU_FEATURE_SHADER_F16)) {
      if (us_cap_atom_init(
            &targetAtoms[targetAtomCount++],
            USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
            USL_SEMANTIC_FEATURE_ID_SHADER_F16,
            0u,
            0u) != USLOk) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
  }
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_DESCRIPTOR_INDEXING)) {
    if (us_cap_atom_init(
          &targetAtoms[targetAtomCount++],
          USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
          USL_SEMANTIC_FEATURE_ID_DESCRIPTOR_INDEXING,
          0u,
          0u) != USLOk) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_RAY_QUERY)) {
    if (us_cap_atom_init(
          &targetAtoms[targetAtomCount++],
          USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
          USL_SEMANTIC_FEATURE_ID_RAY_QUERY,
          0u,
          0u) != USLOk) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_RAY_TRACING_PIPELINE)) {
    if (us_cap_atom_init(
          &targetAtoms[targetAtomCount++],
          USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
          USL_SEMANTIC_FEATURE_ID_RAY_TRACING_PIPELINE,
          0u,
          0u) != USLOk) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_SUBGROUP_MATRIX)) {
    if (us_cap_atom_init(
          &targetAtoms[targetAtomCount++],
          USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
          USL_SEMANTIC_FEATURE_ID_SUBGROUP_MATRIX,
          0u,
          0u) != USLOk) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_ATOMIC64)) {
    if (us_cap_atom_init(
          &targetAtoms[targetAtomCount++],
          USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
          USL_SEMANTIC_FEATURE_ID_ATOMIC64,
          0u,
          0u) != USLOk) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_EXECUTION_GRAPH)) {
    if (us_cap_atom_init(
          &targetAtoms[targetAtomCount++],
          USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
          USL_SEMANTIC_FEATURE_ID_EXECUTION_GRAPH,
          0u,
          0u) != USLOk) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_SAMPLER_FEEDBACK)) {
    if (us_cap_atom_init(
          &targetAtoms[targetAtomCount++],
          USL_CAPABILITY_ATOM_FAMILY_SEMANTIC_FEATURE,
          USL_SEMANTIC_FEATURE_ID_SAMPLER_FEEDBACK,
          0u,
          0u) != USLOk) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
  if (targetAtomCount > 0u &&
      us_target_extra_atoms(&target,
                            targetAtoms,
                            targetAtomCount) != USLOk) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (us_compile_options_from_env(&compileOptions) != USLOk) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  encoding   = target.backend == USL_BACKEND_SPIRV
                 ? USL_RUNTIME_EMBEDDED_BLOB_ENCODING_BINARY
                 : USL_RUNTIME_EMBEDDED_BLOB_ENCODING_TEXT;
  compileOutput = malloc(sizeof(*compileOutput));
  if (!compileOutput) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  memset(&compileInput, 0, sizeof(compileInput));
  compileInput.abi_version   = US_COMPILE_INPUT_VERSION;
  compileInput.artifact      = bytecodeData;
  compileInput.artifact_size = (size_t)bytecodeSize;
  compileInput.target        = &target;
  compileInput.options       = &compileOptions;
  if (disableDiskCache) {
    compileInput.flags |= US_COMPILE_INPUT_FLAG_DISABLE_DISK_CACHE;
  }
  if (us_compile(&compileInput, compileOutput) != USLOk ||
      compileOutput->backend != target.backend ||
      compileOutput->encoding != encoding ||
      !compileOutput->backend_data ||
      compileOutput->backend_size == 0) {
    rc = GPU_ERROR_BACKEND_FAILURE;
    goto cleanup;
  }
  if (getenv("GPU_USL_LOG")) {
    payloadSource =
      (compileOutput->flags & US_COMPILE_OUTPUT_FLAG_EMBEDDED_BLOB) != 0u
        ? "embedded"
        : (compileOutput->flags & US_COMPILE_OUTPUT_FLAG_DISK_CACHE) != 0u
            ? "disk-cache"
            : "generated";
    fprintf(stderr,
            "GPU: USL %s payload (%zu bytes, %016llx)\n",
            payloadSource,
            compileOutput->backend_size,
            (unsigned long long)compileOutput->backend_hash);
  }
  if (!compileOutput->reflection.target_info_valid) {
    rc = GPU_ERROR_BACKEND_FAILURE;
    goto cleanup;
  }
  if (!gpu_shaderRequirementsEnabled(device,
                                     &compileOutput->reflection.runtime)) {
    rc = GPU_ERROR_UNSUPPORTED;
    goto cleanup;
  }

  if (target.backend == USL_BACKEND_SPIRV) {
    info.chain.sType      = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
    info.chain.structSize = sizeof(info);
    info.sourceKind       = GPU_SHADER_SOURCE_SPIRV_BINARY;
    info.sourceData       = compileOutput->backend_data;
    info.sourceSize       = compileOutput->backend_size;
    rc = gpu_createShaderLibraryFromBinary(device, &info, outLibrary);
  } else if (target.backend == USL_BACKEND_METAL ||
             target.backend == USL_BACKEND_HLSL ||
             target.backend == USL_BACKEND_WGSL) {
    rc = gpu_createShaderLibraryFromBackendText(device,
                                                compileOutput->backend_data,
                                                compileOutput->backend_size,
                                                0u,
                                                outLibrary);
  } else {
    rc = GPU_ERROR_UNSUPPORTED;
  }
  if (rc == GPU_OK && outLibrary && *outLibrary) {
    if (!gpu_setShaderLibraryMetadata(*outLibrary,
                                      &compileOutput->reflection)) {
      GPUDestroyShaderLibrary(*outLibrary);
      *outLibrary = NULL;
      rc = GPU_ERROR_BACKEND_FAILURE;
    }
  }

cleanup:
  us_free_compile_output(compileOutput);
  free(compileOutput);
  return rc;
}

GPU_EXPORT
GPUResult
GPUCreateShaderLibraryFromUSL(GPUDevice *device,
                              const void *artifactData,
                              uint64_t artifactSize,
                              GPUShaderLibrary **outLibrary) {
  if (!outLibrary) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLibrary = NULL;
  if (!device || !artifactData || artifactSize == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return gpu_createShaderLibraryFromUSLImpl(device,
                                            artifactData,
                                            artifactSize,
                                            false,
                                            outLibrary);
}

GPU_EXPORT
GPUResult
GPUGetShaderReflection(const GPUShaderLibrary *library,
                       GPUShaderReflection *outReflection) {
  if (!library || !outReflection) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return gpu_copyShaderReflection(&library->_reflection, outReflection);
}

GPU_EXPORT
void
GPUFreeShaderReflection(GPUShaderReflection *reflection) {
  gpu_clearShaderReflection(reflection);
}

GPU_EXPORT
void
GPUDestroyShaderLibrary(GPUShaderLibrary *library) {
  GPUApi *api;

  if (!library)
    return;

  gpu_clearShaderMetadata(library);
  if (!(api = library->_api)) {
    free(library);
    return;
  }

  if (api->library.destroyLibrary) {
    api->library.destroyLibrary(library);
  } else {
    free(library);
  }
}
