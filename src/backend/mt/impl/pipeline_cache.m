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
#include "../../cache_file.h"
#include "../../../api/pipeline_cache_internal.h"
#include "pipeline_cache.h"

#define MT_PIPELINE_CACHE_META_MAGIC   UINT64_C(0x4750554d544c4348)
#define MT_PIPELINE_CACHE_META_VERSION 1u
#define MT_PIPELINE_CACHE_HASH_SEED    UINT64_C(14695981039346656037)

typedef enum MTPipelineCacheKind {
  MT_PIPELINE_CACHE_CLASSIC = 1,
  MT_PIPELINE_CACHE_MODERN
} MTPipelineCacheKind;

typedef struct MTPipelineCacheMetadata {
  uint64_t magic;
  uint64_t archiveSize;
  uint64_t archiveHash;
  uint64_t deviceKey;
  uint64_t systemKey;
  uint32_t version;
  uint32_t kind;
} MTPipelineCacheMetadata;

typedef struct MTPipelineCache {
  id<MTLBinaryArchive> archive;
  id                   compiler;
  id                   serializer;
  id                   taskOptions;
  id                   lookupArchive;
  NSArray             *archives;
  NSURL               *url;
  uint64_t              deviceKey;
  uint64_t              systemKey;
  uint32_t              kind;
  os_unfair_lock       lock;
  bool                 dirty;
  bool                 modern;
} MTPipelineCache;

