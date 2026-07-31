#define _POSIX_C_SOURCE 200809L
#define GPU_SAMPLE_PLATFORM_IMPLEMENTATION

#include "linux.h"
#include "asset_io.h"

#include <curl/curl.h>
#include <jpeglib.h>
#include <png.h>

#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef void
(*GPULinuxRenderCallback)(void *userData);

typedef struct GPULinuxDownload {
  uint8_t *bytes;
  size_t   size;
  size_t   capacity;
} GPULinuxDownload;

typedef struct GPULinuxJPEGError {
  struct jpeg_error_mgr base;
  jmp_buf               jump;
} GPULinuxJPEGError;

struct GPULinuxSample {
  GPULinuxWindow         *window;
  GPUInstance            *instance;
  GPUAdapter             *adapter;
  GPUDevice              *device;
  GPUQueue               *queue;
  GPUSurface             *surface;
  GPUSwapchain           *swapchain;
  GPULinuxRenderCallback  render;
  void                   *renderData;
  const char             *name;
  char                    status[256];
  uint32_t                width;
  uint32_t                height;
  bool                    canceled;
  bool                    failed;
};

static GPULinuxSample *activeSample;
static bool            curlInitialized;

static bool
make_directory(const char *path) {
  char   directory[PATH_MAX];
  char  *cursor;
  size_t length;

  if (!path || !(length = strlen(path)) || length >= sizeof(directory)) {
    return false;
  }
  memcpy(directory, path, length + 1u);

  for (cursor = directory + 1; *cursor; cursor++) {
    if (*cursor != '/') {
      continue;
    }
    *cursor = '\0';
    if (mkdir(directory, 0755) != 0 && errno != EEXIST) {
      return false;
    }
    *cursor = '/';
  }
  return mkdir(directory, 0755) == 0 || errno == EEXIST;
}

static bool
multiply_size(size_t left, size_t right, size_t *result) {
  if (!result || (left != 0u && right > SIZE_MAX / left)) {
    return false;
  }
  *result = left * right;
  return true;
}

static const char*
asset_name(const char *path) {
  return path && path[0] == '/' ? path + 1 : path;
}

static bool
asset_path(const char *path, char out[PATH_MAX]) {
  char   executable[PATH_MAX];
  char  *slash;
  ssize_t length;

  if (!path || !out) {
    return false;
  }
  length = readlink("/proc/self/exe", executable, sizeof(executable) - 1u);
  if (length <= 0 || (size_t)length >= sizeof(executable)) {
    return false;
  }
  executable[length] = '\0';
  slash = strrchr(executable, '/');
  if (!slash) {
    return false;
  }
  *slash = '\0';
  return snprintf(out,
                  PATH_MAX,
                  "%s/%s",
                  executable,
                  asset_name(path)) < PATH_MAX;
}

static uint8_t*
decode_png(const void *bytes,
           size_t      byteCount,
           uint32_t   *width,
           uint32_t   *height) {
  png_image image;
  uint8_t  *pixels;
  size_t    size;

  if (!bytes || byteCount == 0u || !width || !height) {
    return NULL;
  }

  memset(&image, 0, sizeof(image));
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&image, bytes, byteCount)) {
    return NULL;
  }
  image.format = PNG_FORMAT_RGBA;
  size         = PNG_IMAGE_SIZE(image);
  pixels       = size > 0u ? malloc(size) : NULL;
  if (!pixels ||
      !png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
    free(pixels);
    png_image_free(&image);
    return NULL;
  }

  *width  = image.width;
  *height = image.height;
  png_image_free(&image);
  return pixels;
}

static void
jpeg_error_exit(j_common_ptr image) {
  GPULinuxJPEGError *error;

  error = (GPULinuxJPEGError *)image->err;
  longjmp(error->jump, 1);
}

