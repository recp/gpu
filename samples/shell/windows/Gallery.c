#define WIN32_LEAN_AND_MEAN

#include "NativeSamples.h"
#include "../../common/Win32Image.h"

#include <gpu/gpu.h>

#include <dwmapi.h>
#include <uxtheme.h>
#include <windows.h>
#include <windowsx.h>

#include <stdbool.h>
#include <stdint.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum {
  GPU_GALLERY_TIMER_CHILD         = 1u,
  GPU_GALLERY_ADAPTER_SELECTOR_ID = 100u,
  GPU_GALLERY_ADAPTER_AUTO        = 0u,
  GPU_GALLERY_ADAPTER_LOW_POWER   = 1u,
  GPU_GALLERY_ADAPTER_HIGH_POWER  = 2u,
  GPU_GALLERY_ADAPTER_FIRST       = 3u,
  GPU_GALLERY_ADAPTERS_READY      = WM_APP + 1u
};

typedef struct GPUGalleryPreview {
  uint8_t  *pixels;
  uint32_t  width;
  uint32_t  height;
} GPUGalleryPreview;

typedef struct GPUGallery {
  HWND               window;
  HWND               adapterSelector;
  HANDLE             child;
  HANDLE             job;
  HDC                backContext;
  HBITMAP            backBitmap;
  HBITMAP            backDefaultBitmap;
  HFONT              titleFont;
  HFONT              bodyFont;
  GPUGalleryPreview *previews;
  const char        *status;
  int                backWidth;
  int                backHeight;
  int                scroll;
  int                scrollMax;
  int                hovered;
  uint32_t           adapterSelection;
  float              scale;
} GPUGallery;

typedef struct GPUGalleryLayout {
  int columns;
  int margin;
  int gap;
  int cardWidth;
  int cardHeight;
  int previewHeight;
  int offset;
} GPUGalleryLayout;

typedef struct GPUGalleryAdapters {
  wchar_t  (*labels)[256];
  uint32_t  *indices;
  uint32_t   count;
} GPUGalleryAdapters;

static COLORREF
rgb(uint8_t red, uint8_t green, uint8_t blue) {
  return RGB(red, green, blue);
}

static bool
read_file(const char *path, void **outData, size_t *outSize) {
  FILE   *file;
  void   *data;
  __int64 length;

  if (!path || !outData || !outSize) {
    return false;
  }
  *outData = NULL;
  *outSize = 0u;
  file     = fopen(path, "rb");
  if (!file) {
    return false;
  }
  if (_fseeki64(file, 0, SEEK_END) != 0 ||
      (length = _ftelli64(file)) <= 0 ||
      (uint64_t)length > SIZE_MAX ||
      _fseeki64(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return false;
  }
  data = malloc((size_t)length);
  if (!data || fread(data, (size_t)length, 1u, file) != 1u) {
    free(data);
    fclose(file);
    return false;
  }
  fclose(file);
  *outData = data;
  *outSize = (size_t)length;
  return true;
}

static void
load_previews(GPUGallery *gallery) {
  if (!gallery || gallery->previews) {
    return;
  }
  gallery->previews = calloc(gpuNativeSampleCount,
                             sizeof(*gallery->previews));
  if (!gallery->previews) {
    return;
  }

  for (size_t i = 0u; i < gpuNativeSampleCount; i++) {
    GPUGalleryPreview *preview;
    void              *bytes;
    size_t             byteCount;

    bytes     = NULL;
    byteCount = 0u;
    preview   = &gallery->previews[i];
    if (!read_file(gpuNativeSamples[i].preview, &bytes, &byteCount)) {
      continue;
    }
    preview->pixels = GPUSampleWin32DecodeImage(bytes,
                                               byteCount,
                                               &preview->width,
                                               &preview->height);
    free(bytes);
    if (!preview->pixels) {
      continue;
    }
    for (size_t pixel = 0u;
         pixel < (size_t)preview->width * preview->height;
         pixel++) {
      uint8_t red;

      red = preview->pixels[pixel * 4u];
      preview->pixels[pixel * 4u] =
        preview->pixels[pixel * 4u + 2u];
      preview->pixels[pixel * 4u + 2u] = red;
    }
  }
}

static void
free_previews(GPUGallery *gallery) {
  if (!gallery || !gallery->previews) {
    return;
  }
  for (size_t i = 0u; i < gpuNativeSampleCount; i++) {
    free(gallery->previews[i].pixels);
  }
  free(gallery->previews);
  gallery->previews = NULL;
}

static int
scaled(const GPUGallery *gallery, int value) {
  return (int)((float)value * gallery->scale + 0.5f);
}

static bool
monitor_work_area(HWND window, RECT *work) {
  MONITORINFO monitorInfo = {0};
  HMONITOR    monitor;
  POINT       origin = {0};

  if (!work) {
    return false;
  }
  monitor = window
              ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST)
              : MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
  if (!monitor) {
    return false;
  }
  monitorInfo.cbSize = sizeof(monitorInfo);
  if (!GetMonitorInfoW(monitor, &monitorInfo)) {
    return false;
  }
  *work = monitorInfo.rcWork;
  return true;
}

