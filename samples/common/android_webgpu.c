#include "sample_platform.h"
#include "asset_io.h"

#include <android/asset_manager.h>
#include <android/bitmap.h>
#include <android/log.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPU_ANDROID_WEB_TAG "gpu-web-sample"

typedef struct GPUAndroidWebRuntime {
  GPUAndroidSample *sample;
  void (*render)(void *);
  void             *renderData;
  bool              canceled;
  bool              failed;
} GPUAndroidWebRuntime;

typedef struct GPUAndroidFetchRequest {
  struct GPUAndroidFetchRequest *next;
  SampleFetchCallback            callback;
  void                          *userData;
  void                          *bytes;
  char                          *error;
  uint64_t                       byteCount;
  uint64_t                       generation;
} GPUAndroidFetchRequest;

static GPUAndroidFetchRequest *fetchHead;
static GPUAndroidFetchRequest *fetchTail;
static GPUAndroidWebRuntime     runtime;
static pthread_mutex_t          fetchMutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t                 fetchGeneration = 1u;

static void*
decode_image_bytes(const void *data,
                   uint64_t    size,
                   uint32_t   *width,
                   uint32_t   *height);

static const char*
asset_name(const char *path) {
  return path && path[0] == '/' ? path + 1 : path;
}

static bool
bridge_create(GPUAndroidSample *sample, void *userData) {
  const GPUAndroidWebConfig *config;

  (void)userData;
  memset(&runtime, 0, sizeof(runtime));
  runtime.sample = sample;
  config = sample && sample->definition
             ? sample->definition->config
             : NULL;
  if (!config || !config->start || config->start() != 0 || runtime.failed) {
    return false;
  }
  return true;
}

static void
drain_fetches(void) {
  GPUAndroidFetchRequest *request;

  for (;;) {
    pthread_mutex_lock(&fetchMutex);
    request = fetchHead;
    if (request) {
      fetchHead = request->next;
      if (!fetchHead) {
        fetchTail = NULL;
      }
    }
    pthread_mutex_unlock(&fetchMutex);

    if (!request) {
      return;
    }
    request->callback(request->bytes,
                      request->byteCount,
                      request->error,
                      request->userData);
    free(request->error);
    free(request);
  }
}

static bool
bridge_render(GPUAndroidSample *sample, void *userData) {
  (void)sample;
  (void)userData;
  drain_fetches();
  if (runtime.failed || runtime.canceled) {
    return false;
  }
  if (runtime.render) {
    runtime.render(runtime.renderData);
  }
  return !runtime.failed && !runtime.canceled;
}

static void
cancel_fetches(void) {
  GPUAndroidFetchRequest *request;

  pthread_mutex_lock(&fetchMutex);
  fetchGeneration++;
  request   = fetchHead;
  fetchHead = NULL;
  fetchTail = NULL;
  pthread_mutex_unlock(&fetchMutex);

  while (request) {
    GPUAndroidFetchRequest *next;

    next = request->next;
    free(request->bytes);
    free(request->error);
    free(request);
    request = next;
  }
}

static void
bridge_destroy(GPUAndroidSample *sample, void *userData) {
  (void)sample;
  (void)userData;
  cancel_fetches();
  memset(&runtime, 0, sizeof(runtime));
}

static const GPUAndroidSampleCallbacks bridgeCallbacks = {
  .create  = bridge_create,
  .render  = bridge_render,
  .destroy = bridge_destroy
};

const GPUAndroidSampleCallbacks*
GPUSampleAndroidWebCallbacks(void) {
  return &bridgeCallbacks;
}

void
set_status(const char *message, int failed) {
  __android_log_print(failed ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO,
                      GPU_ANDROID_WEB_TAG,
                      "%s",
                      message ? message : "GPU sample status");
  if (failed) {
    runtime.failed = true;
  }
}

void
set_status_notice(const char *message) {
  __android_log_print(ANDROID_LOG_WARN,
                      GPU_ANDROID_WEB_TAG,
                      "%s",
                      message ? message : "GPU sample notice");
}

