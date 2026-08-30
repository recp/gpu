/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "../../../src/backend/cache_file.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(WIN32)
#  include <process.h>
#  include <windows.h>
#else
#  include <errno.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <time.h>
#  include <unistd.h>
#endif

enum {
  CachePayloadSize = 4096u,
  CacheLockHoldMs  = 500u
};

typedef struct CacheRecord {
  uint64_t magic;
  uint64_t sequence;
  uint64_t hash;
  uint32_t writer;
  uint32_t payloadSize;
  uint8_t  payload[CachePayloadSize];
} CacheRecord;

typedef struct ChildProcess {
#if defined(_WIN32) || defined(WIN32)
  HANDLE handle;
#else
  pid_t pid;
#endif
} ChildProcess;

static const uint64_t CacheRecordMagic = UINT64_C(0x4750554341434845);

static uint64_t
cache_hashBytes(uint64_t hash, const void *data, size_t size) {
  const uint8_t *bytes;

  bytes = data;
  for (size_t i = 0u; i < size; i++) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static uint64_t
cache_recordHash(const CacheRecord *record) {
  uint64_t hash;

  hash = UINT64_C(14695981039346656037);
  hash = cache_hashBytes(hash, &record->magic, sizeof(record->magic));
  hash = cache_hashBytes(hash, &record->sequence, sizeof(record->sequence));
  hash = cache_hashBytes(hash, &record->writer, sizeof(record->writer));
  hash = cache_hashBytes(hash,
                         &record->payloadSize,
                         sizeof(record->payloadSize));
  return cache_hashBytes(hash, record->payload, sizeof(record->payload));
}

static bool
cache_recordValid(const CacheRecord *record) {
  return record && record->magic == CacheRecordMagic &&
         record->payloadSize == CachePayloadSize &&
         record->hash == cache_recordHash(record);
}

static void
cache_fillRecord(CacheRecord *record, uint64_t sequence, uint32_t writer) {
  memset(record, 0, sizeof(*record));
  record->magic       = CacheRecordMagic;
  record->sequence    = sequence;
  record->writer      = writer;
  record->payloadSize = CachePayloadSize;
  for (uint32_t i = 0u; i < CachePayloadSize; i++) {
    record->payload[i] = (uint8_t)(sequence * 131u + writer * 17u + i);
  }
  record->hash = cache_recordHash(record);
}

static bool
cache_readRecord(const char *path, CacheRecord *record) {
  FILE *file;
  bool  valid;

  file = path && record ? fopen(path, "rb") : NULL;
  if (!file) {
    return false;
  }
  valid = fread(record, sizeof(*record), 1u, file) == 1u &&
          fgetc(file) == EOF && !ferror(file) && cache_recordValid(record);
  fclose(file);
  return valid;
}

static bool
cache_writeRecord(const char *path, const CacheRecord *record) {
  FILE *file;
  bool  written;

  file = path && cache_recordValid(record) ? fopen(path, "wb") : NULL;
  if (!file) {
    return false;
  }
  written = fwrite(record, sizeof(*record), 1u, file) == 1u;
  if (fclose(file) != 0) {
    written = false;
  }
  return written;
}

static void
cache_sleep(uint32_t milliseconds) {
#if defined(_WIN32) || defined(WIN32)
  Sleep(milliseconds);
#else
  struct timespec duration;

  duration.tv_sec  = (time_t)(milliseconds / 1000u);
  duration.tv_nsec = (long)(milliseconds % 1000u) * 1000000l;
  while (nanosleep(&duration, &duration) != 0 && errno == EINTR) {
  }
#endif
}

static int
cache_child(const char *path, uint32_t writer) {
  GPUCacheFileGuard guard;
  CacheRecord       current;
  CacheRecord       next;
  char             *temporaryPath;
  bool              replaced;

  if (!path || writer == 0u || !gpuCacheFileBegin(path, &guard)) {
    return EXIT_FAILURE;
  }
  if (!cache_readRecord(path, &current)) {
    gpuCacheFileEnd(&guard);
    return EXIT_FAILURE;
  }

  cache_fillRecord(&next, current.sequence + 1u, writer);
  cache_sleep(CacheLockHoldMs);
  temporaryPath = gpuCacheFileTemporaryPath(path, &next);
  if (!temporaryPath) {
    gpuCacheFileEnd(&guard);
    return EXIT_FAILURE;
  }
  remove(temporaryPath);
  replaced = cache_writeRecord(temporaryPath, &next) &&
             gpuCacheFileReplace(temporaryPath, path);
  if (!replaced) {
    remove(temporaryPath);
  }
  free(temporaryPath);
  gpuCacheFileEnd(&guard);
  return replaced ? EXIT_SUCCESS : EXIT_FAILURE;
}

static bool
cache_spawnChild(const char   *executable,
                 const char   *path,
                 uint32_t      writer,
                 ChildProcess *child) {
#if defined(_WIN32) || defined(WIN32)
  PROCESS_INFORMATION process = {0};
  STARTUPINFOA        startup = {0};
  char               *command;
  size_t              commandSize;
  bool                started;

  if (!executable || !path || !child) {
    return false;
  }
  commandSize = strlen(executable) + strlen(path) + 64u;
  command     = malloc(commandSize);
  if (!command) {
    return false;
  }
  snprintf(command,
           commandSize,
           "\"%s\" --child \"%s\" %u",
           executable,
           path,
           writer);
  startup.cb = sizeof(startup);
  started = CreateProcessA(NULL,
                           command,
                           NULL,
                           NULL,
                           FALSE,
                           0u,
                           NULL,
                           NULL,
                           &startup,
                           &process) != 0;
  free(command);
  if (!started) {
    return false;
  }
  CloseHandle(process.hThread);
  child->handle = process.hProcess;
  return true;
#else
  pid_t pid;

  if (!executable || !path || !child) {
    return false;
  }
  pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    char writerText[16];

    snprintf(writerText, sizeof(writerText), "%u", writer);
    execl(executable,
          executable,
          "--child",
          path,
          writerText,
          (char *)NULL);
    _exit(127);
  }
  child->pid = pid;
  return true;
#endif
}