static float
display_scale(HWND window, UINT dpi) {
  RECT  work;
  float dpiScale, resolutionScaleX, resolutionScaleY, resolutionScale;

  dpiScale = (float)dpi / 96.0f;
  if (!monitor_work_area(window, &work)) {
    return dpiScale;
  }
  resolutionScaleX = (float)(work.right - work.left) / 1920.0f;
  resolutionScaleY = (float)(work.bottom - work.top) / 1080.0f;
  resolutionScale  = resolutionScaleX < resolutionScaleY
                       ? resolutionScaleX
                       : resolutionScaleY;
  if (resolutionScale > dpiScale) {
    dpiScale = resolutionScale;
  }
  if (dpiScale < 1.0f) {
    dpiScale = 1.0f;
  } else if (dpiScale > 2.0f) {
    dpiScale = 2.0f;
  }
  return dpiScale;
}

static bool
create_fonts(GPUGallery *gallery) {
  HFONT titleFont, bodyFont;

  if (!gallery) {
    return false;
  }
  titleFont = CreateFontW(-scaled(gallery, 28),
                          0,
                          0,
                          0,
                          FW_EXTRABOLD,
                          FALSE,
                          FALSE,
                          FALSE,
                          DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY,
                          DEFAULT_PITCH,
                          L"Segoe UI");
  bodyFont = CreateFontW(-scaled(gallery, 18),
                         0,
                         0,
                         0,
                         FW_SEMIBOLD,
                         FALSE,
                         FALSE,
                         FALSE,
                         DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY,
                         DEFAULT_PITCH,
                         L"Segoe UI");
  if (!titleFont || !bodyFont) {
    DeleteObject(bodyFont);
    DeleteObject(titleFont);
    return false;
  }
  DeleteObject(gallery->bodyFont);
  DeleteObject(gallery->titleFont);
  gallery->titleFont = titleFont;
  gallery->bodyFont  = bodyFont;
  if (gallery->adapterSelector) {
    SendMessageW(gallery->adapterSelector,
                 WM_SETFONT,
                 (WPARAM)gallery->bodyFont,
                 TRUE);
  }
  return true;
}

static void
adapter_selector_rect(const GPUGallery *gallery,
                      const RECT       *client,
                      RECT             *selector) {
  int available, margin, width;

  margin    = scaled(gallery, 28);
  available = client->right - margin * 2;
  width     = scaled(gallery, 292);
  if (width > client->right - scaled(gallery, 360)) {
    width = client->right - scaled(gallery, 360);
  }
  if (width < scaled(gallery, 180)) {
    width = scaled(gallery, 180);
  }
  if (width > available) {
    width = available;
  }
  if (width < 1) {
    width = 1;
  }
  selector->right  = client->right - margin;
  selector->left   = selector->right - width;
  selector->top    = scaled(gallery, 28);
  selector->bottom = selector->top + scaled(gallery, 260);
}

static void
position_adapter_selector(GPUGallery *gallery) {
  RECT client, selector;

  if (!gallery || !gallery->adapterSelector ||
      !GetClientRect(gallery->window, &client)) {
    return;
  }
  adapter_selector_rect(gallery, &client, &selector);
  MoveWindow(gallery->adapterSelector,
             selector.left,
             selector.top,
             selector.right - selector.left,
             selector.bottom - selector.top,
             TRUE);
}

static bool
adapter_label(const GPUAdapterProperties *properties,
              wchar_t                     label[256]) {
  if (!properties || !properties->name || !label) {
    return false;
  }
  if (MultiByteToWideChar(CP_UTF8,
                          MB_ERR_INVALID_CHARS,
                          properties->name,
                          -1,
                          label,
                          256) > 0) {
    return true;
  }
  return MultiByteToWideChar(CP_ACP,
                             0u,
                             properties->name,
                             -1,
                             label,
                             256) > 0;
}

static void
add_adapter_option(HWND selector, const wchar_t *label, uint32_t value) {
  LRESULT item;

  item = SendMessageW(selector, CB_ADDSTRING, 0u, (LPARAM)label);
  if (item != CB_ERR && item != CB_ERRSPACE) {
    SendMessageW(selector, CB_SETITEMDATA, (WPARAM)item, (LPARAM)value);
  }
}

