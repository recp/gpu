#ifndef gpu_sample_android_h
#define gpu_sample_android_h

#include <android_native_app_glue.h>
#include <gpu/gpu.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct GPUAndroidSample GPUAndroidSample;
typedef int (*GPUAndroidSampleStart)(void);

typedef struct GPUAndroidWebConfig {
  GPUAndroidSampleStart start;
} GPUAndroidWebConfig;

typedef struct GPUAndroidSampleCallbacks {
  bool (*create)(GPUAndroidSample *sample, void *userData);
  bool (*resize)(GPUAndroidSample *sample,
                 uint32_t          width,
                 uint32_t          height,
                 void             *userData);
  bool (*render)(GPUAndroidSample *sample, void *userData);
  void (*destroy)(GPUAndroidSample *sample, void *userData);
} GPUAndroidSampleCallbacks;

typedef struct GPUAndroidSampleDefinition {
  const GPUAndroidSampleCallbacks *callbacks;
  const void                      *config;
  const GPUFeature                *optionalFeatures;
  const char                      *id;
  const char                      *name;
  size_t                           userDataSize;
  uint32_t                         optionalFeatureCount;
} GPUAndroidSampleDefinition;

struct GPUAndroidSample {
  struct android_app              *app;
  GPUInstance                     *instance;
  GPUAdapter                      *adapter;
  GPUDevice                       *device;
  GPUQueue                        *queue;
  GPUSurface                      *surface;
  GPUSwapchain                    *swapchain;
  const GPUAndroidSampleCallbacks *callbacks;
  const GPUAndroidSampleDefinition *definition;
  void                            *userData;
  const char                      *name;
  uint32_t                         width;
  uint32_t                         height;
  bool                             focused;
  bool                             failed;
};

bool
GPUSampleAndroidLoadUSL(GPUAndroidSample  *sample,
                        const char        *assetName,
                        GPUShaderLibrary **outLibrary);

double
GPUSampleAndroidTime(void);

bool
GPUSampleAndroidFail(GPUAndroidSample *sample, const char *stage);

void
GPUSampleAndroidRun(struct android_app              *app,
                    const char                      *name,
                    const GPUAndroidSampleCallbacks *callbacks,
                    void                            *userData,
                    const GPUAndroidSampleDefinition *definition);

void
GPUSampleAndroidRunDefinition(struct android_app               *app,
                              const GPUAndroidSampleDefinition *definition);

bool
GPUSampleAndroidIntentExtra(struct android_app *app,
                            const char         *key,
                            char               *value,
                            size_t              capacity);

const GPUAndroidSampleCallbacks*
GPUSampleAndroidWebCallbacks(void);

#endif
