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

#include "../common.h"
#include "pacer_internal.h"
#include "surface_internal.h"
#include "swapchain_internal.h"

#if defined(_WIN32) || defined(WIN32)
#ifndef COBJMACROS
#  define COBJMACROS
#endif
#include <windows.h>
#include <dxgi.h>
#include <dwmapi.h>

/* Vulkan, DX12 and WebGPU on Windows: a thread waits for each refresh and
 * posts it to a message-only window of the creating thread, whose message loop
 * ticks the pacer (as a run loop does on Apple). The refresh comes from the
 * compositor clock (DCompositionWaitForCompositorClock, Windows 11: the stop
 * event wakes it at once), else the window's output's vblank
 * (IDXGIOutput::WaitForVBlank), else DwmFlush; DWM's timing info gives the
 * refresh's time and period on the performance counter. One post is in
 * flight at a time: a busy thread gets the latest refresh, not a backlog. */

#define GPU_PACER_MESSAGE (WM_APP + 0x47u)

/* IDXGIFactory1, here so no import library is needed */
static const GUID pacer__factoryIID = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};

typedef HRESULT (WINAPI *PacerDwmTimingFn)(HWND, DWM_TIMING_INFO *);
typedef HRESULT (WINAPI *PacerDwmFlushFn)(void);
typedef DWORD   (WINAPI *PacerCompositorClockFn)(UINT, const HANDLE *, DWORD);
typedef HRESULT (WINAPI *PacerDXGIFactoryFn)(REFIID, void **);

static struct {
  PacerDwmTimingFn       timing;
  PacerDwmFlushFn        flush;
  PacerCompositorClockFn compositorClock;
  PacerDXGIFactoryFn     createFactory;
  double                 qpcSeconds;   /* seconds per performance-counter tick */
  volatile LONG          state;        /* 0, 1 loading, 2 loaded */
  ATOM                   windowClass;
} pacer__win;

struct GPUPacerClock {
  GPUPacer     *pacer;
  HWND          window;       /* message-only, the creating thread's */
  HWND          target;       /* the swapchain's window, NULL for a CoreWindow */
  HANDLE        thread;
  HANDLE        stop;         /* manual reset */
  HANDLE        resume;       /* manual reset: set while ticks are wanted */
  IDXGIOutput  *output;       /* the waiter's own */
  HMONITOR      monitor;
  volatile LONG running;
  volatile LONG posted;
  volatile LONG64 vsyncTicks; /* the last refresh, performance-counter ticks */
  volatile LONG64 periodTicks;
};

static double
pacer__seconds(LONG64 ticks) {
  return (double)ticks * pacer__win.qpcSeconds;
}

static void
pacer__load(void) {
  LARGE_INTEGER frequency;
  HMODULE       dwm, dcomp, dxgi;

  if (InterlockedCompareExchange(&pacer__win.state, 1, 0) != 0) {
    while (pacer__win.state != 2)
      Sleep(0);
    return;
  }

  if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
    pacer__win.qpcSeconds = 1.0 / (double)frequency.QuadPart;

  if ((dwm = LoadLibraryW(L"dwmapi.dll"))) {
    pacer__win.timing = (PacerDwmTimingFn)(void (*)(void))GetProcAddress(dwm, "DwmGetCompositionTimingInfo");
    pacer__win.flush  = (PacerDwmFlushFn)(void (*)(void))GetProcAddress(dwm, "DwmFlush");
  }

  if ((dcomp = LoadLibraryW(L"dcomp.dll")))
    pacer__win.compositorClock = (PacerCompositorClockFn)(void (*)(void))
      GetProcAddress(dcomp, "DCompositionWaitForCompositorClock");

  if ((dxgi = LoadLibraryW(L"dxgi.dll")))
    pacer__win.createFactory = (PacerDXGIFactoryFn)(void (*)(void))GetProcAddress(dxgi, "CreateDXGIFactory1");

  InterlockedExchange(&pacer__win.state, 2);
}

