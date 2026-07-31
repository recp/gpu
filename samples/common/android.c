#include "android.h"
#include "sample_orbit.h"

#include <android/asset_manager.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GPU_ANDROID_TAG "gpu-sample"

bool
GPUSampleAndroidFail(GPUAndroidSample *sample, const char *stage) {
  __android_log_print(ANDROID_LOG_ERROR,
                      GPU_ANDROID_TAG,
                      "%s failed at %s",
                      sample && sample->name ? sample->name : "GPU sample",
                      stage ? stage : "unknown stage");
  return false;
}

static void
gpu_android_destroy(GPUAndroidSample *sample) {
  if (sample->callbacks && sample->callbacks->destroy) {
    sample->callbacks->destroy(sample, sample->userData);
  }
  if (sample->swapchain) {
    GPUDestroySwapchain(sample->swapchain);
  }
  if (sample->surface) {
    GPUDestroySurface(sample->surface);
  }
  if (sample->device) {
    GPUDestroyDevice(sample->device);
  }
  if (sample->instance) {
    GPUDestroyInstance(sample->instance);
  }

  sample->instance  = NULL;
  sample->adapter   = NULL;
  sample->device    = NULL;
  sample->queue     = NULL;
  sample->surface   = NULL;
  sample->swapchain = NULL;
  sample->width     = 0u;
  sample->height    = 0u;
}

static bool
gpu_android_resize(GPUAndroidSample *sample) {
  uint32_t width, height;

  if (!sample->swapchain || !sample->app->window) {
    return false;
  }

  width  = (uint32_t)ANativeWindow_getWidth(sample->app->window);
  height = (uint32_t)ANativeWindow_getHeight(sample->app->window);
  if (width == 0u || height == 0u) {
    return false;
  }
  if (width == sample->width && height == sample->height) {
    return true;
  }
  if (GPUResizeSwapchain(sample->swapchain, width, height) != GPU_OK) {
    return false;
  }

  sample->width  = width;
  sample->height = height;
  return !sample->callbacks->resize ||
         sample->callbacks->resize(sample,
                                   width,
                                   height,
                                   sample->userData);
}

static bool
gpu_android_create(GPUAndroidSample *sample) {
  GPUInstanceCreateInfo instanceInfo      = {0};
  GPUAdapterProperties  adapterProperties = {0};
  GPUDeviceCreateInfo   deviceInfo        = {0};
  GPURuntimeConfig      runtime           = {0};
  GPUFeature            features[16];
  uint32_t              featureCount;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.label            = sample->name;
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  instanceInfo.enableValidation = false;
  if (GPUCreateInstance(&instanceInfo, &sample->instance) != GPU_OK ||
      !sample->instance) {
    return GPUSampleAndroidFail(sample, "instance");
  }

  sample->adapter = GPUGetAutoSelectedAdapter(sample->instance);
  if (!sample->adapter) {
    return GPUSampleAndroidFail(sample, "adapter");
  }

  features[0] = GPU_FEATURE_COMPUTE;
  features[1] = GPU_FEATURE_INDIRECT_DRAW;
  features[2] = GPU_FEATURE_MULTI_DRAW;
  featureCount = 3u;
  if (sample->definition) {
    for (uint32_t i = 0u;
         i < sample->definition->optionalFeatureCount &&
         featureCount < sizeof(features) / sizeof(features[0]);
         i++) {
      GPUFeature feature;
      bool       duplicate;

      feature   = sample->definition->optionalFeatures[i];
      duplicate = false;
      for (uint32_t j = 0u; j < featureCount; j++) {
        duplicate |= features[j] == feature;
      }
      if (!duplicate) {
        features[featureCount++] = feature;
      }
    }
  }
  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.optional.pFeatures    = features;
  deviceInfo.optional.featureCount = featureCount;
  if (GPUCreateDevice(
        sample->adapter,
        &deviceInfo,
        &sample->device
      ) != GPU_OK ||
      !sample->device) {
    return GPUSampleAndroidFail(sample, "device");
  }
  sample->queue = GPUGetQueue(sample->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!sample->queue) {
    return GPUSampleAndroidFail(sample, "queue");
  }

  runtime.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtime.chain.structSize = sizeof(runtime);
  runtime.validationMode   = GPU_VALIDATION_FULL;
  runtime.enableStats      = true;
  if (GPUConfigureRuntime(sample->device, &runtime) != GPU_OK) {
    return GPUSampleAndroidFail(sample, "runtime configuration");
  }

  sample->surface = GPUCreateSurfaceFromNative(
    sample->instance,
    sample->adapter,
    sample->app->window,
    GPU_SURFACE_ANDROID_NATIVE_WINDOW,
    1.0f);
  if (!sample->surface) {
    return GPUSampleAndroidFail(sample, "surface");
  }

  sample->width  = (uint32_t)ANativeWindow_getWidth(sample->app->window);
  sample->height = (uint32_t)ANativeWindow_getHeight(sample->app->window);
  if (sample->width == 0u || sample->height == 0u) {
    return GPUSampleAndroidFail(sample, "window extent");
  }
  sample->swapchain = GPUCreateSwapchainDefault(sample->device,
                                                sample->surface,
                                                sample->width,
                                                sample->height);
  if (!sample->swapchain) {
    return GPUSampleAndroidFail(sample, "swapchain");
  }
  if (!sample->callbacks->create(sample, sample->userData)) {
    return GPUSampleAndroidFail(sample, "renderer");
  }

  if (GPUGetAdapterProperties(sample->adapter, &adapterProperties) == GPU_OK) {
    __android_log_print(ANDROID_LOG_INFO,
                        GPU_ANDROID_TAG,
                        "%s ready on %s (%ux%u)",
                        sample->name,
                        adapterProperties.name
                          ? adapterProperties.name
                          : "Vulkan adapter",
                        sample->width,
                        sample->height);
  }
  return true;
}

