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
  NSArray             *archives;
  NSURL               *url;
  os_unfair_lock       lock;
  bool                 dirty;
} MTPipelineCache;

static MTPipelineCache *
mt_nativeCache(GPUPipelineCache *cache) {
  return cache ? cache->_priv : NULL;
}

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
      return GPU_ERROR_BACKEND_FAILURE;
    }

    native = calloc(1, sizeof(*native));
    if (!native) {
      [archive release];
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    native->archive  = archive;
    native->archives = [[NSArray alloc] initWithObjects:archive, nil];
    native->url      = [url retain];
    native->lock     = OS_UNFAIR_LOCK_INIT;
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
  if (![native->archive serializeToURL:temporaryURL error:&error]) {
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
  [native->archives release];
  [native->url release];
  [native->archive release];
  free(native);
  cache->_priv = NULL;
}

GPU_HIDE
void
mt_useRenderCache(GPUPipelineCache            *cache,
                  MTLRenderPipelineDescriptor *descriptor) {
  MTPipelineCache *native;
  NSError         *error;
  BOOL             added;

  native = mt_nativeCache(cache);
  if (!native || !descriptor) {
    return;
  }
  if (@available(macOS 11.0, iOS 14.0, *)) {
    error = nil;
    os_unfair_lock_lock(&native->lock);
    added = [native->archive addRenderPipelineFunctionsWithDescriptor:descriptor
                                                                 error:&error];
    native->dirty |= added;
    os_unfair_lock_unlock(&native->lock);
    descriptor.binaryArchives = native->archives;
  }
}

GPU_HIDE
void
mt_useComputeCache(GPUPipelineCache             *cache,
                   MTLComputePipelineDescriptor *descriptor) {
  MTPipelineCache *native;
  NSError         *error;
  BOOL             added;

  native = mt_nativeCache(cache);
  if (!native || !descriptor) {
    return;
  }
  if (@available(macOS 11.0, iOS 14.0, *)) {
    error = nil;
    os_unfair_lock_lock(&native->lock);
    added = [native->archive addComputePipelineFunctionsWithDescriptor:descriptor
                                                                  error:&error];
    native->dirty |= added;
    os_unfair_lock_unlock(&native->lock);
    descriptor.binaryArchives = native->archives;
  }
}

GPU_HIDE
void
mt_initPipelineCache(GPUApiPipelineCache *api) {
  api->create  = mt_createCache;
  api->destroy = mt_destroyCache;
}
