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

@interface GPUSwapChainObjc: NSObject {
@public
  GPUSwapChainMetal *swapChainMtl;
  float              backingScaleFactor;
  id                 observedObject;
  NSString          *observedKeyPath;
}
- (void)gpuObserveObject:(id)object keyPath:(NSString *)keyPath;
- (void)gpuStopObserving;
@end

@implementation GPUSwapChainObjc
- (void)gpuObserveObject:(id)object keyPath:(NSString *)keyPath {
  [self gpuStopObserving];
  observedObject  = [object retain];
  observedKeyPath = [keyPath copy];
  [observedObject addObserver:self
                   forKeyPath:observedKeyPath
                      options:NSKeyValueObservingOptionNew
                      context:NULL];
}

- (void)gpuStopObserving {
  if (observedObject && observedKeyPath) {
    [observedObject removeObserver:self forKeyPath:observedKeyPath];
    [observedObject release];
    [observedKeyPath release];
    observedObject  = nil;
    observedKeyPath = nil;
  }
}

- (void)observeValueForKeyPath: (NSString *)keyPath
                      ofObject: (id)object
                      change:   (NSDictionary *)change
                      context:  (void *)context {
  if ([keyPath isEqualToString:@"bounds"]) {
    CGRect newFrame     = [change[NSKeyValueChangeNewKey] CGRectValue];
    CGSize drawableSize = CGSizeMake(newFrame.size.width  * backingScaleFactor,
                                     newFrame.size.height * backingScaleFactor);

    swapChainMtl->layer.frame        = newFrame;
    swapChainMtl->layer.drawableSize = drawableSize;
  }
}
- (void)dealloc {
  [self gpuStopObserving];
  [super dealloc];
}
@end

GPU_EXPORT
void
mt_swapChainAttachToLayer(GPUSwapChain * __restrict swapChain,
                          void         * __restrict targetLayer,
                          bool                      autoResize) {
  GPUSwapChainMetal *swapChainMtl;
  GPUViewLayer      *_targetLayer;

  _targetLayer = (GPUViewLayer *)targetLayer;

  swapChainMtl                      = swapChain->_priv;
  swapChainMtl->layer.contentsScale = _targetLayer.contentsScale * swapChain->backingScaleFactor;
  swapChainMtl->layer.frame         = _targetLayer.bounds;

  [_targetLayer addSublayer: swapChainMtl->layer];

  if (autoResize) {
    [(GPUSwapChainObjc *)swapChainMtl->objc gpuObserveObject:_targetLayer
                                                     keyPath:@"bounds"];
  }
}

GPU_EXPORT
void
mt_swapChainAttachToView(GPUSwapChain * __restrict swapChain,
                         void         * __restrict viewHandle,
                         bool                      autoResize, 
                         bool                      replace) {
  GPUSwapChainMetal *swapChainMtl;
  GPUViewHandle     *_viewHandle;

  swapChainMtl = swapChain->_priv;
  _viewHandle  = (GPUViewHandle *)viewHandle;

#if TARGET_OS_IOS
  // Ensure the view's layer is a CAMetalLayer for iOS
  if (replace || ![_viewHandle.layer isKindOfClass: [CAMetalLayer class]]) {
    _viewHandle.layer = swapChainMtl->layer;
  } else {
    [_viewHandle.layer addSublayer:swapChainMtl->layer];
  }
  swapChainMtl->layer.contentsScale = _viewHandle.contentScaleFactor;
#elif TARGET_OS_MAC
  // Ensure the view's layer is a CAMetalLayer for macOS
  if (replace || ![_viewHandle.layer isKindOfClass:[CAMetalLayer class]]) {
    _viewHandle.wantsLayer = YES;
    _viewHandle.layer      = swapChainMtl->layer;
  } else {
    [_viewHandle.layer addSublayer:swapChainMtl->layer];
  }
#else
#  error "Unsupported platform"
#endif

  // Set the frame for the CAMetalLayer
  swapChainMtl->layer.frame = _viewHandle.bounds;

  if (autoResize) {
    [(GPUSwapChainObjc *)swapChainMtl->objc gpuObserveObject:_viewHandle.layer
                                                     keyPath:@"bounds"];
  }
}