static unsigned __stdcall
discover_adapters(void *userData) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUGalleryAdapters   *result;
  GPUInstance          *instance;
  GPUAdapter          **adapters;
  HWND                  window;
  uint32_t              adapterCount;

  window                         = userData;
  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.label            = "windows-gallery-adapters";
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  result                         = NULL;
  instance                       = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    return 0u;
  }

  adapterCount = 0u;
  adapters     = NULL;
  if (GPUEnumerateAdapters(instance, &adapterCount, NULL) == GPU_OK &&
      adapterCount > 0u &&
      (result = calloc(1u, sizeof(*result))) &&
      (result->labels = calloc(adapterCount, sizeof(*result->labels))) &&
      (result->indices = calloc(adapterCount, sizeof(*result->indices))) &&
      (adapters = calloc(adapterCount, sizeof(*adapters))) &&
      GPUEnumerateAdapters(instance, &adapterCount, adapters) == GPU_OK) {
    for (uint32_t i = 0u; i < adapterCount; i++) {
      GPUAdapterProperties properties = {0};

      if (GPUGetAdapterProperties(adapters[i], &properties) != GPU_OK) {
        continue;
      }
      if (!adapter_label(&properties, result->labels[result->count])) {
        swprintf(result->labels[result->count],
                 256u,
                 L"Adapter %u",
                 i + 1u);
      }
      result->indices[result->count] = i;
      result->count++;
    }
  }
  free(adapters);
  GPUDestroyInstance(instance);
  if (!result || !PostMessageW(window,
                               GPU_GALLERY_ADAPTERS_READY,
                               0u,
                               (LPARAM)result)) {
    if (result) {
      free(result->labels);
      free(result->indices);
      free(result);
    }
  }
  return 0u;
}

static void
start_adapter_discovery(HWND window) {
  uintptr_t thread;

  thread = _beginthreadex(NULL,
                          0u,
                          discover_adapters,
                          window,
                          0u,
                          NULL);
  if (thread) {
    CloseHandle((HANDLE)thread);
  }
}

static bool
create_adapter_selector(GPUGallery *gallery) {
  if (!gallery || !gallery->window) {
    return false;
  }
  gallery->adapterSelector =
    CreateWindowExW(0u,
                    L"COMBOBOX",
                    NULL,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                      CBS_DROPDOWNLIST | CBS_HASSTRINGS,
                    0,
                    0,
                    0,
                    0,
                    gallery->window,
                    (HMENU)(UINT_PTR)GPU_GALLERY_ADAPTER_SELECTOR_ID,
                    GetModuleHandleW(NULL),
                    NULL);
  if (!gallery->adapterSelector) {
    return false;
  }
  SetWindowTheme(gallery->adapterSelector, L"DarkMode_Explorer", NULL);
  SendMessageW(gallery->adapterSelector,
               WM_SETFONT,
               (WPARAM)gallery->bodyFont,
               TRUE);
  add_adapter_option(gallery->adapterSelector,
                     L"Auto",
                     GPU_GALLERY_ADAPTER_AUTO);
  add_adapter_option(gallery->adapterSelector,
                     L"Low power",
                     GPU_GALLERY_ADAPTER_LOW_POWER);
  add_adapter_option(gallery->adapterSelector,
                     L"High performance",
                     GPU_GALLERY_ADAPTER_HIGH_POWER);
  SendMessageW(gallery->adapterSelector, CB_SETCURSEL, 0u, 0u);
  position_adapter_selector(gallery);
  return true;
}

static void
gallery_layout(const GPUGallery *gallery,
               const RECT       *client,
               GPUGalleryLayout *layout) {
  int available;
  int minimumWidth;

  layout->margin = scaled(gallery, 28);
  layout->gap    = scaled(gallery, 18);
  available     = client->right - client->left - layout->margin * 2;
  minimumWidth  = scaled(gallery, 280);
  layout->columns = 3;
  while (layout->columns > 1) {
    layout->cardWidth =
      (available - layout->gap * (layout->columns - 1)) / layout->columns;
    if (layout->cardWidth >= minimumWidth) {
      break;
    }
    layout->columns--;
  }
  layout->cardWidth =
    (available - layout->gap * (layout->columns - 1)) / layout->columns;
  if (layout->cardWidth < 1) {
    layout->cardWidth = 1;
  }
  layout->previewHeight =
    (layout->cardWidth - scaled(gallery, 24)) * 9 / 16;
  if (layout->previewHeight < scaled(gallery, 96)) {
    layout->previewHeight = scaled(gallery, 96);
  }
  layout->cardHeight =
    layout->previewHeight + scaled(gallery, 66);
  layout->offset = scaled(gallery, 92) - gallery->scroll;
}

static void
card_rect(const GPUGallery *gallery,
          const RECT       *client,
          size_t            index,
          RECT             *outRect) {
  GPUGalleryLayout layout;
  int              column, row;

  gallery_layout(gallery, client, &layout);
  column = (int)(index % (size_t)layout.columns);
  row    = (int)(index / (size_t)layout.columns);

  outRect->left = layout.margin +
                  column * (layout.cardWidth + layout.gap);
  outRect->top    = layout.offset +
                    row * (layout.cardHeight + layout.gap);
  outRect->right  = outRect->left + layout.cardWidth;
  outRect->bottom = outRect->top + layout.cardHeight;
}