/* the output the swapchain's window is on, for its vblank */
static void
pacer__findOutput(GPUPacerClock *clock) {
  IDXGIFactory1 *factory;
  IDXGIAdapter1 *adapter;
  IDXGIOutput   *output;
  DXGI_OUTPUT_DESC desc;
  HMONITOR       monitor;
  UINT           a, o;

  if (!clock->target || !pacer__win.createFactory)
    return;

  monitor = MonitorFromWindow(clock->target, MONITOR_DEFAULTTONEAREST);
  if (monitor == clock->monitor && clock->output)
    return;

  if (clock->output) {
    IDXGIOutput_Release(clock->output);
    clock->output = NULL;
  }

  clock->monitor = monitor;
  factory        = NULL;
  if (FAILED(pacer__win.createFactory(&pacer__factoryIID, (void **)&factory)) || !factory)
    return;

  for (a = 0u; !clock->output && IDXGIFactory1_EnumAdapters1(factory, a, &adapter) == S_OK; a++) {
    for (o = 0u; !clock->output && IDXGIAdapter1_EnumOutputs(adapter, o, &output) == S_OK; o++) {
      if (SUCCEEDED(IDXGIOutput_GetDesc(output, &desc)) && desc.Monitor == monitor)
        clock->output = output;
      else
        IDXGIOutput_Release(output);
    }

    IDXGIAdapter1_Release(adapter);
  }

  IDXGIFactory1_Release(factory);
}

/* true at a refresh, false when told to stop */
static bool
pacer__waitRefresh(GPUPacerClock *clock, uint32_t *count) {
  DWORD result;

  if (pacer__win.compositorClock) {
    result = pacer__win.compositorClock(1u, &clock->stop, INFINITE);
    if (result == WAIT_OBJECT_0)
      return false;

    if (result == WAIT_OBJECT_0 + 1u)
      return true;
  }

  if (WaitForSingleObject(clock->stop, 0) == WAIT_OBJECT_0)
    return false;

  /* the window may have moved to another display */
  if (clock->target && ((*count)++ & 63u) == 0u)
    pacer__findOutput(clock);

  if (clock->output && SUCCEEDED(IDXGIOutput_WaitForVBlank(clock->output)))
    return true;

  if (pacer__win.flush && SUCCEEDED(pacer__win.flush()))
    return true;

  return WaitForSingleObject(clock->stop, 16u) != WAIT_OBJECT_0;
}

static DWORD WINAPI
pacer__waiter(void *user) {
  GPUPacerClock *clock;
  DWM_TIMING_INFO timing;
  LARGE_INTEGER  now;
  HANDLE         handles[2];
  uint32_t       count;
  LONG64         last;

  clock      = user;
  handles[0] = clock->stop;
  handles[1] = clock->resume;
  count      = 0u;
  last       = 0;

  for (;;) {
    /* paused: asleep until resumed or stopped */
    if (WaitForMultipleObjects(2u, handles, FALSE, INFINITE) == WAIT_OBJECT_0)
      break;

    if (!pacer__waitRefresh(clock, &count))
      break;

    QueryPerformanceCounter(&now);
    memset(&timing, 0, sizeof(timing));
    timing.cbSize = sizeof(timing);
    if (pacer__win.timing && SUCCEEDED(pacer__win.timing(NULL, &timing))
        && timing.qpcRefreshPeriod > 0u && timing.qpcVBlank > 0u) {
      /* the refresh at or before now */
      InterlockedExchange64(&clock->periodTicks, (LONG64)timing.qpcRefreshPeriod);
      InterlockedExchange64(&clock->vsyncTicks, (LONG64)timing.qpcVBlank <= now.QuadPart
                              ? (LONG64)timing.qpcVBlank : now.QuadPart);
    } else {
      if (last > 0 && now.QuadPart > last)
        InterlockedExchange64(&clock->periodTicks, now.QuadPart - last);

      InterlockedExchange64(&clock->vsyncTicks, now.QuadPart);
    }

    last = now.QuadPart;
    if (clock->running && InterlockedExchange(&clock->posted, 1) == 0
        && !PostMessageW(clock->window, GPU_PACER_MESSAGE, 0, 0))
      InterlockedExchange(&clock->posted, 0);
  }

  return 0;
}

static LRESULT CALLBACK
pacer__windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  GPUPacerClock *clock;
  GPUPacerVsync  vsync = {0};

  if (message != GPU_PACER_MESSAGE)
    return DefWindowProcW(window, message, wParam, lParam);

  clock = (GPUPacerClock *)GetWindowLongPtrW(window, GWLP_USERDATA);
  if (!clock)
    return 0;

  InterlockedExchange(&clock->posted, 0);
  if (!clock->running || !clock->pacer)
    return 0;

  vsync.vsync  = pacer__seconds(InterlockedCompareExchange64(&clock->vsyncTicks, 0, 0));
  vsync.period = pacer__seconds(InterlockedCompareExchange64(&clock->periodTicks, 0, 0));
  framePacerVsync(clock->pacer, &vsync);
  return 0;
}

