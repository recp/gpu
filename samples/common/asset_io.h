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

#ifndef gpu_sample_asset_io_h
#define gpu_sample_asset_io_h

#include <stddef.h>
#include <stdint.h>

typedef void
(*SampleFetchCallback)(void       *bytes,
                       uint64_t    byteCount,
                       const char *error,
                       void       *userData);

typedef void
(*SampleImageCallback)(uint8_t    *pixels,
                       uint32_t    width,
                       uint32_t    height,
                       const char *error,
                       void       *userData);

int
sample_fetch_url(const char         *url,
                 SampleFetchCallback callback,
                 void               *userData);

int
sample_decode_image(const void         *bytes,
                    uint64_t            byteCount,
                    SampleImageCallback callback,
                    void               *userData);

int
sample_temporary_path(const char *name, char *path, size_t capacity);

#endif
