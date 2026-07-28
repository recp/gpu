#include "../common/android_samples.h"

#include <string.h>

static const GPUAndroidSampleDefinition*
sample_definition(const char *id) {
  const GPUAndroidSampleDefinition *definitions[] = {
    GPUSampleAndroidTriangle(),
    GPUSampleAndroidTexturedCube()
  };

  for (uint32_t i = 0u;
       i < sizeof(definitions) / sizeof(definitions[0]);
       i++) {
    if (id && strcmp(id, definitions[i]->id) == 0) {
      return definitions[i];
    }
  }
  return definitions[0];
}

void
android_main(struct android_app *app) {
  char id[64];

  id[0] = '\0';
  (void)GPUSampleAndroidIntentExtra(app,
                                    "sample",
                                    id,
                                    sizeof(id));
  GPUSampleAndroidRunDefinition(app, sample_definition(id));
}
