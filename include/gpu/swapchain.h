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

#ifndef gpu_swapchain_h
#define gpu_swapchain_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

struct GPUCommandQueue;

typedef struct GPUSwapChain {
  void *_priv;
  void *target; /* draw target */
  float backingScaleFactor;
} GPUSwapChain;


typedef enum GPUWindowType {
  GPU_WINDOW_TYPE_HWND,
  GPU_WINDOW_TYPE_COREWINDOW,
  GPU_WINDOW_TYPE_COCOA,
} GPUWindowType;

typedef struct GPUWindowHandle {
  GPUWindowType type;
  void         *ptr;
} GPUWindowHandle;

GPU_EXPORT
GPUSwapChain*
GPUCreateSwapChainForView(GPUDevice              * __restrict device,
                          struct GPUCommandQueue * __restrict cmdQue,
                          void                   * __restrict viewHandle,
                          GPUWindowType                       viewHandleType,
                          float                               backingScaleFactor,
                          uint32_t                            width,
                          uint32_t                            height,
                          bool                                autoResize);

GPU_EXPORT
GPUSwapChain*
GPUCreateSwapChainForLayer(GPUDevice              * __restrict device,
                           struct GPUCommandQueue * __restrict cmdQue,
                           float                               backingScaleFactor,
                           uint32_t                            width,
                           uint32_t                            height,
                           bool                                autoResize);

GPU_EXPORT
void
GPUSwapChainAttachToLayer(GPUSwapChain* swapChain, void* targetLayer, bool autoResize);

GPU_EXPORT
void
GPUSwapChainAttachToView(GPUSwapChain  *swapChain,
                         void          *viewHandle,
                         bool           autoResize,
                         bool           replace);

#if defined(__APPLE__) && defined(__OBJC__)
#if TARGET_OS_MAC
#import <AppKit/AppKit.h>

GPU_INLINE
GPUSwapChain*
GPUCreateSwapChainForNSView(GPUDevice              * __restrict device,
                            struct GPUCommandQueue * __restrict cmdQue,
                            NSView                 * __restrict view,
                            bool                                autoResize) {
  return GPUCreateSwapChainForView(device,
                                   cmdQue,
                                   view,
                                   GPU_WINDOW_TYPE_COCOA,
                                   GPUScreenScale(view),
                                   view.frame.size.width,
                                   view.frame.size.height,
                                   autoResize);
}
#elif TARGET_OS_IOS || TARGET_OS_TV
#import <UIKit/UIKit.h>

GPU_INLINE
GPUSwapChain*
GPUCreateSwapChainForUIView(GPUDevice              * __restrict device,
                            struct GPUCommandQueue * __restrict cmdQue,
                            UIView                 * __restrict view,
                            bool                                autoResize) {
  return GPUCreateSwapChainForView(device,
                                   cmdQue,
                                   view,
                                   GPU_WINDOW_TYPE_COCOA,
                                   GPUScreenScale(view),
                                   view.frame.size.width,
                                   view.frame.size.height,
                                   autoResize);
}
#endif
#endif

#ifdef __cplusplus
}
#endif
#endif /* gpu_swapchain_h */
