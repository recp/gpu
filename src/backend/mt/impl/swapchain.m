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
#include <math.h>

@interface GPUSwapchainObjc: NSObject {
@public
  GPUSwapchainMetal *swapchainMtl;
  float             backingScaleFactor;
  id                observedObject;
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

- (void)observeValueForKeyPath:(NSString *)keyPath
                    ofObject:(id)object
                      change:(NSDictionary *)change
                     context:(void *)context {
  CGRect newFrame;
  CGSize drawableSize;

  if ([keyPath isEqualToString:@"bounds"]) {
    newFrame     = [(CALayer *)object bounds];
    drawableSize = CGSizeMake(ceil(newFrame.size.width * backingScaleFactor),
                              ceil(newFrame.size.height * backingScaleFactor));

    swapchainMtl->layer.frame        = newFrame;
    swapchainMtl->layer.drawableSize = drawableSize;
  }
}

- (void)dealloc {
  [self gpuStopObserving];
  [super dealloc];
}

@end

static bool
mt_validSwapchainSize(id<MTLDevice> device, GPUExtent2D size, float scale) {
  double   width, height;
  uint32_t limit = 16384u;

#if (defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000) \
    || (defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 260000)
  if ([device supportsFamily:MTLGPUFamilyApple10])
    limit = 32768u;
#endif

  width  = (double)size.width * scale;
  height = (double)size.height * scale;

  return width >= 1.0 && width <= limit && height >= 1.0 && height <= limit;
}

static void
mt_swapchainAttachToView(GPUSwapchain *__restrict swapchain,
                         void         *__restrict viewHandle,
                         bool                     autoResize,
                         bool                     replace) {
  GPUSwapchainMetal *swapchainMtl;
  GPUViewHandle     *_viewHandle;

  swapchainMtl = swapchain->_priv;
  _viewHandle  = (GPUViewHandle *)viewHandle;

#if TARGET_OS_IOS
  GPU__UNUSED(replace);
  [_viewHandle.layer addSublayer:swapchainMtl->layer];
#elif TARGET_OS_MAC
  /* ensure the view's layer is a CAMetalLayer for macOS */

  _viewHandle.wantsLayer = YES;
  if (replace) {
    _viewHandle.layer      = swapchainMtl->layer;
  } else {
    [_viewHandle.layer addSublayer:swapchainMtl->layer];
  }
#else
#  error "Unsupported platform"
#endif

  /* set the frame for the CAMetalLayer */

  swapchainMtl->layer.frame = _viewHandle.bounds;

  if (autoResize) {
    [(GPUSwapchainObjc *)swapchainMtl->objc gpuObserveObject:_viewHandle.layer
                                                     keyPath:@"bounds"];
  }
}

GPU_HIDE
GPUSwapchain*
mt_createSwapchain(GPUApi                       *__restrict api,
                   GPUDevice                    *__restrict device,
                   GPUQueue                     *__restrict cmdQue,
                   const GPUSwapchainCreateInfo *__restrict info) {
  GPUExtent2D               size;
  const GPUSwapchainOptions *options;
  GPUDeviceMT              *deviceMT;
  GPUSwapchain             *swapchain;
  GPUSwapchainMetal        *swapchainMtl;
  GPUSwapchainObjc         *objc;
  GPUSurface               *surface;
  CGColorSpaceRef           colorSpace = NULL;
  uint32_t                  imageCount;

  GPU__UNUSED(api);
  GPU__UNUSED(cmdQue);

  if (!device || !info || !info->surface || ![NSThread isMainThread]) {
    return NULL;
  }

  surface     = info->surface;
  options     = info->chain.pNext;
  size.width  = info->width;
  size.height = info->height;
  imageCount  = info->imageCount ? info->imageCount : 3u;

  deviceMT     = device->_priv;
  if (!mt_validSwapchainSize(deviceMT->device, size, surface->scale))
    return NULL;

  swapchain    = calloc(1, sizeof(*swapchain));
  swapchainMtl = calloc(1, sizeof(*swapchainMtl));

  if (!swapchain || !swapchainMtl) {
    free(swapchainMtl);
    free(swapchain);
    return NULL;
  }

  if (!(swapchainMtl->layer = [[CAMetalLayer alloc] init])) {
    free(swapchainMtl);
    free(swapchain);
    return NULL;
  }

  if (options && options->colorSpace != GPU_COLOR_SPACE_DEFAULT) {
    colorSpace = CGColorSpaceCreateWithName(options->colorSpace == GPU_COLOR_SPACE_SRGB
                                             ? kCGColorSpaceSRGB : kCGColorSpaceDisplayP3);
    if (!colorSpace) {
      [swapchainMtl->layer release];
      free(swapchainMtl);
      free(swapchain);
      return NULL;
    }

    swapchainMtl->layer.colorspace = colorSpace;
    CGColorSpaceRelease(colorSpace);
  }

  swapchainMtl->layer.bounds          = CGRectMake(0, 0, size.width, size.height);
  swapchainMtl->layer.drawableSize    = CGSizeMake(ceil((double)size.width * surface->scale),
                                                  ceil((double)size.height * surface->scale));
  swapchainMtl->layer.device          = deviceMT->device;
  swapchainMtl->layer.pixelFormat     = mt_format(info->format);
  swapchainMtl->layer.opaque          = YES;
  swapchainMtl->layer.contentsScale   = surface->scale;
  swapchainMtl->layer.contentsGravity = kCAGravityResizeAspectFill;

  swapchainMtl->layer.maximumDrawableCount = imageCount;
  swapchainMtl->presentsWithTransaction = options && options->presentsWithTransaction;
  swapchainMtl->layer.presentsWithTransaction = swapchainMtl->presentsWithTransaction;
#if TARGET_OS_OSX
  swapchainMtl->layer.displaySyncEnabled = info->presentMode != GPU_PRESENT_MODE_IMMEDIATE;
#endif

  swapchain->_priv              = swapchainMtl;
  swapchain->backingScaleFactor = surface->scale;
  swapchainMtl->objc            = [GPUSwapchainObjc new];
  if (!swapchainMtl->objc) {
    [swapchainMtl->layer release];
    free(swapchainMtl);
    free(swapchain);
    return NULL;
  }

  objc                     = swapchainMtl->objc;
  objc->swapchainMtl       = swapchainMtl;
  objc->backingScaleFactor = surface->scale;

  /* AppKit owns the backing layer's colour space; explicit tagging needs a
   * sublayer so display changes cannot overwrite the requested content space. */
  mt_swapchainAttachToView(swapchain, surface->_priv, true,
                           !options || options->colorSpace == GPU_COLOR_SPACE_DEFAULT);

  return swapchain;
}

GPU_HIDE
GPUResult
mt_resizeSwapchain(GPUSwapchain *__restrict swapchain,
                   GPUExtent2D              size) {
  CGRect             bounds;
  CGSize             drawableSize;
  GPUSwapchainMetal *swapchainMtl;
  GPUSwapchainObjc  *objc;
  GPUDeviceMT       *deviceMT;

  if (!swapchain || !swapchain->_priv || size.width == 0 || size.height == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  swapchainMtl = swapchain->_priv;

  if (!swapchainMtl->layer) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (swapchainMtl->frameActive || ![NSThread isMainThread])
    return GPU_ERROR_INVALID_ARGUMENT;

  deviceMT = swapchain->device->_priv;
  if (!mt_validSwapchainSize(deviceMT->device, size, swapchain->backingScaleFactor))
    return GPU_ERROR_UNSUPPORTED;

  bounds       = CGRectMake(0, 0, size.width, size.height);
  drawableSize = CGSizeMake(ceil((double)size.width * swapchain->backingScaleFactor),
                            ceil((double)size.height * swapchain->backingScaleFactor));

  objc = swapchainMtl->objc;
  objc->backingScaleFactor          = swapchain->backingScaleFactor;
  swapchainMtl->layer.contentsScale = swapchain->backingScaleFactor;
  swapchainMtl->layer.bounds        = bounds;
  swapchainMtl->layer.drawableSize  = drawableSize;

  return GPU_OK;
}

GPU_HIDE
void
mt_destroySwapchain(GPUSwapchain *__restrict swapchain) {
  GPUSwapchainMetal *swapchainMtl;

  if (!swapchain) {
    return;
  }

  swapchainMtl = swapchain->_priv;

  if (swapchainMtl) {
    if (swapchainMtl->frameActive)
      GPUEndFrame(&swapchainMtl->frame);

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
mt_initSwapchain(GPUSwapchainApi *api) {
  api->createSwapchain  = mt_createSwapchain;
  api->resizeSwapchain  = mt_resizeSwapchain;
  api->destroySwapchain = mt_destroySwapchain;
}
