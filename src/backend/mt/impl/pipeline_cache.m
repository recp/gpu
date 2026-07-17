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
#include "../../../api/pipeline_cache_internal.h"
#include "pipeline_cache.h"

typedef struct MTPipelineCache {
  id<MTLBinaryArchive> archive;
  id                   compiler;
  id                   serializer;
  id                   taskOptions;
  id                   lookupArchive;
  NSArray             *archives;
  NSURL               *url;
  os_unfair_lock       lock;
  bool                 dirty;
  bool                 modern;
} MTPipelineCache;

static MTPipelineCache *
mt_nativeCache(GPUPipelineCache *cache) {
  return cache ? cache->_priv : NULL;
}

GPU_HIDE
GPUResult
mt_initPipelineCompiler(GPUDeviceMT *device) {
  if (!device || device->commandMode != MTCommandMode4) {
    return GPU_OK;
  }
#if MT_HAS_METAL4
  if (@available(macOS 26.0, iOS 26.0, *)) {
    MTL4CompilerDescriptor *descriptor;
    NSError                *error;

    descriptor = [MTL4CompilerDescriptor new];
    error      = nil;
    device->compiler = [device->device newCompilerWithDescriptor:descriptor
                                                            error:&error];
    [descriptor release];
    if (!device->compiler) {
#if GPU_BUILD_WITH_VALIDATION
      NSLog(@"Failed to create Metal 4 compiler: %@", error);
#endif
      return GPU_ERROR_BACKEND_FAILURE;
    }
    return GPU_OK;
  }
#endif
  return GPU_ERROR_UNSUPPORTED;
}

GPU_HIDE
void
mt_destroyPipelineCompiler(GPUDeviceMT *device) {
  if (!device) {
    return;
  }
  [device->compiler release];
  device->compiler = nil;
}

#if MT_HAS_METAL4
static GPUResult
mt_createCache4(GPUDeviceMT                    *device,
                const GPUPipelineCacheCreateInfo *info,
                NSString                       *path,
                MTPipelineCache                *native) {
  MTL4PipelineDataSetSerializerDescriptor *serializerDesc;
  MTL4CompilerDescriptor                  *compilerDesc;
  MTL4CompilerTaskOptions                 *taskOptions;
  id<MTL4PipelineDataSetSerializer>         serializer;
  id<MTL4Compiler>                          compiler;
  id<MTL4Archive>                           lookupArchive;
  NSFileManager                            *fileManager;
  NSError                                  *error;
  NSURL                                    *url;
  BOOL                                      exists;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    url         = [NSURL fileURLWithPath:path];
    fileManager = [NSFileManager defaultManager];
    exists      = [fileManager fileExistsAtPath:path];
    error       = nil;
    lookupArchive = exists
                      ? [device->device newArchiveWithURL:url error:&error]
                      : nil;
    if (exists && !lookupArchive) {
      [fileManager removeItemAtURL:url error:nil];
    }

    serializerDesc = [MTL4PipelineDataSetSerializerDescriptor new];
    serializerDesc.configuration =
      MTL4PipelineDataSetSerializerConfigurationCaptureBinaries;
    serializer = [device->device
      newPipelineDataSetSerializerWithDescriptor:serializerDesc];
    [serializerDesc release];
    if (!serializer) {
      [lookupArchive release];
      return GPU_ERROR_BACKEND_FAILURE;
    }

    compilerDesc                           = [MTL4CompilerDescriptor new];
    compilerDesc.pipelineDataSetSerializer = serializer;
    if (info->label) {
      compilerDesc.label = [NSString stringWithUTF8String:info->label];
    }
    error    = nil;
    compiler = [device->device newCompilerWithDescriptor:compilerDesc
                                                    error:&error];
    [compilerDesc release];
    if (!compiler) {
#if GPU_BUILD_WITH_VALIDATION
      NSLog(@"Failed to create Metal 4 cache compiler: %@", error);
#endif
      [serializer release];
      [lookupArchive release];
      return GPU_ERROR_BACKEND_FAILURE;
    }

    taskOptions = nil;
    if (lookupArchive) {
      taskOptions                = [MTL4CompilerTaskOptions new];
      taskOptions.lookupArchives = @[lookupArchive];
      if (info->label) {
        lookupArchive.label = [NSString stringWithUTF8String:info->label];
      }
    }

    native->compiler      = compiler;
    native->serializer    = serializer;
    native->taskOptions   = taskOptions;
    native->lookupArchive = lookupArchive;
    native->url           = [url retain];
    native->modern        = true;
    return native->url ? GPU_OK : GPU_ERROR_OUT_OF_MEMORY;
  }
  return GPU_ERROR_UNSUPPORTED;
}
#endif

