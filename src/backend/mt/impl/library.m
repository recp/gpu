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
#include "../../../api/constants_internal.h"

static const MTLCompareFunction mt_compareFunctions[] = {
  [GPU_COMPARE_NEVER]         = MTLCompareFunctionNever,
  [GPU_COMPARE_LESS]          = MTLCompareFunctionLess,
  [GPU_COMPARE_EQUAL]         = MTLCompareFunctionEqual,
  [GPU_COMPARE_LESS_EQUAL]     = MTLCompareFunctionLessEqual,
  [GPU_COMPARE_GREATER]       = MTLCompareFunctionGreater,
  [GPU_COMPARE_NOT_EQUAL]     = MTLCompareFunctionNotEqual,
  [GPU_COMPARE_GREATER_EQUAL] = MTLCompareFunctionGreaterEqual,
  [GPU_COMPARE_ALWAYS]        = MTLCompareFunctionAlways
};

static void
mt_setSafeMathFallback(MTLCompileOptions *options) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  options.fastMathEnabled = NO;
#pragma clang diagnostic pop
}

static void
mt_destroyFunction(GPUShaderFunction *function) {
  MTShaderFunction *native;

  if (!function) {
    return;
  }

  native = function->_priv;

  if (native) {
    [native->constants release];
    [native->name release];
    [native->library release];
    [native->function release];
    free(native);
  }

  free(function);
}

static MTLCompareFunction
mt_samplerCompareFunction(GPUCompareOp op) {
  return (uint32_t)op < GPU_ARRAY_LEN(mt_compareFunctions) ? mt_compareFunctions[op] : MTLCompareFunctionNever;
}