static uint8_t*
decode_jpeg(const void *bytes,
            size_t      byteCount,
            uint32_t   *width,
            uint32_t   *height) {
  struct jpeg_decompress_struct image;
  GPULinuxJPEGError             error;
  uint8_t                      *pixels;
  uint8_t                      *rgba;
  size_t                        pixelCount, rgbSize, rowStride;
  uint32_t                      imageWidth, imageHeight;

  if (!bytes || byteCount == 0u || !width || !height) {
    return NULL;
  }

  memset(&image, 0, sizeof(image));
  image.err             = jpeg_std_error(&error.base);
  error.base.error_exit = jpeg_error_exit;
  pixels                = NULL;
  if (setjmp(error.jump)) {
    jpeg_destroy_decompress(&image);
    free(pixels);
    return NULL;
  }

  jpeg_create_decompress(&image);
  jpeg_mem_src(&image, bytes, byteCount);
  if (jpeg_read_header(&image, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&image);
    return NULL;
  }
  image.out_color_space = JCS_RGB;
  jpeg_start_decompress(&image);
  if (image.output_width == 0u || image.output_height == 0u ||
      image.output_width > UINT32_MAX || image.output_height > UINT32_MAX) {
    jpeg_destroy_decompress(&image);
    return NULL;
  }

  imageWidth  = (uint32_t)image.output_width;
  imageHeight = (uint32_t)image.output_height;
  if (!multiply_size((size_t)imageWidth, 3u, &rowStride) ||
      !multiply_size(rowStride, (size_t)imageHeight, &rgbSize)) {
    jpeg_destroy_decompress(&image);
    return NULL;
  }
  pixels  = malloc(rgbSize);
  if (!pixels) {
    jpeg_destroy_decompress(&image);
    return NULL;
  }

  while (image.output_scanline < image.output_height) {
    JSAMPROW row;

    row = pixels + (size_t)image.output_scanline * rowStride;
    if (jpeg_read_scanlines(&image, &row, 1u) != 1u) {
      jpeg_destroy_decompress(&image);
      free(pixels);
      return NULL;
    }
  }
  jpeg_finish_decompress(&image);
  jpeg_destroy_decompress(&image);

  if (!multiply_size((size_t)imageWidth,
                     (size_t)imageHeight,
                     &pixelCount) ||
      !multiply_size(pixelCount, 4u, &rgbSize)) {
    free(pixels);
    return NULL;
  }
  rgba = realloc(pixels, rgbSize);
  if (!rgba) {
    free(pixels);
    return NULL;
  }
  for (size_t i = pixelCount; i > 0u; i--) {
    size_t source, target;

    source           = (i - 1u) * 3u;
    target           = (i - 1u) * 4u;
    rgba[target]     = rgba[source];
    rgba[target + 1] = rgba[source + 1u];
    rgba[target + 2] = rgba[source + 2u];
    rgba[target + 3] = UINT8_MAX;
  }

  *width  = imageWidth;
  *height = imageHeight;
  return rgba;
}

static uint8_t*
decode_image(const void *bytes,
             size_t      byteCount,
             uint32_t   *width,
             uint32_t   *height) {
  static const uint8_t pngMagic[] = {
    0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au
  };
  const uint8_t *data;

  if (!bytes || byteCount < 2u || !width || !height) {
    return NULL;
  }
  data = bytes;
  if (byteCount >= sizeof(pngMagic) &&
      memcmp(data, pngMagic, sizeof(pngMagic)) == 0) {
    return decode_png(bytes, byteCount, width, height);
  }
  if (data[0] == 0xffu && data[1] == 0xd8u) {
    return decode_jpeg(bytes, byteCount, width, height);
  }
  return NULL;
}

static bool
resize_surface(GPULinuxSample *sample,
               GPUSwapchain   *swapchain,
               uint32_t       *width,
               uint32_t       *height) {
  uint32_t nextHeight, nextWidth;

  if (!sample || !sample->window || !width || !height) {
    return false;
  }
  nextWidth  = sample->window->width;
  nextHeight = sample->window->height;
  if (nextWidth == 0u || nextHeight == 0u) {
    return false;
  }
  if ((nextWidth != sample->width || nextHeight != sample->height) &&
      swapchain &&
      GPUResizeSwapchain(swapchain, nextWidth, nextHeight) != GPU_OK) {
    return false;
  }

  sample->width  = nextWidth;
  sample->height = nextHeight;
  *width         = nextWidth;
  *height        = nextHeight;
  return true;
}