static HINSTANCE
pacer__module(void) {
  HMODULE module;

  module = NULL;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)(void *)pacer__windowProc, &module))
    module = GetModuleHandleW(NULL);

  return module;
}

GPU_HIDE
bool
pacerClockSupported(void) {
  pacer__load();
  return pacer__win.qpcSeconds > 0.0;
}

GPU_HIDE
uintptr_t
pacerThreadSelf(void) {
  return (uintptr_t)GetCurrentThreadId();
}

GPU_HIDE
double
pacerHostSeconds(void) {
  LARGE_INTEGER now;

  pacer__load();
  QueryPerformanceCounter(&now);
  return pacer__seconds(now.QuadPart);
}

GPU_HIDE
GPUResult
pacerClockCreate(GPUPacer *pacer, GPUSwapchain *swapchain, GPUPacerClock **outClock) {
  WNDCLASSEXW    windowClass = {0};
  GPUPacerClock *clock;
  HINSTANCE      module;

  *outClock = NULL;
  if (!pacerClockSupported())
    return GPU_ERROR_UNSUPPORTED;

  if (!(clock = calloc(1u, sizeof(*clock))))
    return GPU_ERROR_OUT_OF_MEMORY;

  module = pacer__module();
  if (!pacer__win.windowClass) {
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.lpfnWndProc   = pacer__windowProc;
    windowClass.hInstance     = module;
    windowClass.lpszClassName = L"GPUFramePacer";
    pacer__win.windowClass    = RegisterClassExW(&windowClass);
  }

  clock->pacer  = pacer;
  clock->target = swapchain->surface && swapchain->surface->type == GPU_SURFACE_WINDOWS_HWND
                    ? (HWND)swapchain->surface->nativeHandle : NULL;
  clock->window = CreateWindowExW(0, L"GPUFramePacer", L"", 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, NULL, module, NULL);
  clock->stop   = CreateEventW(NULL, TRUE, FALSE, NULL);
  clock->resume = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (!clock->window || !clock->stop || !clock->resume) {
    if (clock->window)
      DestroyWindow(clock->window);
    if (clock->stop)
      CloseHandle(clock->stop);
    if (clock->resume)
      CloseHandle(clock->resume);
    free(clock);
    return GPU_ERROR_UNSUPPORTED;
  }

  SetWindowLongPtrW(clock->window, GWLP_USERDATA, (LONG_PTR)clock);
  pacer__findOutput(clock);
  if (!(clock->thread = CreateThread(NULL, 0u, pacer__waiter, clock, 0u, NULL))) {
    SetWindowLongPtrW(clock->window, GWLP_USERDATA, 0);
    DestroyWindow(clock->window);
    CloseHandle(clock->stop);
    CloseHandle(clock->resume);
    if (clock->output)
      IDXGIOutput_Release(clock->output);
    free(clock);
    return GPU_ERROR_UNSUPPORTED;
  }

  SetThreadPriority(clock->thread, THREAD_PRIORITY_TIME_CRITICAL);
  *outClock = clock;
  return GPU_OK;
}

GPU_HIDE
void
pacerClockRun(GPUPacerClock *clock, bool run) {
  if (!clock)
    return;

  InterlockedExchange(&clock->running, run ? 1 : 0);
  if (run)
    SetEvent(clock->resume);
  else
    ResetEvent(clock->resume);
}

/* the display's rate isn't asked here: the pacer thins the refreshes */
GPU_HIDE
void
pacerClockRate(GPUPacerClock *clock, const GPUPacer *pacer) {
  (void)clock;
  (void)pacer;
}

GPU_HIDE
void
pacerClockDestroy(GPUPacerClock *clock) {
  if (!clock)
    return;

  InterlockedExchange(&clock->running, 0);
  SetWindowLongPtrW(clock->window, GWLP_USERDATA, 0);
  SetEvent(clock->stop);
  WaitForSingleObject(clock->thread, INFINITE);
  CloseHandle(clock->thread);
  CloseHandle(clock->stop);
  CloseHandle(clock->resume);
  if (clock->output)
    IDXGIOutput_Release(clock->output);

  /* only its own thread destroys the window; from another, it's closed there */
  if (!DestroyWindow(clock->window))
    PostMessageW(clock->window, WM_CLOSE, 0, 0);

  free(clock);
}

GPU_HIDE
void
pacerClockLock(GPUPacerClock *clock) {
  (void)clock;
}

GPU_HIDE
void
pacerClockUnlock(GPUPacerClock *clock) {
  (void)clock;
}

#endif /* _WIN32 */
