#include "asset_io.h"

#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SampleFetchRequest {
  SampleFetchCallback callback;
  void               *userData;
} SampleFetchRequest;

typedef struct SampleImageRequest {
  SampleImageCallback callback;
  void               *userData;
} SampleImageRequest;

static void
sample_fetch_success(emscripten_fetch_t *fetch) {
  SampleFetchRequest *request;
  void               *bytes;
  uint64_t            byteCount;

  request   = fetch->userData;
  byteCount = fetch->numBytes > 0 ? (uint64_t)fetch->numBytes : 0u;
  bytes     = byteCount > 0u ? malloc((size_t)byteCount) : NULL;
  if (bytes) {
    memcpy(bytes, fetch->data, (size_t)byteCount);
  }
  emscripten_fetch_close(fetch);

  if (!bytes) {
    request->callback(NULL,
                      0u,
                      "sample: failed to retain downloaded bytes",
                      request->userData);
  } else {
    request->callback(bytes, byteCount, NULL, request->userData);
  }
  free(request);
}

static void
sample_fetch_error(emscripten_fetch_t *fetch) {
  SampleFetchRequest *request;

  request = fetch->userData;
  emscripten_fetch_close(fetch);
  request->callback(NULL,
                    0u,
                    "sample: download failed",
                    request->userData);
  free(request);
}

int
sample_fetch_url(const char         *url,
                 SampleFetchCallback callback,
                 void               *userData) {
  emscripten_fetch_attr_t attributes;
  SampleFetchRequest     *request;

  if (!url || !callback) {
    return 0;
  }

  request = malloc(sizeof(*request));
  if (!request) {
    return 0;
  }
  request->callback = callback;
  request->userData = userData;

  emscripten_fetch_attr_init(&attributes);
  strcpy(attributes.requestMethod, "GET");
  attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
  attributes.onsuccess  = sample_fetch_success;
  attributes.onerror    = sample_fetch_error;
  attributes.userData   = request;
  if (!emscripten_fetch(&attributes, url)) {
    free(request);
    return 0;
  }
  return 1;
}

EM_JS(void,
      sample_decode_image_js,
      (uintptr_t request, const uint8_t *bytes, uint32_t byteCount), {
  const encoded = HEAPU8.slice(bytes, bytes + byteCount);
  const blob = new Blob([encoded]);

  createImageBitmap(blob, {
    colorSpaceConversion: "none",
    premultiplyAlpha: "none"
  }).then((bitmap) => {
    let pixels = 0;
    let width = 0;
    let height = 0;

    try {
      const canvas = document.createElement("canvas");

      canvas.width = bitmap.width;
      canvas.height = bitmap.height;

      const context = canvas.getContext("2d", {
        alpha: true,
        colorSpace: "srgb",
        willReadFrequently: true
      });
      if (!context) {
        throw new Error("2D canvas context unavailable");
      }

      context.drawImage(bitmap, 0, 0);
      const image = context.getImageData(0, 0, bitmap.width, bitmap.height);

      pixels = _malloc(image.data.byteLength);
      if (!pixels) {
        throw new Error("Wasm image allocation failed");
      }
      HEAPU8.set(image.data, pixels);
      width = canvas.width;
      height = canvas.height;
    } catch (_) {
      if (pixels) {
        _free(pixels);
      }
      pixels = 0;
      width = 0;
      height = 0;
    } finally {
      bitmap.close();
    }

    _sample_image_ready(request, pixels, width, height);
  }, () => {
    _sample_image_ready(request, 0, 0, 0);
  });
});

EMSCRIPTEN_KEEPALIVE
void
sample_image_ready(uintptr_t requestValue,
                   uint8_t  *pixels,
                   uint32_t  width,
                   uint32_t  height) {
  SampleImageRequest *request;

  request = (SampleImageRequest *)requestValue;
  if (!request) {
    free(pixels);
    return;
  }

  request->callback(pixels,
                    width,
                    height,
                    pixels && width > 0u && height > 0u
                      ? NULL
                      : "sample: image decode failed",
                    request->userData);
  free(request);
}

int
sample_decode_image(const void         *bytes,
                    uint64_t            byteCount,
                    SampleImageCallback callback,
                    void               *userData) {
  SampleImageRequest *request;

  if (!bytes || byteCount == 0u || byteCount > UINT32_MAX || !callback) {
    return 0;
  }

  request = malloc(sizeof(*request));
  if (!request) {
    return 0;
  }
  request->callback = callback;
  request->userData = userData;
  sample_decode_image_js((uintptr_t)request,
                         bytes,
                         (uint32_t)byteCount);
  return 1;
}

int
sample_temporary_path(const char *name, char *path, size_t capacity) {
  int length;

  if (!name || !path || capacity == 0u) {
    return 0;
  }

  length = snprintf(path, capacity, "/tmp/%s", name);
  return length > 0 && (size_t)length < capacity;
}