int
read_file(const char *path, void **outData, uint64_t *outSize) {
  AAsset *asset;
  void   *data;
  off_t   size;

  if (!runtime.sample || !path || !outData || !outSize) {
    return 0;
  }

  *outData = NULL;
  *outSize = 0u;
  asset = AAssetManager_open(
    runtime.sample->app->activity->assetManager,
    asset_name(path),
    AASSET_MODE_BUFFER
  );
  if (!asset) {
    return 0;
  }

  size = AAsset_getLength(asset);
  data = size > 0 ? malloc((size_t)size) : NULL;
  if (!data ||
      AAsset_read(asset, data, (size_t)size) != size) {
    free(data);
    AAsset_close(asset);
    return 0;
  }

  AAsset_close(asset);
  *outData = data;
  *outSize = (uint64_t)size;
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
  if (!instance || !request || !callback || !runtime.sample) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  request->callback  = callback;
  request->adapter   = runtime.sample->adapter;
  request->userData  = userData;
  request->result    = GPU_OK;
  request->completed = true;
  callback(GPU_OK,
           runtime.sample->adapter,
           runtime.sample->device,
           userData);
  return runtime.failed ? GPU_ERROR_BACKEND_FAILURE : GPU_OK;
}

int
resize_webgpu_canvas(GPUSwapchain *swapchain,
                     uint32_t     *width,
                     uint32_t     *height) {
  (void)swapchain;
  if (!runtime.sample || !width || !height ||
      runtime.sample->width == 0u || runtime.sample->height == 0u) {
    return 0;
  }

  *width  = runtime.sample->width;
  *height = runtime.sample->height;
  return 1;
}

void
gpu_android_set_main_loop(void (*callback)(void *),
                          void  *userData,
                          int    fps,
                          bool   simulateInfiniteLoop) {
  (void)fps;
  (void)simulateInfiniteLoop;
  runtime.render     = callback;
  runtime.renderData = userData;
}

void
gpu_android_cancel_main_loop(void) {
  runtime.canceled = true;
}

double
gpu_android_get_now(void) {
  return GPUSampleAndroidTime() * 1000.0;
}

GPUResult
gpu_android_sample_create_instance(const GPUInstanceCreateInfo *info,
                                   GPUInstance                **outInstance) {
  (void)info;
  if (!runtime.sample || !outInstance) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outInstance = runtime.sample->instance;
  return GPU_OK;
}

GPUSurface*
gpu_android_sample_create_surface(GPUInstance          *instance,
                                  GPUAdapter           *adapter,
                                  void                 *nativeHandle,
                                  GPUSurfaceType        nativeType,
                                  float                 contentScale) {
  (void)instance;
  (void)adapter;
  (void)nativeHandle;
  (void)nativeType;
  (void)contentScale;
  return runtime.sample ? runtime.sample->surface : NULL;
}

GPUSwapchain*
gpu_android_sample_create_swapchain(GPUDevice  *device,
                                    GPUSurface *surface,
                                    uint32_t    width,
                                    uint32_t    height) {
  (void)device;
  (void)surface;
  (void)width;
  (void)height;
  return runtime.sample ? runtime.sample->swapchain : NULL;
}

void*
gpu_android_load_image(const char *path, int *width, int *height) {
  void     *data;
  void     *pixels;
  uint64_t  size;
  uint32_t  imageWidth, imageHeight;

  if (!runtime.sample || !width || !height ||
      !read_file(path, &data, &size)) {
    return NULL;
  }

  imageWidth  = 0u;
  imageHeight = 0u;
  pixels = decode_image_bytes(data, size, &imageWidth, &imageHeight);
  free(data);
  if (pixels) {
    *width  = (int)imageWidth;
    *height = (int)imageHeight;
  }
  return pixels;
}

