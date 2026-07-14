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

@interface GPUSwapchainObjc: NSObject {
@public
  GPUSwapchainMetal *swapchainMtl;
  float              backingScaleFactor;
  id                 observedObject;
  NSString          *observedKeyPath;
}
- (void)gpuObserveObject:(id)object keyPath:(NSString *)keyPath;
- (void)gpuStopObserving;
@end

@implementation GPUSwapchainObjc
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

    swapchainMtl->layer.frame        = newFrame;
    swapchainMtl->layer.drawableSize = drawableSize;
  }
}
- (void)dealloc {
  [self gpuStopObserving];
  [super dealloc];
}
@end

static void
mt_swapchainAttachToView(GPUSwapchain * __restrict swapchain,
                         void         * __restrict viewHandle,
                         bool                      autoResize, 
                         bool                      replace) {
  GPUSwapchainMetal *swapchainMtl;
  GPUViewHandle     *_viewHandle;

  swapchainMtl = swapchain->_priv;
  _viewHandle  = (GPUViewHandle *)viewHandle;

#if TARGET_OS_IOS
  GPU__UNUSED(replace);
  [_viewHandle.layer addSublayer:swapchainMtl->layer];
  swapchainMtl->layer.contentsScale = _viewHandle.contentScaleFactor;
#elif TARGET_OS_MAC
  // Ensure the view's layer is a CAMetalLayer for macOS
  if (replace || ![_viewHandle.layer isKindOfClass:[CAMetalLayer class]]) {
    _viewHandle.wantsLayer = YES;
    _viewHandle.layer      = swapchainMtl->layer;
  } else {
    [_viewHandle.layer addSublayer:swapchainMtl->layer];
  }
#else
#  error "Unsupported platform"
#endif

  // Set the frame for the CAMetalLayer
  swapchainMtl->layer.frame = _viewHandle.bounds;

  if (autoResize) {
    [(GPUSwapchainObjc *)swapchainMtl->objc gpuObserveObject:_viewHandle.layer
                                                     keyPath:@"bounds"];
  }
}

GPU_HIDE
GPUSwapchain*
mt_createSwapchain(GPUApi          * __restrict api,
                   GPUDevice       * __restrict device,
                   GPUQueue        * __restrict cmdQue,
                   const GPUSwapchainCreateInfo * __restrict info) {
  GPUDeviceMT       *deviceMT;
  GPUSwapchain      *swapchain;
  GPUSwapchainMetal *swapchainMtl;
  GPUSwapchainObjc  *objc;
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
  swapchain                           = calloc(1, sizeof(*swapchain));
  swapchainMtl                        = calloc(1, sizeof(*swapchainMtl));
  swapchainMtl->layer                 = [[CAMetalLayer alloc] init];
  swapchainMtl->layer.bounds          = CGRectMake(0, 0, size.width, size.height);
  swapchainMtl->layer.device          = deviceMT->device;
  swapchainMtl->layer.pixelFormat     = mt_format(info->format);
  swapchainMtl->layer.opaque          = YES;
  swapchainMtl->layer.contentsScale   = surface->scale;
  swapchainMtl->layer.contentsGravity = kCAGravityResizeAspectFill;
  swapchain->_priv                    = swapchainMtl;
  swapchain->backingScaleFactor       = surface->scale;
  swapchainMtl->objc                  = [GPUSwapchainObjc new];

  objc                                = swapchainMtl->objc;
  objc->swapchainMtl                  = swapchainMtl;
  objc->backingScaleFactor            = surface->scale;

  mt_swapchainAttachToView(swapchain, surface->_priv, true, true);

  return swapchain;
}

GPU_HIDE
GPUResult
mt_resizeSwapchain(GPUSwapchain * __restrict swapchain,
                   GPUExtent2D                size) {
  GPUSwapchainMetal *swapchainMtl;
  CGRect             bounds;
  CGSize             drawableSize;

  if (!swapchain || !swapchain->_priv || size.width == 0 || size.height == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  swapchainMtl = swapchain->_priv;
  if (!swapchainMtl->layer) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  bounds       = CGRectMake(0, 0, size.width, size.height);
  drawableSize = CGSizeMake(size.width  * swapchain->backingScaleFactor,
                            size.height * swapchain->backingScaleFactor);

  swapchainMtl->layer.bounds       = bounds;
  swapchainMtl->layer.drawableSize = drawableSize;

  return GPU_OK;
}

GPU_HIDE
void
mt_destroySwapchain(GPUSwapchain * __restrict swapchain) {
  GPUSwapchainMetal *swapchainMtl;

  if (!swapchain) {
    return;
  }

  swapchainMtl = swapchain->_priv;
  if (swapchainMtl) {
    if (swapchainMtl->objc) {
      [(GPUSwapchainObjc *)swapchainMtl->objc gpuStopObserving];
      [(id)swapchainMtl->objc release];
    }
    if (swapchainMtl->layer) {
      [swapchainMtl->layer removeFromSuperlayer];
      [swapchainMtl->layer release];
    }
    free(swapchainMtl);
  }

  free(swapchain);
}

GPU_HIDE
void
mt_initSwapchain(GPUApiSwapchain *api) {
  api->createSwapchain  = mt_createSwapchain;
  api->resizeSwapchain  = mt_resizeSwapchain;
  api->destroySwapchain = mt_destroySwapchain;
}
