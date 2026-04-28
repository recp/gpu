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

GPU_EXPORT
GPULibrary*
mt_defaultLibrary(GPUDevice *device) {
  GPUDeviceMT *deviceMT;
  GPULibrary  *library;
  id<MTLLibrary> mtLibrary;

  deviceMT  = device->_priv;
  mtLibrary = [deviceMT->device newDefaultLibrary];
  library   = calloc(1, sizeof(*library));

  library->_priv = mtLibrary;

  return library;
}

GPU_HIDE
GPULibrary*
mt_newLibraryWithSource(GPUDevice *device,
                        const char *source,
                        uint64_t sourceSize) {
  GPUDeviceMT *deviceMT;
  GPULibrary  *library;
  id<MTLLibrary> mtLibrary;
  NSError     *error;
  NSString    *nsSource;
  MTLCompileOptions *options;

  deviceMT  = device->_priv;
  error     = nil;
  nsSource  = [[NSString alloc] initWithBytes:source
                                       length:(NSUInteger)sourceSize
                                     encoding:NSUTF8StringEncoding];
  options   = [MTLCompileOptions new];
  mtLibrary = [deviceMT->device newLibraryWithSource:nsSource options:options error:&error];
  if (!mtLibrary) {
    if (error) {
      NSLog(@"GPU mt_newLibraryWithSource failed: %@", error);
    }
    return NULL;
  }

  library = calloc(1, sizeof(*library));
  if (!library) {
    return NULL;
  }

  library->_priv = mtLibrary;
  return library;
}

GPU_EXPORT
GPUFunction*
mt_newFunction(GPULibrary *lib, const char *name) {
  GPUFunction *func;
  id<MTLFunction> mtFunc;

  mtFunc = [(id<MTLLibrary>)lib->_priv newFunctionWithName:[NSString stringWithUTF8String:name]];
  func   = calloc(1, sizeof(*func));

  func->_priv = mtFunc;

  return func;
}

GPU_HIDE
MTLSamplerMinMagFilter
mt_uslSamplerFilter(uint32_t filter) {
  return filter == GPUUSLSamplerFilterNearest ?
    MTLSamplerMinMagFilterNearest :
    MTLSamplerMinMagFilterLinear;
}

GPU_HIDE
MTLSamplerMipFilter
mt_uslSamplerMipFilter(uint32_t filter) {
  return filter == GPUUSLSamplerFilterNearest ?
    MTLSamplerMipFilterNearest :
    MTLSamplerMipFilterLinear;
}

GPU_HIDE
MTLSamplerAddressMode
mt_uslSamplerAddressMode(uint32_t mode) {
  switch (mode) {
    case GPUUSLSamplerAddressRepeat:
      return MTLSamplerAddressModeRepeat;
    case GPUUSLSamplerAddressMirroredRepeat:
      return MTLSamplerAddressModeMirrorRepeat;
    case GPUUSLSamplerAddressClampToZero:
      return MTLSamplerAddressModeClampToZero;
    case GPUUSLSamplerAddressClampToBorder:
      return MTLSamplerAddressModeClampToBorderColor;
    case GPUUSLSamplerAddressClampToEdge:
    default:
      return MTLSamplerAddressModeClampToEdge;
  }
}

GPU_HIDE
MTLCompareFunction
mt_uslSamplerCompareFunction(uint32_t func) {
  switch (func) {
    case GPUUSLSamplerCompareLess:
      return MTLCompareFunctionLess;
    case GPUUSLSamplerCompareEqual:
      return MTLCompareFunctionEqual;
    case GPUUSLSamplerCompareLessEqual:
      return MTLCompareFunctionLessEqual;
    case GPUUSLSamplerCompareGreater:
      return MTLCompareFunctionGreater;
    case GPUUSLSamplerCompareNotEqual:
      return MTLCompareFunctionNotEqual;
    case GPUUSLSamplerCompareGreaterEqual:
      return MTLCompareFunctionGreaterEqual;
    case GPUUSLSamplerCompareAlways:
      return MTLCompareFunctionAlways;
    case GPUUSLSamplerCompareNever:
    default:
      return MTLCompareFunctionNever;
  }
}

GPU_HIDE
GPUSampler *
mt_createSamplerFromUSLStaticSampler(GPUApi * __restrict api,
                                     GPUDevice * __restrict device,
                                     const GPUUSLStaticSamplerDesc *uslDesc,
                                     bool staticIfSupported) {
  GPUDeviceMT *deviceMT;
  MTLSamplerDescriptor *desc;
  id<MTLSamplerState> state;

  (void)api;
  (void)staticIfSupported;

  if (!device) {
    return NULL;
  }

  deviceMT = device->_priv;
  desc = [MTLSamplerDescriptor new];
  if (uslDesc) {
    desc.minFilter = mt_uslSamplerFilter(uslDesc->minFilter);
    desc.magFilter = mt_uslSamplerFilter(uslDesc->magFilter);
    desc.mipFilter = mt_uslSamplerMipFilter(uslDesc->mipFilter);
    desc.sAddressMode = mt_uslSamplerAddressMode(uslDesc->addressMode);
    desc.tAddressMode = mt_uslSamplerAddressMode(uslDesc->addressMode);
    desc.rAddressMode = mt_uslSamplerAddressMode(uslDesc->addressMode);
    desc.normalizedCoordinates = uslDesc->coordSpace == GPUUSLSamplerCoordNormalized;
    desc.compareFunction = uslDesc->hasCompare ?
      mt_uslSamplerCompareFunction(uslDesc->compareFunc) :
      MTLCompareFunctionNever;
    if (uslDesc->maxAnisotropy > 0) {
      desc.maxAnisotropy = uslDesc->maxAnisotropy > 16u ? 16u : uslDesc->maxAnisotropy;
    }
  } else {
    desc.minFilter = MTLSamplerMinMagFilterLinear;
    desc.magFilter = MTLSamplerMinMagFilterLinear;
    desc.mipFilter = MTLSamplerMipFilterLinear;
    desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    desc.rAddressMode = MTLSamplerAddressModeClampToEdge;
  }
  state = [deviceMT->device newSamplerStateWithDescriptor:desc];
  return (GPUSampler *)state;
}

GPU_HIDE
GPUSampler *
mt_createSampler(GPUApi * __restrict api,
                 GPUDevice * __restrict device,
                 bool staticIfSupported) {
  return mt_createSamplerFromUSLStaticSampler(api, device, NULL, staticIfSupported);
}

GPU_HIDE
void
mt_destroyLibrary(GPULibrary *lib) {
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
  api->defaultLibrary      = mt_defaultLibrary;
  api->newLibraryWithSource = mt_newLibraryWithSource;
  api->newFunction         = mt_newFunction;
  api->destroyLibrary      = mt_destroyLibrary;
}

GPU_HIDE
void
mt_initSampler(GPUApiSampler *api) {
  api->createSampler = mt_createSampler;
  api->createSamplerFromUSLStaticSampler = mt_createSamplerFromUSLStaticSampler;
}