static uint64_t
mt_hashBytes(uint64_t hash, const void *data, size_t size) {
  const uint8_t *bytes;

  bytes = data;
  for (size_t i = 0u; i < size; i++) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static bool
mt_hashFile(NSString *path, uint64_t *outSize, uint64_t *outHash) {
  uint8_t  bytes[16384];
  uint64_t fileSize;
  uint64_t hash;
  size_t   readSize;
  FILE    *file;

  file = fopen(path.fileSystemRepresentation, "rb");
  if (!file) {
    return false;
  }

  fileSize = 0u;
  hash     = MT_PIPELINE_CACHE_HASH_SEED;
  while ((readSize = fread(bytes, 1u, sizeof(bytes), file)) != 0u) {
    fileSize += readSize;
    hash      = mt_hashBytes(hash, bytes, readSize);
  }
  if (ferror(file)) {
    fclose(file);
    return false;
  }
  fclose(file);

  *outSize = fileSize;
  *outHash = hash;
  return true;
}

static uint64_t
mt_systemKey(void) {
  NSString   *version;
  const char *string;

  version = [NSProcessInfo processInfo].operatingSystemVersionString;
  string  = version.UTF8String;
  return string
           ? mt_hashBytes(MT_PIPELINE_CACHE_HASH_SEED, string, strlen(string))
           : 0u;
}

static NSString *
mt_metadataPath(NSString *path) {
  return [path stringByAppendingString:@".meta"];
}

static bool
mt_cacheMetadataMatches(MTPipelineCache *native, NSString *path) {
  MTPipelineCacheMetadata metadata;
  uint64_t                archiveSize;
  uint64_t                archiveHash;
  FILE                   *file;
  bool                    valid;

  file = fopen(mt_metadataPath(path).fileSystemRepresentation, "rb");
  if (!file) {
    return false;
  }
  valid = fread(&metadata, sizeof(metadata), 1u, file) == 1u &&
          fgetc(file) == EOF && !ferror(file);
  fclose(file);
  if (!valid || metadata.magic != MT_PIPELINE_CACHE_META_MAGIC ||
      metadata.version != MT_PIPELINE_CACHE_META_VERSION ||
      metadata.kind != native->kind ||
      metadata.deviceKey != native->deviceKey ||
      metadata.systemKey != native->systemKey ||
      !mt_hashFile(path, &archiveSize, &archiveHash)) {
    return false;
  }
  return metadata.archiveSize == archiveSize &&
         metadata.archiveHash == archiveHash;
}

static bool
mt_writeCacheMetadata(MTPipelineCache *native,
                      NSString        *archivePath,
                      NSString        *metadataPath) {
  MTPipelineCacheMetadata metadata;
  FILE                   *file;
  bool                    written;

  if (!mt_hashFile(archivePath, &metadata.archiveSize, &metadata.archiveHash)) {
    return false;
  }
  metadata.magic     = MT_PIPELINE_CACHE_META_MAGIC;
  metadata.deviceKey = native->deviceKey;
  metadata.systemKey = native->systemKey;
  metadata.version   = MT_PIPELINE_CACHE_META_VERSION;
  metadata.kind      = native->kind;

  file = fopen(metadataPath.fileSystemRepresentation, "wb");
  if (!file) {
    return false;
  }
  written = fwrite(&metadata, sizeof(metadata), 1u, file) == 1u &&
            fflush(file) == 0;
  if (written) {
    written = fsync(fileno(file)) == 0;
  }
  written &= fclose(file) == 0;
  return written;
}

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
    exists      = [fileManager fileExistsAtPath:path] &&
                  mt_cacheMetadataMatches(native, path);
    error       = nil;
    lookupArchive = nil;
    if (exists) {
      @try {
        lookupArchive = [device->device newArchiveWithURL:url error:&error];
      } @catch (NSException *exception) {
#if GPU_BUILD_WITH_VALIDATION
        NSLog(@"Failed to open Metal 4 pipeline cache: %@", exception);
#endif
      }
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
  GPUCacheFileGuard          guard;
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
    if (!gpuCacheFileBegin(url.fileSystemRepresentation, &guard)) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    native = calloc(1, sizeof(*native));
    if (!native) {
      gpuCacheFileEnd(&guard);
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    native->lock = OS_UNFAIR_LOCK_INIT;
    native->deviceKey = deviceMT->device.registryID;
    native->systemKey = mt_systemKey();
#if MT_HAS_METAL4
    if (deviceMT->commandMode == MTCommandMode4) {
      GPUResult result;

      native->kind = MT_PIPELINE_CACHE_MODERN;
      result = mt_createCache4(deviceMT, info, path, native);
      if (result != GPU_OK) {
        [native->taskOptions release];
        [native->lookupArchive release];
        [native->serializer release];
        [native->compiler release];
        [native->url release];
        free(native);
        gpuCacheFileEnd(&guard);
        return result;
      }
      cache->_priv = native;
      gpuCacheFileEnd(&guard);
      return GPU_OK;
    }
#endif

    native->kind  = MT_PIPELINE_CACHE_CLASSIC;
    exists        = [fileManager fileExistsAtPath:url.path] &&
                    mt_cacheMetadataMatches(native, path);
    descriptor    = [MTLBinaryArchiveDescriptor new];
    descriptor.url = exists ? url : nil;
    error         = nil;
    archive       = nil;
    @try {
      archive = [deviceMT->device
        newBinaryArchiveWithDescriptor:descriptor
                                    error:&error];
    } @catch (NSException *exception) {
#if GPU_BUILD_WITH_VALIDATION
      NSLog(@"Failed to open Metal pipeline cache: %@", exception);
#endif
    }
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
      gpuCacheFileEnd(&guard);
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
      gpuCacheFileEnd(&guard);
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    if (info->label) {
      archive.label = [NSString stringWithUTF8String:info->label];
    }
    cache->_priv = native;
    gpuCacheFileEnd(&guard);
    return GPU_OK;
  }

  return GPU_ERROR_UNSUPPORTED;
}

static void
mt_storeCache(MTPipelineCache *native) {
  GPUCacheFileGuard guard;
  char             *metadataTemporaryBytes;
  char             *temporaryBytes;
  NSFileManager    *fileManager;
  NSString         *metadataPath;
  NSString         *metadataTemporaryPath;
  NSString         *temporaryPath;
  NSURL            *metadataURL;
  NSURL            *temporaryURL;
  NSError          *error;
  bool              serialized;

  if (!native || !native->dirty) {
    return;
  }

  if (!gpuCacheFileBegin(native->url.fileSystemRepresentation, &guard)) {
    return;
  }
  metadataPath          = mt_metadataPath(native->url.path);
  temporaryBytes        = gpuCacheFileTemporaryPath(
                            native->url.fileSystemRepresentation,
                            native);
  metadataTemporaryBytes = gpuCacheFileTemporaryPath(
                             metadataPath.fileSystemRepresentation,
                             native);
  temporaryPath = temporaryBytes
                    ? [NSString stringWithUTF8String:temporaryBytes]
                    : nil;
  metadataTemporaryPath = metadataTemporaryBytes
                            ? [NSString stringWithUTF8String:
                                metadataTemporaryBytes]
                            : nil;
  if (!temporaryPath || !metadataTemporaryPath) {
    free(metadataTemporaryBytes);
    free(temporaryBytes);
    gpuCacheFileEnd(&guard);
    return;
  }

  temporaryURL = [NSURL fileURLWithPath:temporaryPath];
  metadataURL  = [NSURL fileURLWithPath:metadataTemporaryPath];
  fileManager  = [NSFileManager defaultManager];
  [fileManager removeItemAtURL:temporaryURL error:nil];
  [fileManager removeItemAtURL:metadataURL error:nil];
  error      = nil;
  serialized = false;
  if (native->modern) {
#if MT_HAS_METAL4
    if (@available(macOS 26.0, iOS 26.0, *)) {
      serialized = [(id<MTL4PipelineDataSetSerializer>)native->serializer
        serializeAsArchiveAndFlushToURL:temporaryURL
                                 error:&error];
      if (!serialized) {
        NSLog(@"Failed to serialize Metal 4 pipeline cache: %@", error);
      }
    }
#endif
  } else {
    serialized = [native->archive serializeToURL:temporaryURL error:&error];
    if (!serialized) {
      NSLog(@"Failed to serialize Metal pipeline cache: %@", error);
    }
  }
  if (serialized &&
      !mt_writeCacheMetadata(native, temporaryPath, metadataTemporaryPath)) {
    NSLog(@"Failed to write Metal pipeline cache metadata at %@", metadataPath);
    serialized = false;
  }
  if (serialized &&
      !gpuCacheFileReplace(temporaryURL.fileSystemRepresentation,
                           native->url.fileSystemRepresentation)) {
    NSLog(@"Failed to replace Metal pipeline cache at %@", native->url.path);
    serialized = false;
  }
  if (serialized &&
      !gpuCacheFileReplace(metadataURL.fileSystemRepresentation,
                           metadataPath.fileSystemRepresentation)) {
    NSLog(@"Failed to replace Metal pipeline cache metadata at %@", metadataPath);
    serialized = false;
  }
  [fileManager removeItemAtURL:temporaryURL error:nil];
  [fileManager removeItemAtURL:metadataURL error:nil];
  if (serialized) {
    native->dirty = false;
  }
  free(metadataTemporaryBytes);
  free(temporaryBytes);
  gpuCacheFileEnd(&guard);
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