GPU_HIDE
GPUSwapChain*
mt_createSwapChain(GPUApi          * __restrict api,
                   GPUDevice       * __restrict device,
                   GPUCommandQueue * __restrict cmdQue,
                   GPUSurface      * __restrict surface,
                   GPUExtent2D                  size,
                   bool                         autoResize) {
  GPUDeviceMT       *deviceMT;
  GPUSwapChain      *swapChain;
  GPUSwapChainMetal *swapChainMtl;
  GPUSwapChainObjc  *objc;

  deviceMT                            = device->_priv;
  swapChain                           = calloc(1, sizeof(*swapChain));
  swapChainMtl                        = calloc(1, sizeof(*swapChainMtl));
  swapChainMtl->layer                 = [[CAMetalLayer alloc] init];
  swapChainMtl->layer.bounds          = CGRectMake(0, 0, size.width, size.height);
  swapChainMtl->layer.device          = deviceMT->device;
  //  swapChainMtl->layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
  swapChainMtl->layer.opaque          = YES;
  swapChainMtl->layer.contentsScale   = surface->scale;
  swapChainMtl->layer.contentsGravity = kCAGravityResizeAspectFill;
  swapChain->_priv                    = swapChainMtl;
  swapChain->backingScaleFactor       = surface->scale;
  swapChainMtl->objc                  = [GPUSwapChainObjc new];

  objc                                = swapChainMtl->objc;
  objc->swapChainMtl                  = swapChainMtl;
  objc->backingScaleFactor            = surface->scale;

  mt_swapChainAttachToView(swapChain, surface->_priv, autoResize, true);

  return swapChain;
}

GPU_HIDE
GPUSwapChain*
mt_createSwapChainForView(struct GPUApi          * __restrict api,
                          struct GPUDevice       * __restrict device,
                          struct GPUCommandQueue * __restrict cmdQue,
                          void                   * __restrict viewHandle,
                          GPUWindowType                       viewHandleType,
                          float                               backingScaleFactor,
                          uint32_t                            width,
                          uint32_t                            height,
                          bool                                autoResize) {
  GPUDeviceMT       *deviceMT;
  GPUSwapChain      *swapChain;
  GPUSwapChainMetal *swapChainMtl;
  GPUSwapChainObjc  *objc;

  deviceMT                            = device->_priv;
  swapChain                           = calloc(1, sizeof(*swapChain));
  swapChainMtl                        = calloc(1, sizeof(*swapChainMtl));
  swapChainMtl->layer                 = [[CAMetalLayer alloc] init];
  swapChainMtl->layer.bounds          = CGRectMake(0, 0, width, height);
  swapChainMtl->layer.device          = deviceMT->device;
  //  swapChainMtl->layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
  swapChainMtl->layer.opaque          = YES;
  swapChainMtl->layer.contentsScale   = backingScaleFactor;
  swapChainMtl->layer.contentsGravity = kCAGravityResizeAspectFill;
  swapChain->_priv                    = swapChainMtl;
  swapChain->backingScaleFactor       = backingScaleFactor;
  swapChainMtl->objc                  = [GPUSwapChainObjc new];

  objc                                = swapChainMtl->objc;
  objc->swapChainMtl                  = swapChainMtl;
  objc->backingScaleFactor            = backingScaleFactor;

  mt_swapChainAttachToView(swapChain, viewHandle, autoResize, true);

  return swapChain;
}

GPU_HIDE
GPUSwapChain*
mt_createSwapChainForLayer(struct GPUApi          * __restrict api,
                           struct GPUDevice       * __restrict device,
                           struct GPUCommandQueue * __restrict cmdQue,
                           float                               backingScaleFactor,
                           uint32_t                            width,
                           uint32_t                            height,
                           bool                                autoResize) {
  GPUDeviceMT       *deviceMT;
  GPUSwapChain      *swapChain;
  GPUSwapChainMetal *swapChainMtl;
  GPUSwapChainObjc  *objc;

  deviceMT                            = device->_priv;
  swapChain                           = calloc(1, sizeof(*swapChain));
  swapChainMtl                        = calloc(1, sizeof(*swapChainMtl));
  swapChainMtl->layer                 = [[CAMetalLayer alloc] init];
  swapChainMtl->layer.bounds          = CGRectMake(0, 0, width, height);
  swapChainMtl->layer.device          = deviceMT->device;
  //  swapChainMtl->layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
  swapChainMtl->layer.opaque          = YES;
  swapChainMtl->layer.contentsScale   = backingScaleFactor;
  swapChainMtl->layer.contentsGravity = kCAGravityResizeAspectFill;
  swapChain->_priv                    = swapChainMtl;
  swapChain->backingScaleFactor       = backingScaleFactor;
  swapChainMtl->objc                  = [GPUSwapChainObjc new];

  objc                                = swapChainMtl->objc;
  objc->swapChainMtl                  = swapChainMtl;
  objc->backingScaleFactor            = backingScaleFactor;

  return swapChain;
}

