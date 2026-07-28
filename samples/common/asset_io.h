#ifndef gpu_sample_asset_io_h
#define gpu_sample_asset_io_h

#include <stddef.h>
#include <stdint.h>

typedef void
(*SampleFetchCallback)(void       *bytes,
                       uint64_t    byteCount,
                       const char *error,
                       void       *userData);

typedef void
(*SampleImageCallback)(uint8_t    *pixels,
                       uint32_t    width,
                       uint32_t    height,
                       const char *error,
                       void       *userData);

int
sample_fetch_url(const char         *url,
                 SampleFetchCallback callback,
                 void               *userData);

int
sample_decode_image(const void         *bytes,
                    uint64_t            byteCount,
                    SampleImageCallback callback,
                    void               *userData);

int
sample_temporary_path(const char *name, char *path, size_t capacity);

#endif
