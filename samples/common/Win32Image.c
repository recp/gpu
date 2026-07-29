#define CINTERFACE
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN

#include "Win32Image.h"

#include <windows.h>
#include <objbase.h>
#include <wincodec.h>

#include <stdbool.h>
#include <stdlib.h>

static IWICImagingFactory *imageFactory;
static bool                imageFactoryAttempted;

static IWICImagingFactory*
image_factory(void) {
  HRESULT result;

  if (imageFactoryAttempted) {
    return imageFactory;
  }
  imageFactoryAttempted = true;
  result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
    return NULL;
  }
  result = CoCreateInstance(&CLSID_WICImagingFactory,
                            NULL,
                            CLSCTX_INPROC_SERVER,
                            &IID_IWICImagingFactory,
                            (void **)&imageFactory);
  return SUCCEEDED(result) ? imageFactory : NULL;
}

uint8_t*
GPUSampleWin32DecodeImage(const void *bytes,
                          size_t      byteCount,
                          uint32_t   *width,
                          uint32_t   *height) {
  IWICImagingFactory   *factory;
  IWICBitmapDecoder    *decoder;
  IWICBitmapFrameDecode *frame;
  IWICFormatConverter  *converter;
  IWICStream           *stream;
  uint8_t              *pixels;
  size_t                rowBytes, pixelBytes;
  UINT                  imageWidth, imageHeight;
  HRESULT               result;

  if (!bytes || byteCount == 0u || byteCount > UINT32_MAX ||
      !width || !height) {
    return NULL;
  }
  factory   = image_factory();
  decoder   = NULL;
  frame     = NULL;
  converter = NULL;
  stream    = NULL;
  pixels    = NULL;
  if (!factory ||
      FAILED(IWICImagingFactory_CreateStream(factory, &stream)) ||
      FAILED(IWICStream_InitializeFromMemory(stream,
                                             (BYTE *)bytes,
                                             (DWORD)byteCount)) ||
      FAILED(IWICImagingFactory_CreateDecoderFromStream(
        factory,
        (IStream *)stream,
        NULL,
        WICDecodeMetadataCacheOnLoad,
        &decoder)) ||
      FAILED(IWICBitmapDecoder_GetFrame(decoder, 0u, &frame)) ||
      FAILED(IWICBitmapFrameDecode_GetSize(frame,
                                           &imageWidth,
                                           &imageHeight)) ||
      imageWidth == 0u || imageHeight == 0u ||
      (size_t)imageWidth > SIZE_MAX / 4u) {
    goto cleanup;
  }

  rowBytes = (size_t)imageWidth * 4u;
  if ((size_t)imageHeight > SIZE_MAX / rowBytes) {
    goto cleanup;
  }
  pixelBytes = rowBytes * (size_t)imageHeight;
  if (pixelBytes > UINT32_MAX ||
      FAILED(IWICImagingFactory_CreateFormatConverter(factory,
                                                       &converter))) {
    goto cleanup;
  }
  result = IWICFormatConverter_Initialize(
    converter,
    (IWICBitmapSource *)frame,
    &GUID_WICPixelFormat32bppRGBA,
    WICBitmapDitherTypeNone,
    NULL,
    0.0,
    WICBitmapPaletteTypeCustom);
  if (FAILED(result)) {
    goto cleanup;
  }

  pixels = malloc(pixelBytes);
  if (!pixels ||
      FAILED(IWICFormatConverter_CopyPixels(converter,
                                            NULL,
                                            (UINT)rowBytes,
                                            (UINT)pixelBytes,
                                            pixels))) {
    free(pixels);
    pixels = NULL;
    goto cleanup;
  }
  *width  = imageWidth;
  *height = imageHeight;

cleanup:
  if (converter) {
    IWICFormatConverter_Release(converter);
  }
  if (frame) {
    IWICBitmapFrameDecode_Release(frame);
  }
  if (decoder) {
    IWICBitmapDecoder_Release(decoder);
  }
  if (stream) {
    IWICStream_Release(stream);
  }
  return pixels;
}
