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

#include "../common.h"

GPU_HIDE
GPUShaderLibrary*
mt_newLibraryWithSource(GPUDevice *device,
                        const char *source,
                        uint64_t sourceSize) {
  GPUDeviceMT          *deviceMT;
  GPUShaderLibrary     *library;
  id<MTLLibrary>        mtLibrary;
  NSError              *error;
  NSString             *nsSource;
  MTLCompileOptions    *options;
#if MT_HAS_METAL4
  MTL4LibraryDescriptor *descriptor;
#endif

  deviceMT  = device->_priv;
  error     = nil;
  nsSource  = [[NSString alloc] initWithBytes:source
                                       length:(NSUInteger)sourceSize
                                     encoding:NSUTF8StringEncoding];
  options   = [MTLCompileOptions new];
  mtLibrary = nil;
  if (!deviceMT || !nsSource || !options) {
    [options release];
    [nsSource release];
    return NULL;
  }

#if MT_HAS_METAL4
  if (deviceMT->commandMode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      descriptor         = [MTL4LibraryDescriptor new];
      descriptor.source  = nsSource;
      descriptor.options = options;
      mtLibrary = [(id<MTL4Compiler>)deviceMT->compiler
        newLibraryWithDescriptor:descriptor
                           error:&error];
      [descriptor release];
    }
  } else
#endif
  {
    mtLibrary = [deviceMT->device newLibraryWithSource:nsSource
                                               options:options
                                                 error:&error];
  }
  [options release];
  [nsSource release];
  if (!mtLibrary) {
    if (error) {
      NSLog(@"GPU mt_newLibraryWithSource failed: %@", error);
    }
    return NULL;
  }

  library = calloc(1, sizeof(*library));
  if (!library) {
    [mtLibrary release];
    return NULL;
  }

  library->_priv = mtLibrary;
  return library;
}

GPU_HIDE
GPUShaderFunction*
mt_newFunction(GPUShaderLibrary *lib, const char *name) {
  GPUShaderFunction *func;
  MTShaderFunction *native;
  id<MTLFunction>   mtFunc;
  NSString         *mtName;

  mtName = [NSString stringWithUTF8String:name];
  mtFunc = [(id<MTLLibrary>)lib->_priv newFunctionWithName:mtName];
  if (!mtFunc) {
    return NULL;
  }

  func   = calloc(1, sizeof(*func));
  native = calloc(1, sizeof(*native));
  if (!func || !native) {
    free(native);
    free(func);
    [mtFunc release];
    return NULL;
  }

  native->function = mtFunc;
  native->library  = [(id<MTLLibrary>)lib->_priv retain];
  native->name     = [mtName copy];
  if (!native->library || !native->name) {
    [native->name release];
    [native->library release];
    [native->function release];
    free(native);
    free(func);
    return NULL;
  }
  func->_priv      = native;

  return func;
}

static void
mt_destroyFunction(GPUShaderFunction *function) {
  MTShaderFunction *native;

  if (!function) {
    return;
  }
  native = function->_priv;
  if (native) {
    [native->name release];
    [native->library release];
    [native->function release];
    free(native);
  }
  free(function);
}

GPU_HIDE
MTLSamplerMinMagFilter
mt_samplerFilter(GPUFilter filter) {
  return filter == GPU_FILTER_NEAREST ?
    MTLSamplerMinMagFilterNearest :
    MTLSamplerMinMagFilterLinear;
}

GPU_HIDE
MTLSamplerMipFilter
mt_samplerMipFilter(GPUMipFilter filter) {
  return filter == GPU_MIP_FILTER_NEAREST ?
    MTLSamplerMipFilterNearest :
    MTLSamplerMipFilterLinear;
}

GPU_HIDE
MTLSamplerAddressMode
mt_samplerAddressMode(GPUAddressMode mode) {
  switch (mode) {
    case GPU_ADDRESS_MODE_REPEAT:
      return MTLSamplerAddressModeRepeat;
    case GPU_ADDRESS_MODE_MIRRORED_REPEAT:
      return MTLSamplerAddressModeMirrorRepeat;
    case GPU_ADDRESS_MODE_CLAMP_TO_EDGE:
    default:
      return MTLSamplerAddressModeClampToEdge;
  }
}

GPU_HIDE
GPUResult
mt_createSampler(GPUApi * __restrict api,
                 GPUDevice * __restrict device,
                 const GPUSamplerCreateInfo *info,
                 bool staticIfSupported,
                 GPUSampler **outSampler) {
  GPUDeviceMT *deviceMT;
  MTLSamplerDescriptor *desc;
  GPUSampler *sampler;
  id<MTLSamplerState> state;

  (void)api;
  (void)staticIfSupported;

  if (!device || !info || !outSampler) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outSampler = NULL;

  deviceMT = device->_priv;
  desc = [MTLSamplerDescriptor new];
  desc.minFilter = mt_samplerFilter(info->desc.minFilter);
  desc.magFilter = mt_samplerFilter(info->desc.magFilter);
  desc.mipFilter = mt_samplerMipFilter(info->desc.mipFilter);
  desc.sAddressMode = mt_samplerAddressMode(info->desc.addressU);
  desc.tAddressMode = mt_samplerAddressMode(info->desc.addressV);
  desc.rAddressMode = mt_samplerAddressMode(info->desc.addressW);

  state = [deviceMT->device newSamplerStateWithDescriptor:desc];
  [desc release];
  if (!state) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  sampler = calloc(1, sizeof(*sampler));
  if (!sampler) {
    [state release];
    return GPU_ERROR_BACKEND_FAILURE;
  }

  sampler->_priv = state;
  if (@available(macOS 13.0, iOS 16.0, *)) {
    sampler->_gpuResourceID = state.gpuResourceID._impl;
  }
  *outSampler = sampler;
  return GPU_OK;
}

GPU_HIDE
void
mt_destroySampler(GPUSampler * __restrict sampler) {
  if (!sampler) {
    return;
  }

  if (sampler->_priv) {
    [(id<MTLSamplerState>)sampler->_priv release];
  }
  free(sampler);
}

GPU_HIDE
void
mt_destroyLibrary(GPUShaderLibrary *lib) {
  if (!lib) {
    return;
  }

  if (lib->_priv) {
    [(id)lib->_priv release];
  }

  free(lib);
}

GPU_HIDE
void
mt_initLibrary(GPUApiLibrary *api) {
  api->newLibraryWithSource = mt_newLibraryWithSource;
  api->newFunction          = mt_newFunction;
  api->destroyFunction      = mt_destroyFunction;
  api->destroyLibrary       = mt_destroyLibrary;
}

GPU_HIDE
void
mt_initSampler(GPUApiSampler *api) {
  api->createSampler  = mt_createSampler;
  api->destroySampler = mt_destroySampler;
}
