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

/* frame_pacer.c's window on Windows: a small top-level window, this thread's
 * message loop (the pacer's ticks come through it) */

#include "frame_pacer_host.h"
#include <windows.h>

static HWND   pacer__window;
static double pacer__qpcSeconds;

static LRESULT CALLBACK
pacer__proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  return DefWindowProcW(window, message, wParam, lParam);
}

bool
pacerHostOpen(GPUInstance *inst, GPUAdapter *adapter, uint32_t width, uint32_t height, GPUSurface **outSurface) {
  WNDCLASSEXW   windowClass = {0};
  LARGE_INTEGER frequency;
  RECT          rect = {0, 0, (LONG)width, (LONG)height};

  QueryPerformanceFrequency(&frequency);
  pacer__qpcSeconds = 1.0 / (double)frequency.QuadPart;

  windowClass.cbSize        = sizeof(windowClass);
  windowClass.lpfnWndProc   = pacer__proc;
  windowClass.hInstance     = GetModuleHandleW(NULL);
  windowClass.lpszClassName = L"GPUFramePacerTest";
  RegisterClassExW(&windowClass);
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  pacer__window = CreateWindowExW(0, L"GPUFramePacerTest", L"pacer", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  40, 40, rect.right - rect.left, rect.bottom - rect.top,
                                  NULL, NULL, windowClass.hInstance, NULL);
  if (!pacer__window)
    return false;

  *outSurface = GPUCreateSurfaceFromNative(inst, adapter, pacer__window, GPU_SURFACE_WINDOWS_HWND, 1.0f);
  return *outSurface != NULL;
}

bool
pacerHostResize(uint32_t width, uint32_t height) {
  RECT rect = {0, 0, (LONG)width, (LONG)height};

  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  return SetWindowPos(pacer__window, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != 0;
}

double
pacerHostNow(void) {
  LARGE_INTEGER now;

  QueryPerformanceCounter(&now);
  return (double)now.QuadPart * pacer__qpcSeconds;
}

void
pacerHostPump(double seconds) {
  MSG    message;
  double end, left;

  end = pacerHostNow() + seconds;
  while ((left = end - pacerHostNow()) > 0.0) {
    MsgWaitForMultipleObjects(0u, NULL, FALSE, (DWORD)(left * 1e3) + 1u, QS_ALLINPUT);
    while (PeekMessageW(&message, NULL, 0u, 0u, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
}

uint32_t
pacerHostPresented(double *times, uint32_t capacity) {
  (void)times;
  (void)capacity;
  return 0u;
}

const char*
pacerHostPresentKind(void) {
  return "display";
}

void
pacerHostClose(void) {
  if (pacer__window)
    DestroyWindow(pacer__window);
  pacer__window = NULL;
}
