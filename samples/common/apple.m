#include "apple.h"
#include "asset_io.h"

#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <QuartzCore/QuartzCore.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void
(*GPUAppleRenderCallback)(void *userData);

struct GPUAppleSample {
  NSView                  *view;
  GPUInstance             *instance;
  GPUAdapter              *adapter;
  GPUDevice               *device;
  GPUQueue                *queue;
  GPUSurface              *surface;
  GPUSwapchain            *swapchain;
  GPUAppleRenderCallback   render;
  void                    *renderData;
  const char              *name;
  char                     status[256];
  uint64_t                 renderedFrameCount;
  uint32_t                 width;
  uint32_t                 height;
  uint32_t                 surfaceWidth;
  uint32_t                 surfaceHeight;
  float                    contentScale;
  bool                     canceled;
  bool                     failed;
};

static GPUAppleSample *activeSample;

static const char*
asset_name(const char *path) {
  return path && path[0] == '/' ? path + 1 : path;
}

static NSString*
asset_path(const char *path) {
  NSURL    *directory;
  NSString *name;

  if (!path) {
    return nil;
  }

  name      = [NSString stringWithUTF8String:asset_name(path)];
  directory = NSBundle.mainBundle.executableURL.URLByDeletingLastPathComponent;
  return [directory.path stringByAppendingPathComponent:name];
}

static uint8_t*
decode_image(CGImageSourceRef source, uint32_t *width, uint32_t *height) {
  CGColorSpaceRef colorSpace;
  CGContextRef    context;
  CGImageRef      image;
  uint8_t        *pixels;
  size_t          imageWidth, imageHeight, rowBytes;

  if (!source || !width || !height) {
    return NULL;
  }

  image = CGImageSourceCreateImageAtIndex(source, 0u, NULL);
  if (!image) {
    return NULL;
  }

  imageWidth  = CGImageGetWidth(image);
  imageHeight = CGImageGetHeight(image);
  rowBytes    = imageWidth * 4u;
  pixels      = imageWidth > 0u && imageHeight > 0u
                  ? malloc(rowBytes * imageHeight)
                  : NULL;
  colorSpace  = pixels ? CGColorSpaceCreateWithName(kCGColorSpaceSRGB) : NULL;
  context     = colorSpace
                  ? CGBitmapContextCreate(pixels,
                                          imageWidth,
                                          imageHeight,
                                          8u,
                                          rowBytes,
                                          colorSpace,
                                          kCGImageAlphaPremultipliedLast |
                                            kCGBitmapByteOrder32Big)
                  : NULL;
  if (context) {
    CGContextDrawImage(context,
                       CGRectMake(0.0, 0.0, imageWidth, imageHeight),
                       image);
    *width  = (uint32_t)imageWidth;
    *height = (uint32_t)imageHeight;
  } else {
    free(pixels);
    pixels = NULL;
  }

  if (context) {
    CGContextRelease(context);
  }
  if (colorSpace) {
    CGColorSpaceRelease(colorSpace);
  }
  CGImageRelease(image);
  return pixels;
}

static bool
resize_surface(GPUAppleSample *sample,
               GPUSwapchain   *swapchain,
               uint32_t       *width,
               uint32_t       *height) {
  NSRect   bounds;
  uint32_t nextHeight, nextSurfaceHeight, nextSurfaceWidth, nextWidth;

  if (!sample || !sample->view || !width || !height) {
    return false;
  }

  bounds            = sample->view.bounds;
  nextSurfaceWidth  = (uint32_t)bounds.size.width;
  nextSurfaceHeight = (uint32_t)bounds.size.height;
  nextWidth         = (uint32_t)(bounds.size.width * sample->contentScale);
  nextHeight        = (uint32_t)(bounds.size.height * sample->contentScale);
  if (nextSurfaceWidth == 0u || nextSurfaceHeight == 0u ||
      nextWidth == 0u || nextHeight == 0u) {
    return false;
  }
  if ((nextSurfaceWidth != sample->surfaceWidth ||
       nextSurfaceHeight != sample->surfaceHeight) &&
      swapchain &&
      GPUResizeSwapchain(swapchain,
                         nextSurfaceWidth,
                         nextSurfaceHeight) != GPU_OK) {
    return false;
  }

  sample->width         = nextWidth;
  sample->height        = nextHeight;
  sample->surfaceWidth  = nextSurfaceWidth;
  sample->surfaceHeight = nextSurfaceHeight;
  *width                = nextWidth;
  *height               = nextHeight;
  return true;
}

