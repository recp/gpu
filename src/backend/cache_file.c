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

#include "cache_file.h"

#if defined(_WIN32) || defined(WIN32)
# include <process.h>
# include <windows.h>
#else
# include <fcntl.h>
# include <sys/file.h>
# include <unistd.h>
#endif

GPU_HIDE
bool
gpuCacheFileBegin(const char        *path,
                  GPUCacheFileGuard *guard) {
  char   *lockPath;
  size_t  pathLength;

  if (!guard) {
    return false;
  }
  guard->native = (intptr_t)-1;
  guard->locked = false;
  if (!path) {
    return false;
  }

  pathLength = strlen(path);
  if (pathLength > SIZE_MAX - sizeof(".lock")) {
    return false;
  }
  lockPath = malloc(pathLength + sizeof(".lock"));
  if (!lockPath) {
    return false;
  }
  memcpy(lockPath, path, pathLength);
  memcpy(lockPath + pathLength, ".lock", sizeof(".lock"));

#if defined(_WIN32) || defined(WIN32)
  {
    HANDLE     handle;
    OVERLAPPED overlap = {0};

    handle = CreateFileA(lockPath,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL,
                         OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    free(lockPath);
    if (handle == INVALID_HANDLE_VALUE ||
        !LockFileEx(handle,
                    LOCKFILE_EXCLUSIVE_LOCK,
                    0u,
                    MAXDWORD,
                    MAXDWORD,
                    &overlap)) {
      if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
      }
      return false;
    }
    guard->native = (intptr_t)handle;
  }
#else
  {
    int descriptor;

    descriptor = open(lockPath, O_CREAT | O_RDWR, 0600);
    free(lockPath);
    if (descriptor < 0 || flock(descriptor, LOCK_EX) != 0) {
      if (descriptor >= 0) {
        close(descriptor);
      }
      return false;
    }
    guard->native = (intptr_t)descriptor;
  }
#endif
  guard->locked = true;
  return true;
}

GPU_HIDE
void
gpuCacheFileEnd(GPUCacheFileGuard *guard) {
  if (!guard || !guard->locked) {
    return;
  }
#if defined(_WIN32) || defined(WIN32)
  {
    HANDLE     handle;
    OVERLAPPED overlap = {0};

    handle = (HANDLE)guard->native;
    UnlockFileEx(handle, 0u, MAXDWORD, MAXDWORD, &overlap);
    CloseHandle(handle);
  }
#else
  {
    int descriptor;

    descriptor = (int)guard->native;
    flock(descriptor, LOCK_UN);
    close(descriptor);
  }
#endif
  guard->native = (intptr_t)-1;
  guard->locked = false;
}

GPU_HIDE
char *
gpuCacheFileTemporaryPath(const char *path, const void *identity) {
  char   *temporaryPath;
  size_t  pathLength;
  int     processId;

  if (!path) {
    return NULL;
  }
  pathLength = strlen(path);
  if (pathLength > SIZE_MAX - 64u) {
    return NULL;
  }
  temporaryPath = malloc(pathLength + 64u);
  if (!temporaryPath) {
    return NULL;
  }
#if defined(_WIN32) || defined(WIN32)
  processId = _getpid();
#else
  processId = (int)getpid();
#endif
  snprintf(temporaryPath,
           pathLength + 64u,
           "%s.tmp.%d.%p",
           path,
           processId,
           identity);
  return temporaryPath;
}

GPU_HIDE
bool
gpuCacheFileReplace(const char *source, const char *destination) {
  if (!source || !destination) {
    return false;
  }
#if defined(_WIN32) || defined(WIN32)
  return MoveFileExA(source,
                     destination,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(source, destination) == 0;
#endif
}
