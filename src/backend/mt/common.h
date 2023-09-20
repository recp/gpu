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

#ifndef metal_common_h
#define metal_common_h

#include "../common.h"
#include <cmt/cmt.h>

#if defined(__APPLE__) && defined(__OBJC__)
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

typedef CALayer GPUViewLayer;

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
typedef UIView GPUViewHandle;
#elif TARGET_OS_MAC
#import <AppKit/AppKit.h>
typedef NSView GPUViewHandle;
#else
#error "Unsupported platform"
#endif

@class GPUSwapChainObjc;

typedef struct GPUSwapChainMetal {
  CAMetalLayer *layer;
  void         *objc;
} GPUSwapChainMetal;
#endif

#endif /* metal_common_h */