static int
content_height(const GPUGallery *gallery, const RECT *client) {
  GPUGalleryLayout layout;
  size_t           rows;

  gallery_layout(gallery, client, &layout);
  rows = (gpuNativeSampleCount + (size_t)layout.columns - 1u) /
         (size_t)layout.columns;
  return scaled(gallery, 92 + 28) +
         (int)rows * layout.cardHeight +
         (rows > 0u ? (int)(rows - 1u) * layout.gap : 0);
}

static void
update_scroll(GPUGallery *gallery) {
  SCROLLINFO info = {0};
  RECT       client;

  if (!gallery || !gallery->window ||
      !GetClientRect(gallery->window, &client)) {
    return;
  }
  gallery->scrollMax = content_height(gallery, &client) - client.bottom;
  if (gallery->scrollMax < 0) {
    gallery->scrollMax = 0;
  }
  if (gallery->scroll > gallery->scrollMax) {
    gallery->scroll = gallery->scrollMax;
  }

  info.cbSize = sizeof(info);
  info.fMask  = SIF_PAGE | SIF_POS | SIF_RANGE;
  info.nMin   = 0;
  info.nMax   = content_height(gallery, &client);
  info.nPage  = (UINT)client.bottom;
  info.nPos   = gallery->scroll;
  SetScrollInfo(gallery->window, SB_VERT, &info, TRUE);
}

static void
set_scroll(GPUGallery *gallery, int value) {
  if (!gallery) {
    return;
  }
  if (value < 0) {
    value = 0;
  } else if (value > gallery->scrollMax) {
    value = gallery->scrollMax;
  }
  if (value == gallery->scroll) {
    return;
  }
  gallery->scroll = value;
  update_scroll(gallery);
  InvalidateRect(gallery->window, NULL, FALSE);
}

static void
draw_preview(HDC                      context,
             const GPUGalleryPreview *preview,
             const RECT              *target) {
  BITMAPINFO bitmap = {0};
  int        sourceX, sourceY, sourceWidth, sourceHeight;
  int        targetX, targetY, targetWidth, targetHeight;
  double     sourceAspect, targetAspect;

  if (!preview || !preview->pixels || preview->width == 0u ||
      preview->height == 0u) {
    return;
  }
  sourceX      = preview->width > 4u ? 2 : 0;
  sourceY      = preview->height > 4u ? 2 : 0;
  sourceWidth  = (int)preview->width - sourceX * 2;
  sourceHeight = (int)preview->height - sourceY * 2;
  targetWidth  = target->right - target->left;
  targetHeight = target->bottom - target->top;
  sourceAspect = (double)sourceWidth / (double)sourceHeight;
  targetAspect = (double)targetWidth / (double)targetHeight;
  if (sourceAspect > targetAspect) {
    targetHeight = (int)((double)targetWidth / sourceAspect + 0.5);
  } else {
    targetWidth = (int)((double)targetHeight * sourceAspect + 0.5);
  }
  targetX = target->left +
            ((target->right - target->left) - targetWidth) / 2;
  targetY = target->top +
            ((target->bottom - target->top) - targetHeight) / 2;

  bitmap.bmiHeader.biSize        = sizeof(bitmap.bmiHeader);
  bitmap.bmiHeader.biWidth       = (LONG)preview->width;
  bitmap.bmiHeader.biHeight      = -(LONG)preview->height;
  bitmap.bmiHeader.biPlanes      = 1u;
  bitmap.bmiHeader.biBitCount    = 32u;
  bitmap.bmiHeader.biCompression = BI_RGB;
  SetStretchBltMode(context, HALFTONE);
  StretchDIBits(context,
                targetX,
                targetY,
                targetWidth,
                targetHeight,
                sourceX,
                sourceY,
                sourceWidth,
                sourceHeight,
                preview->pixels,
                &bitmap,
                DIB_RGB_COLORS,
                SRCCOPY);
}

static void
sample_title(const char *id, char *title, size_t capacity) {
  size_t length;

  if (!id || !title || capacity == 0u) {
    return;
  }
  length = strlen(id);
  if (length >= capacity) {
    length = capacity - 1u;
  }
  memcpy(title, id, length);
  title[length] = '\0';
  if (length > 0u && title[0] >= 'a' && title[0] <= 'z') {
    title[0] = (char)(title[0] - 'a' + 'A');
  }
  for (size_t i = 1u; i < length; i++) {
    if (title[i] == '-') {
      title[i] = ' ';
    }
  }
}

