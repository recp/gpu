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
#if __has_include(<QuartzCore/CAMetalDisplayLink.h>)
#  import <QuartzCore/CAMetalDisplayLink.h>
#endif

#if __has_include(<QuartzCore/CAMetalDisplayLink.h>)
API_AVAILABLE(macos(14.0), ios(17.0), tvos(17.0))
@interface GPUFramePacerTarget : NSObject<CAMetalDisplayLinkDelegate> {
@public
  GPUFramePacer      *pacer;
  CAMetalDisplayLink *link;
  NSThread           *thread;
  GPUFrame           *callbackFrame;
  double             previousPresentation;
  CAFrameRateRange    rateRange;
  float              latencyFrames;
  bool               paused;
  bool               inCallback;
}
- (bool)attachToLayer:(CAMetalLayer *)layer;
@end

#endif

static GPUResult
mt_observeFrame(GPUFrame *frame, GPUFrameFeedback *feedback) {
  if (!frame || !frame->drawable || !feedback)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (@available(macOS 10.15.4, iOS 10.3, *))
    return GPU_OK;

  return GPU_ERROR_UNSUPPORTED;
}

static GPUFrame*
mt_beginDrawableFrame(GPUApi *api, GPUSwapchain *swapchain, void *nativeDrawable) {
  GPUFrame           *frame;
  GPUTexture         *target;
  GPUTextureView     *targetView;
  GPUSwapchainMetal  *swapchainMtl;
  id<CAMetalDrawable> drawable;

  (void)api;
  swapchainMtl = swapchain->_priv;

  if (!swapchainMtl || !swapchainMtl->layer) {
    swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_SURFACE_LOST);
    return NULL;
  }

  if (swapchainMtl->frameActive) {
    return NULL;
  }

  if (swapchainMtl->presentsWithTransaction && ![NSThread isMainThread])
    return NULL;

  drawable = (id<CAMetalDrawable>)nativeDrawable;
  if (!drawable) {
    swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_UNAVAILABLE);
    return NULL;
  }

  swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_READY);

  [drawable retain];
  frame      = &swapchainMtl->frame;
  target     = &swapchainMtl->target;
  targetView = &swapchainMtl->targetView;
  memset(frame, 0, sizeof(*frame));
  memset(target, 0, sizeof(*target));
  memset(targetView, 0, sizeof(*targetView));

  target->_priv         = drawable.texture;
  target->format        = mt_formatFromNative(drawable.texture.pixelFormat);
  target->dimension     = GPU_TEXTURE_DIMENSION_2D;
  target->width         = (uint32_t)drawable.texture.width;
  target->height        = (uint32_t)drawable.texture.height;
  target->depthOrLayers = 1;
  target->mipLevelCount = 1;
  target->sampleCount   = 1;
  target->usage         = GPU_TEXTURE_USAGE_COLOR_TARGET;
  target->_ownsNative   = false;

  targetView->_priv           = drawable.texture;
  targetView->_texture        = target;
  targetView->format          = target->format;
  targetView->viewType        = GPU_TEXTURE_VIEW_2D;
  targetView->baseMipLevel    = 0;
  targetView->mipLevelCount   = 1;
  targetView->baseArrayLayer  = 0;
  targetView->arrayLayerCount = 1;
  targetView->_ownsNative     = false;

  frame->_priv              = swapchainMtl;
  frame->target             = target;
  frame->targetView         = targetView;
  frame->drawable           = drawable;
  swapchainMtl->frameActive = true;

  return frame;
}

GPU_HIDE
GPUFrame*
mt_beginFrame(GPUApi *api, GPUSwapchain *swapchain) {
  GPUSwapchainMetal *native;

  native = swapchain ? swapchain->_priv : NULL;
  if (!native || !native->layer) {
    swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_SURFACE_LOST);
    return NULL;
  }

  if (native->frameActive)
    return NULL;

  if (native->presentsWithTransaction && ![NSThread isMainThread])
    return NULL;

  return mt_beginDrawableFrame(api, swapchain, [native->layer nextDrawable]);
}

GPU_HIDE
void
mt_endFrame(GPUApi   *__restrict api,
            GPUFrame *__restrict frame) {
  GPUSwapchainMetal *swapchainMtl;

  (void)api;

  if (!frame)
    return;

  swapchainMtl = frame->_priv;
  [(id<CAMetalDrawable>)frame->drawable release];
  frame->drawable = NULL;

  if (swapchainMtl) {
    swapchainMtl->frameActive = false;
  }
}

