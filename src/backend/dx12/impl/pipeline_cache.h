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

#ifndef dx12_pipeline_cache_h
#define dx12_pipeline_cache_h

typedef struct DX12PipelineKey {
  uint64_t value[2];
} DX12PipelineKey;

GPU_HIDE
void
dx12_keyInit(DX12PipelineKey *key);

GPU_HIDE
void
dx12_keyWrite(DX12PipelineKey *key, const void *data, size_t size);

GPU_HIDE
GPUResult
dx12_createGraphicsPSO(GPUPipelineCache                         *cache,
                       GPUDeviceDX12                           *device,
                       const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
                       const GPURenderPipelineCreateInfo       *info,
                       const DX12PipelineKey                   *rootKey,
                       ID3D12PipelineState                    **outState);

GPU_HIDE
GPUResult
dx12_createComputePSO(GPUPipelineCache                        *cache,
                      GPUDeviceDX12                          *device,
                      const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
                      const DX12PipelineKey                  *rootKey,
                      ID3D12PipelineState                   **outState);

GPU_HIDE
GPUResult
dx12_createMeshPSO(GPUPipelineCache                        *cache,
                   GPUDeviceDX12                          *device,
                   const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                   D3D12_CACHED_PIPELINE_STATE            *cachedPSO,
                   const GPURenderPipelineCreateInfo      *info,
                   const DX12PipelineKey                  *rootKey,
                   const DX12ShaderCode                   *taskCode,
                   const DX12ShaderCode                   *meshCode,
                   const DX12ShaderCode                   *fragmentCode,
                   ID3D12PipelineState                   **outState);

#endif /* dx12_pipeline_cache_h */