GPUAppleSample*
GPUSampleAppleCreate(void                *nativeView,
                     const char          *name,
                     float                contentScale,
                     GPUAppleSampleStart  start) {
  static const GPUFeature optionalFeatures[] = {
    GPU_FEATURE_COMPUTE,
    GPU_FEATURE_INDIRECT_DRAW,
    GPU_FEATURE_MULTI_DRAW,
    GPU_FEATURE_DESCRIPTOR_INDEXING,
    GPU_FEATURE_SUBGROUPS,
    GPU_FEATURE_SHADER_F16,
    GPU_FEATURE_TIMESTAMPS
  };
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUDeviceCreateInfo   deviceInfo   = {0};
  GPURuntimeConfig      runtimeInfo  = {0};
  GPUAppleSample       *sample;

  if (!nativeView || !name || contentScale <= 0.0f || !start ||
      activeSample) {
    return NULL;
  }

  sample = calloc(1, sizeof(*sample));
  if (!sample) {
    return NULL;
  }

  sample->view         = (__bridge NSView *)nativeView;
  sample->name         = name;
  sample->contentScale = contentScale;
  snprintf(sample->status, sizeof(sample->status), "GPU: starting %s", name);

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.label            = name;
  instanceInfo.preferredBackend = GPU_BACKEND_METAL;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &sample->instance) != GPU_OK ||
      !sample->instance) {
    goto fail;
  }

  sample->adapter = GPUGetAutoSelectedAdapter(sample->instance);
  if (!sample->adapter) {
    goto fail;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.optional.pFeatures    = optionalFeatures;
  deviceInfo.optional.featureCount = GPU_ARRAY_LEN(optionalFeatures);
  if (GPUCreateDevice(sample->adapter,
                      &deviceInfo,
                      &sample->device) != GPU_OK ||
      !sample->device) {
    goto fail;
  }

  sample->queue = GPUGetQueue(sample->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!sample->queue) {
    goto fail;
  }

  runtimeInfo.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeInfo.chain.structSize = sizeof(runtimeInfo);
  runtimeInfo.validationMode   = GPU_VALIDATION_FULL;
  runtimeInfo.enableStats      = true;
  if (GPUConfigureRuntime(sample->device, &runtimeInfo) != GPU_OK) {
    goto fail;
  }

  sample->surface = GPUCreateSurfaceFromNative(sample->instance,
                                               sample->adapter,
                                               nativeView,
                                               GPU_SURFACE_APPLE_NSVIEW,
                                               contentScale);
  if (!sample->surface ||
      !resize_surface(sample, NULL, &sample->width, &sample->height)) {
    goto fail;
  }

  sample->swapchain = GPUCreateSwapchainDefault(sample->device,
                                                sample->surface,
                                                sample->surfaceWidth,
                                                sample->surfaceHeight);
  if (!sample->swapchain) {
    goto fail;
  }

  activeSample = sample;
  if (start() != 0 || sample->failed) {
    goto fail;
  }
  return sample;

fail:
  if (activeSample == sample) {
    activeSample = NULL;
  }
  sample->failed = true;
  if (sample->status[0] == '\0') {
    snprintf(sample->status,
             sizeof(sample->status),
             "GPU: failed to start %s",
             name);
  }
  return sample;
}