static GPUSurface*
create_surface(GPULinuxSample *sample) {
  GPUSurfaceCreateInfo surfaceInfo = {0};
  GPUSurface          *surface;

  surfaceInfo.chain.sType      = GPU_STRUCTURE_TYPE_SURFACE_CREATE_INFO;
  surfaceInfo.chain.structSize = sizeof(surfaceInfo);
  surfaceInfo.label            = sample->name;
  surface                      = NULL;

  if (sample->window->system == GPU_LINUX_WINDOW_XLIB) {
    GPUSurfaceXlibCreateInfo xlibInfo = {0};

    xlibInfo.chain.sType      = GPU_STRUCTURE_TYPE_SURFACE_XLIB_CREATE_INFO;
    xlibInfo.chain.structSize = sizeof(xlibInfo);
    xlibInfo.adapter          = sample->adapter;
    xlibInfo.display          = sample->window->display;
    xlibInfo.window           = sample->window->window;
    xlibInfo.scale            = sample->window->scale;
    surfaceInfo.chain.pNext   = &xlibInfo;
    if (GPUCreateSurface(sample->instance,
                         &surfaceInfo,
                         &surface) != GPU_OK) {
      return NULL;
    }
    return surface;
  }

  if (sample->window->system == GPU_LINUX_WINDOW_WAYLAND) {
    GPUSurfaceWaylandCreateInfo waylandInfo = {0};

    waylandInfo.chain.sType =
      GPU_STRUCTURE_TYPE_SURFACE_WAYLAND_CREATE_INFO;
    waylandInfo.chain.structSize = sizeof(waylandInfo);
    waylandInfo.adapter          = sample->adapter;
    waylandInfo.display          = sample->window->display;
    waylandInfo.surface          = sample->window->surface;
    waylandInfo.scale            = sample->window->scale;
    surfaceInfo.chain.pNext      = &waylandInfo;
    if (GPUCreateSurface(sample->instance,
                         &surfaceInfo,
                         &surface) != GPU_OK) {
      return NULL;
    }
    return surface;
  }

  return NULL;
}

GPULinuxSample*
GPUSampleLinuxCreate(GPULinuxWindow      *window,
                     const char          *name,
                     GPULinuxSampleStart  start) {
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
  GPUDeviceCreateInfo   deviceInfo = {0};
  GPURuntimeConfig      runtimeInfo = {0};
  GPULinuxSample       *sample;

  if (!window || !name || !start || activeSample ||
      !window->display || window->width == 0u || window->height == 0u ||
      !(window->scale > 0.0f)) {
    return NULL;
  }

  sample = calloc(1, sizeof(*sample));
  if (!sample) {
    return NULL;
  }
  sample->window = window;
  sample->name   = name;
  snprintf(sample->status, sizeof(sample->status), "GPU: starting %s", name);

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.label            = name;
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
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

  sample->surface = create_surface(sample);
  if (!sample->surface ||
      !resize_surface(sample, NULL, &sample->width, &sample->height)) {
    goto fail;
  }
  sample->swapchain = GPUCreateSwapchainDefault(sample->device,
                                                sample->surface,
                                                sample->width,
                                                sample->height);
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
  snprintf(sample->status,
           sizeof(sample->status),
           "GPU: failed to start %s",
           name);
  return sample;
}

bool
GPUSampleLinuxRender(GPULinuxSample *sample) {
  if (!sample || sample != activeSample || sample->failed ||
      sample->canceled) {
    return false;
  }
  if (sample->render) {
    sample->render(sample->renderData);
  }
  return !sample->failed && !sample->canceled;
}

void
GPUSampleLinuxStop(GPULinuxSample *sample) {
  if (!sample) {
    return;
  }
  sample->canceled = true;
  if (activeSample == sample) {
    activeSample = NULL;
  }
}

const char*
GPUSampleLinuxStatus(const GPULinuxSample *sample) {
  return sample ? sample->status : "GPU: sample runtime unavailable";
}

