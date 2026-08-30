#define WIN32_LEAN_AND_MEAN

#include "../../common/win32.h"
#include "../../common/sample_orbit.h"

#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

#include <process.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern int
gpu_win32_sample_start(void);

#ifndef GPU_WINDOWS_SAMPLE_NAME
#  define GPU_WINDOWS_SAMPLE_NAME "GPU + USL Sample"
#endif

enum {
  GPU_WIN32_SAMPLE_READY = WM_APP + 1u
};

typedef struct GPUWin32Host {
  GPUWin32Window  window;
  GPUWin32Sample *sample;
  GPUWin32Sample *startupSample;
  HANDLE          startupThread;
  double          startupStart;
  uint32_t        pendingWidth;
  uint32_t        pendingHeight;
  float           pendingScale;
  int             result;
  bool            startupLog;
  bool            firstFrame;
  bool            running;
} GPUWin32Host;

static void
startup_mark(const GPUWin32Host *host, const char *phase) {
  if (!host || !host->startupLog || !phase) {
    return;
  }
  fprintf(stderr,
          "GPU sample host: %-12s %8.3f ms\n",
          phase,
          gpu_win32_get_now() - host->startupStart);
}

static unsigned __stdcall
start_sample(void *userData) {
  GPUWin32Host *host;

  host = userData;
  host->startupSample = GPUSampleWin32Create(&host->window,
                                             GPU_WINDOWS_SAMPLE_NAME,
                                             gpu_win32_sample_start);
  PostMessageW(host->window.handle, GPU_WIN32_SAMPLE_READY, 0u, 0);
  return 0u;
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

static void
initial_window_bounds(DWORD style, RECT *bounds) {
  RECT work;
  UINT dpi;
  int  clientWidth, clientHeight, maxHeight;
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
  clientWidth  = (work.right - work.left) * 72 / 100;
  clientHeight = clientWidth * 10 / 16;
  maxHeight    = (work.bottom - work.top) * 78 / 100;
  if (clientHeight > maxHeight) {
    clientHeight = maxHeight;
    clientWidth  = clientHeight * 16 / 10;
  }

  dpi            = GetDpiForSystem();
  bounds->left   = 0;
  bounds->top    = 0;
  bounds->right  = clientWidth;
  bounds->bottom = clientHeight;
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
        host->pendingWidth  = LOWORD(lParam);
        host->pendingHeight = HIWORD(lParam);
        if (host->sample) {
          host->window.width  = host->pendingWidth;
          host->window.height = host->pendingHeight;
        }
      }
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
      host->pendingScale = display_scale(window, HIWORD(wParam));
      if (host->sample) {
        host->window.scale = host->pendingScale;
      }
      return 0;
    }
    case WM_LBUTTONDOWN:
      if (!host->sample) {
        return 0;
      }
      SetCapture(window);
      sample_orbit_pointer_begin((float)GET_X_LPARAM(lParam),
                                 (float)GET_Y_LPARAM(lParam));
      return 0;
    case WM_LBUTTONUP:
      if (!host->sample) {
        return 0;
      }
      sample_orbit_pointer_end();
      ReleaseCapture();
      return 0;
    case WM_MOUSEMOVE:
      if (host->sample && (wParam & MK_LBUTTON) != 0u) {
        sample_orbit_pointer_move((float)GET_X_LPARAM(lParam),
                                  (float)GET_Y_LPARAM(lParam));
      }
      return 0;
    case WM_MOUSEWHEEL:
      if (!host->sample) {
        return 0;
      }
      sample_orbit_zoom((float)GET_WHEEL_DELTA_WPARAM(wParam) /
                        (float)WHEEL_DELTA);
      return 0;
    case WM_PAINT:
      if (!host->sample) {
        PAINTSTRUCT paint;
        HDC         context;
        RECT        client;

        context = BeginPaint(window, &paint);
        GetClientRect(window, &client);
        FillRect(context, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetBkMode(context, TRANSPARENT);
        SetTextColor(context, RGB(210u, 210u, 210u));
        SelectObject(context, GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(context,
                  L"Starting Direct3D 12...",
                  -1,
                  &client,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        EndPaint(window, &paint);
        return 0;
      }
      return DefWindowProcW(window, message, wParam, lParam);
    case GPU_WIN32_SAMPLE_READY:
      if (host->startupThread) {
        WaitForSingleObject(host->startupThread, INFINITE);
        CloseHandle(host->startupThread);
        host->startupThread = NULL;
      }
      host->sample        = host->startupSample;
      host->window.width  = host->pendingWidth;
      host->window.height = host->pendingHeight;
      host->window.scale  = host->pendingScale;
      startup_mark(host, "gpu-ready");
      if (!host->sample || GPUSampleWin32Failed(host->sample)) {
        host->result = 1;
        MessageBoxA(window,
                    GPUSampleWin32Status(host->sample),
                    GPU_WINDOWS_SAMPLE_NAME,
                    MB_ICONERROR | MB_OK);
        DestroyWindow(window);
      }
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
  RECT                 client;
  HWND                 window;
  MSG                  message;
  uintptr_t            startupThread;
  DWORD                style;

  (void)previousInstance;
  (void)commandLine;
  host.startupLog   = getenv("GPU_SAMPLE_STARTUP_LOG") != NULL;
  host.startupStart = host.startupLog ? gpu_win32_get_now() : 0.0;
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

  style = WS_OVERLAPPEDWINDOW;
  initial_window_bounds(style, &bounds);
  window = CreateWindowExW(0u,
                           className,
                           L"GPU + USL Sample",
                           style,
                           bounds.left,
                           bounds.top,
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
  {
    BOOL darkMode;

    darkMode = TRUE;
    DwmSetWindowAttribute(window,
                          DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &darkMode,
                          sizeof(darkMode));
  }
  GetClientRect(window, &client);
  host.window.width  = (uint32_t)client.right;
  host.window.height = (uint32_t)client.bottom;
  host.window.scale  = display_scale(window, GetDpiForWindow(window));
  host.pendingWidth  = host.window.width;
  host.pendingHeight = host.window.height;
  host.pendingScale  = host.window.scale;
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
  startup_mark(&host, "window-shown");

  startupThread = _beginthreadex(NULL,
                                 0u,
                                 start_sample,
                                 &host,
                                 0u,
                                 NULL);
  if (!startupThread) {
    MessageBoxA(window,
                "GPU: failed to start Direct3D 12 initialization",
                GPU_WINDOWS_SAMPLE_NAME,
                MB_ICONERROR | MB_OK);
    DestroyWindow(window);
    return 1;
  }
  host.startupThread = (HANDLE)startupThread;

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
    if (!host.sample) {
      WaitMessage();
      continue;
    }
    if (!GPUSampleWin32Render(host.sample)) {
      host.result = GPUSampleWin32Failed(host.sample) ? 1 : 0;
      if (host.result != 0) {
        MessageBoxA(window,
                    GPUSampleWin32Status(host.sample),
                    GPU_WINDOWS_SAMPLE_NAME,
                    MB_ICONERROR | MB_OK);
      }
      DestroyWindow(window);
    } else if (host.startupLog && !host.firstFrame) {
      host.firstFrame = true;
      startup_mark(&host, "first-frame");
    }
  }

  if (host.startupThread) {
    WaitForSingleObject(host.startupThread, INFINITE);
    CloseHandle(host.startupThread);
  }
  GPUSampleWin32Stop(host.sample ? host.sample : host.startupSample);
  return host.result;
}
