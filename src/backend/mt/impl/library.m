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