GPU_HIDE
GPUShaderLibrary*
mt_newLibraryWithSource(GPUDevice  *device,
                        const char *source,
                        uint64_t    sourceSize,
                        uint32_t    compileFlags) {
  GPUDeviceMT           *deviceMT;
  GPUShaderLibrary      *library;
  id<MTLLibrary>         mtLibrary;
  NSError               *error;
  NSString              *nsSource;
  MTLCompileOptions     *options;
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

#if defined(__MAC_27_0) && defined(__IPHONE_27_0)
  if (@available(macOS 27.0, iOS 27.0, *)) {
    if (device->uslTargetVersion >= 401u) {
      options.languageVersion = MTLLanguageVersion4_1;
    }
  }
#endif

  if ((compileFlags & GPU_SHADER_SOURCE_COMPILE_STRICT_IEEE) != 0u) {
    if (@available(macOS 15.0, iOS 18.0, *)) {
      options.mathMode                   = MTLMathModeSafe;
      options.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;
    } else {
      mt_setSafeMathFallback(options);
    }
  } else if ((compileFlags & GPU_SHADER_SOURCE_COMPILE_RELAXED_FP) != 0u) {
    if (@available(macOS 15.0, iOS 18.0, *)) {
      options.mathMode = MTLMathModeRelaxed;
    } else {
      mt_setSafeMathFallback(options);
    }
  }

#if MT_HAS_METAL4
  if (deviceMT->commandMode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      descriptor         = [MTL4LibraryDescriptor new];
      descriptor.source  = nsSource;
      descriptor.options = options;
      mtLibrary          = [(id<MTL4Compiler>)deviceMT->compiler newLibraryWithDescriptor:descriptor
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

  if (!(library = calloc(1, sizeof(*library)))) {
    [mtLibrary release];
    return NULL;
  }

  library->_priv = mtLibrary;

  return library;
}

static GPUShaderFunction*
mt_newVariant(GPUShaderLibrary           *lib,
              const char                 *name,
              const GPUPipelineConstants *constants) {
  GPUShaderFunction           *func;
  MTShaderFunction            *native;
  const USLRuntimeSpecConstant *source;
  const GPUConstant           *value;
  MTLFunctionConstantValues   *values;
  id<MTLFunction>              mtFunc;
  NSString                    *mtName;
  NSError                     *error;
  uint64_t                     unsignedValue;
  int64_t                      signedValue;
  uint32_t                     i;
  float                        floatValue;
  _Float16                     halfValue;
  bool                         boolValue;

  mtName = [NSString stringWithUTF8String:name];
  values = nil;
  error  = nil;

  if (lib->_constantCount > 0u) {
    values = [MTLFunctionConstantValues new];

    for (i = 0u; i < lib->_constantCount; i++) {
      source = &lib->_constants[i];

      if (!source->has_default) {
        continue;
      }

      signedValue   = source->default_int;
      unsignedValue = source->default_uint;
      floatValue    = (float)source->default_float;
      halfValue     = (_Float16)source->default_float;
      boolValue     = signedValue != 0;

      switch (source->type.kind) {
        case USL_RUNTIME_TYPE_BOOL:
          [values setConstantValue:&boolValue type:MTLDataTypeBool atIndex:source->function_constant_id];
          break;
        case USL_RUNTIME_TYPE_I8:
          [values setConstantValue:&signedValue type:MTLDataTypeChar atIndex:source->function_constant_id];
          break;
        case USL_RUNTIME_TYPE_I16:
          [values setConstantValue:&signedValue type:MTLDataTypeShort atIndex:source->function_constant_id];
          break;
        case USL_RUNTIME_TYPE_I32:
          [values setConstantValue:&signedValue type:MTLDataTypeInt atIndex:source->function_constant_id];
          break;
        case USL_RUNTIME_TYPE_I64:
          [values setConstantValue:&signedValue type:MTLDataTypeLong atIndex:source->function_constant_id];
          break;
        case USL_RUNTIME_TYPE_U8:
          [values setConstantValue:&unsignedValue type:MTLDataTypeUChar atIndex:source->function_constant_id];
          break;
        case USL_RUNTIME_TYPE_U16:
          [values setConstantValue:&unsignedValue type:MTLDataTypeUShort atIndex:source->function_constant_id];
          break;
        case USL_RUNTIME_TYPE_U32:
          [values setConstantValue:&unsignedValue type:MTLDataTypeUInt atIndex:source->function_constant_id];
          break;
        case USL_RUNTIME_TYPE_U64:
          [values setConstantValue:&unsignedValue type:MTLDataTypeULong atIndex:source->function_constant_id];
          break;
        case USL_RUNTIME_TYPE_F16:
          [values setConstantValue:&halfValue type:MTLDataTypeHalf atIndex:source->function_constant_id];
          break;
        case USL_RUNTIME_TYPE_F32:
          [values setConstantValue:&floatValue type:MTLDataTypeFloat atIndex:source->function_constant_id];
          break;
        default: break;
      }
    }
  }

  if (constants) {
    for (i = 0u; i < constants->constantCount; i++) {
      value = &constants->pConstants[i];

      switch (value->type) {
        case GPU_CONSTANT_BOOL:
          [values setConstantValue:&value->value.boolean type:MTLDataTypeBool atIndex:value->id];
          break;
        case GPU_CONSTANT_I32:
          [values setConstantValue:&value->value.i32 type:MTLDataTypeInt atIndex:value->id];
          break;
        case GPU_CONSTANT_U32:
          [values setConstantValue:&value->value.u32 type:MTLDataTypeUInt atIndex:value->id];
          break;
        case GPU_CONSTANT_F32:
          [values setConstantValue:&value->value.f32 type:MTLDataTypeFloat atIndex:value->id];
          break;
        default: break;
      }
    }
  }

  mtFunc = values
             ? [(id<MTLLibrary>)lib->_priv newFunctionWithName:mtName constantValues:values error:&error]
             : [(id<MTLLibrary>)lib->_priv newFunctionWithName:mtName];

  if (!mtFunc) {
    [values release];
    return NULL;
  }

  func   = calloc(1, sizeof(*func));
  native = calloc(1, sizeof(*native));

  if (!func || !native) {
    free(native);
    free(func);
    [values release];
    [mtFunc release];
    return NULL;
  }

  native->constants = values;
  native->function  = mtFunc;
  native->library   = [(id<MTLLibrary>)lib->_priv retain];
  native->name      = [mtName copy];

  if (!native->library || !native->name) {
    [native->constants release];
    [native->name release];
    [native->library release];
    [native->function release];
    free(native);
    free(func);
    return NULL;
  }

  func->_priv = native;

  return func;
}

GPU_HIDE
GPUShaderFunction*
mt_newFunction(GPUShaderLibrary *lib, const char *name) {
  return mt_newVariant(lib, name, NULL);
}

GPU_HIDE
MTLSamplerMinMagFilter
mt_samplerFilter(GPUFilter filter) {
  return filter == GPU_FILTER_NEAREST ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
}

GPU_HIDE
MTLSamplerMipFilter
mt_samplerMipFilter(GPUMipFilter filter) {
  return filter == GPU_MIP_FILTER_NEAREST ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
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
mt_createSampler(GPUApi          *__restrict api,
                 GPUDevice       *__restrict device,
                 const GPUSamplerCreateInfo *info,
                 bool                        staticIfSupported,
                 GPUSampler                **outSampler) {
  GPUDeviceMT              *deviceMT;
  MTLSamplerDescriptor     *desc;
  const GPUSamplerLODClamp *lod;
  GPUSampler               *sampler;
  id<MTLSamplerState>       state;

  (void)api;
  (void)staticIfSupported;

  if (!device || !info || !outSampler) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outSampler = NULL;

  deviceMT             = device->_priv;
  desc                 = [MTLSamplerDescriptor new];
  desc.minFilter       = mt_samplerFilter(info->desc.minFilter);
  desc.magFilter       = mt_samplerFilter(info->desc.magFilter);
  desc.mipFilter       = mt_samplerMipFilter(info->desc.mipFilter);
  desc.sAddressMode    = mt_samplerAddressMode(info->desc.addressU);
  desc.tAddressMode    = mt_samplerAddressMode(info->desc.addressV);
  desc.rAddressMode    = mt_samplerAddressMode(info->desc.addressW);
  desc.maxAnisotropy   = info->desc.maxAnisotropy > 1u ? info->desc.maxAnisotropy : 1u;
  desc.compareFunction = info->desc.compareEnable
                         ? mt_samplerCompareFunction(info->desc.compare)
                         : MTLCompareFunctionNever;

  lod = samplerLODClamp(info);

  if (lod) {
    desc.lodMinClamp = lod->minLOD;
    desc.lodMaxClamp = lod->maxLOD;
  }

  state = [deviceMT->device newSamplerStateWithDescriptor:desc];
  [desc release];

  if (!state) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (!(sampler = calloc(1, sizeof(*sampler)))) {
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
mt_destroySampler(GPUSampler *__restrict sampler) {
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
mt_initLibrary(GPULibraryApi *api) {
  api->newLibraryWithSource = mt_newLibraryWithSource;
  api->newFunction          = mt_newFunction;
  api->newVariant           = mt_newVariant;
  api->destroyFunction      = mt_destroyFunction;
  api->destroyLibrary       = mt_destroyLibrary;
}

GPU_HIDE
void
mt_initSampler(GPUSamplerApi *api) {
  api->createSampler  = mt_createSampler;
  api->destroySampler = mt_destroySampler;
}
