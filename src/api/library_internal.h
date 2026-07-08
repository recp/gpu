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

#ifndef gpu_library_internal_h
#define gpu_library_internal_h

#include "../common.h"

GPU_HIDE
int
gpuGetShaderLibraryComputeWorkgroupSize(const GPUShaderLibrary *library,
                                        const char *entryPoint,
                                        uint32_t outSize[3]);

GPU_HIDE
int
gpuGetShaderLibraryEntryStage(const GPUShaderLibrary *library,
                              const char *entryPoint,
                              GPUShaderStageFlags *outStage);

#endif /* gpu_library_internal_h */
