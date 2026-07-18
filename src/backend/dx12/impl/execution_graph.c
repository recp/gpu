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

#include "../common.h"
#include "../impl.h"

#if GPU_DX12_HAS_EXECUTION_GRAPHS

typedef struct GPUExecutionGraphDX12 {
  ID3D12StateObject                     *stateObject;
  ID3D12StateObjectProperties1          *stateProperties;
  ID3D12WorkGraphProperties             *graphProperties;
  ID3D12RootSignature                   *rootSignature;
  GPUExecutionGraphEntryEXT             *entries;
  D3D12_PROGRAM_IDENTIFIER               programIdentifier;
  uint32_t                               graphIndex;
  uint32_t                               entryCount;
} GPUExecutionGraphDX12;

typedef struct GPUExecutionGraphInstanceDX12 {
  ID3D12Resource            *backingMemory;
  ID3D12Resource            *inputTable;
  D3D12_NODE_CPU_INPUT      *cpuInputs;
  D3D12_NODE_GPU_INPUT      *gpuInputs;
  D3D12_GPU_VIRTUAL_ADDRESS  backingAddress;
  D3D12_GPU_VIRTUAL_ADDRESS  inputTableAddress;
  LONG                       initialized;
  uint32_t                   inputCapacity;
} GPUExecutionGraphInstanceDX12;

static bool
dx12_graphWideName(const char *name, wchar_t outName[256]) {
  int count;

  if (!outName) {
    return false;
  }
  if (!name) {
    name = "main";
  }
  count = MultiByteToWideChar(CP_UTF8,
                              MB_ERR_INVALID_CHARS,
                              name,
                              -1,
                              outName,
                              256);
  return count > 0;
}

static void
dx12_destroyExecutionGraphState(GPUExecutionGraphDX12 *native) {
  if (!native) {
    return;
  }
  if (native->graphProperties) {
    native->graphProperties->lpVtbl->Release(native->graphProperties);
  }
  if (native->stateProperties) {
    native->stateProperties->lpVtbl->Release(native->stateProperties);
  }
  if (native->stateObject) {
    native->stateObject->lpVtbl->Release(native->stateObject);
  }
  if (native->rootSignature) {
    native->rootSignature->lpVtbl->Release(native->rootSignature);
  }
  free(native->entries);
  free(native);
}