static void
paint_gallery(GPUGallery *gallery, HDC context) {
  HBRUSH background, cardBrush, previewBrush;
  HPEN   borderPen, hoverPen, oldPen;
  HFONT  oldFont;
  RECT   client, header, selector, card, preview, titleRect;

  GetClientRect(gallery->window, &client);
  background   = CreateSolidBrush(rgb(7u, 9u, 13u));
  cardBrush    = CreateSolidBrush(rgb(14u, 18u, 26u));
  previewBrush = CreateSolidBrush(rgb(4u, 9u, 22u));
  borderPen    = CreatePen(PS_SOLID, scaled(gallery, 1),
                           rgb(47u, 52u, 61u));
  hoverPen     = CreatePen(PS_SOLID, scaled(gallery, 2),
                           rgb(255u, 112u, 20u));

  FillRect(context, &client, background);
  SetBkMode(context, TRANSPARENT);
  SetTextColor(context, rgb(243u, 241u, 235u));
  oldFont = SelectObject(context, gallery->titleFont);
  header  = client;
  header.left   += scaled(gallery, 30);
  header.top    += scaled(gallery, 26);
  header.right  -= scaled(gallery, 30);
  header.bottom  = header.top + scaled(gallery, 42);
  DrawTextA(context,
            "GPU | Universal Shading (USL)",
            -1,
            &header,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER);

  SelectObject(context, gallery->bodyFont);
  SetTextColor(context, rgb(145u, 143u, 138u));
  adapter_selector_rect(gallery, &client, &selector);
  header.right = selector.left - scaled(gallery, 14);
  DrawTextA(context,
            gallery->status ? gallery->status : "Direct3D 12",
            -1,
            &header,
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

  for (size_t i = 0u; i < gpuNativeSampleCount; i++) {
    char title[96];

    card_rect(gallery, &client, i, &card);
    if (card.bottom < 0 || card.top > client.bottom) {
      continue;
    }
    oldPen = SelectObject(context,
                          gallery->hovered == (int)i ? hoverPen : borderPen);
    SelectObject(context, cardBrush);
    RoundRect(context,
              card.left,
              card.top,
              card.right,
              card.bottom,
              scaled(gallery, 18),
              scaled(gallery, 18));
    SelectObject(context, oldPen);

    preview = card;
    InflateRect(&preview, -scaled(gallery, 12), -scaled(gallery, 12));
    preview.bottom = preview.top +
                     (preview.right - preview.left) * 9 / 16;
    FillRect(context, &preview, previewBrush);
    if (gallery->previews) {
      draw_preview(context, &gallery->previews[i], &preview);
    }

    titleRect        = card;
    titleRect.left  += scaled(gallery, 14);
    titleRect.right -= scaled(gallery, 14);
    titleRect.top    = preview.bottom + scaled(gallery, 8);
    titleRect.bottom = card.bottom - scaled(gallery, 8);
    sample_title(gpuNativeSamples[i].id, title, sizeof(title));
    SetTextColor(context, rgb(243u, 241u, 235u));
    DrawTextA(context,
              title,
              -1,
              &titleRect,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
  }

  SelectObject(context, oldFont);
  DeleteObject(hoverPen);
  DeleteObject(borderPen);
  DeleteObject(previewBrush);
  DeleteObject(cardBrush);
  DeleteObject(background);
}

static bool
ensure_backbuffer(GPUGallery *gallery,
                  HDC         reference,
                  int         width,
                  int         height) {
  HBITMAP bitmap, replaced;

  if (!gallery || !reference || width <= 0 || height <= 0) {
    return false;
  }
  if (gallery->backContext && gallery->backBitmap &&
      gallery->backWidth == width && gallery->backHeight == height) {
    return true;
  }
  if (!gallery->backContext) {
    gallery->backContext = CreateCompatibleDC(reference);
    if (!gallery->backContext) {
      return false;
    }
  }
  bitmap = CreateCompatibleBitmap(reference, width, height);
  if (!bitmap) {
    return false;
  }
  replaced = SelectObject(gallery->backContext, bitmap);
  if (!gallery->backDefaultBitmap) {
    gallery->backDefaultBitmap = replaced;
  }
  DeleteObject(gallery->backBitmap);
  gallery->backBitmap = bitmap;
  gallery->backWidth  = width;
  gallery->backHeight = height;
  return true;
}

static void
free_backbuffer(GPUGallery *gallery) {
  if (!gallery || !gallery->backContext) {
    return;
  }
  if (gallery->backDefaultBitmap) {
    SelectObject(gallery->backContext, gallery->backDefaultBitmap);
  }
  DeleteObject(gallery->backBitmap);
  DeleteDC(gallery->backContext);
  gallery->backDefaultBitmap = NULL;
  gallery->backBitmap        = NULL;
  gallery->backContext       = NULL;
  gallery->backWidth         = 0;
  gallery->backHeight        = 0;
}

static int
sample_at_point(const GPUGallery *gallery, int x, int y) {
  RECT client, card;
  POINT point;

  if (!gallery || !GetClientRect(gallery->window, &client)) {
    return -1;
  }
  point.x = x;
  point.y = y;
  for (size_t i = 0u; i < gpuNativeSampleCount; i++) {
    card_rect(gallery, &client, i, &card);
    if (PtInRect(&card, point)) {
      return (int)i;
    }
  }
  return -1;
}

static bool
working_directory(const char *executable,
                  char        directory[MAX_PATH]) {
  char *slash;
  int   length;

  if (!executable || !directory) {
    return false;
  }
  length = snprintf(directory, MAX_PATH, "%s", executable);
  if (length <= 0 || length >= MAX_PATH) {
    return false;
  }
  slash = strrchr(directory, '\\');
  if (!slash) {
    slash = strrchr(directory, '/');
  }
  if (!slash) {
    return false;
  }
  *slash = '\0';
  return true;
}

static void
adapter_environment_value(const GPUGallery *gallery,
                          char              value[32]) {
  uint32_t selection;

  selection = gallery ? gallery->adapterSelection : 0u;
  switch (selection) {
    case GPU_GALLERY_ADAPTER_LOW_POWER:
      snprintf(value, 32u, "low");
      break;
    case GPU_GALLERY_ADAPTER_HIGH_POWER:
      snprintf(value, 32u, "high");
      break;
    case GPU_GALLERY_ADAPTER_AUTO:
      snprintf(value, 32u, "auto");
      break;
    default:
      snprintf(value,
               32u,
               "index:%u",
               selection - GPU_GALLERY_ADAPTER_FIRST);
      break;
  }
}

static char*
save_environment_value(const char *name) {
  char  *value;
  DWORD  length;

  length = GetEnvironmentVariableA(name, NULL, 0u);
  if (length == 0u || !(value = malloc(length))) {
    return NULL;
  }
  if (GetEnvironmentVariableA(name, value, length) == 0u) {
    free(value);
    return NULL;
  }
  return value;
}

static void
start_sample(GPUGallery *gallery, size_t index) {
  STARTUPINFOA        startup = {0};
  PROCESS_INFORMATION process = {0};
  char                command[MAX_PATH * 2u];
  char                directory[MAX_PATH];
  char                adapterValue[32];
  char               *previousAdapterValue;
  int                 length;
  BOOL                created;

  if (!gallery || gallery->child || index >= gpuNativeSampleCount ||
      !working_directory(gpuNativeSamples[index].executable, directory)) {
    return;
  }
  length = snprintf(command,
                    sizeof(command),
                    "\"%s\"",
                    gpuNativeSamples[index].executable);
  if (length <= 0 || (size_t)length >= sizeof(command)) {
    gallery->status = "Sample path is too long";
    InvalidateRect(gallery->window, NULL, FALSE);
    return;
  }

  startup.cb = sizeof(startup);
  adapter_environment_value(gallery, adapterValue);
  previousAdapterValue = save_environment_value("GPU_SAMPLE_ADAPTER");
  if (!SetEnvironmentVariableA("GPU_SAMPLE_ADAPTER", adapterValue)) {
    free(previousAdapterValue);
    gallery->status = "Adapter selection failed";
    InvalidateRect(gallery->window, NULL, FALSE);
    return;
  }
  created = CreateProcessA(gpuNativeSamples[index].executable,
                           command,
                           NULL,
                           NULL,
                           FALSE,
                           0u,
                           NULL,
                           directory,
                           &startup,
                           &process);
  SetEnvironmentVariableA("GPU_SAMPLE_ADAPTER", previousAdapterValue);
  free(previousAdapterValue);
  if (!created) {
    gallery->status = "Sample launch failed";
    InvalidateRect(gallery->window, NULL, FALSE);
    return;
  }
  CloseHandle(process.hThread);
  gallery->child  = process.hProcess;
  gallery->status = gpuNativeSamples[index].id;
  if (gallery->job) {
    AssignProcessToJobObject(gallery->job, process.hProcess);
  }
  EnableWindow(gallery->window, FALSE);
  SetTimer(gallery->window, GPU_GALLERY_TIMER_CHILD, 100u, NULL);
  InvalidateRect(gallery->window, NULL, FALSE);
}

static void
poll_child(GPUGallery *gallery) {
  DWORD exitCode;

  if (!gallery || !gallery->child ||
      WaitForSingleObject(gallery->child, 0u) != WAIT_OBJECT_0) {
    return;
  }
  exitCode = 1u;
  GetExitCodeProcess(gallery->child, &exitCode);
  CloseHandle(gallery->child);
  gallery->child  = NULL;
  gallery->status = exitCode == 0u ? "Direct3D 12" : "Sample failed";
  KillTimer(gallery->window, GPU_GALLERY_TIMER_CHILD);
  EnableWindow(gallery->window, TRUE);
  SetForegroundWindow(gallery->window);
  InvalidateRect(gallery->window, NULL, FALSE);
}

static LRESULT CALLBACK
window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  GPUGallery *gallery;

  gallery = (GPUGallery *)GetWindowLongPtrW(window, GWLP_USERDATA);
  if (message == WM_NCCREATE) {
    CREATESTRUCTW *create;

    create  = (CREATESTRUCTW *)lParam;
    gallery = create->lpCreateParams;
    gallery->window = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)gallery);
  }
  if (!gallery) {
    return DefWindowProcW(window, message, wParam, lParam);
  }

  switch (message) {
    case WM_CREATE: {
      BOOL darkMode;

      darkMode = TRUE;
      DwmSetWindowAttribute(window,
                            DWMWA_USE_IMMERSIVE_DARK_MODE,
                            &darkMode,
                            sizeof(darkMode));
      SetWindowTheme(window, L"DarkMode_Explorer", NULL);
      create_adapter_selector(gallery);
      load_previews(gallery);
      update_scroll(gallery);
      return 0;
    }
    case WM_SIZE:
      position_adapter_selector(gallery);
      update_scroll(gallery);
      InvalidateRect(window, NULL, FALSE);
      return 0;
    case WM_DPICHANGED: {
      const RECT *suggested;

      suggested = (const RECT *)lParam;
      SetWindowPos(window,
                   NULL,
                   suggested->left,
                   suggested->top,
                   suggested->right - suggested->left,
                   suggested->bottom - suggested->top,
                   SWP_NOACTIVATE | SWP_NOZORDER);
      gallery->scale = display_scale(window, HIWORD(wParam));
      create_fonts(gallery);
      position_adapter_selector(gallery);
      update_scroll(gallery);
      InvalidateRect(window, NULL, FALSE);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wParam) == GPU_GALLERY_ADAPTER_SELECTOR_ID &&
          HIWORD(wParam) == CBN_SELCHANGE) {
        LRESULT item, selection;

        item = SendMessageW(gallery->adapterSelector,
                            CB_GETCURSEL,
                            0u,
                            0u);
        selection = item == CB_ERR
                      ? CB_ERR
                      : SendMessageW(gallery->adapterSelector,
                                     CB_GETITEMDATA,
                                     (WPARAM)item,
                                     0u);
        if (selection != CB_ERR) {
          gallery->adapterSelection = (uint32_t)selection;
        }
        return 0;
      }
      return DefWindowProcW(window, message, wParam, lParam);
    case GPU_GALLERY_ADAPTERS_READY: {
      GPUGalleryAdapters *adapters;

      adapters = (GPUGalleryAdapters *)lParam;
      if (adapters) {
        for (uint32_t i = 0u; i < adapters->count; i++) {
          add_adapter_option(gallery->adapterSelector,
                             adapters->labels[i],
                             GPU_GALLERY_ADAPTER_FIRST +
                               adapters->indices[i]);
        }
        free(adapters->labels);
        free(adapters->indices);
        free(adapters);
      }
      return 0;
    }
    case WM_VSCROLL: {
      SCROLLINFO info = {0};
      int        value;

      info.cbSize = sizeof(info);
      info.fMask  = SIF_ALL;
      GetScrollInfo(window, SB_VERT, &info);
      value = gallery->scroll;
      switch (LOWORD(wParam)) {
        case SB_LINEUP:
          value -= scaled(gallery, 42);
          break;
        case SB_LINEDOWN:
          value += scaled(gallery, 42);
          break;
        case SB_PAGEUP:
          value -= (int)info.nPage;
          break;
        case SB_PAGEDOWN:
          value += (int)info.nPage;
          break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
          value = info.nTrackPos;
          break;
        default:
          break;
      }
      set_scroll(gallery, value);
      return 0;
    }
    case WM_MOUSEWHEEL:
      set_scroll(gallery,
                 gallery->scroll -
                   GET_WHEEL_DELTA_WPARAM(wParam) *
                     scaled(gallery, 84) / WHEEL_DELTA);
      return 0;
    case WM_MOUSEMOVE: {
      TRACKMOUSEEVENT tracking = {0};
      int             hovered;

      hovered = sample_at_point(gallery,
                                GET_X_LPARAM(lParam),
                                GET_Y_LPARAM(lParam));
      if (hovered != gallery->hovered) {
        gallery->hovered = hovered;
        InvalidateRect(window, NULL, FALSE);
      }
      tracking.cbSize      = sizeof(tracking);
      tracking.dwFlags     = TME_LEAVE;
      tracking.hwndTrack   = window;
      tracking.dwHoverTime = HOVER_DEFAULT;
      TrackMouseEvent(&tracking);
      return 0;
    }
    case WM_MOUSELEAVE:
      gallery->hovered = -1;
      InvalidateRect(window, NULL, FALSE);
      return 0;
    case WM_LBUTTONUP: {
      int index;

      index = sample_at_point(gallery,
                              GET_X_LPARAM(lParam),
                              GET_Y_LPARAM(lParam));
      if (index >= 0) {
        start_sample(gallery, (size_t)index);
      }
      return 0;
    }
    case WM_TIMER:
      if (wParam == GPU_GALLERY_TIMER_CHILD) {
        poll_child(gallery);
      }
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint;
      HDC         context;
      RECT        client;

      context = BeginPaint(window, &paint);
      GetClientRect(window, &client);
      if (ensure_backbuffer(gallery,
                            context,
                            client.right,
                            client.bottom)) {
        paint_gallery(gallery, gallery->backContext);
        BitBlt(context,
               0,
               0,
               client.right,
               client.bottom,
               gallery->backContext,
               0,
               0,
               SRCCOPY);
      } else {
        paint_gallery(gallery, context);
      }
      EndPaint(window, &paint);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_KEYDOWN:
      if (wParam == VK_ESCAPE && !gallery->child) {
        DestroyWindow(window);
      }
      return 0;
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