bool
GPUSampleAppleRender(GPUAppleSample *sample) {
  if (!sample || sample != activeSample || sample->failed ||
      sample->canceled) {
    return false;
  }

  if (sample->render) {
    @autoreleasepool {
      sample->render(sample->renderData);
    }
    if (!sample->failed && !sample->canceled) {
      sample->renderedFrameCount++;
    }
  }
  return !sample->failed && !sample->canceled;
}

bool
GPUSampleAppleHasRenderedFrame(const GPUAppleSample *sample) {
  return sample && sample->renderedFrameCount > 0u;
}

void
GPUSampleAppleStop(GPUAppleSample *sample) {
  if (!sample) {
    return;
  }

  sample->canceled = true;
  if (activeSample == sample) {
    activeSample = NULL;
  }
}

const char*
GPUSampleAppleStatus(const GPUAppleSample *sample) {
  return sample ? sample->status : "GPU: sample runtime unavailable";
}

bool
GPUSampleAppleFailed(const GPUAppleSample *sample) {
  return !sample || sample->failed;
}

void
set_status(const char *message, int failed) {
  if (!activeSample) {
    return;
  }

  snprintf(activeSample->status,
           sizeof(activeSample->status),
           "%s",
           message ? message : "GPU sample status");
  activeSample->failed |= failed != 0;
  fprintf(failed ? stderr : stdout, "%s\n", activeSample->status);
}

void
set_status_notice(const char *message) {
  set_status(message, 0);
}

int
read_file(const char *path, void **outData, uint64_t *outSize) {
  NSData *data;
  void   *bytes;

  if (!path || !outData || !outSize) {
    return 0;
  }

  *outData = NULL;
  *outSize = 0u;
  data     = [NSData dataWithContentsOfFile:asset_path(path)];
  bytes    = data.length > 0u ? malloc(data.length) : NULL;
  if (!bytes) {
    return 0;
  }

  memcpy(bytes, data.bytes, data.length);
  *outData = bytes;
  *outSize = (uint64_t)data.length;
  return 1;
}

GPUResult
request_webgpu_device(GPUInstance        *instance,
                      WebGPURequest      *request,
                      WebGPUReadyCallback callback,
                      void               *userData) {
  return request_webgpu_device_features(instance,
                                        request,
                                        callback,
                                        userData,
                                        NULL,
                                        0u);
}

GPUResult
request_webgpu_device_features(GPUInstance        *instance,
                               WebGPURequest      *request,
                               WebGPUReadyCallback callback,
                               void               *userData,
                               const GPUFeature   *optionalFeatures,
                               uint32_t            optionalFeatureCount) {
  (void)optionalFeatures;
  (void)optionalFeatureCount;
  if (!activeSample || !instance || !request || !callback) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  request->callback  = callback;
  request->adapter   = activeSample->adapter;
  request->userData  = userData;
  request->result    = GPU_OK;
  request->completed = true;
  callback(GPU_OK,
           activeSample->adapter,
           activeSample->device,
           userData);
  return activeSample->failed ? GPU_ERROR_BACKEND_FAILURE : GPU_OK;
}

int
resize_webgpu_canvas(GPUSwapchain *swapchain,
                     uint32_t     *width,
                     uint32_t     *height) {
  return resize_surface(activeSample, swapchain, width, height);
}

void
gpu_apple_set_main_loop(void (*callback)(void *),
                        void  *userData,
                        int    fps,
                        bool   simulateInfiniteLoop) {
  (void)fps;
  (void)simulateInfiniteLoop;
  if (!activeSample) {
    return;
  }

  activeSample->render     = callback;
  activeSample->renderData = userData;
}

void
gpu_apple_cancel_main_loop(void) {
  if (activeSample) {
    activeSample->canceled = true;
  }
}

double
gpu_apple_get_now(void) {
  return CACurrentMediaTime() * 1000.0;
}