static GPUResult
mt_createCache(GPUDevice                        *device,
               const GPUPipelineCacheCreateInfo *info,
               GPUPipelineCache                 *cache) {
  MTLBinaryArchiveDescriptor *descriptor;
  id<MTLBinaryArchive>        archive;
  NSFileManager              *fileManager;
  MTPipelineCache            *native;
  GPUDeviceMT                *deviceMT;
  NSString                   *path;
  NSURL                      *directoryURL;
  NSURL                      *url;
  NSError                    *error;
  BOOL                        exists;

  if (!device || !info || !cache || !info->cachePath) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (@available(macOS 11.0, iOS 14.0, *)) {
    deviceMT = device->_priv;
    path     = [NSString stringWithUTF8String:info->cachePath];
    if (!deviceMT || !path) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    url          = [NSURL fileURLWithPath:path];
    directoryURL = [url URLByDeletingLastPathComponent];
    fileManager  = [NSFileManager defaultManager];
    error        = nil;
    if (![fileManager createDirectoryAtURL:directoryURL
               withIntermediateDirectories:YES
                                attributes:nil
                                     error:&error]) {
      NSLog(@"Failed to create Metal pipeline cache directory: %@", error);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    native = calloc(1, sizeof(*native));
    if (!native) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    native->lock = OS_UNFAIR_LOCK_INIT;
#if MT_HAS_METAL4
    if (deviceMT->commandMode == MTCommandMode4) {
      GPUResult result;

      result = mt_createCache4(deviceMT, info, path, native);
      if (result != GPU_OK) {
        [native->taskOptions release];
        [native->lookupArchive release];
        [native->serializer release];
        [native->compiler release];
        [native->url release];
        free(native);
        return result;
      }
      cache->_priv = native;
      return GPU_OK;
    }
#endif

    exists        = [fileManager fileExistsAtPath:url.path];
    descriptor    = [MTLBinaryArchiveDescriptor new];
    descriptor.url = exists ? url : nil;
    error         = nil;
    archive       = [deviceMT->device
      newBinaryArchiveWithDescriptor:descriptor
                                  error:&error];
    if (!archive && exists) {
      descriptor.url = nil;
      error           = nil;
      archive = [deviceMT->device
        newBinaryArchiveWithDescriptor:descriptor
                                    error:&error];
    }
    [descriptor release];
    if (!archive) {
      NSLog(@"Failed to create Metal pipeline cache: %@", error);
      free(native);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    native->archive  = archive;
    native->archives = [[NSArray alloc] initWithObjects:archive, nil];
    native->url      = [url retain];
    if (!native->archives || !native->url) {
      [native->archives release];
      [native->url release];
      [archive release];
      free(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    if (info->label) {
      archive.label = [NSString stringWithUTF8String:info->label];
    }
    cache->_priv = native;
    return GPU_OK;
  }

  return GPU_ERROR_UNSUPPORTED;
}

static void
mt_storeCache(MTPipelineCache *native) {
  NSFileManager *fileManager;
  NSString      *temporaryPath;
  NSURL         *temporaryURL;
  NSError       *error;

  if (!native || !native->dirty) {
    return;
  }

  temporaryPath = [native->url.path stringByAppendingString:@".tmp"];
  temporaryURL  = [NSURL fileURLWithPath:temporaryPath];
  fileManager   = [NSFileManager defaultManager];
  [fileManager removeItemAtURL:temporaryURL error:nil];
  error = nil;
  if (native->modern) {
#if MT_HAS_METAL4
    if (@available(macOS 26.0, iOS 26.0, *)) {
      if (![(id<MTL4PipelineDataSetSerializer>)native->serializer
            serializeAsArchiveAndFlushToURL:temporaryURL
                                       error:&error]) {
        NSLog(@"Failed to serialize Metal 4 pipeline cache: %@", error);
        return;
      }
    } else {
      return;
    }
#else
    return;
#endif
  } else if (![native->archive serializeToURL:temporaryURL error:&error]) {
    NSLog(@"Failed to serialize Metal pipeline cache: %@", error);
    return;
  }
  if (rename(temporaryURL.fileSystemRepresentation,
             native->url.fileSystemRepresentation) != 0) {
    NSLog(@"Failed to replace Metal pipeline cache at %@", native->url.path);
    [fileManager removeItemAtURL:temporaryURL error:nil];
  }
}

static void
mt_destroyCache(GPUPipelineCache *cache) {
  MTPipelineCache *native;

  native = mt_nativeCache(cache);
  if (!native) {
    return;
  }
  if (@available(macOS 11.0, iOS 14.0, *)) {
    mt_storeCache(native);
  }
  [native->taskOptions release];
  [native->lookupArchive release];
  [native->serializer release];
  [native->compiler release];
  [native->archives release];
  [native->url release];
  [native->archive release];
  free(native);
  cache->_priv = NULL;
}

GPU_HIDE
bool
mt_useRenderCache(GPUPipelineCache            *cache,
                  MTLRenderPipelineDescriptor *descriptor) {
  MTPipelineCache *native;

  native = mt_nativeCache(cache);
  if (!native || native->modern || !descriptor) {
    return false;
  }
  if (@available(macOS 11.0, iOS 14.0, *)) {
    descriptor.binaryArchives = native->archives;
    return true;
  }
  return false;
}

GPU_HIDE
bool
mt_addRenderCache(GPUPipelineCache            *cache,
                  MTLRenderPipelineDescriptor *descriptor) {
  MTPipelineCache *native;
  NSError         *error;
  BOOL             added;

  native = mt_nativeCache(cache);
  if (!native || native->modern || !descriptor) {
    return false;
  }
  if (@available(macOS 11.0, iOS 14.0, *)) {
    error = nil;
    os_unfair_lock_lock(&native->lock);
    added = [native->archive addRenderPipelineFunctionsWithDescriptor:descriptor
                                                                 error:&error];
    native->dirty |= added;
    os_unfair_lock_unlock(&native->lock);
    return added;
  }
  return false;
}

GPU_HIDE
bool
mt_useComputeCache(GPUPipelineCache             *cache,
                   MTLComputePipelineDescriptor *descriptor) {
  MTPipelineCache *native;

  native = mt_nativeCache(cache);
  if (!native || native->modern || !descriptor) {
    return false;
  }
  if (@available(macOS 11.0, iOS 14.0, *)) {
    descriptor.binaryArchives = native->archives;
    return true;
  }
  return false;
}

GPU_HIDE
bool
mt_addComputeCache(GPUPipelineCache             *cache,
                   MTLComputePipelineDescriptor *descriptor) {
  MTPipelineCache *native;
  NSError         *error;
  BOOL             added;

  native = mt_nativeCache(cache);
  if (!native || native->modern || !descriptor) {
    return false;
  }
  if (@available(macOS 11.0, iOS 14.0, *)) {
    error = nil;
    os_unfair_lock_lock(&native->lock);
    added = [native->archive addComputePipelineFunctionsWithDescriptor:descriptor
                                                                  error:&error];
    native->dirty |= added;
    os_unfair_lock_unlock(&native->lock);
    return added;
  }
  return false;
}

GPU_HIDE
id
mt_compileRenderPipeline4(GPUPipelineCache *cache,
                          GPUDeviceMT      *device,
                          id                descriptor,
                          NSError         **error) {
#if MT_HAS_METAL4
  MTPipelineCache *native;
  id<MTL4Compiler> compiler;
  id                state;

  if (!device || !descriptor) {
    return nil;
  }
  if (@available(macOS 26.0, iOS 26.0, *)) {
    native   = mt_nativeCache(cache);
    compiler = native && native->modern
                 ? (id<MTL4Compiler>)native->compiler
                 : (id<MTL4Compiler>)device->compiler;
    if (!compiler) {
      return nil;
    }
    state = [compiler
      newRenderPipelineStateWithDescriptor:(MTL4PipelineDescriptor *)descriptor
                       compilerTaskOptions:native && native->modern
                                               ? native->taskOptions
                                               : nil
                                     error:error];
    if (state && native && native->modern) {
      os_unfair_lock_lock(&native->lock);
      native->dirty = true;
      os_unfair_lock_unlock(&native->lock);
    }
    return state;
  }
#else
  GPU__UNUSED(cache);
  GPU__UNUSED(device);
  GPU__UNUSED(descriptor);
  GPU__UNUSED(error);
#endif
  return nil;
}

GPU_HIDE
id
mt_compileComputePipeline4(GPUPipelineCache *cache,
                           GPUDeviceMT      *device,
                           id                descriptor,
                           NSError         **error) {
#if MT_HAS_METAL4
  MTPipelineCache *native;
  id<MTL4Compiler> compiler;
  id                state;

  if (!device || !descriptor) {
    return nil;
  }
  if (@available(macOS 26.0, iOS 26.0, *)) {
    native   = mt_nativeCache(cache);
    compiler = native && native->modern
                 ? (id<MTL4Compiler>)native->compiler
                 : (id<MTL4Compiler>)device->compiler;
    if (!compiler) {
      return nil;
    }
    state = [compiler
      newComputePipelineStateWithDescriptor:
        (MTL4ComputePipelineDescriptor *)descriptor
                        compilerTaskOptions:native && native->modern
                                                ? native->taskOptions
                                                : nil
                                      error:error];
    if (state && native && native->modern) {
      os_unfair_lock_lock(&native->lock);
      native->dirty = true;
      os_unfair_lock_unlock(&native->lock);
    }
    return state;
  }
#else
  GPU__UNUSED(cache);
  GPU__UNUSED(device);
  GPU__UNUSED(descriptor);
  GPU__UNUSED(error);
#endif
  return nil;
}

GPU_HIDE
void
mt_initPipelineCache(GPUApiPipelineCache *api) {
  api->create  = mt_createCache;
  api->destroy = mt_destroyCache;
}