static GPUResult
dx12_createExecutionGraph(GPUDevice                            *device,
                          const GPUExecutionGraphCreateInfoEXT *info,
                          GPUExecutionGraphEXT                 *graph) {
  GPUDeviceDX12              *deviceDX12;
  GPUShaderLibraryDX12       *library;
  GPUExecutionGraphDX12      *native;
  D3D12_STATE_SUBOBJECT       subobjects[3] = {0};
  D3D12_DXIL_LIBRARY_DESC     libraryDesc = {0};
  D3D12_SHADER_BYTECODE       bytecode = {0};
  D3D12_GLOBAL_ROOT_SIGNATURE globalRoot = {0};
  D3D12_WORK_GRAPH_DESC       graphDesc = {0};
  D3D12_STATE_OBJECT_DESC     stateDesc = {0};
  D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS requirements = {0};
  DX12ShaderCode              libraryCode = {0};
  wchar_t                     graphName[256];
  uint64_t                    rootKey[2];
  HRESULT                     result;

  deviceDX12 = device ? device->_priv : NULL;
  library    = info && info->library ? info->library->_priv : NULL;
  if (!deviceDX12 || !deviceDX12->executionGraph ||
      !deviceDX12->d3dDevice5 || !library || !graph ||
      !dx12_graphWideName(info->graphName, graphName) ||
      !dx12_compileExecutionGraphLibrary(deviceDX12,
                                         library,
                                         &libraryCode)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    dx12_freeShaderCode(&libraryCode);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  if (dx12_createShaderRootSignature(device,
                                     info->layout,
                                     info->library,
                                     &native->rootSignature,
                                     rootKey) != GPU_OK) {
    dx12_destroyExecutionGraphState(native);
    dx12_freeShaderCode(&libraryCode);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  GPU__UNUSED(rootKey);

  bytecode.pShaderBytecode = libraryCode.data;
  bytecode.BytecodeLength  = libraryCode.size;
  libraryDesc.DXILLibrary  = bytecode;
  globalRoot.pGlobalRootSignature = native->rootSignature;
  graphDesc.ProgramName = graphName;
  graphDesc.Flags       = D3D12_WORK_GRAPH_FLAG_INCLUDE_ALL_AVAILABLE_NODES;

  subobjects[0].Type  = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  subobjects[0].pDesc = &libraryDesc;
  subobjects[1].Type  = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
  subobjects[1].pDesc = &globalRoot;
  subobjects[2].Type  = D3D12_STATE_SUBOBJECT_TYPE_WORK_GRAPH;
  subobjects[2].pDesc = &graphDesc;
  stateDesc.Type          = D3D12_STATE_OBJECT_TYPE_EXECUTABLE;
  stateDesc.NumSubobjects = (UINT)GPU_ARRAY_LEN(subobjects);
  stateDesc.pSubobjects   = subobjects;

  result = deviceDX12->d3dDevice5->lpVtbl->CreateStateObject(
    deviceDX12->d3dDevice5,
    &stateDesc,
    &IID_ID3D12StateObject,
    (void **)&native->stateObject
  );
  dx12_freeShaderCode(&libraryCode);
  if (FAILED(result) || !native->stateObject ||
      FAILED(native->stateObject->lpVtbl->QueryInterface(
        native->stateObject,
        &IID_ID3D12StateObjectProperties1,
        (void **)&native->stateProperties
      )) || !native->stateProperties ||
      FAILED(native->stateObject->lpVtbl->QueryInterface(
        native->stateObject,
        &IID_ID3D12WorkGraphProperties,
        (void **)&native->graphProperties
      )) || !native->graphProperties ||
      !native->stateProperties->lpVtbl->GetProgramIdentifier(
        native->stateProperties,
        &native->programIdentifier,
        graphName
      )) {
    dx12_destroyExecutionGraphState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->graphIndex = native->graphProperties->lpVtbl->GetWorkGraphIndex(
    native->graphProperties,
    graphName
  );
  if (native->graphIndex == UINT_MAX) {
    dx12_destroyExecutionGraphState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  native->entryCount = native->graphProperties->lpVtbl->GetNumEntrypoints(
    native->graphProperties,
    native->graphIndex
  );
  if (native->entryCount == 0u ||
      native->entryCount > SIZE_MAX / sizeof(*native->entries)) {
    dx12_destroyExecutionGraphState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  native->entries = calloc(native->entryCount, sizeof(*native->entries));
  if (!native->entries) {
    dx12_destroyExecutionGraphState(native);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  for (uint32_t i = 0u; i < native->entryCount; i++) {
    uint32_t alignment;

    alignment = native->graphProperties->lpVtbl
      ->GetEntrypointRecordAlignmentInBytes(native->graphProperties,
                                             native->graphIndex,
                                             i);
    native->entries[i].index                = i;
    native->entries[i].recordSizeBytes      = native->graphProperties->lpVtbl
      ->GetEntrypointRecordSizeInBytes(native->graphProperties,
                                        native->graphIndex,
                                        i);
    native->entries[i].recordAlignmentBytes = alignment ? alignment : 1u;
  }

  native->graphProperties->lpVtbl->GetWorkGraphMemoryRequirements(
    native->graphProperties,
    native->graphIndex,
    &requirements
  );
  graph->memoryRequirements.minSizeBytes = requirements.MinSizeInBytes;
  graph->memoryRequirements.maxSizeBytes = requirements.MaxSizeInBytes;
  graph->memoryRequirements.sizeGranularityBytes =
    requirements.SizeGranularityInBytes;
  graph->_priv = native;
  return GPU_OK;
}

static void
dx12_destroyExecutionGraph(GPUExecutionGraphEXT *graph) {
  if (!graph) {
    return;
  }
  dx12_destroyExecutionGraphState(graph->_priv);
  graph->_priv = NULL;
}

static GPUResult
dx12_createGraphBuffer(GPUDeviceDX12       *device,
                       uint64_t             sizeBytes,
                       D3D12_HEAP_TYPE      heapType,
                       D3D12_RESOURCE_FLAGS flags,
                       D3D12_RESOURCE_STATES state,
                       ID3D12Resource     **outResource) {
  D3D12_HEAP_PROPERTIES heap = {0};
  D3D12_RESOURCE_DESC   desc = {0};
  HRESULT               result;

  if (!device || !device->d3dDevice || sizeBytes == 0u || !outResource) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outResource = NULL;
  heap.Type             = heapType;
  heap.CreationNodeMask = 1u;
  heap.VisibleNodeMask  = 1u;
  desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width            = sizeBytes;
  desc.Height           = 1u;
  desc.DepthOrArraySize = 1u;
  desc.MipLevels        = 1u;
  desc.SampleDesc.Count = 1u;
  desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags            = flags;
  result = device->d3dDevice->lpVtbl->CreateCommittedResource(
    device->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_NONE,
    &desc,
    state,
    NULL,
    &IID_ID3D12Resource,
    (void **)outResource
  );
  return SUCCEEDED(result) && *outResource
           ? GPU_OK
           : GPU_ERROR_BACKEND_FAILURE;
}

static void
dx12_destroyExecutionGraphInstanceState(
  GPUExecutionGraphInstanceDX12 *native) {
  if (!native) {
    return;
  }
  if (native->inputTable) {
    if (native->gpuInputs) {
      native->inputTable->lpVtbl->Unmap(native->inputTable, 0u, NULL);
    }
    native->inputTable->lpVtbl->Release(native->inputTable);
  }
  if (native->backingMemory) {
    native->backingMemory->lpVtbl->Release(native->backingMemory);
  }
  free(native->cpuInputs);
  free(native);
}

static GPUResult
dx12_createExecutionGraphInstance(
  GPUDevice                                    *device,
  const GPUExecutionGraphInstanceCreateInfoEXT *info,
  GPUExecutionGraphInstanceEXT                 *instance) {
  GPUDeviceDX12                  *deviceDX12;
  GPUExecutionGraphDX12          *graph;
  GPUExecutionGraphInstanceDX12  *native;
  D3D12_RANGE                     noRead = {0};
  uint64_t                        tableSize;
  GPUResult                       result;

  deviceDX12 = device ? device->_priv : NULL;
  graph      = info && info->graph ? info->graph->_priv : NULL;
  if (!deviceDX12 || !deviceDX12->executionGraph || !graph || !instance ||
      graph->entryCount > SIZE_MAX / sizeof(*native->cpuInputs) ||
      graph->entryCount > UINT64_MAX / sizeof(*native->gpuInputs)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->inputCapacity = graph->entryCount;
  native->cpuInputs = calloc(graph->entryCount, sizeof(*native->cpuInputs));
  if (!native->cpuInputs) {
    dx12_destroyExecutionGraphInstanceState(native);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  if (info->memorySizeBytes > 0u) {
    result = dx12_createGraphBuffer(
      deviceDX12,
      info->memorySizeBytes,
      D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COMMON,
      &native->backingMemory
    );
    if (result != GPU_OK) {
      dx12_destroyExecutionGraphInstanceState(native);
      return result;
    }
    native->backingAddress = native->backingMemory->lpVtbl
      ->GetGPUVirtualAddress(native->backingMemory);
    if (!native->backingAddress) {
      dx12_destroyExecutionGraphInstanceState(native);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  tableSize = (uint64_t)graph->entryCount * sizeof(*native->gpuInputs);
  result = dx12_createGraphBuffer(deviceDX12,
                                  tableSize,
                                  D3D12_HEAP_TYPE_UPLOAD,
                                  D3D12_RESOURCE_FLAG_NONE,
                                  D3D12_RESOURCE_STATE_GENERIC_READ,
                                  &native->inputTable);
  if (result != GPU_OK ||
      FAILED(native->inputTable->lpVtbl->Map(native->inputTable,
                                             0u,
                                             &noRead,
                                             (void **)&native->gpuInputs)) ||
      !native->gpuInputs) {
    dx12_destroyExecutionGraphInstanceState(native);
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  native->inputTableAddress = native->inputTable->lpVtbl
    ->GetGPUVirtualAddress(native->inputTable);
  if (!native->inputTableAddress) {
    dx12_destroyExecutionGraphInstanceState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  instance->_priv = native;
  return GPU_OK;
}

static void
dx12_destroyExecutionGraphInstance(GPUExecutionGraphInstanceEXT *instance) {
  if (!instance) {
    return;
  }
  dx12_destroyExecutionGraphInstanceState(instance->_priv);
  instance->_priv = NULL;
}

static GPUResult
dx12_getExecutionGraphEntry(const GPUExecutionGraphEXT *graph,
                            const char                 *entryName,
                            GPUExecutionGraphEntryEXT  *outEntry) {
  GPUExecutionGraphDX12 *native;
  GPUShaderExecutionGraphEntryInfo reflected;
  D3D12_NODE_ID          node = {0};
  wchar_t                name[256];
  uint32_t               index;

  native = graph ? graph->_priv : NULL;
  if (!native || !native->graphProperties || !outEntry ||
      !gpuGetShaderLibraryExecutionGraphEntry(graph->library,
                                              entryName,
                                              &reflected) ||
      !dx12_graphWideName(reflected.nodeName, name)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  node.Name       = name;
  node.ArrayIndex = reflected.nodeIndex;
  index = native->graphProperties->lpVtbl->GetEntrypointIndex(
    native->graphProperties,
    native->graphIndex,
    node
  );
  if (index == UINT_MAX || index >= native->entryCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outEntry = native->entries[index];
  return GPU_OK;
}

static void
dx12_bindExecutionGraph(GPUComputePassEncoder *pass,
                        GPUExecutionGraphEXT  *graph) {
  GPUComputeEncoderDX12 *encoder;
  GPUExecutionGraphDX12 *native;

  encoder = pass ? pass->_priv : NULL;
  native  = graph ? graph->_priv : NULL;
  if (!encoder || !encoder->commandList || !encoder->commandList10 ||
      !native || !native->rootSignature) {
    return;
  }
  encoder->commandList->lpVtbl->SetComputeRootSignature(
    encoder->commandList,
    native->rootSignature
  );
  encoder->rootSignature         = native->rootSignature;
  encoder->executionGraph        = graph;
  encoder->executionGraphInstance = NULL;
}

static bool
dx12_executionGraphEntryMatches(const GPUExecutionGraphDX12       *graph,
                                const GPUExecutionGraphEntryEXT   *entry) {
  const GPUExecutionGraphEntryEXT *expected;

  if (!graph || !entry || entry->index >= graph->entryCount) {
    return false;
  }
  expected = &graph->entries[entry->index];
  return expected->recordSizeBytes == entry->recordSizeBytes &&
         expected->recordAlignmentBytes == entry->recordAlignmentBytes;
}

static bool
dx12_setExecutionGraphInstance(GPUComputePassEncoder        *pass,
                               GPUExecutionGraphInstanceEXT *instance) {
  GPUComputeEncoderDX12          *encoder;
  GPUExecutionGraphDX12          *graph;
  GPUExecutionGraphInstanceDX12  *native;
  D3D12_SET_PROGRAM_DESC          setProgram = {0};

  encoder = pass ? pass->_priv : NULL;
  graph   = instance && instance->graph ? instance->graph->_priv : NULL;
  native  = instance ? instance->_priv : NULL;
  if (!encoder || !encoder->commandList10 || !graph || !native ||
      encoder->executionGraph != instance->graph) {
    return false;
  }
  if (encoder->executionGraphInstance == instance) {
    return true;
  }

  setProgram.Type = D3D12_PROGRAM_TYPE_WORK_GRAPH;
  setProgram.WorkGraph.ProgramIdentifier = graph->programIdentifier;
  setProgram.WorkGraph.Flags =
    InterlockedCompareExchange(&native->initialized, 1, 0) == 0
      ? D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE
      : D3D12_SET_WORK_GRAPH_FLAG_NONE;
  setProgram.WorkGraph.BackingMemory.StartAddress = native->backingAddress;
  setProgram.WorkGraph.BackingMemory.SizeInBytes  = instance->memorySizeBytes;
  encoder->commandList10->lpVtbl->SetProgram(encoder->commandList10,
                                              &setProgram);
  encoder->executionGraphInstance = instance;
  return true;
}

static void
dx12_dispatchExecutionGraph(GPUComputePassEncoder           *pass,
                            GPUExecutionGraphInstanceEXT    *instance,
                            uint32_t                         inputCount,
                            const GPUExecutionGraphInputEXT *inputs) {
  GPUComputeEncoderDX12          *encoder;
  GPUExecutionGraphDX12          *graph;
  GPUExecutionGraphInstanceDX12  *native;
  D3D12_DISPATCH_GRAPH_DESC       desc = {0};

  encoder = pass ? pass->_priv : NULL;
  graph   = instance && instance->graph ? instance->graph->_priv : NULL;
  native  = instance ? instance->_priv : NULL;
  if (!encoder || !graph || !native || !inputs || inputCount == 0u ||
      inputCount > native->inputCapacity ||
      !dx12_setExecutionGraphInstance(pass, instance)) {
    return;
  }

  for (uint32_t i = 0u; i < inputCount; i++) {
    if (!dx12_executionGraphEntryMatches(graph, &inputs[i].entry)) {
      return;
    }
    native->cpuInputs[i].EntrypointIndex = inputs[i].entry.index;
    native->cpuInputs[i].NumRecords      = inputs[i].recordCount;
    native->cpuInputs[i].pRecords        = inputs[i].pRecords;
    native->cpuInputs[i].RecordStrideInBytes =
      inputs[i].recordStrideBytes
        ? inputs[i].recordStrideBytes
        : inputs[i].entry.recordSizeBytes;
  }
  if (inputCount == 1u) {
    desc.Mode         = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
    desc.NodeCPUInput = native->cpuInputs[0];
  } else {
    desc.Mode = D3D12_DISPATCH_MODE_MULTI_NODE_CPU_INPUT;
    desc.MultiNodeCPUInput.NumNodeInputs = inputCount;
    desc.MultiNodeCPUInput.pNodeInputs    = native->cpuInputs;
    desc.MultiNodeCPUInput.NodeInputStrideInBytes =
      sizeof(*native->cpuInputs);
  }
  encoder->commandList10->lpVtbl->DispatchGraph(encoder->commandList10,
                                                 &desc);
}

static void
dx12_dispatchExecutionGraphBuffer(
  GPUComputePassEncoder                 *pass,
  GPUExecutionGraphInstanceEXT          *instance,
  uint32_t                               inputCount,
  const GPUExecutionGraphBufferInputEXT *inputs) {
  GPUComputeEncoderDX12          *encoder;
  GPUExecutionGraphDX12          *graph;
  GPUExecutionGraphInstanceDX12  *native;
  D3D12_DISPATCH_GRAPH_DESC       desc = {0};

  encoder = pass ? pass->_priv : NULL;
  graph   = instance && instance->graph ? instance->graph->_priv : NULL;
  native  = instance ? instance->_priv : NULL;
  if (!encoder || !graph || !native || !inputs || inputCount == 0u ||
      inputCount > native->inputCapacity ||
      !dx12_setExecutionGraphInstance(pass, instance)) {
    return;
  }

  for (uint32_t i = 0u; i < inputCount; i++) {
    GPUBufferDX12 *buffer;

    buffer = inputs[i].records ? inputs[i].records->_priv : NULL;
    if (!buffer || !buffer->gpuAddress ||
        !dx12_executionGraphEntryMatches(graph, &inputs[i].entry)) {
      return;
    }
    native->gpuInputs[i].EntrypointIndex = inputs[i].entry.index;
    native->gpuInputs[i].NumRecords      = inputs[i].recordCount;
    native->gpuInputs[i].Records.StartAddress =
      buffer->gpuAddress + inputs[i].recordOffset;
    native->gpuInputs[i].Records.StrideInBytes =
      inputs[i].recordStrideBytes
        ? inputs[i].recordStrideBytes
        : inputs[i].entry.recordSizeBytes;
  }
  if (inputCount == 1u) {
    desc.Mode         = D3D12_DISPATCH_MODE_NODE_GPU_INPUT;
    desc.NodeGPUInput = native->inputTableAddress;
  } else {
    desc.Mode = D3D12_DISPATCH_MODE_MULTI_NODE_GPU_INPUT;
    desc.MultiNodeGPUInput.NumNodeInputs = inputCount;
    desc.MultiNodeGPUInput.NodeInputs.StartAddress = native->inputTableAddress;
    desc.MultiNodeGPUInput.NodeInputs.StrideInBytes =
      sizeof(*native->gpuInputs);
  }
  encoder->commandList10->lpVtbl->DispatchGraph(encoder->commandList10,
                                                 &desc);
}

#endif

GPU_HIDE
void
dx12_initExecutionGraph(GPUApiExecutionGraph *api) {
#if GPU_DX12_HAS_EXECUTION_GRAPHS
  api->create          = dx12_createExecutionGraph;
  api->destroy         = dx12_destroyExecutionGraph;
  api->createInstance  = dx12_createExecutionGraphInstance;
  api->destroyInstance = dx12_destroyExecutionGraphInstance;
  api->getEntry        = dx12_getExecutionGraphEntry;
  api->bind            = dx12_bindExecutionGraph;
  api->dispatch        = dx12_dispatchExecutionGraph;
  api->dispatchBuffer  = dx12_dispatchExecutionGraphBuffer;
#else
  GPU__UNUSED(api);
#endif
}
