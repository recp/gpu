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

static void
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
                   const GPUSwapchainCreateInfo * __restrict info) {
  GPUDeviceMT       *deviceMT;
  GPUSwapChain      *swapChain;
  GPUSwapChainMetal *swapChainMtl;
  GPUSwapChainObjc  *objc;
  GPUSurface        *surface;
  GPUExtent2D        size;

  GPU__UNUSED(api);
  GPU__UNUSED(cmdQue);

  if (!device || !info || !info->surface) {
    return NULL;
  }

  surface     = info->surface;
  size.width  = info->width;
  size.height = info->height;

  deviceMT                            = device->_priv;
  swapChain                           = calloc(1, sizeof(*swapChain));
  swapChainMtl                        = calloc(1, sizeof(*swapChainMtl));
  swapChainMtl->layer                 = [[CAMetalLayer alloc] init];
  swapChainMtl->layer.bounds          = CGRectMake(0, 0, size.width, size.height);
  swapChainMtl->layer.device          = deviceMT->device;
  swapChainMtl->layer.pixelFormat     = (MTLPixelFormat)info->format;
  swapChainMtl->layer.opaque          = YES;
  swapChainMtl->layer.contentsScale   = surface->scale;
  swapChainMtl->layer.contentsGravity = kCAGravityResizeAspectFill;
  swapChain->_priv                    = swapChainMtl;
  swapChain->backingScaleFactor       = surface->scale;
  swapChainMtl->objc                  = [GPUSwapChainObjc new];

  objc                                = swapChainMtl->objc;
  objc->swapChainMtl                  = swapChainMtl;
  objc->backingScaleFactor            = surface->scale;

  mt_swapChainAttachToView(swapChain, surface->_priv, true, true);

  return swapChain;
}

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
  api->createSwapChain  = mt_createSwapChain;
  api->resizeSwapChain  = mt_resizeSwapChain;
  api->destroySwapChain = mt_destroySwapChain;
}
