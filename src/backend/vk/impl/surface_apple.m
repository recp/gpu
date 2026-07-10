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

#import <QuartzCore/CAMetalLayer.h>

#if TARGET_OS_IOS
#  import <UIKit/UIKit.h>
#else
#  import <AppKit/AppKit.h>
#endif

GPU_HIDE
void*
vk_createMetalLayer(void *nativeHandle, GPUSurfaceType type, float scale) {
  CAMetalLayer *layer;
  CALayer      *rootLayer;
  CGRect        bounds;

  if (!nativeHandle || scale <= 0.0f) {
    return NULL;
  }

#if TARGET_OS_IOS
  if (type != GPU_SURFACE_APPLE_UIVIEW) {
    return NULL;
  }

  UIView *view = (UIView *)nativeHandle;
  rootLayer    = view.layer;
  bounds       = view.bounds;
#else
  if (type != GPU_SURFACE_APPLE_NSVIEW) {
    return NULL;
  }

  NSView *view   = (NSView *)nativeHandle;
  view.wantsLayer = YES;
  rootLayer       = view.layer;
  bounds          = view.bounds;
#endif

  if (!rootLayer) {
    return NULL;
  }

  layer                 = [[CAMetalLayer alloc] init];
  layer.frame           = bounds;
  layer.contentsScale   = scale;
  layer.drawableSize    = CGSizeMake(bounds.size.width * scale,
                                     bounds.size.height * scale);
  layer.opaque          = YES;
  layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
  [rootLayer addSublayer:layer];
  return layer;
}

GPU_HIDE
void
vk_resizeMetalLayer(void *metalLayer,
                    uint32_t width,
                    uint32_t height,
                    float scale) {
  CAMetalLayer *layer;

  layer = (CAMetalLayer *)metalLayer;
  if (!layer || width == 0u || height == 0u || scale <= 0.0f) {
    return;
  }

  layer.frame        = CGRectMake(0.0, 0.0, width, height);
  layer.contentsScale = scale;
  layer.drawableSize = CGSizeMake((CGFloat)width * scale,
                                  (CGFloat)height * scale);
}

GPU_HIDE
void
vk_destroyMetalLayer(void *metalLayer) {
  CAMetalLayer *layer;

  layer = (CAMetalLayer *)metalLayer;
  if (!layer) {
    return;
  }

  [layer removeFromSuperlayer];
  [layer release];
}
