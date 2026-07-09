#ifndef gpu_sample_usl_h
#define gpu_sample_usl_h

#import <Foundation/Foundation.h>
#include <stdlib.h>
#include <string.h>
#import <mach-o/dyld.h>

#import "../../include/gpu/gpu.h"

static inline NSString *
GPUSampleDir(void) {
  NSString *executablePath;
  uint32_t sizeBytes = 0;

  _NSGetExecutablePath(NULL, &sizeBytes);
  char *buffer = malloc(sizeBytes);
  if (!buffer) {
    return nil;
  }
  if (_NSGetExecutablePath(buffer, &sizeBytes) != 0) {
    free(buffer);
    return nil;
  }

  executablePath = [[NSFileManager defaultManager]
                    stringWithFileSystemRepresentation:buffer
                    length:strlen(buffer)];
  free(buffer);
  return [executablePath stringByDeletingLastPathComponent];
}

static inline BOOL
GPUSampleLoadUSL(GPUDevice *device,
                 NSString *artifactName,
                 uint32_t expectedLayoutCount,
                 GPUShaderLibrary **outLibrary,
                 GPUShaderLayout **outLayout) {
  NSString *sampleDir;
  NSString *artifactPath;
  NSData *artifactData;

  if (!device || !artifactName || !outLibrary || !outLayout) {
    return NO;
  }

  *outLibrary = NULL;
  *outLayout = NULL;
  sampleDir = GPUSampleDir();
  if (!sampleDir) {
    NSLog(@"GPU: failed to resolve sample directory");
    return NO;
  }

  artifactPath = [sampleDir stringByAppendingPathComponent:artifactName];
  artifactData = [NSData dataWithContentsOfFile:artifactPath];
  if (!artifactData) {
    NSLog(@"GPU: failed to load USL bytecode at %@", artifactPath);
    return NO;
  }

  GPUShaderLibraryCreateInfo shaderInfo = {0};
  shaderInfo.chain.sType = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  shaderInfo.chain.structSize = sizeof(shaderInfo);
  shaderInfo.label = artifactName.UTF8String;
  shaderInfo.sourceKind = GPU_SHADER_SOURCE_USL_BYTECODE;
  shaderInfo.sourceData = artifactData.bytes;
  shaderInfo.sourceSize = (uint64_t)artifactData.length;
  shaderInfo.sourcePathHint = artifactPath.UTF8String;
  shaderInfo.generateReflection = true;

  if (GPUCreateShaderLibrary(device, &shaderInfo, outLibrary) != GPU_OK) {
    NSLog(@"GPU: failed to create shader library");
    return NO;
  }

  if (GPUCreateShaderLayout(device, *outLibrary, outLayout) != GPU_OK ||
      !*outLayout ||
      (*outLayout)->bindGroupLayoutCount != expectedLayoutCount ||
      (expectedLayoutCount > 0u && !(*outLayout)->bindGroupLayouts[0]) ||
      !(*outLayout)->pipelineLayout) {
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