static bool
cache_waitChild(ChildProcess *child) {
#if defined(_WIN32) || defined(WIN32)
  DWORD exitCode;
  bool  succeeded;

  if (!child || !child->handle) {
    return false;
  }
  succeeded = WaitForSingleObject(child->handle, INFINITE) == WAIT_OBJECT_0 &&
              GetExitCodeProcess(child->handle, &exitCode) &&
              exitCode == EXIT_SUCCESS;
  CloseHandle(child->handle);
  child->handle = NULL;
  return succeeded;
#else
  int status;

  if (!child || child->pid <= 0) {
    return false;
  }
  if (waitpid(child->pid, &status, 0) != child->pid) {
    return false;
  }
  child->pid = 0;
  return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
#endif
}

static int
cache_parent(const char *executable) {
  CacheRecord  initial;
  CacheRecord  final;
  ChildProcess children[2] = {0};
  char         lockPath[192];
  char         path[192];
  int          processId;
  bool         firstStarted;
  bool         secondStarted;
  bool         firstPassed;
  bool         secondPassed;

#if defined(_WIN32) || defined(WIN32)
  processId = _getpid();
#else
  processId = (int)getpid();
#endif
  snprintf(path,
           sizeof(path),
           ".gpu-cache-file-concurrency-%d.bin",
           processId);
  snprintf(lockPath, sizeof(lockPath), "%s.lock", path);
  remove(path);
  remove(lockPath);

  cache_fillRecord(&initial, 0u, 0u);
  if (!cache_writeRecord(path, &initial)) {
    fprintf(stderr, "cache-file concurrency setup failed\n");
    return EXIT_FAILURE;
  }

  firstStarted  = cache_spawnChild(executable, path, 1u, &children[0]);
  secondStarted = cache_spawnChild(executable, path, 2u, &children[1]);
  firstPassed   = firstStarted && cache_waitChild(&children[0]);
  secondPassed  = secondStarted && cache_waitChild(&children[1]);

  if (!firstPassed || !secondPassed || !cache_readRecord(path, &final) ||
      final.sequence != 2u || final.writer < 1u || final.writer > 2u) {
    fprintf(stderr, "cache-file process serialization failed\n");
    remove(path);
    remove(lockPath);
    return EXIT_FAILURE;
  }

  remove(path);
  remove(lockPath);
  return EXIT_SUCCESS;
}

int
main(int argc, char **argv) {
  if (argc == 4 && strcmp(argv[1], "--child") == 0) {
    char         *end;
    unsigned long writer;

    writer = strtoul(argv[3], &end, 10);
    if (!end || *end != '\0' || writer == 0u || writer > UINT32_MAX) {
      return EXIT_FAILURE;
    }
    return cache_child(argv[2], (uint32_t)writer);
  }
  if (argc != 1 || !argv[0]) {
    return EXIT_FAILURE;
  }
  return cache_parent(argv[0]);
}