static void*
decode_image_bytes(const void *data,
                   uint64_t    size,
                   uint32_t   *width,
                   uint32_t   *height) {
  ANativeActivity  *activity;
  JavaVM           *vm;
  JNIEnv           *env;
  jclass            bitmapFactoryClass;
  jmethodID         decodeByteArray;
  jbyteArray        bytes;
  jobject           bitmap;
  AndroidBitmapInfo info;
  void             *bitmapPixels;
  void             *pixels;
  jint              envStatus;
  bool              attached;

  if (!runtime.sample || !data || size == 0u || size > INT32_MAX ||
      !width || !height) {
    return NULL;
  }

  activity = runtime.sample->app->activity;
  vm       = activity->vm;
  env      = NULL;
  attached = false;
  envStatus = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6);
  if (envStatus == JNI_EDETACHED) {
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
      return NULL;
    }
    attached = true;
  } else if (envStatus != JNI_OK || !env) {
    return NULL;
  }

  bitmapFactoryClass = (*env)->FindClass(env, "android/graphics/BitmapFactory");
  decodeByteArray = bitmapFactoryClass
                      ? (*env)->GetStaticMethodID(
                          env,
                          bitmapFactoryClass,
                          "decodeByteArray",
                          "([BII)Landroid/graphics/Bitmap;"
                        )
                      : NULL;
  bytes = decodeByteArray
            ? (*env)->NewByteArray(env, (jsize)size)
            : NULL;
  if (bytes) {
    (*env)->SetByteArrayRegion(env,
                               bytes,
                               0,
                               (jsize)size,
                               data);
  }
  bitmap = bytes
             ? (*env)->CallStaticObjectMethod(env,
                                              bitmapFactoryClass,
                                              decodeByteArray,
                                              bytes,
                                              0,
                                              (jint)size)
             : NULL;

  pixels       = NULL;
  bitmapPixels = NULL;
  if (bitmap &&
      AndroidBitmap_getInfo(env, bitmap, &info) == ANDROID_BITMAP_RESULT_SUCCESS &&
      info.format == ANDROID_BITMAP_FORMAT_RGBA_8888 &&
      AndroidBitmap_lockPixels(env, bitmap, &bitmapPixels) ==
        ANDROID_BITMAP_RESULT_SUCCESS) {
    size_t rowBytes;

    rowBytes = (size_t)info.width * 4u;
    pixels   = malloc(rowBytes * info.height);
    if (pixels) {
      for (uint32_t y = 0u; y < info.height; y++) {
        memcpy((uint8_t *)pixels + rowBytes * y,
               (const uint8_t *)bitmapPixels + info.stride * y,
               rowBytes);
      }
      *width  = info.width;
      *height = info.height;
    }
    AndroidBitmap_unlockPixels(env, bitmap);
  }

  if (bitmap) {
    (*env)->DeleteLocalRef(env, bitmap);
  }
  if (bytes) {
    (*env)->DeleteLocalRef(env, bytes);
  }
  if (bitmapFactoryClass) {
    (*env)->DeleteLocalRef(env, bitmapFactoryClass);
  }
  if (attached) {
    (*vm)->DetachCurrentThread(vm);
  }
  return pixels;
}

int
sample_fetch_url(const char         *url,
                 SampleFetchCallback callback,
                 void               *userData) {
  GPUAndroidFetchRequest *request;
  ANativeActivity        *activity;
  JNIEnv                 *env;
  jclass                  activityClass;
  jmethodID               fetchUrl;
  jstring                 nativeUrl;

  if (!runtime.sample || !url || !callback) {
    return 0;
  }

  request = calloc(1, sizeof(*request));
  if (!request) {
    return 0;
  }
  pthread_mutex_lock(&fetchMutex);
  request->generation = fetchGeneration;
  pthread_mutex_unlock(&fetchMutex);
  request->callback = callback;
  request->userData = userData;

  activity      = runtime.sample->app->activity;
  env           = activity->env;
  activityClass = (*env)->GetObjectClass(env, activity->clazz);
  fetchUrl = activityClass
               ? (*env)->GetMethodID(env,
                                     activityClass,
                                     "fetchUrl",
                                     "(Ljava/lang/String;J)V")
               : NULL;
  nativeUrl = fetchUrl ? (*env)->NewStringUTF(env, url) : NULL;
  if (nativeUrl) {
    (*env)->CallVoidMethod(env,
                           activity->clazz,
                           fetchUrl,
                           nativeUrl,
                           (jlong)(uintptr_t)request);
  }

  if ((*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
    fetchUrl = NULL;
  }
  if (nativeUrl) {
    (*env)->DeleteLocalRef(env, nativeUrl);
  }
  if (activityClass) {
    (*env)->DeleteLocalRef(env, activityClass);
  }
  if (!fetchUrl) {
    free(request);
    return 0;
  }
  return 1;
}

JNIEXPORT void JNICALL
Java_com_recp_gpu_samples_SampleActivity_nativeFetchComplete(
  JNIEnv    *env,
  jclass     type,
  jlong      requestValue,
  jbyteArray data,
  jstring    message
) {
  GPUAndroidFetchRequest *request;
  const char             *error;
  jsize                   length;
  bool                    stale;

  (void)type;
  request = (GPUAndroidFetchRequest *)(uintptr_t)requestValue;
  if (!request) {
    return;
  }

  length = data ? (*env)->GetArrayLength(env, data) : 0;
  if (length > 0) {
    request->bytes = malloc((size_t)length);
    if (request->bytes) {
      (*env)->GetByteArrayRegion(env,
                                 data,
                                 0,
                                 length,
                                 request->bytes);
      if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        free(request->bytes);
        request->bytes = NULL;
      } else {
        request->byteCount = (uint64_t)length;
      }
    }
  }

  error = message ? (*env)->GetStringUTFChars(env, message, NULL) : NULL;
  if (error) {
    request->error = strdup(error);
    (*env)->ReleaseStringUTFChars(env, message, error);
  }
  if (!request->bytes && !request->error) {
    request->error = strdup("sample: Android download failed");
  }

  pthread_mutex_lock(&fetchMutex);
  stale = request->generation != fetchGeneration;
  if (!stale) {
    request->next = NULL;
    if (fetchTail) {
      fetchTail->next = request;
    } else {
      fetchHead = request;
    }
    fetchTail = request;
  }
  pthread_mutex_unlock(&fetchMutex);

  if (stale) {
    free(request->bytes);
    free(request->error);
    free(request);
  }
}