static HANDLE
create_child_job(void) {
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
  HANDLE                               job;

  job = CreateJobObjectW(NULL, NULL);
  if (!job) {
    return NULL;
  }
  limits.BasicLimitInformation.LimitFlags =
    JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job,
                               JobObjectExtendedLimitInformation,
                               &limits,
                               sizeof(limits))) {
    CloseHandle(job);
    return NULL;
  }
  return job;
}

static void
initial_window_bounds(DWORD style, RECT *bounds) {
  RECT work;
  UINT dpi;
  int  width, height;

  if (!bounds) {
    return;
  }
  if (!monitor_work_area(NULL, &work)) {
    work.left   = 0;
    work.top    = 0;
    work.right  = GetSystemMetrics(SM_CXSCREEN);
    work.bottom = GetSystemMetrics(SM_CYSCREEN);
  }
  dpi            = GetDpiForSystem();
  bounds->left   = 0;
  bounds->top    = 0;
  bounds->right  = (work.right - work.left) * 84 / 100;
  bounds->bottom = (work.bottom - work.top) * 86 / 100;
  if (!AdjustWindowRectExForDpi(bounds, style, FALSE, 0u, dpi)) {
    AdjustWindowRectEx(bounds, style, FALSE, 0u);
  }
  width          = bounds->right - bounds->left;
  height         = bounds->bottom - bounds->top;
  bounds->left   = work.left + ((work.right - work.left) - width) / 2;
  bounds->top    = work.top + ((work.bottom - work.top) - height) / 2;
  bounds->right  = bounds->left + width;
  bounds->bottom = bounds->top + height;
}

