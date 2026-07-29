#ifndef gpu_sample_win32_image_h
#define gpu_sample_win32_image_h

#include <stddef.h>
#include <stdint.h>

uint8_t*
GPUSampleWin32DecodeImage(const void *bytes,
                          size_t      byteCount,
                          uint32_t   *width,
                          uint32_t   *height);

#endif /* gpu_sample_win32_image_h */