int
sample_decode_image(const void         *bytes,
                    uint64_t            byteCount,
                    SampleImageCallback callback,
                    void               *userData) {
  uint8_t *pixels;
  uint32_t width, height;

  if (!callback) {
    return 0;
  }

  width  = 0u;
  height = 0u;
  pixels = decode_image_bytes(bytes, byteCount, &width, &height);
  callback(pixels,
           width,
           height,
           pixels ? NULL : "sample: Android image decode failed",
           userData);
  return pixels != NULL;
}

int
sample_temporary_path(const char *name, char *path, size_t capacity) {
  ANativeActivity *activity;
  JNIEnv          *env;
  jclass           activityClass;
  jclass           fileClass;
  jmethodID        getCacheDir;
  jmethodID        getAbsolutePath;
  jobject          cacheDir;
  jstring          absolutePath;
  const char      *directory;
  int              length;

  if (!runtime.sample || !name || !path || capacity == 0u) {
    return 0;
  }

  activity       = runtime.sample->app->activity;
  env            = activity->env;
  activityClass  = (*env)->GetObjectClass(env, activity->clazz);
  getCacheDir    = activityClass
                     ? (*env)->GetMethodID(env,
                                           activityClass,
                                           "getCacheDir",
                                           "()Ljava/io/File;")
                     : NULL;
  cacheDir       = getCacheDir
                     ? (*env)->CallObjectMethod(env,
                                                activity->clazz,
                                                getCacheDir)
                     : NULL;
  fileClass      = cacheDir ? (*env)->GetObjectClass(env, cacheDir) : NULL;
  getAbsolutePath = fileClass
                      ? (*env)->GetMethodID(env,
                                            fileClass,
                                            "getAbsolutePath",
                                            "()Ljava/lang/String;")
                      : NULL;
  absolutePath = getAbsolutePath
                   ? (*env)->CallObjectMethod(env, cacheDir, getAbsolutePath)
                   : NULL;
  directory = absolutePath
                ? (*env)->GetStringUTFChars(env, absolutePath, NULL)
                : NULL;
  length = directory
             ? snprintf(path, capacity, "%s/%s", directory, name)
             : -1;
  if (directory) {
    (*env)->ReleaseStringUTFChars(env, absolutePath, directory);
  }
  if (absolutePath) {
    (*env)->DeleteLocalRef(env, absolutePath);
  }
  if (fileClass) {
    (*env)->DeleteLocalRef(env, fileClass);
  }
  if (cacheDir) {
    (*env)->DeleteLocalRef(env, cacheDir);
  }
  if (activityClass) {
    (*env)->DeleteLocalRef(env, activityClass);
  }
  return length > 0 && (size_t)length < capacity;
}
