#define GPU_SAMPLE_PLATFORM_IMPLEMENTATION
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
  GPUAndroidSample     *sample;
  GPUTextureView       *frameTargetView;
  GPURenderPassEncoder *fittedPass;
  void (*render)(void *);
  void                 *renderData;
  bool                  ready;
  bool                  canceled;
  bool                  failed;
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
static bool                     fetchNativeRegistered;

static void*
decode_image_bytes(const void *data,
                   uint64_t    size,
                   uint32_t   *width,
                   uint32_t   *height);

static void JNICALL
android_fetch_complete(JNIEnv    *env,
                       jclass     type,
                       jlong      requestValue,
                       jbyteArray data,
                       jstring    message);

static void
android_set_ready(void);

static bool
android_jni_attach(ANativeActivity *activity,
                   JNIEnv         **env,
                   bool            *attached) {
  JavaVM *vm;
  jint    status;

  if (!activity || !env || !attached) {
    return false;
  }

  vm        = activity->vm;
  *env      = NULL;
  *attached = false;
  status    = (*vm)->GetEnv(vm, (void **)env, JNI_VERSION_1_6);
  if (status == JNI_EDETACHED) {
    if ((*vm)->AttachCurrentThread(vm, env, NULL) != JNI_OK) {
      return false;
    }
    *attached = true;
  } else if (status != JNI_OK || !*env) {
    return false;
  }
  return true;
}

static void
android_jni_detach(ANativeActivity *activity, bool attached) {
  if (activity && attached) {
    (*activity->vm)->DetachCurrentThread(activity->vm);
  }
}

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
    runtime.frameTargetView = NULL;
    runtime.fittedPass      = NULL;
    runtime.render(runtime.renderData);
    if (!runtime.ready && !runtime.failed && !runtime.canceled) {
      android_set_ready();
      runtime.ready = true;
    }
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

