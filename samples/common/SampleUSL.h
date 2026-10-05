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

#ifndef gpu_sample_usl_h
#define gpu_sample_usl_h

#import <Foundation/Foundation.h>
#include <stdlib.h>
#include <string.h>
#import <mach-o/dyld.h>

#import "../../include/gpu/gpu.h"

static inline NSString*
GPUSampleDir(void) {
  NSString *executablePath;
  char     *buffer;
  uint32_t  sizeBytes = 0;

  _NSGetExecutablePath(NULL, &sizeBytes);

  if (!(buffer = malloc(sizeBytes))) {
    return nil;
  }

  if (_NSGetExecutablePath(buffer, &sizeBytes) != 0) {
    free(buffer);
    return nil;
  }

  executablePath = [[NSFileManager defaultManager] stringWithFileSystemRepresentation:buffer
                                                                              length:strlen(buffer)];
  free(buffer);

  return [executablePath stringByDeletingLastPathComponent];
}

static inline BOOL
GPUSampleLoadUSL(GPUDevice         *device,
                 NSString          *artifactName,
                 uint32_t           expectedLayoutCount,
                 GPUShaderLibrary **outLibrary,
                 GPUShaderLayout  **outLayout) {
  NSString *sampleDir;
  NSString *artifactPath;
  NSData   *artifactData;

  if (!device || !artifactName || !outLibrary || !outLayout) {
    return NO;
  }

  *outLibrary = NULL;
  *outLayout  = NULL;

  if (!(sampleDir = GPUSampleDir())) {
    NSLog(@"GPU: failed to resolve sample directory");
    return NO;
  }

  artifactPath = [sampleDir stringByAppendingPathComponent:artifactName];

  if (!(artifactData = [NSData dataWithContentsOfFile:artifactPath])) {
    NSLog(@"GPU: failed to load USL bytecode at %@", artifactPath);
    return NO;
  }

  if (GPUCreateShaderLibraryFromUSL(device,
                                   artifactData.bytes,
                                   (uint64_t)artifactData.length,
                                   outLibrary) != GPU_OK) {
    NSLog(@"GPU: failed to create shader library");
    return NO;
  }

  if (GPUCreateShaderLayout(device, *outLibrary, outLayout) != GPU_OK
      || !*outLayout
      || (*outLayout)->bindGroupLayoutCount != expectedLayoutCount
      || (expectedLayoutCount > 0u && !(*outLayout)->bindGroupLayouts[0])
      || !(*outLayout)->pipelineLayout) {
    NSLog(@"GPU: failed to create shader layout");

    if (*outLayout) {
      GPUDestroyShaderLayout(*outLayout);
      *outLayout = NULL;
    }

    GPUDestroyShaderLibrary(*outLibrary);
    *outLibrary = NULL;
    return NO;
  }

  return YES;
}

#endif
