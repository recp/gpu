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

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalPerformanceShadersGraph/MetalPerformanceShadersGraph.h>

#include <stdio.h>

static int
make_model(NSString *path) {
  MPSGraph                                  *graph;
  MPSGraphTensor                            *a;
  MPSGraphTensor                            *output;
  MPSGraphShapedType                        *shape;
  MPSGraphExecutable                        *executable;
  MPSGraphExecutableSerializationDescriptor *serialization;
  NSURL                                     *url;

  graph  = [MPSGraph new];
  a      = [graph placeholderWithShape:@[@8, @16] dataType:MPSDataTypeFloat32 name:@"inputA"];
  output = [graph identityWithTensor:a name:@"output"];
  shape  = [[MPSGraphShapedType alloc] initWithShape:@[@8, @16] dataType:MPSDataTypeFloat32];

  executable = [graph compileWithDevice:nil
                                 feeds:@{a: shape}
                         targetTensors:@[output]
                      targetOperations:nil
                 compilationDescriptor:nil];

  if (!executable)
    return 0;

  serialization                         = [MPSGraphExecutableSerializationDescriptor new];
  serialization.minimumDeploymentTarget = @"26.0";

  url = [NSURL fileURLWithPath:[path stringByAppendingPathComponent:@"library.mpsgraphpackage"]];
  [executable serializeToMPSGraphPackageAtURL:url descriptor:serialization];
  return [[NSFileManager defaultManager] fileExistsAtPath:url.path];
}

static int
check_pipeline(NSString *path) {
  NSInteger                              dimensions[2];
  id<MTLDevice>                          device;
  id<MTLLibrary>                         library;
  id<MTL4Compiler>                       compiler;
  id<MTL4MachineLearningPipelineState>   pipeline;
  MTL4CompilerDescriptor                *compilerInfo;
  MTL4LibraryFunctionDescriptor         *function;
  MTL4MachineLearningPipelineDescriptor *descriptor;
  MTL4PipelineOptions                   *options;
  MTLFunctionReflection                 *reflection;
  id<MTLBinding>                         binding;
  id<MTLTensorBinding>                   tensor;
  MTLTensorExtents                      *extents;
  NSError                               *error = nil;

  device  = MTLCreateSystemDefaultDevice();
  library = [device newLibraryWithURL:[NSURL fileURLWithPath:path] error:&error];

  if (!library) {
    fprintf(stderr, "library failed: %s\n", error.localizedDescription.UTF8String);
    return 0;
  }

  compilerInfo = [MTL4CompilerDescriptor new];
  compiler     = [device newCompilerWithDescriptor:compilerInfo error:&error];

  function         = [MTL4LibraryFunctionDescriptor new];
  function.library = library;
  function.name    = @"main";

  options                  = [MTL4PipelineOptions new];
  options.shaderReflection = MTL4ShaderReflectionBindingInfo;

  descriptor                                   = [MTL4MachineLearningPipelineDescriptor new];
  descriptor.machineLearningFunctionDescriptor = function;
  descriptor.options                           = options;

  reflection = [library reflectionForFunctionWithName:@"main"];

  if (!compiler || !reflection)
    return 0;

  for (binding in reflection.bindings) {
    tensor = (id<MTLTensorBinding>)binding;
    printf("binding name=%s slot=%lu access=%lu rank=%lu\n", binding.name.UTF8String,
           (unsigned long)binding.index, (unsigned long)binding.access,
           (unsigned long)tensor.dimensions.rank);

    if (binding.access == MTLBindingAccessWriteOnly)
      continue;

    dimensions[0] = 16;
    dimensions[1] = 8;
    extents       = [[MTLTensorExtents alloc] initWithRank:2 values:dimensions];
    [descriptor setInputDimensions:extents atBufferIndex:binding.index];
  }

  pipeline = [compiler newMachineLearningPipelineStateWithDescriptor:descriptor error:&error];

  if (!pipeline) {
    fprintf(stderr, "pipeline failed: %s\n", error.localizedDescription.UTF8String);
    return 0;
  }

  printf("native scratch=%lu\n", (unsigned long)pipeline.intermediatesHeapSize);
  return 1;
}

int
main(int argc, const char *argv[]) {
  NSString *path;
  NSString *manifest;
  NSError  *error = nil;

  if (argc != 2)
    return 2;

  @autoreleasepool {
    path = [NSString stringWithUTF8String:argv[1]];

    if (![[NSFileManager defaultManager] createDirectoryAtPath:path
                                 withIntermediateDirectories:YES
                                                  attributes:nil
                                                       error:&error] || !make_model(path))
      return 1;

    manifest = @"{\"mtlpackage\":{\"version\":{\"major\":1,\"minor\":0,\"patch\":0},"
                "\"pkgtype\":\"MLLibrary\",\"content\":{\"mpspkgname\":\"library.mpsgraphpackage\"}}}";

    if (![manifest writeToFile:[path stringByAppendingPathComponent:@"manifest.json"]
                   atomically:YES
                     encoding:NSUTF8StringEncoding
                        error:&error])
      return 1;

    return check_pipeline(path) ? 0 : 1;
  }
}
