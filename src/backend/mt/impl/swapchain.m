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
}
@end

@implementation GPUSwapChainObjc
- (void)observeValueForKeyPath: (NSString *)keyPath
                      ofObject: (id)object
                      change:   (NSDictionary *)change
                      context:  (void *)context {
  if ([keyPath isEqualToString:@"bounds"]) {
    // Here, adjust the frame of the CAMetalLayer to match the parent layer.
    CGRect newFrame = [change[NSKeyValueChangeNewKey] CGRectValue];
    swapChainMtl->layer.frame = newFrame;


    CGSize drawableSize = CGSizeMake(newFrame.size.width * backingScaleFactor, 
                                     newFrame.size.height * backingScaleFactor);
    swapChainMtl->layer.drawableSize = drawableSize;
  }
}
- (void)dealloc {
  [super dealloc];
  [swapChainMtl->layer removeObserver: self forKeyPath:@"frame"];
}
@end

GPU_EXPORT
void
mt_swapChainAttachToLayer(GPUSwapChain* swapChain, void* targetLayer, bool autoResize) {
  GPUSwapChainMetal *swapChainMtl;
  GPUViewLayer      *_targetLayer;

  _targetLayer = (GPUViewLayer *)targetLayer;

  swapChainMtl                      = swapChain->_priv;
  swapChainMtl->layer.contentsScale = _targetLayer.contentsScale * swapChain->backingScaleFactor;
  swapChainMtl->layer.frame         = _targetLayer.bounds;

  [_targetLayer addSublayer: swapChainMtl->layer];

  if (autoResize) {
    [_targetLayer addObserver: swapChainMtl->objc
                   forKeyPath: @"bounds"
                      options: NSKeyValueObservingOptionNew
                      context: NULL];
  }
}

GPU_EXPORT
void
mt_swapChainAttachToView(GPUSwapChain* swapChain, void *viewHandle, bool autoResize, bool replace) {
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
#error "Unsupported platform"
#endif

  // Set the frame for the CAMetalLayer
  swapChainMtl->layer.frame = _viewHandle.bounds;

  if (autoResize) {
    [_viewHandle.layer addObserver: swapChainMtl->objc
                        forKeyPath: @"bounds"
                           options: NSKeyValueObservingOptionNew
                           context: NULL];
  }
}

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
  GPUSwapChain      *swapChain;
  GPUSwapChainMetal *swapChainMtl;
  GPUSwapChainObjc  *objc;

  swapChain                           = calloc(1, sizeof(*swapChain));
  swapChainMtl                        = calloc(1, sizeof(*swapChainMtl));
  swapChainMtl->layer                 = [CAMetalLayer layer];
  swapChainMtl->layer.bounds          = CGRectMake(0, 0, width, height);
  swapChainMtl->layer.device          = device->priv;
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

GPUSwapChain*
mt_createSwapChainForLayer(struct GPUApi          * __restrict api,
                           struct GPUDevice       * __restrict device,
                           struct GPUCommandQueue * __restrict cmdQue,
                           float                               backingScaleFactor,
                           uint32_t                            width,
                           uint32_t                            height,
                           bool                                autoResize) {
  GPUSwapChain      *swapChain;
  GPUSwapChainMetal *swapChainMtl;
  GPUSwapChainObjc  *objc;

  swapChain                           = calloc(1, sizeof(*swapChain));
  swapChainMtl                        = calloc(1, sizeof(*swapChainMtl));
  swapChainMtl->layer                 = [CAMetalLayer layer];
  swapChainMtl->layer.bounds          = CGRectMake(0, 0, width, height);
  swapChainMtl->layer.device          = device->priv;
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

GPU_HIDE
GPUSwapChain*
mt_createSwapChain(GPUApi          * __restrict api,
                   GPUDevice       * __restrict device,
                   GPUCommandQueue * __restrict cmdQue,
                   float                        backingScaleFactor,
                   float                        width,
                   float                        height) {
  GPUSwapChain      *swapChain;
  GPUSwapChainMetal *swapChainMtl;
  GPUSwapChainObjc  *objc;

  swapChain                           = calloc(1, sizeof(*swapChain));
  swapChainMtl                        = calloc(1, sizeof(*swapChainMtl));
  swapChainMtl->layer                 = [CAMetalLayer layer];
  swapChainMtl->layer.bounds          = CGRectMake(0, 0, width, height);
  swapChainMtl->layer.device          = device->priv;
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

GPU_HIDE
void
mt_initSwapChain(GPUApiSwapChain *api) {
  api->createSwapChainForView  = mt_createSwapChainForView;
  api->createSwapChainForLayer = mt_createSwapChainForLayer;
  api->attachToLayer           = mt_swapChainAttachToLayer;
  api->attachToView            = mt_swapChainAttachToView;
}
