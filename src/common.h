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

#ifndef src_common_h
#define src_common_h

#ifdef __GNUC__
#  define GPU_DESTRUCTOR  __attribute__((destructor))
#  define GPU_CONSTRUCTOR __attribute__((constructor))
#else
#  define GPU_DESTRUCTOR
#  define GPU_CONSTRUCTOR
#endif

#define GPU__UNUSED(X) (void)X

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(WIN32)
#  define WIN32_LEAN_AND_MEAN 
#  include <SDKDDKVer.h>
#  include <windows.h>
#endif

#include "../include/gpu/gpu.h"
#include "../include/gpu/api/gpudef.h"

#endif /* src_common_h */