static void
gpu_android_command(struct android_app *app, int32_t command) {
  GPUAndroidSample *sample;

  sample = app ? app->userData : NULL;
  if (!sample) {
    return;
  }

  switch (command) {
    case APP_CMD_INIT_WINDOW:
      if (app->window && !sample->swapchain) {
        sample->failed = !gpu_android_create(sample);
        if (sample->failed) {
          __android_log_print(ANDROID_LOG_ERROR,
                              GPU_ANDROID_TAG,
                              "%s initialization failed",
                              sample->name);
          gpu_android_destroy(sample);
        }
      }
      break;
    case APP_CMD_TERM_WINDOW:
      gpu_android_destroy(sample);
      sample->failed = false;
      break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONTENT_RECT_CHANGED:
    case APP_CMD_CONFIG_CHANGED:
      if (sample->swapchain && !gpu_android_resize(sample)) {
        sample->failed = true;
      }
      break;
    case APP_CMD_GAINED_FOCUS:
      sample->focused = true;
      break;
    case APP_CMD_LOST_FOCUS:
      sample->focused = false;
      break;
    default:
      break;
  }
}

static int32_t
gpu_android_input(struct android_app *app, AInputEvent *event) {
  static float pinchSpan;
  int32_t action;
  size_t  pointerCount;

  (void)app;
  if (!event || !sample_orbit_active() ||
      AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) {
    return 0;
  }

  action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
  pointerCount = AMotionEvent_getPointerCount(event);
  switch (action) {
    case AMOTION_EVENT_ACTION_DOWN:
      sample_orbit_pointer_begin(AMotionEvent_getX(event, 0u),
                                 AMotionEvent_getY(event, 0u));
      break;
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
      if (pointerCount >= 2u) {
        float deltaX, deltaY;

        deltaX = AMotionEvent_getX(event, 1u) -
                 AMotionEvent_getX(event, 0u);
        deltaY = AMotionEvent_getY(event, 1u) -
                 AMotionEvent_getY(event, 0u);
        pinchSpan = deltaX * deltaX + deltaY * deltaY;
        sample_orbit_pointer_end();
      }
      break;
    case AMOTION_EVENT_ACTION_MOVE:
      if (pointerCount >= 2u) {
        float deltaX, deltaY, nextSpan;

        deltaX   = AMotionEvent_getX(event, 1u) -
                   AMotionEvent_getX(event, 0u);
        deltaY   = AMotionEvent_getY(event, 1u) -
                   AMotionEvent_getY(event, 0u);
        nextSpan = deltaX * deltaX + deltaY * deltaY;
        if (pinchSpan > 1.0f) {
          sample_orbit_zoom((nextSpan - pinchSpan) / pinchSpan * 2.0f);
        }
        pinchSpan = nextSpan;
      } else {
        sample_orbit_pointer_move(AMotionEvent_getX(event, 0u),
                                  AMotionEvent_getY(event, 0u));
      }
      break;
    case AMOTION_EVENT_ACTION_POINTER_UP:
      pinchSpan = 0.0f;
      break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_CANCEL:
      pinchSpan = 0.0f;
      sample_orbit_pointer_end();
      break;
    default:
      return 0;
  }
  return 1;
}

bool
GPUSampleAndroidLoadUSL(GPUAndroidSample  *sample,
                        const char        *assetName,
                        GPUShaderLibrary **outLibrary) {
  AAsset     *asset;
  const void *data;
  off_t       size;
  GPUResult   result;

  if (!sample || !assetName || !outLibrary) {
    return false;
  }

  *outLibrary = NULL;
  asset = AAssetManager_open(sample->app->activity->assetManager,
                             assetName,
                             AASSET_MODE_BUFFER);
  if (!asset) {
    return false;
  }

  data = AAsset_getBuffer(asset);
  size = AAsset_getLength(asset);
  result = data && size > 0
             ? GPUCreateShaderLibraryFromUSL(sample->device,
                                             data,
                                             (uint64_t)size,
                                             outLibrary)
             : GPU_ERROR_INVALID_ARGUMENT;
  AAsset_close(asset);
  return result == GPU_OK && *outLibrary;
}

