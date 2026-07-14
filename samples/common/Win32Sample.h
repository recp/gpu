#ifndef gpu_win32_sample_h
#define gpu_win32_sample_h

#include <windows.h>

#include <stdbool.h>
#include <stdlib.h>

#define GPU_SAMPLE_SKIP_RETURN_CODE 77

static inline bool
GPUSampleShouldSkipNonInteractive(void) {
  const char     *enabled;
  HWINSTA         station;
  USEROBJECTFLAGS flags;
  DWORD           size;

  enabled = getenv("GPU_SAMPLE_SKIP_NONINTERACTIVE");
  if (!enabled || enabled[0] == '\0' ||
      (enabled[0] == '0' && enabled[1] == '\0')) {
    return false;
  }

  station = GetProcessWindowStation();
  size    = 0u;
  return !station ||
         !GetUserObjectInformationW(station,
                                    UOI_FLAGS,
                                    &flags,
                                    sizeof(flags),
                                    &size) ||
         (flags.dwFlags & WSF_VISIBLE) == 0u;
}

#endif