#if __has_include(<QuartzCore/CAMetalDisplayLink.h>)
@implementation GPUFramePacerTarget
- (bool)attachToLayer:(CAMetalLayer *)layer {
  CAMetalDisplayLink *created;

  if (!(created = [[CAMetalDisplayLink alloc] initWithMetalLayer:layer]))
    return false;

  created.delegate = self;
  created.paused   = YES;
  created.preferredFrameLatency = latencyFrames;
  if (rateRange.maximum > 0.0f)
    created.preferredFrameRateRange = rateRange;

  link = created;
  [link addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
  return true;
}

- (void)metalDisplayLink:(CAMetalDisplayLink *)displayLink needsUpdate:(CAMetalDisplayLinkUpdate *)update {
  GPUFrameTiming  timing;
  GPUFrame       *frame;
  void           *user;
  GPUFramePacerFn callback;

  [self retain];
  if (!pacer || displayLink != link || inCallback || thread != [NSThread currentThread] || paused) {
    [self release];
    return;
  }

  inCallback = true;
  timing.targetPresentTime = update.targetPresentationTimestamp;
  timing.deadline          = update.targetTimestamp;
  timing.refreshInterval   = previousPresentation > 0.0
                               ? timing.targetPresentTime - previousPresentation : 0.0;
  previousPresentation     = timing.targetPresentTime;
  callback = pacer->callback;
  user     = pacer->user;
  frame    = frameBeginDrawable(pacer->swapchain, update.drawable);
  if (frame)
    frame->targetPresentTime = timing.targetPresentTime;

  callbackFrame = frame;
  callback(frame, &timing, user);

  /* callback destruction clears pacer before releasing its wrapper. */
  if (pacer && frame && frame->drawable == update.drawable)
    GPUEndFrame(frame);

  callbackFrame = NULL;
  inCallback    = false;
  [self release];
}

- (void)dealloc {
  [thread release];
  [link release];
  [super dealloc];
}
@end
#endif

static GPUResult
mt_createFramePacer(GPUFramePacer *pacer, const GPUFramePacerCreateInfo *info) {
#if __has_include(<QuartzCore/CAMetalDisplayLink.h>)
  GPUFramePacerTarget *target;
  GPUSwapchainMetal  *native;

  if (@available(macOS 14.0, iOS 17.0, *)) {
    native = pacer->swapchain->_priv;
    if (!native || !native->layer || native->frameActive)
      return GPU_ERROR_INVALID_ARGUMENT;

    if (!(target = [GPUFramePacerTarget new]))
      return GPU_ERROR_OUT_OF_MEMORY;

    target->pacer  = pacer;
    target->thread = [[NSThread currentThread] retain];
    target->paused = true;
    target->latencyFrames = info->latencyFrames ? info->latencyFrames : 1u;
    if (info->maxRate > 0.0f)
      target->rateRange = CAFrameRateRangeMake(info->minRate,
          info->maxRate, info->preferredRate ? info->preferredRate : info->maxRate);

    if (![target attachToLayer:native->layer]) {
      [target release];
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    pacer->_priv = target;
    return GPU_OK;
  }
#endif
  return GPU_ERROR_UNSUPPORTED;
}

static void
mt_pauseFramePacer(GPUFramePacer *pacer, bool paused) {
#if __has_include(<QuartzCore/CAMetalDisplayLink.h>)
  GPUFramePacerTarget *target;
  GPUSwapchainMetal  *native;

  if (@available(macOS 14.0, iOS 17.0, *)) {
    target = pacer->_priv;
    if (!target || target->thread != [NSThread currentThread])
      return;

    native = pacer->swapchain->_priv;
    if (!paused && native && native->frameActive && target->callbackFrame != &native->frame)
      return;

    if (!paused && !target->link) {
      if (!native || !native->layer) {
        deviceReportError(pacer->device, GPU_DEVICE_ERROR_BACKEND, GPU_DEVICE_LOST_REASON_UNKNOWN,
                          GPU_ERROR_BACKEND_FAILURE, "cannot resume frame pacer without a Metal layer");
        return;
      }

      if (![target attachToLayer:native->layer]) {
        deviceReportError(pacer->device, GPU_DEVICE_ERROR_OUT_OF_MEMORY, GPU_DEVICE_LOST_REASON_UNKNOWN,
                          GPU_ERROR_OUT_OF_MEMORY, "cannot recreate Metal frame display link");
        return;
      }
    }

    target->paused = paused;
    target->previousPresentation = 0.0;
    target->link.paused = paused;
  }
#endif
}

static void
mt_destroyFramePacer(GPUFramePacer *pacer) {
#if __has_include(<QuartzCore/CAMetalDisplayLink.h>)
  GPUFramePacerTarget *target;
  GPUFrame           *frame;

  if (@available(macOS 14.0, iOS 17.0, *)) {
    target = pacer->_priv;
    frame  = target->callbackFrame;
    target->pacer         = NULL;
    target->callbackFrame = NULL;

    [target->link invalidate];
    [target->link release];
    target->link = nil;
    if (frame && frame->drawable)
      GPUEndFrame(frame);

    [target release];
    pacer->_priv = NULL;
  }
#endif
}

static bool
mt_prepareWithPacer(GPUFramePacer *pacer) {
#if __has_include(<QuartzCore/CAMetalDisplayLink.h>)
  GPUFramePacerTarget *target;
  GPUSwapchainMetal  *native;

  if (@available(macOS 14.0, iOS 17.0, *)) {
    target = pacer->_priv;
    if (!target || target->thread != [NSThread currentThread])
      return false;

    native = pacer->swapchain->_priv;
    if (!target->paused || target->inCallback || !native || native->frameActive)
      return false;

    /* a paused display link still owns acquisition until it is invalidated. */
    [target->link invalidate];
    [target->link release];
    target->link = nil;
    return true;
  }
#endif
  return false;
}

GPU_HIDE
void
mt_initFrame(GPUFrameApi *api) {
  api->beginFrame = mt_beginFrame;
  api->endFrame   = mt_endFrame;
  api->beginDrawable = mt_beginDrawableFrame;
  api->createPacer   = mt_createFramePacer;
  api->pausePacer    = mt_pauseFramePacer;
  api->destroyPacer  = mt_destroyFramePacer;
  api->prepareWithPacer = mt_prepareWithPacer;
  api->observe = mt_observeFrame;
}
