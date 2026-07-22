/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "surface_apple.h"

#import <QuartzCore/CAMetalLayer.h>
#import <TargetConditionals.h>

#if TARGET_OS_IOS
#  import <UIKit/UIKit.h>
#else
#  import <AppKit/AppKit.h>
#endif

GPU_HIDE
void *
gpuCreateMetalLayer(void *nativeHandle, GPUSurfaceType type, float scale) {
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

  layer                  = [[CAMetalLayer alloc] init];
  layer.frame            = bounds;
  layer.contentsScale    = scale;
  layer.drawableSize     = CGSizeMake(bounds.size.width * scale,
                                      bounds.size.height * scale);
  layer.opaque           = YES;
  layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
  [rootLayer addSublayer:layer];
  return layer;
}

GPU_HIDE
void
gpuResizeMetalLayer(void *metalLayer,
                    uint32_t width,
                    uint32_t height,
                    float scale) {
  CAMetalLayer *layer;

  layer = (CAMetalLayer *)metalLayer;
  if (!layer || width == 0u || height == 0u || scale <= 0.0f) {
    return;
  }

  layer.frame         = CGRectMake(0.0, 0.0, width, height);
  layer.contentsScale = scale;
  layer.drawableSize  = CGSizeMake((CGFloat)width * scale,
                                   (CGFloat)height * scale);
}

GPU_HIDE
void
gpuDestroyMetalLayer(void *metalLayer) {
  CAMetalLayer *layer;

  layer = (CAMetalLayer *)metalLayer;
  if (!layer) {
    return;
  }

  [layer removeFromSuperlayer];
  [layer release];
}