bool
GPUSampleLinuxFailed(const GPULinuxSample *sample) {
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
  char    resolved[PATH_MAX];
  FILE   *file;
  void   *data;
  long    length;

  if (!path || !outData || !outSize || !asset_path(path, resolved)) {
    return 0;
  }
  *outData = NULL;
  *outSize = 0u;
  file     = fopen(resolved, "rb");
  if (!file) {
    return 0;
  }
  if (fseek(file, 0, SEEK_END) != 0 ||
      (length = ftell(file)) <= 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return 0;
  }
  data = malloc((size_t)length);
  if (!data || fread(data, (size_t)length, 1u, file) != 1u) {
    free(data);
    fclose(file);
    return 0;
  }
  fclose(file);
  *outData = data;
  *outSize = (uint64_t)length;
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
gpu_linux_set_main_loop(void (*callback)(void *),
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
gpu_linux_cancel_main_loop(void) {
  if (activeSample) {
    activeSample->canceled = true;
  }
}

double
gpu_linux_get_now(void) {
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

void*
gpu_linux_load_image(const char *path, int *width, int *height) {
  char     resolved[PATH_MAX];
  uint8_t *bytes;
  uint8_t *pixels;
  FILE    *file;
  long     length;
  uint32_t imageWidth, imageHeight;

  if (!path || !width || !height || !asset_path(path, resolved)) {
    return NULL;
  }
  file = fopen(resolved, "rb");
  if (!file) {
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0 ||
      (length = ftell(file)) <= 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  bytes = malloc((size_t)length);
  if (!bytes || fread(bytes, (size_t)length, 1u, file) != 1u) {
    free(bytes);
    fclose(file);
    return NULL;
  }
  fclose(file);
  imageWidth  = 0u;
  imageHeight = 0u;
  pixels      = decode_image(bytes, (size_t)length, &imageWidth, &imageHeight);
  free(bytes);
  if (!pixels) {
    return NULL;
  }
  *width  = (int)imageWidth;
  *height = (int)imageHeight;
  return pixels;
}

GPUResult
gpu_linux_sample_create_instance(const GPUInstanceCreateInfo *info,
                                 GPUInstance                **outInstance) {
  (void)info;
  if (!activeSample || !outInstance) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outInstance = activeSample->instance;
  return GPU_OK;
}

GPUSurface*
gpu_linux_sample_create_surface(GPUInstance   *instance,
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
gpu_linux_sample_create_swapchain(GPUDevice  *device,
                                  GPUSurface *surface,
                                  uint32_t    width,
                                  uint32_t    height) {
  (void)device;
  (void)surface;
  (void)width;
  (void)height;
  return activeSample ? activeSample->swapchain : NULL;
}

static size_t
download_write(void *contents, size_t size, size_t count, void *userData) {
  GPULinuxDownload *download;
  uint8_t          *bytes;
  size_t            appendSize, required, capacity;

  download   = userData;
  appendSize = size * count;
  if (!download || appendSize == 0u ||
      download->size > SIZE_MAX - appendSize) {
    return 0u;
  }
  required = download->size + appendSize;
  if (required > download->capacity) {
    capacity = download->capacity ? download->capacity : 64u * 1024u;
    while (capacity < required) {
      if (capacity > SIZE_MAX / 2u) {
        return 0u;
      }
      capacity *= 2u;
    }
    bytes = realloc(download->bytes, capacity);
    if (!bytes) {
      return 0u;
    }
    download->bytes    = bytes;
    download->capacity = capacity;
  }
  memcpy(download->bytes + download->size, contents, appendSize);
  download->size += appendSize;
  return appendSize;
}

int
sample_fetch_url(const char         *url,
                 SampleFetchCallback callback,
                 void               *userData) {
  GPULinuxDownload download = {0};
  CURL            *curl;
  CURLcode         result;

  if (!url || !callback) {
    return 0;
  }
  if (!curlInitialized) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
      callback(NULL, 0u, "sample: libcurl initialization failed", userData);
      return 0;
    }
    curlInitialized = true;
  }
  curl = curl_easy_init();
  if (!curl) {
    callback(NULL, 0u, "sample: libcurl handle creation failed", userData);
    return 0;
  }
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, download_write);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "gpu-samples/1");
  result = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  callback(result == CURLE_OK ? download.bytes : NULL,
           result == CURLE_OK ? (uint64_t)download.size : 0u,
           result == CURLE_OK ? NULL : curl_easy_strerror(result),
           userData);
  if (result != CURLE_OK) {
    free(download.bytes);
  }
  return result == CURLE_OK;
}

int
sample_decode_image(const void         *bytes,
                    uint64_t            byteCount,
                    SampleImageCallback callback,
                    void               *userData) {
  uint8_t *pixels;
  uint32_t width, height;

  if (!bytes || byteCount == 0u || byteCount > SIZE_MAX || !callback) {
    return 0;
  }
  width  = 0u;
  height = 0u;
  pixels = decode_image(bytes, (size_t)byteCount, &width, &height);
  callback(pixels,
           width,
           height,
           pixels ? NULL : "sample: Linux image decode failed",
           userData);
  return pixels != NULL;
}

int
sample_temporary_path(const char *name, char *path, size_t capacity) {
  const char *base;
  char        directory[PATH_MAX];
  int         length;

  if (!name || !path || capacity == 0u) {
    return 0;
  }
  base = getenv("XDG_CACHE_HOME");
  if (!base || !base[0]) {
    base = getenv("HOME");
    if (!base || !base[0]) {
      base = "/tmp";
    }
    length = snprintf(directory, sizeof(directory), "%s/.cache/gpu-samples",
                      base);
  } else {
    length = snprintf(directory, sizeof(directory), "%s/gpu-samples", base);
  }
  if (length <= 0 || (size_t)length >= sizeof(directory)) {
    return 0;
  }
  if (!make_directory(directory)) {
    return 0;
  }
  length = snprintf(path, capacity, "%s/%s", directory, name);
  return length > 0 && (size_t)length < capacity;
}