void*
gpu_apple_load_image(const char *path, int *width, int *height) {
  CGImageSourceRef source;
  uint8_t         *pixels;
  uint32_t         imageWidth, imageHeight;

  if (!path || !width || !height) {
    return NULL;
  }

  source = CGImageSourceCreateWithURL(
    (__bridge CFURLRef)[NSURL fileURLWithPath:asset_path(path)],
    NULL
  );
  imageWidth  = 0u;
  imageHeight = 0u;
  pixels      = decode_image(source, &imageWidth, &imageHeight);
  if (source) {
    CFRelease(source);
  }
  if (pixels) {
    *width  = (int)imageWidth;
    *height = (int)imageHeight;
  }
  return pixels;
}

GPUResult
gpu_apple_sample_create_instance(const GPUInstanceCreateInfo *info,
                                 GPUInstance                **outInstance) {
  (void)info;
  if (!activeSample || !outInstance) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outInstance = activeSample->instance;
  return GPU_OK;
}

GPUSurface*
gpu_apple_sample_create_surface(GPUInstance   *instance,
                                GPUAdapter    *adapter,
                                void          *nativeHandle,
                                GPUSurfaceType nativeType,
                                float          contentScale) {
  (void)instance;
  (void)adapter;
  (void)nativeHandle;
  (void)nativeType;
  (void)contentScale;
  return activeSample ? activeSample->surface : NULL;
}

GPUSwapchain*
gpu_apple_sample_create_swapchain(GPUDevice  *device,
                                  GPUSurface *surface,
                                  uint32_t    width,
                                  uint32_t    height) {
  (void)device;
  (void)surface;
  (void)width;
  (void)height;
  return activeSample ? activeSample->swapchain : NULL;
}

int
sample_fetch_url(const char         *url,
                 SampleFetchCallback callback,
                 void               *userData) {
  NSURL *nativeURL;

  if (!activeSample || !url || !callback) {
    return 0;
  }

  nativeURL = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
  if (!nativeURL) {
    return 0;
  }

  [[[NSURLSession sharedSession]
    dataTaskWithURL:nativeURL
  completionHandler:^(NSData *data,
                      NSURLResponse *response,
                      NSError *error) {
    void *bytes;

    (void)response;
    bytes = !error && data.length > 0u ? malloc(data.length) : NULL;
    if (bytes) {
      memcpy(bytes, data.bytes, data.length);
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      callback(bytes,
               bytes ? (uint64_t)data.length : 0u,
               bytes ? NULL : "sample: Apple download failed",
               userData);
    });
  }] resume];
  return 1;
}

int
sample_decode_image(const void         *bytes,
                    uint64_t            byteCount,
                    SampleImageCallback callback,
                    void               *userData) {
  CGDataProviderRef provider;
  CGImageSourceRef  source;
  uint8_t          *pixels;
  uint32_t          width, height;

  if (!bytes || byteCount == 0u || byteCount > SIZE_MAX || !callback) {
    return 0;
  }

  provider = CGDataProviderCreateWithData(NULL,
                                          bytes,
                                          (size_t)byteCount,
                                          NULL);
  source   = provider
               ? CGImageSourceCreateWithDataProvider(provider, NULL)
               : NULL;
  width    = 0u;
  height   = 0u;
  pixels   = decode_image(source, &width, &height);
  callback(pixels,
           width,
           height,
           pixels ? NULL : "sample: Apple image decode failed",
           userData);
  if (source) {
    CFRelease(source);
  }
  if (provider) {
    CGDataProviderRelease(provider);
  }
  return pixels != NULL;
}

int
sample_temporary_path(const char *name, char *path, size_t capacity) {
  NSString *temporary;
  NSString *fullPath;
  int       length;

  if (!name || !path || capacity == 0u) {
    return 0;
  }

  temporary = NSTemporaryDirectory();
  fullPath  = [temporary
    stringByAppendingPathComponent:[NSString stringWithUTF8String:name]];
  length = snprintf(path, capacity, "%s", fullPath.fileSystemRepresentation);
  return length > 0 && (size_t)length < capacity;
}
