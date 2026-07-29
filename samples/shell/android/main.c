#include "../common/android_samples.h"

static const GPUAndroidSampleDefinition*
sample_definition(const char *id) {
  return GPUSampleAndroidWebDefinition(id);
}

void
android_main(struct android_app *app) {
  char id[64];

  id[0] = '\0';
  (void)GPUSampleAndroidIntentExtra(app,
                                    "sample",
                                    id,
                                    sizeof(id));
  const GPUAndroidSampleDefinition *definition;

  definition = sample_definition(id);
  if (definition) {
    GPUSampleAndroidRunDefinition(app, definition);
  }
}
