#define WIN32_LEAN_AND_MEAN

#include "../../common/win32.h"
#include "../../common/sample_orbit.h"

#include <windows.h>
#include <windowsx.h>

#include <stdbool.h>
#include <stdint.h>

extern int
gpu_win32_sample_start(void);

#ifndef GPU_WINDOWS_SAMPLE_NAME
#  define GPU_WINDOWS_SAMPLE_NAME "GPU + USL Sample"
#endif

typedef struct GPUWin32Host {
  GPUWin32Window window;
  GPUWin32Sample *sample;
  bool            running;
} GPUWin32Host;

static LRESULT CALLBACK
window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  GPUWin32Host *host;

  host = (GPUWin32Host *)GetWindowLongPtrW(window, GWLP_USERDATA);
  if (message == WM_NCCREATE) {
    CREATESTRUCTW *create;

    create = (CREATESTRUCTW *)lParam;
    host   = create->lpCreateParams;
    SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)host);
  }
  if (!host) {
    return DefWindowProcW(window, message, wParam, lParam);
  }

  switch (message) {
    case WM_SIZE:
      if (wParam != SIZE_MINIMIZED) {
        host->window.width  = LOWORD(lParam);
        host->window.height = HIWORD(lParam);
      }
      return 0;
    case WM_LBUTTONDOWN:
      SetCapture(window);
      sample_orbit_pointer_begin((float)GET_X_LPARAM(lParam),
                                 (float)GET_Y_LPARAM(lParam));
      return 0;
    case WM_LBUTTONUP:
      sample_orbit_pointer_end();
      ReleaseCapture();
      return 0;
    case WM_MOUSEMOVE:
      if ((wParam & MK_LBUTTON) != 0u) {
        sample_orbit_pointer_move((float)GET_X_LPARAM(lParam),
                                  (float)GET_Y_LPARAM(lParam));
      }
      return 0;
    case WM_MOUSEWHEEL:
      sample_orbit_zoom((float)GET_WHEEL_DELTA_WPARAM(wParam) /
                        (float)WHEEL_DELTA);
      return 0;
    case WM_KEYDOWN:
      if (wParam == VK_ESCAPE) {
        DestroyWindow(window);
      }
      return 0;
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      host->running = false;
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

int WINAPI
wWinMain(HINSTANCE instance,
         HINSTANCE previousInstance,
         wchar_t  *commandLine,
         int       showCommand) {
  static const wchar_t className[] = L"GPUUSLSampleWindow";
  WNDCLASSEXW          windowClass = {0};
  GPUWin32Host         host = {0};
  RECT                 bounds;
  HWND                 window;
  MSG                  message;
  int                  result;

  (void)previousInstance;
  (void)commandLine;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  windowClass.cbSize        = sizeof(windowClass);
  windowClass.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
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
  bounds.right  = 1120;
  bounds.bottom = 720;
  AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0u);
  window = CreateWindowExW(0u,
                           className,
                           L"GPU + USL Sample",
                           WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT,
                           CW_USEDEFAULT,
                           bounds.right - bounds.left,
                           bounds.bottom - bounds.top,
                           NULL,
                           NULL,
                           instance,
                           &host);
  if (!window) {
    return 1;
  }

  host.window.handle = window;
  host.window.width  = 1120u;
  host.window.height = 720u;
  host.window.scale  = (float)GetDpiForWindow(window) / 96.0f;
  host.running       = true;

  {
    wchar_t title[256];

    if (MultiByteToWideChar(CP_UTF8,
                            0u,
                            GPU_WINDOWS_SAMPLE_NAME,
                            -1,
                            title,
                            GPU_ARRAY_LEN(title)) > 0) {
      SetWindowTextW(window, title);
    }
  }
  ShowWindow(window, showCommand);
  UpdateWindow(window);

  host.sample = GPUSampleWin32Create(&host.window,
                                     GPU_WINDOWS_SAMPLE_NAME,
                                     gpu_win32_sample_start);
  if (!host.sample || GPUSampleWin32Failed(host.sample)) {
    MessageBoxA(window,
                GPUSampleWin32Status(host.sample),
                GPU_WINDOWS_SAMPLE_NAME,
                MB_ICONERROR | MB_OK);
    DestroyWindow(window);
    return 1;
  }

  result = 0;
  while (host.running) {
    while (PeekMessageW(&message, NULL, 0u, 0u, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        host.running = false;
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (!host.running) {
      break;
    }
    if (!GPUSampleWin32Render(host.sample)) {
      result = GPUSampleWin32Failed(host.sample) ? 1 : 0;
      if (result != 0) {
        MessageBoxA(window,
                    GPUSampleWin32Status(host.sample),
                    GPU_WINDOWS_SAMPLE_NAME,
                    MB_ICONERROR | MB_OK);
      }
      DestroyWindow(window);
    }
  }

  GPUSampleWin32Stop(host.sample);
  return result;
}