static void
android_set_status(const char *message, bool failed) {
  ANativeActivity *activity;
  JNIEnv          *env;
  jclass           activityClass;
  jmethodID        setSampleStatus;
  jstring          nativeMessage;
  bool             attached;

  if (!runtime.sample) {
    return;
  }

  activity = runtime.sample->app->activity;
  if (!android_jni_attach(activity, &env, &attached)) {
    return;
  }

  activityClass = (*env)->GetObjectClass(env, activity->clazz);
  setSampleStatus = activityClass
                      ? (*env)->GetMethodID(env,
                                            activityClass,
                                            "setSampleStatus",
                                            "(Ljava/lang/String;Z)V")
                      : NULL;
  nativeMessage = setSampleStatus && message
                    ? (*env)->NewStringUTF(env, message)
                    : NULL;
  if (setSampleStatus) {
    (*env)->CallVoidMethod(env,
                           activity->clazz,
                           setSampleStatus,
                           nativeMessage,
                           failed ? JNI_TRUE : JNI_FALSE);
  }
  if ((*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
  }
  if (nativeMessage) {
    (*env)->DeleteLocalRef(env, nativeMessage);
  }
  if (activityClass) {
    (*env)->DeleteLocalRef(env, activityClass);
  }
  android_jni_detach(activity, attached);
}

static void
android_set_notice(const char *message) {
  ANativeActivity *activity;
  JNIEnv          *env;
  jclass           activityClass;
  jmethodID        setSampleNotice;
  jstring          nativeMessage;
  bool             attached;

  if (!runtime.sample) {
    return;
  }

  activity = runtime.sample->app->activity;
  if (!android_jni_attach(activity, &env, &attached)) {
    return;
  }

  activityClass = (*env)->GetObjectClass(env, activity->clazz);
  setSampleNotice = activityClass
                      ? (*env)->GetMethodID(env,
                                            activityClass,
                                            "setSampleNotice",
                                            "(Ljava/lang/String;)V")
                      : NULL;
  nativeMessage = setSampleNotice && message
                    ? (*env)->NewStringUTF(env, message)
                    : NULL;
  if (setSampleNotice) {
    (*env)->CallVoidMethod(env,
                           activity->clazz,
                           setSampleNotice,
                           nativeMessage);
  }
  if ((*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
  }
  if (nativeMessage) {
    (*env)->DeleteLocalRef(env, nativeMessage);
  }
  if (activityClass) {
    (*env)->DeleteLocalRef(env, activityClass);
  }
  android_jni_detach(activity, attached);
}

static void
android_set_ready(void) {
  ANativeActivity *activity;
  JNIEnv          *env;
  jclass           activityClass;
  jmethodID        setSampleReady;
  bool             attached;

  if (!runtime.sample) {
    return;
  }

  activity = runtime.sample->app->activity;
  if (!android_jni_attach(activity, &env, &attached)) {
    return;
  }

  activityClass = (*env)->GetObjectClass(env, activity->clazz);
  setSampleReady = activityClass
                     ? (*env)->GetMethodID(env,
                                           activityClass,
                                           "setSampleReady",
                                           "()V")
                     : NULL;
  if (setSampleReady) {
    (*env)->CallVoidMethod(env, activity->clazz, setSampleReady);
  }
  if ((*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
  }
  if (activityClass) {
    (*env)->DeleteLocalRef(env, activityClass);
  }
  android_jni_detach(activity, attached);
}

void
set_status(const char *message, int failed) {
  __android_log_print(failed ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO,
                      GPU_ANDROID_WEB_TAG,
                      "%s",
                      message ? message : "GPU sample status");
  android_set_status(message, failed != 0);
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
  android_set_notice(message);
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

static void
content_rect(float *x, float *y, float *width, float *height) {
  float surfaceWidth, surfaceHeight;
  float targetWidth, targetHeight;

  surfaceWidth  = runtime.sample ? (float)runtime.sample->width : 0.0f;
  surfaceHeight = runtime.sample ? (float)runtime.sample->height : 0.0f;
  targetWidth   = surfaceWidth;
  targetHeight  = targetWidth * 10.0f / 16.0f;
  if (targetHeight > surfaceHeight) {
    targetHeight = surfaceHeight;
    targetWidth  = targetHeight * 16.0f / 10.0f;
  }

  *x      = (surfaceWidth - targetWidth) * 0.5f;
  *y      = (surfaceHeight - targetHeight) * 0.5f;
  *width  = targetWidth;
  *height = targetHeight;
}

GPUTextureView*
gpu_android_frame_target_view(GPUFrame *frame) {
  runtime.frameTargetView = GPUFrameGetTargetView(frame);
  return runtime.frameTargetView;
}

GPURenderPassEncoder*
gpu_android_begin_render_pass(GPUCommandBuffer             *cmdb,
                              const GPURenderPassCreateInfo *info) {
  GPURenderPassEncoder *pass;
  GPUViewport           viewport = {0};
  GPUScissorRect        scissor  = {0};
  float                 x, y, width, height;
  bool                  targetsFrame;

  pass = GPUBeginRenderPass(cmdb, info);
  if (!pass || !info || !runtime.frameTargetView) {
    return pass;
  }

  targetsFrame = false;
  for (uint32_t i = 0u; i < info->colorAttachmentCount; i++) {
    const GPURenderPassColorAttachment *color;

    color = &info->pColorAttachments[i];
    targetsFrame |= color->view == runtime.frameTargetView ||
                    color->resolveView == runtime.frameTargetView;
  }
  if (!targetsFrame) {
    return pass;
  }

  content_rect(&x, &y, &width, &height);
  viewport.x        = x;
  viewport.y        = y;
  viewport.width    = width;
  viewport.height   = height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  scissor.x         = (int32_t)x;
  scissor.y         = (int32_t)y;
  scissor.width     = (uint32_t)width;
  scissor.height    = (uint32_t)height;
  GPUSetViewport(pass, &viewport);
  GPUSetScissor(pass, &scissor);
  runtime.fittedPass = pass;
  return pass;
}

void
gpu_android_end_render_pass(GPURenderPassEncoder *pass) {
  if (runtime.fittedPass == pass) {
    runtime.fittedPass = NULL;
  }
  GPUEndRenderPass(pass);
}

void
gpu_android_set_viewport(GPURenderPassEncoder *pass,
                         const GPUViewport     *viewport) {
  GPUViewport mapped;
  float       x, y, width, height;
  float       scaleX, scaleY;

  if (!viewport || pass != runtime.fittedPass || !runtime.sample ||
      runtime.sample->width == 0u || runtime.sample->height == 0u) {
    GPUSetViewport(pass, viewport);
    return;
  }

  content_rect(&x, &y, &width, &height);
  scaleX         = width / (float)runtime.sample->width;
  scaleY         = height / (float)runtime.sample->height;
  mapped         = *viewport;
  mapped.x       = x + viewport->x * scaleX;
  mapped.y       = y + viewport->y * scaleY;
  mapped.width  *= scaleX;
  mapped.height *= scaleY;
  GPUSetViewport(pass, &mapped);
}

void
gpu_android_set_scissor(GPURenderPassEncoder *pass,
                        const GPUScissorRect  *scissor) {
  GPUScissorRect mapped;
  float          x, y, width, height;
  float          scaleX, scaleY;

  if (!scissor || pass != runtime.fittedPass || !runtime.sample ||
      runtime.sample->width == 0u || runtime.sample->height == 0u) {
    GPUSetScissor(pass, scissor);
    return;
  }

  content_rect(&x, &y, &width, &height);
  scaleX        = width / (float)runtime.sample->width;
  scaleY        = height / (float)runtime.sample->height;
  mapped.x      = (int32_t)(x + (float)scissor->x * scaleX);
  mapped.y      = (int32_t)(y + (float)scissor->y * scaleY);
  mapped.width  = (uint32_t)((float)scissor->width * scaleX);
  mapped.height = (uint32_t)((float)scissor->height * scaleY);
  GPUSetScissor(pass, &mapped);
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
  JNIEnv           *env;
  jclass            bitmapFactoryClass;
  jmethodID         decodeByteArray;
  jbyteArray        bytes;
  jobject           bitmap;
  AndroidBitmapInfo info;
  void             *bitmapPixels;
  void             *pixels;
  bool              attached;

  if (!runtime.sample || !data || size == 0u || size > INT32_MAX ||
      !width || !height) {
    return NULL;
  }

  activity = runtime.sample->app->activity;
  if (!android_jni_attach(activity, &env, &attached)) {
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
  android_jni_detach(activity, attached);
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
  JNINativeMethod         fetchCompleteMethod;
  jmethodID               fetchUrl;
  jstring                 nativeUrl;
  bool                    attached;

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
  if (!android_jni_attach(activity, &env, &attached)) {
    free(request);
    return 0;
  }
  activityClass = (*env)->GetObjectClass(env, activity->clazz);
  fetchCompleteMethod.name      = "nativeFetchComplete";
  fetchCompleteMethod.signature = "(J[BLjava/lang/String;)V";
  fetchCompleteMethod.fnPtr     = (void *)android_fetch_complete;
  if (!fetchNativeRegistered && activityClass) {
    if ((*env)->RegisterNatives(env,
                                activityClass,
                                &fetchCompleteMethod,
                                1) == JNI_OK) {
      fetchNativeRegistered = true;
    } else if ((*env)->ExceptionCheck(env)) {
      (*env)->ExceptionClear(env);
    }
  }
  fetchUrl = activityClass
               && fetchNativeRegistered
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
  android_jni_detach(activity, attached);
  if (!fetchUrl) {
    free(request);
    return 0;
  }
  return 1;
}

static void JNICALL
android_fetch_complete(
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
  bool             attached;

  if (!runtime.sample || !name || !path || capacity == 0u) {
    return 0;
  }

  activity       = runtime.sample->app->activity;
  if (!android_jni_attach(activity, &env, &attached)) {
    return 0;
  }
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
  android_jni_detach(activity, attached);
  return length > 0 && (size_t)length < capacity;
}
