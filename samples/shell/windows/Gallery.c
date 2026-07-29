#define WIN32_LEAN_AND_MEAN

#include "NativeSamples.h"
#include "../../common/Win32Image.h"

#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  GPU_GALLERY_TIMER_CHILD = 1u
};

typedef struct GPUGalleryPreview {
  uint8_t  *pixels;
  uint32_t  width;
  uint32_t  height;
} GPUGalleryPreview;

typedef struct GPUGallery {
  HWND               window;
  HANDLE             child;
  HANDLE             job;
  HFONT              titleFont;
  HFONT              bodyFont;
  GPUGalleryPreview *previews;
  const char        *status;
  int                scroll;
  int                scrollMax;
  int                hovered;
  float              scale;
} GPUGallery;

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

static void
card_rect(const GPUGallery *gallery,
          const RECT       *client,
          size_t            index,
          RECT             *outRect) {
  int margin, gap, cardWidth, cardHeight;
  int column, row, available, offset;

  margin     = scaled(gallery, 28);
  gap        = scaled(gallery, 18);
  cardHeight = scaled(gallery, 278);
  available  = client->right - client->left - margin * 2 - gap * 2;
  cardWidth  = available / 3;
  column     = (int)(index % 3u);
  row        = (int)(index / 3u);
  offset     = scaled(gallery, 92) - gallery->scroll;

  outRect->left   = margin + column * (cardWidth + gap);
  outRect->top    = offset + row * (cardHeight + gap);
  outRect->right  = outRect->left + cardWidth;
  outRect->bottom = outRect->top + cardHeight;
}

static int
content_height(const GPUGallery *gallery) {
  size_t rows;

  rows = (gpuNativeSampleCount + 2u) / 3u;
  return scaled(gallery, 92 + 28) +
         (int)rows * scaled(gallery, 278) +
         (rows > 0u ? (int)(rows - 1u) * scaled(gallery, 18) : 0);
}

static void
update_scroll(GPUGallery *gallery) {
  SCROLLINFO info = {0};
  RECT       client;

  if (!gallery || !gallery->window ||
      !GetClientRect(gallery->window, &client)) {
    return;
  }
  gallery->scrollMax = content_height(gallery) - client.bottom;
  if (gallery->scrollMax < 0) {
    gallery->scrollMax = 0;
  }
  if (gallery->scroll > gallery->scrollMax) {
    gallery->scroll = gallery->scrollMax;
  }

  info.cbSize = sizeof(info);
  info.fMask  = SIF_PAGE | SIF_POS | SIF_RANGE;
  info.nMin   = 0;
  info.nMax   = content_height(gallery);
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
  RECT   client, header, card, preview, titleRect;

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
    preview.bottom = preview.top + scaled(gallery, 212);
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
start_sample(GPUGallery *gallery, size_t index) {
  STARTUPINFOA        startup = {0};
  PROCESS_INFORMATION process = {0};
  char                command[MAX_PATH * 2u];
  char                directory[MAX_PATH];
  int                 length;

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
  if (!CreateProcessA(gpuNativeSamples[index].executable,
                      command,
                      NULL,
                      NULL,
                      FALSE,
                      0u,
                      NULL,
                      directory,
                      &startup,
                      &process)) {
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
  gallery->status = exitCode == 0u ? "Sample closed" : "Sample failed";
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
      load_previews(gallery);
      update_scroll(gallery);
      return 0;
    }
    case WM_SIZE:
      update_scroll(gallery);
      InvalidateRect(window, NULL, FALSE);
      return 0;
    case WM_DPICHANGED: {
      const RECT *suggested;

      gallery->scale = (float)HIWORD(wParam) / 96.0f;
      suggested      = (const RECT *)lParam;
      SetWindowPos(window,
                   NULL,
                   suggested->left,
                   suggested->top,
                   suggested->right - suggested->left,
                   suggested->bottom - suggested->top,
                   SWP_NOACTIVATE | SWP_NOZORDER);
      update_scroll(gallery);
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

      context = BeginPaint(window, &paint);
      paint_gallery(gallery, context);
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

  (void)previousInstance;
  (void)commandLine;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  gallery.scale   = 1.0f;
  gallery.hovered = -1;
  gallery.status  = "Direct3D 12";
  gallery.job     = create_child_job();
  gallery.titleFont = CreateFontW(-28,
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
  gallery.bodyFont = CreateFontW(-18,
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

  windowClass.cbSize        = sizeof(windowClass);
  windowClass.style         = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc   = window_proc;
  windowClass.hInstance     = instance;
  windowClass.hCursor       = LoadCursorW(NULL,
                                         MAKEINTRESOURCEW(32512));
  windowClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  windowClass.lpszClassName = className;
  if (!RegisterClassExW(&windowClass) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return 1;
  }

  bounds.left   = 0;
  bounds.top    = 0;
  bounds.right  = 1240;
  bounds.bottom = 840;
  AdjustWindowRectEx(&bounds,
                     WS_OVERLAPPEDWINDOW | WS_VSCROLL,
                     FALSE,
                     0u);
  window = CreateWindowExW(0u,
                           className,
                           L"GPU + USL Samples",
                           WS_OVERLAPPEDWINDOW | WS_VSCROLL,
                           CW_USEDEFAULT,
                           CW_USEDEFAULT,
                           bounds.right - bounds.left,
                           bounds.bottom - bounds.top,
                           NULL,
                           NULL,
                           instance,
                           &gallery);
  if (!window) {
    return 1;
  }
  gallery.scale = (float)GetDpiForWindow(window) / 96.0f;
  update_scroll(&gallery);
  ShowWindow(window, showCommand);
  UpdateWindow(window);

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
  free_previews(&gallery);
  DeleteObject(gallery.bodyFont);
  DeleteObject(gallery.titleFont);
  return 0;
}
