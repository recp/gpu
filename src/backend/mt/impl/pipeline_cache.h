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

#ifndef mt_pipeline_cache_h
#define mt_pipeline_cache_h

GPU_HIDE
bool
mt_useRenderCache(GPUPipelineCache            *cache,
                  MTLRenderPipelineDescriptor *descriptor);

GPU_HIDE
bool
mt_addRenderCache(GPUPipelineCache            *cache,
                  MTLRenderPipelineDescriptor *descriptor);

GPU_HIDE
bool
mt_useComputeCache(GPUPipelineCache             *cache,
                   MTLComputePipelineDescriptor *descriptor);

GPU_HIDE
bool
mt_addComputeCache(GPUPipelineCache             *cache,
                   MTLComputePipelineDescriptor *descriptor);

#endif /* mt_pipeline_cache_h */