int WINAPI
wWinMain(HINSTANCE instance,
         HINSTANCE previousInstance,
         wchar_t  *commandLine,
         int       showCommand) {
  static const wchar_t className[] = L"GPUUSLGalleryWindow";
  WNDCLASSEXW          windowClass = {0};
  GPUGallery           gallery = {0};
  RECT                 bounds;
  HWND                 window;
  MSG                  message;
  DWORD                style;
  UINT                 dpi;

  (void)previousInstance;
  (void)commandLine;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  dpi             = GetDpiForSystem();
  gallery.scale   = display_scale(NULL, dpi);
  gallery.hovered = -1;
  gallery.status  = "Direct3D 12";
  gallery.adapterSelection = GPU_GALLERY_ADAPTER_AUTO;
  gallery.job     = create_child_job();
  if (!create_fonts(&gallery)) {
    if (gallery.job) {
      CloseHandle(gallery.job);
    }
    return 1;
  }

  windowClass.cbSize        = sizeof(windowClass);
  windowClass.style         = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc   = window_proc;
  windowClass.hInstance     = instance;
  windowClass.hCursor       = LoadCursorW(NULL,
                                         MAKEINTRESOURCEW(32512));
  windowClass.hbrBackground = NULL;
  windowClass.lpszClassName = className;
  if (!RegisterClassExW(&windowClass) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    if (gallery.job) {
      CloseHandle(gallery.job);
    }
    DeleteObject(gallery.bodyFont);
    DeleteObject(gallery.titleFont);
    return 1;
  }

  style = WS_OVERLAPPEDWINDOW | WS_VSCROLL;
  initial_window_bounds(style, &bounds);
  window = CreateWindowExW(0u,
                           className,
                           L"GPU + USL Samples",
                           style,
                           bounds.left,
                           bounds.top,
                           bounds.right - bounds.left,
                           bounds.bottom - bounds.top,
                           NULL,
                           NULL,
                           instance,
                           &gallery);
  if (!window) {
    if (gallery.job) {
      CloseHandle(gallery.job);
    }
    DeleteObject(gallery.bodyFont);
    DeleteObject(gallery.titleFont);
    return 1;
  }
  gallery.scale = display_scale(window, GetDpiForWindow(window));
  create_fonts(&gallery);
  update_scroll(&gallery);
  ShowWindow(window, showCommand);
  UpdateWindow(window);
  start_adapter_discovery(window);

  while (GetMessageW(&message, NULL, 0u, 0u) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  if (gallery.child) {
    CloseHandle(gallery.child);
  }
  if (gallery.job) {
    CloseHandle(gallery.job);
  }
  free_backbuffer(&gallery);
  free_previews(&gallery);
  DeleteObject(gallery.bodyFont);
  DeleteObject(gallery.titleFont);
  return 0;
}