//GPU_HIDE
//GPUSwapChain*
//mt_createSwapChain(GPUApi          * __restrict api,
//                   GPUDevice       * __restrict device,
//                   GPUCommandQueue * __restrict cmdQue,
//                   float                        backingScaleFactor,
//                   float                        width,
//                   float                        height) {
//  GPUSwapChain      *swapChain;
//  GPUSwapChainMetal *swapChainMtl;
//  GPUSwapChainObjc  *objc;
//
//  swapChain                           = calloc(1, sizeof(*swapChain));
//  swapChainMtl                        = calloc(1, sizeof(*swapChainMtl));
//  swapChainMtl->layer                 = [CAMetalLayer layer];
//  swapChainMtl->layer.bounds          = CGRectMake(0, 0, width, height);
//  swapChainMtl->layer.device          = device->_priv;
////  swapChainMtl->layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
//  swapChainMtl->layer.opaque          = YES;
//  swapChainMtl->layer.contentsScale   = backingScaleFactor;
//  swapChainMtl->layer.contentsGravity = kCAGravityResizeAspectFill;
//  swapChain->_priv                    = swapChainMtl;
//  swapChain->backingScaleFactor       = backingScaleFactor;
//  swapChainMtl->objc                  = [GPUSwapChainObjc new];
//
//  objc                                = swapChainMtl->objc;
//  objc->swapChainMtl                  = swapChainMtl;
//  objc->backingScaleFactor            = backingScaleFactor;
//
//  return swapChain;
//}

GPU_HIDE
GPUResult
mt_resizeSwapChain(GPUSwapChain * __restrict swapChain,
                   GPUExtent2D                size) {
  GPUSwapChainMetal *swapChainMtl;
  CGRect             bounds;
  CGSize             drawableSize;

  if (!swapChain || !swapChain->_priv || size.width == 0 || size.height == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  swapChainMtl = swapChain->_priv;
  if (!swapChainMtl->layer) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  bounds       = CGRectMake(0, 0, size.width, size.height);
  drawableSize = CGSizeMake(size.width  * swapChain->backingScaleFactor,
                            size.height * swapChain->backingScaleFactor);

  swapChainMtl->layer.bounds       = bounds;
  swapChainMtl->layer.drawableSize = drawableSize;

  return GPU_OK;
}

GPU_HIDE
void
mt_destroySwapChain(GPUSwapChain * __restrict swapChain) {
  GPUSwapChainMetal *swapChainMtl;

  if (!swapChain) {
    return;
  }

  swapChainMtl = swapChain->_priv;
  if (swapChainMtl) {
    if (swapChainMtl->objc) {
      [(GPUSwapChainObjc *)swapChainMtl->objc gpuStopObserving];
      [(id)swapChainMtl->objc release];
    }
    if (swapChainMtl->layer) {
      [swapChainMtl->layer removeFromSuperlayer];
      [swapChainMtl->layer release];
    }
    free(swapChainMtl);
  }

  free(swapChain);
}

GPU_HIDE
void
mt_initSwapChain(GPUApiSwapChain *api) {
  api->createSwapChain         = mt_createSwapChain;
  api->createSwapChainForView  = mt_createSwapChainForView;
  api->createSwapChainForLayer = mt_createSwapChainForLayer;
  api->resizeSwapChain         = mt_resizeSwapChain;
  api->attachToLayer           = mt_swapChainAttachToLayer;
  api->attachToView            = mt_swapChainAttachToView;
  api->destroySwapChain        = mt_destroySwapChain;
}
