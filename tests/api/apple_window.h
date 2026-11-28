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

#ifndef gpu_test_apple_window_h
#define gpu_test_apple_window_h

#include <gpu/gpu.h>
#include <stdbool.h>
#include <stdint.h>

/* windows the frame tests present to, on Apple platforms: on macOS a small
 * titled window above other apps' (not activated: no focus taken); on iOS a
 * window of its own in the app's scene, above the app's. All on the main
 * thread, whose run loop appleWindowPump runs: a pacer ticks there, and only
 * through it does the system's word on a window (shown, covered, the scene in
 * the background) arrive. On iOS the test runs inside an app (UIApplication),
 * called on the main thread outside any main-queue block */

typedef struct AppleWindow AppleWindow;

/* a window of that content size at x, y (points, from the screen's origin:
 * bottom left on macOS, top left on iOS); NULL where there is none to make
 * (no app on iOS). Not waited for: appleWindowWaitShown */
AppleWindow*
appleWindowOpen(double x, double y, uint32_t width, uint32_t height);

/* the view a surface is made from (NSView, UIView) and its surface type */
void*
appleWindowView(AppleWindow *window);

GPUSurfaceType
appleWindowSurfaceType(void);

GPUSurface*
appleWindowSurface(AppleWindow *window, GPUInstance *instance, GPUAdapter *adapter);

/* the system shows it: on macOS not covered (its occlusion state), on iOS its
 * scene in the foreground */
bool
appleWindowShown(AppleWindow *window);

/* the run loop pumped until the window is shown (or not), for at most that long */
bool
appleWindowWaitShown(AppleWindow *window, bool shown, double seconds);

/* the window's content takes that size */
void
appleWindowResize(AppleWindow *window, uint32_t width, uint32_t height);

/* the fastest refresh its display gives this app, Hz: the screen's most (an
 * phone app's 60 without CADisableMinimumFrameDurationOnPhone in its
 * Info.plist); zero while the system may hold it lower (Low Power Mode, a
 * thermal limit) */
double
appleWindowDisplayRate(AppleWindow *window);

/* hidden as the platform hides it: on macOS covered by another window, on iOS
 * the app sent to the background (kept running there by a background task).
 * False when it can't be, or wasn't hidden within that long */
bool
appleWindowHide(AppleWindow *window, double seconds);

/* shown again: on macOS the cover taken away; on iOS the app can't come back
 * by itself, so it asks whoever runs it on its output, one line
 * "apple window: waiting for the foreground" (the iOS suite's runner brings it
 * back), and waits that long for its scene to be active */
bool
appleWindowUnhide(AppleWindow *window, double seconds);

void
appleWindowClose(AppleWindow *window);

/* the main run loop run for that long; at least once */
void
appleWindowPump(double seconds);

#endif /* gpu_test_apple_window_h */
