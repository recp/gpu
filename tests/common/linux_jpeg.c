/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>

#ifndef JPEG_DECODER
#  define JPEG_DECODER "linux_jpeg.inc"
#endif

static void    *owned;
static unsigned allocations, releases, destroys, failures, checks;
static unsigned failAllocation;

static void*
decoder_malloc(size_t size) {
  allocations++;

  if (failAllocation == 1u)
    return NULL;

  owned = malloc(size);

  return owned;
}

static void
decoder_free(void *pointer) {
  if (pointer) {
    if (pointer != owned)
      abort();

    owned = NULL;
    releases++;
  }

  free(pointer);
}

static void*
decoder_realloc(void *pointer, size_t size) {
  void *result;

  if (pointer != owned)
    abort();

  if (failAllocation == 2u)
    return NULL;

  if ((result = realloc(pointer, size)))
    owned = result;

  return result;
}

static void
decoder_destroy(j_decompress_ptr image) {
  destroys++;
  jpeg_destroy_decompress(image);
}

#define malloc                  decoder_malloc
#define free                    decoder_free
#define realloc                 decoder_realloc
#define jpeg_destroy_decompress decoder_destroy
#include JPEG_DECODER
#undef jpeg_destroy_decompress
#undef realloc
#undef free
#undef malloc

static unsigned char*
make_jpeg(unsigned mode, unsigned long *size) {
  uint8_t                       pixels[9u * 17u * 3u];
  struct jpeg_compress_struct   image;
  struct jpeg_error_mgr         error;
  JSAMPROW                      row;
  unsigned char                *bytes = NULL;
  unsigned                      i;

  for (i = 0u; i < sizeof(pixels); i++)
    pixels[i] = (uint8_t)(i * 13u + i / 7u);

  memset(&image, 0, sizeof(image));
  image.err = jpeg_std_error(&error);
  jpeg_create_compress(&image);
  jpeg_mem_dest(&image, &bytes, size);

  image.image_width      = 9u;
  image.image_height     = 17u;
  image.input_components = mode == 1u ? 1 : 3;
  image.in_color_space   = mode == 1u ? JCS_GRAYSCALE : JCS_RGB;
  jpeg_set_defaults(&image);
  jpeg_set_quality(&image, 91, TRUE);

  if (mode == 2u)
    jpeg_simple_progression(&image);

  jpeg_start_compress(&image, TRUE);

  while (image.next_scanline < image.image_height) {
    row = pixels + image.next_scanline * image.image_width * (unsigned)image.input_components;
    jpeg_write_scanlines(&image, &row, 1u);
  }

  jpeg_finish_compress(&image);
  jpeg_destroy_compress(&image);

  return bytes;
}

static void
reference_rgba(const unsigned char *bytes, unsigned long size, uint8_t *pixels) {
  struct jpeg_decompress_struct image;
  struct jpeg_error_mgr         error;
  uint8_t                       rgb[9u * 17u * 3u];
  JSAMPROW                      row;
  size_t                        i;

  memset(&image, 0, sizeof(image));
  image.err = jpeg_std_error(&error);
  jpeg_create_decompress(&image);
  jpeg_mem_src(&image, bytes, size);
  jpeg_read_header(&image, TRUE);
  image.out_color_space = JCS_RGB;
  jpeg_start_decompress(&image);

  while (image.output_scanline < image.output_height) {
    row = rgb + image.output_scanline * image.output_width * 3u;
    jpeg_read_scanlines(&image, &row, 1u);
  }

  jpeg_finish_decompress(&image);
  jpeg_destroy_decompress(&image);

  for (i = 0u; i < 9u * 17u; i++) {
    memcpy(pixels + i * 4u, rgb + i * 3u, 3u);
    pixels[i * 4u + 3u] = UINT8_MAX;
  }
}

static void
check_decode(const unsigned char *bytes,
             size_t               size,
             const uint8_t       *reference,
             unsigned             fail,
             bool                 success,
             bool                 afterAllocation) {
  uint8_t *pixels;
  uint32_t width  = 0x11223344u;
  uint32_t height = 0x55667788u;
  bool     ok;

  owned          = NULL;
  allocations    = 0u;
  releases       = 0u;
  destroys       = 0u;
  failAllocation = fail;

  pixels = decode_jpeg(bytes, size, &width, &height);
  ok     = destroys == 1u && (pixels != NULL) == success;

  if (success) {
    ok = ok && width == 9u && height == 17u
         && memcmp(pixels, reference, 9u * 17u * 4u) == 0;
    decoder_free(pixels);
  } else {
    ok = ok && width == 0x11223344u && height == 0x55667788u;
  }

  if (afterAllocation)
    ok = ok && allocations == 1u && releases == 1u;

  ok = ok && !owned;
  checks++;

  if (!ok) {
    failures++;
    fprintf(stderr,
            "jpeg decode failed: success=%u fail=%u allocations=%u releases=%u destroys=%u live=%u\n",
            (unsigned)success,
            fail,
            allocations,
            releases,
            destroys,
            (unsigned)(owned != NULL));
  }

  /* reclaim a failed decoder leak so later cases stay independent. */
  free(owned);
  owned = NULL;
}

int
main(void) {
  uint8_t        rgba[9u * 17u * 4u];
  unsigned char *bytes;
  unsigned long  size;
  unsigned       mode, repeat;

  for (mode = 0u; mode < 3u; mode++) {
    size  = 0u;
    bytes = make_jpeg(mode, &size);

    if (!bytes || size < 2u || bytes[size - 2u] != 0xffu || bytes[size - 1u] != 0xd9u)
      return 2;

    reference_rgba(bytes, size, rgba);

    for (repeat = 0u; repeat < 16u; repeat++) {
      check_decode(bytes, size, rgba, 0u, true, true);
      check_decode(bytes, size, NULL, 1u, false, false);
      check_decode(bytes, size, NULL, 2u, false, true);
    }

    /* an unsupported terminal marker fails at finish for baseline/grayscale data. */
    bytes[size - 1u] = 0x02u;

    for (repeat = 0u; repeat < 16u; repeat++) {
      check_decode(bytes, size, NULL, 0u, false, mode != 2u);
    }

    free(bytes);
  }

  printf("jpeg checks=%u failures=%u\n", checks, failures);

  return failures ? 1 : 0;
}