double
GPUSampleAndroidTime(void) {
  struct timespec time;

  clock_gettime(CLOCK_MONOTONIC, &time);
  return (double)time.tv_sec + (double)time.tv_nsec * 0.000000001;
}

void
GPUSampleAndroidRun(struct android_app              *app,
                    const char                      *name,
                    const GPUAndroidSampleCallbacks *callbacks,
                    void                            *userData,
                    const GPUAndroidSampleDefinition *definition) {
  GPUAndroidSample sample = {0};

  if (!app || !name || !callbacks || !callbacks->create ||
      !callbacks->render) {
    return;
  }

  sample.app        = app;
  sample.callbacks  = callbacks;
  sample.definition = definition;
  sample.userData   = userData;
  sample.name       = name;
  app->userData     = &sample;
  app->onAppCmd     = gpu_android_command;
  app->onInputEvent = gpu_android_input;

  while (!app->destroyRequested) {
    struct android_poll_source *source;
    int events, ident, timeout;

    timeout = sample.focused && sample.swapchain ? 0 : -1;
    while ((ident = ALooper_pollOnce(timeout,
                                     NULL,
                                     &events,
                                     (void **)&source)) >= 0) {
      if (source) {
        source->process(app, source);
      }
      if (app->destroyRequested) {
        break;
      }
      timeout = 0;
    }

    if (sample.focused && sample.swapchain && !sample.failed &&
        !callbacks->render(&sample, userData)) {
      sample.failed = true;
      __android_log_print(ANDROID_LOG_ERROR,
                          GPU_ANDROID_TAG,
                          "%s frame failed",
                          sample.name);
    }
  }

  gpu_android_destroy(&sample);
}

void
GPUSampleAndroidRunDefinition(struct android_app               *app,
                              const GPUAndroidSampleDefinition *definition) {
  void *userData;

  if (!definition || !definition->callbacks ||
      definition->userDataSize == 0u) {
    return;
  }

  userData = calloc(1, definition->userDataSize);
  if (!userData) {
    return;
  }
  GPUSampleAndroidRun(app,
                      definition->name,
                      definition->callbacks,
                      userData,
                      definition);
  free(userData);
}

bool
GPUSampleAndroidIntentExtra(struct android_app *app,
                            const char         *key,
                            char               *value,
                            size_t              capacity) {
  ANativeActivity *activity;
  JavaVM          *vm;
  JNIEnv          *env;
  jclass           activityClass;
  jclass           intentClass;
  jmethodID        getIntent;
  jmethodID        getStringExtra;
  jobject          intent;
  jstring          nativeKey;
  jstring          nativeValue;
  const char      *text;
  size_t           length;
  jint             envStatus;
  bool             attached;
  bool             result;

  if (!app || !app->activity || !key || !value || capacity == 0u) {
    return false;
  }

  value[0] = '\0';
  activity = app->activity;
  vm       = activity->vm;
  env      = NULL;
  attached = false;
  result   = false;
  envStatus = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6);
  if (envStatus == JNI_EDETACHED) {
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
      return false;
    }
    attached = true;
  } else if (envStatus != JNI_OK || !env) {
    return false;
  }

  activityClass = (*env)->GetObjectClass(env, activity->clazz);
  getIntent = activityClass
                ? (*env)->GetMethodID(env,
                                      activityClass,
                                      "getIntent",
                                      "()Landroid/content/Intent;")
                : NULL;
  intent = getIntent
             ? (*env)->CallObjectMethod(env, activity->clazz, getIntent)
             : NULL;
  intentClass = intent ? (*env)->GetObjectClass(env, intent) : NULL;
  getStringExtra = intentClass
                     ? (*env)->GetMethodID(
                         env,
                         intentClass,
                         "getStringExtra",
                         "(Ljava/lang/String;)Ljava/lang/String;"
                       )
                     : NULL;
  nativeKey = getStringExtra
                ? (*env)->NewStringUTF(env, key)
                : NULL;
  nativeValue = nativeKey
                  ? (jstring)(*env)->CallObjectMethod(env,
                                                     intent,
                                                     getStringExtra,
                                                     nativeKey)
                  : NULL;
  text = nativeValue
           ? (*env)->GetStringUTFChars(env, nativeValue, NULL)
           : NULL;
  if (text) {
    length = strlen(text);
    if (length >= capacity) {
      length = capacity - 1u;
    }
    memcpy(value, text, length);
    value[length] = '\0';
    result = length > 0u;
    (*env)->ReleaseStringUTFChars(env, nativeValue, text);
  }

  if (nativeValue) {
    (*env)->DeleteLocalRef(env, nativeValue);
  }
  if (nativeKey) {
    (*env)->DeleteLocalRef(env, nativeKey);
  }
  if (intentClass) {
    (*env)->DeleteLocalRef(env, intentClass);
  }
  if (intent) {
    (*env)->DeleteLocalRef(env, intent);
  }
  if (activityClass) {
    (*env)->DeleteLocalRef(env, activityClass);
  }
  if ((*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
    result = false;
  }
  if (attached) {
    (*vm)->DetachCurrentThread(vm);
  }
  return result;
}
