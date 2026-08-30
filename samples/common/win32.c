#define GPU_SAMPLE_PLATFORM_IMPLEMENTATION
#define WIN32_LEAN_AND_MEAN

#include "win32.h"
#include "Win32Image.h"
#include "asset_io.h"

#include <direct.h>
#include <winhttp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void
(*GPUWin32RenderCallback)(void *userData);

typedef struct GPUWin32Fetch {
  struct GPUWin32Fetch *next;
  SampleFetchCallback   callback;
  void                 *userData;
  uint8_t              *bytes;
  uint64_t              byteCount;
  char                  error[160];
  wchar_t              *url;
} GPUWin32Fetch;

typedef struct GPUWin32AdapterRequest {
  HANDLE      completed;
  GPUAdapter *adapter;
  GPUResult   result;
} GPUWin32AdapterRequest;

struct GPUWin32Sample {
  GPUWin32Window         *window;
  GPUInstance            *instance;
  GPUAdapter             *adapter;
  GPUDevice              *device;
  GPUQueue               *queue;
  GPUSurface             *surface;
  GPUSwapchain           *swapchain;
  GPUWin32RenderCallback  render;
  void                   *renderData;
  const char             *name;
  char                    status[256];
  uint32_t                width;
  uint32_t                height;
  bool                    canceled;
  bool                    failed;
};

static GPUWin32Sample *activeSample;
static GPUWin32Fetch  *completedFetches;
static SRWLOCK         fetchLock = SRWLOCK_INIT;

static double
startup_mark(bool          enabled,
             const char   *phase,
             double        started,
             double        previous) {
  double now;

  if (!enabled) {
    return previous;
  }
  now = gpu_win32_get_now();
  fprintf(stderr,
          "GPU sample startup: %-12s %8.3f ms (%8.3f ms total)\n",
          phase,
          now - previous,
          now - started);
  return now;
}

static void
adapter_ready(GPUResult result, GPUAdapter *adapter, void *userData) {
  GPUWin32AdapterRequest *request;

  request          = userData;
  request->adapter = adapter;
  request->result  = result;
  SetEvent(request->completed);
}

static GPUAdapter*
request_adapter(GPUInstance *instance, GPUPowerPreference preference) {
  GPUAdapterRequestOptions options = {0};
  GPUWin32AdapterRequest   request = {0};
  GPUResult                result;

  request.completed = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (!request.completed) {
    return NULL;
  }
  options.chain.sType      = GPU_STRUCTURE_TYPE_ADAPTER_REQUEST_OPTIONS;
  options.chain.structSize = sizeof(options);
  options.powerPreference  = preference;
  result = GPURequestAdapter(instance, &options, adapter_ready, &request);
  if (result == GPU_OK &&
      WaitForSingleObject(request.completed, INFINITE) == WAIT_OBJECT_0 &&
      request.result == GPU_OK) {
    CloseHandle(request.completed);
    return request.adapter;
  }
  CloseHandle(request.completed);
  return NULL;
}

static GPUAdapter*
adapter_at_index(GPUInstance *instance, uint32_t index) {
  GPUAdapter **adapters;
  GPUAdapter  *adapter;
  uint32_t     adapterCount;

  adapterCount = 0u;
  adapters     = NULL;
  if (GPUEnumerateAdapters(instance, &adapterCount, NULL) != GPU_OK ||
      index >= adapterCount ||
      !(adapters = calloc(adapterCount, sizeof(*adapters))) ||
      GPUEnumerateAdapters(instance, &adapterCount, adapters) != GPU_OK) {
    free(adapters);
    return NULL;
  }
  adapter = adapters[index];
  free(adapters);
  return adapter;
}

static GPUAdapter*
select_adapter(GPUInstance *instance) {
  char          selection[64];
  char         *end;
  unsigned long index;
  DWORD         length;

  length = GetEnvironmentVariableA("GPU_SAMPLE_ADAPTER",
                                   selection,
                                   sizeof(selection));
  if (length == 0u || length >= sizeof(selection) ||
      strcmp(selection, "auto") == 0) {
    return request_adapter(instance, GPU_POWER_PREFERENCE_DEFAULT);
  }
  if (strcmp(selection, "low") == 0) {
    return request_adapter(instance, GPU_POWER_PREFERENCE_LOW_POWER);
  }
  if (strcmp(selection, "high") == 0) {
    return request_adapter(instance,
                           GPU_POWER_PREFERENCE_HIGH_PERFORMANCE);
  }
  if (strncmp(selection, "index:", 6u) != 0 || selection[6] == '\0') {
    return request_adapter(instance, GPU_POWER_PREFERENCE_DEFAULT);
  }

  index = strtoul(selection + 6u, &end, 10);
  if (*end != '\0' || index > UINT32_MAX) {
    return NULL;
  }
  return adapter_at_index(instance, (uint32_t)index);
}

static const char*
asset_name(const char *path) {
  if (!path) {
    return NULL;
  }
  while (*path == '/' || *path == '\\') {
    path++;
  }
  return path;
}

static bool
asset_path(const char *path, char out[MAX_PATH]) {
  const char *name;
  char       *slash;
  DWORD       length;
  int         written;

  if (!path || !out || !(name = asset_name(path)) || !name[0]) {
    return false;
  }
  length = GetModuleFileNameA(NULL, out, MAX_PATH);
  if (length == 0u || length >= MAX_PATH) {
    return false;
  }
  slash = strrchr(out, '\\');
  if (!slash) {
    return false;
  }
  slash[1] = '\0';
  written  = snprintf(out + (slash + 1 - out),
                      MAX_PATH - (size_t)(slash + 1 - out),
                      "%s",
                      name);
  return written > 0 &&
         (size_t)written < MAX_PATH - (size_t)(slash + 1 - out);
}

static bool
read_path(const char *path, void **outData, uint64_t *outSize) {
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
  *outSize = (uint64_t)length;
  return true;
}

static bool
resize_surface(GPUWin32Sample *sample,
               GPUSwapchain   *swapchain,
               uint32_t       *width,
               uint32_t       *height) {
  RECT     bounds;
  uint32_t nextHeight, nextWidth;

  if (!sample || !sample->window || !sample->window->handle ||
      !width || !height ||
      !GetClientRect(sample->window->handle, &bounds)) {
    return false;
  }
  nextWidth  = bounds.right > bounds.left
                 ? (uint32_t)(bounds.right - bounds.left)
                 : 0u;
  nextHeight = bounds.bottom > bounds.top
                 ? (uint32_t)(bounds.bottom - bounds.top)
                 : 0u;
  if (nextWidth == 0u || nextHeight == 0u) {
    return false;
  }
  if ((nextWidth != sample->width || nextHeight != sample->height) &&
      swapchain &&
      GPUResizeSwapchain(swapchain, nextWidth, nextHeight) != GPU_OK) {
    return false;
  }

  sample->window->width  = nextWidth;
  sample->window->height = nextHeight;
  sample->width          = nextWidth;
  sample->height         = nextHeight;
  *width                 = nextWidth;
  *height                = nextHeight;
  return true;
}

static void
dispatch_fetches(void) {
  GPUWin32Fetch *fetch;
  GPUWin32Fetch *next;

  AcquireSRWLockExclusive(&fetchLock);
  fetch            = completedFetches;
  completedFetches = NULL;
  ReleaseSRWLockExclusive(&fetchLock);

  while (fetch) {
    next = fetch->next;
    fetch->callback(fetch->bytes,
                    fetch->byteCount,
                    fetch->error[0] ? fetch->error : NULL,
                    fetch->userData);
    free(fetch->url);
    free(fetch);
    fetch = next;
  }
}

static bool
append_download(GPUWin32Fetch *fetch,
                const void    *bytes,
                size_t         byteCount,
                size_t        *capacity) {
  uint8_t *nextBytes;
  size_t   required, nextCapacity;

  if (!fetch || !bytes || byteCount == 0u || !capacity ||
      fetch->byteCount > SIZE_MAX - byteCount) {
    return false;
  }
  required = (size_t)fetch->byteCount + byteCount;
  if (required > *capacity) {
    nextCapacity = *capacity ? *capacity : 64u * 1024u;
    while (nextCapacity < required) {
      if (nextCapacity > SIZE_MAX / 2u) {
        return false;
      }
      nextCapacity *= 2u;
    }
    nextBytes = realloc(fetch->bytes, nextCapacity);
    if (!nextBytes) {
      return false;
    }
    fetch->bytes = nextBytes;
    *capacity    = nextCapacity;
  }
  memcpy(fetch->bytes + fetch->byteCount, bytes, byteCount);
  fetch->byteCount += byteCount;
  return true;
}

static void
set_fetch_error(GPUWin32Fetch *fetch, const char *message) {
  if (!fetch || fetch->error[0]) {
    return;
  }
  snprintf(fetch->error,
           sizeof(fetch->error),
           "%s",
           message ? message : "sample: Windows download failed");
}

static DWORD WINAPI
fetch_worker(void *userData) {
  GPUWin32Fetch *fetch;
  URL_COMPONENTS components = {0};
  HINTERNET      session, connection, request;
  wchar_t       *host, *path;
  uint8_t        chunk[64u * 1024u];
  size_t         capacity;
  DWORD          available, read, status, statusSize;
  bool           success;

  fetch                 = userData;
  components.dwStructSize = sizeof(components);
  components.dwHostNameLength = (DWORD)-1;
  components.dwUrlPathLength  = (DWORD)-1;
  components.dwExtraInfoLength = (DWORD)-1;
  session               = NULL;
  connection            = NULL;
  request               = NULL;
  host                  = NULL;
  path                  = NULL;
  capacity              = 0u;
  success               = false;

  if (!fetch || !fetch->url ||
      !WinHttpCrackUrl(fetch->url, 0u, 0u, &components) ||
      components.dwHostNameLength == 0u) {
    set_fetch_error(fetch, "sample: invalid download URL");
    goto complete;
  }

  host = calloc((size_t)components.dwHostNameLength + 1u, sizeof(*host));
  path = calloc((size_t)components.dwUrlPathLength +
                  (size_t)components.dwExtraInfoLength + 1u,
                sizeof(*path));
  if (!host || !path) {
    set_fetch_error(fetch, "sample: download allocation failed");
    goto complete;
  }
  memcpy(host,
         components.lpszHostName,
         (size_t)components.dwHostNameLength * sizeof(*host));
  memcpy(path,
         components.lpszUrlPath,
         (size_t)components.dwUrlPathLength * sizeof(*path));
  if (components.dwExtraInfoLength > 0u) {
    memcpy(path + components.dwUrlPathLength,
           components.lpszExtraInfo,
           (size_t)components.dwExtraInfoLength * sizeof(*path));
  }

  session = WinHttpOpen(L"gpu-samples/1",
                        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                        WINHTTP_NO_PROXY_NAME,
                        WINHTTP_NO_PROXY_BYPASS,
                        0u);
  connection = session
                 ? WinHttpConnect(session,
                                  host,
                                  components.nPort,
                                  0u)
                 : NULL;
  request = connection
              ? WinHttpOpenRequest(
                  connection,
                  L"GET",
                  path[0] ? path : L"/",
                  NULL,
                  WINHTTP_NO_REFERER,
                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                  components.nScheme == INTERNET_SCHEME_HTTPS
                    ? WINHTTP_FLAG_SECURE
                    : 0u)
              : NULL;
  if (!request ||
      !WinHttpSendRequest(request,
                          WINHTTP_NO_ADDITIONAL_HEADERS,
                          0u,
                          WINHTTP_NO_REQUEST_DATA,
                          0u,
                          0u,
                          0u) ||
      !WinHttpReceiveResponse(request, NULL)) {
    set_fetch_error(fetch, "sample: Windows HTTP request failed");
    goto complete;
  }

  status     = 0u;
  statusSize = sizeof(status);
  if (!WinHttpQueryHeaders(request,
                           WINHTTP_QUERY_STATUS_CODE |
                             WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX,
                           &status,
                           &statusSize,
                           WINHTTP_NO_HEADER_INDEX) ||
      status < 200u || status >= 300u) {
    set_fetch_error(fetch, "sample: download returned an HTTP error");
    goto complete;
  }

  for (;;) {
    available = 0u;
    if (!WinHttpQueryDataAvailable(request, &available)) {
      set_fetch_error(fetch, "sample: failed to query download data");
      goto complete;
    }
    if (available == 0u) {
      break;
    }
    while (available > 0u) {
      DWORD requestSize;

      requestSize = available < sizeof(chunk)
                      ? available
                      : (DWORD)sizeof(chunk);
      read = 0u;
      if (!WinHttpReadData(request, chunk, requestSize, &read) || read == 0u ||
          !append_download(fetch, chunk, read, &capacity)) {
        set_fetch_error(fetch, "sample: failed to read download data");
        goto complete;
      }
      available -= read;
    }
  }
  success = fetch->byteCount > 0u;
  if (!success) {
    set_fetch_error(fetch, "sample: download returned no data");
  }

complete:
  if (!success) {
    free(fetch->bytes);
    fetch->bytes     = NULL;
    fetch->byteCount = 0u;
  }
  if (request) {
    WinHttpCloseHandle(request);
  }
  if (connection) {
    WinHttpCloseHandle(connection);
  }
  if (session) {
    WinHttpCloseHandle(session);
  }
  free(path);
  free(host);

  AcquireSRWLockExclusive(&fetchLock);
  fetch->next      = completedFetches;
  completedFetches = fetch;
  ReleaseSRWLockExclusive(&fetchLock);
  return 0u;
}

GPUWin32Sample*
GPUSampleWin32Create(GPUWin32Window      *window,
                     const char          *name,
                     GPUWin32SampleStart  start) {
  static const GPUFeature optionalFeatures[] = {
    GPU_FEATURE_COMPUTE,
    GPU_FEATURE_INDIRECT_DRAW,
    GPU_FEATURE_MULTI_DRAW,
    GPU_FEATURE_DESCRIPTOR_INDEXING,
    GPU_FEATURE_SUBGROUPS,
    GPU_FEATURE_SHADER_F16,
    GPU_FEATURE_TIMESTAMPS
  };
  static const GPUQueueRequest queueRequests[] = {
    {
      .type  = GPU_QUEUE_GRAPHICS,
      .count = 1u
    }
  };
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUDeviceCreateInfo   deviceInfo = {0};
  GPURuntimeConfig      runtimeInfo = {0};
  GPUWin32Sample       *sample;
  const char           *failure;
  double                startupLast;
  double                startupStart;
  bool                  startupLog;

  if (!window || !window->handle || !name || !start || activeSample ||
      window->width == 0u || window->height == 0u ||
      !(window->scale > 0.0f)) {
    return NULL;
  }
  startupLog   = getenv("GPU_SAMPLE_STARTUP_LOG") != NULL;
  startupStart = startupLog ? gpu_win32_get_now() : 0.0;
  startupLast  = startupStart;
  sample = calloc(1, sizeof(*sample));
  if (!sample) {
    return NULL;
  }
  failure        = "initialize sample";
  sample->window = window;
  sample->name   = name;
  snprintf(sample->status, sizeof(sample->status), "GPU: starting %s", name);

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.label            = name;
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  failure = "create the Direct3D 12 instance";
  if (GPUCreateInstance(&instanceInfo, &sample->instance) != GPU_OK ||
      !sample->instance) {
    goto fail;
  }
  startupLast = startup_mark(startupLog,
                             "instance",
                             startupStart,
                             startupLast);

  failure         = "select the requested Direct3D 12 adapter";
  sample->adapter = select_adapter(sample->instance);
  if (!sample->adapter) {
    goto fail;
  }
  startupLast = startup_mark(startupLog,
                             "adapter",
                             startupStart,
                             startupLast);

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.optional.pFeatures    = optionalFeatures;
  deviceInfo.optional.featureCount = GPU_ARRAY_LEN(optionalFeatures);
  deviceInfo.queues.chain.sType      =
    GPU_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  deviceInfo.queues.chain.structSize = sizeof(deviceInfo.queues);
  deviceInfo.queues.pRequests        = queueRequests;
  deviceInfo.queues.requestCount     = GPU_ARRAY_LEN(queueRequests);
  failure = "create the Direct3D 12 device";
  if (GPUCreateDevice(sample->adapter,
                      &deviceInfo,
                      &sample->device) != GPU_OK ||
      !sample->device) {
    goto fail;
  }
  failure       = "get the graphics queue";
  sample->queue = GPUGetQueue(sample->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!sample->queue) {
    goto fail;
  }
  startupLast = startup_mark(startupLog,
                             "device+queue",
                             startupStart,
                             startupLast);

  runtimeInfo.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeInfo.chain.structSize = sizeof(runtimeInfo);
  runtimeInfo.validationMode   = GPU_VALIDATION_FULL;
  runtimeInfo.enableStats      = true;
  failure = "configure the GPU runtime";
  if (GPUConfigureRuntime(sample->device, &runtimeInfo) != GPU_OK) {
    goto fail;
  }
  startupLast = startup_mark(startupLog,
                             "runtime",
                             startupStart,
                             startupLast);

  failure = "create the window surface";
  sample->surface = GPUCreateSurfaceFromNative(sample->instance,
                                               sample->adapter,
                                               window->handle,
                                               GPU_SURFACE_WINDOWS_HWND,
                                               window->scale);
  if (!sample->surface ||
      !resize_surface(sample, NULL, &sample->width, &sample->height)) {
    goto fail;
  }
  startupLast = startup_mark(startupLog,
                             "surface",
                             startupStart,
                             startupLast);
  failure = "create the swapchain";
  sample->swapchain = GPUCreateSwapchainDefault(sample->device,
                                                sample->surface,
                                                sample->width,
                                                sample->height);
  if (!sample->swapchain) {
    goto fail;
  }
  startupLast = startup_mark(startupLog,
                             "swapchain",
                             startupStart,
                             startupLast);

  activeSample = sample;
  failure      = "initialize sample resources";
  if (start() != 0 || sample->failed) {
    goto fail;
  }
  startup_mark(startupLog,
               "resources",
               startupStart,
               startupLast);
  return sample;

fail:
  if (activeSample == sample) {
    activeSample = NULL;
  }
  sample->failed = true;
  if (strncmp(sample->status, "GPU: starting ", 14u) == 0) {
    snprintf(sample->status,
             sizeof(sample->status),
             "GPU: failed to %s",
             failure);
  }
  fprintf(stderr, "%s\n", sample->status);
  return sample;
}

bool
GPUSampleWin32Render(GPUWin32Sample *sample) {
  if (!sample || sample != activeSample || sample->failed ||
      sample->canceled) {
    return false;
  }
  dispatch_fetches();
  if (sample->render) {
    sample->render(sample->renderData);
  }
  return !sample->failed && !sample->canceled;
}

void
GPUSampleWin32Stop(GPUWin32Sample *sample) {
  if (!sample) {
    return;
  }
  sample->canceled = true;
  if (activeSample == sample) {
    activeSample = NULL;
  }
}

const char*
GPUSampleWin32Status(const GPUWin32Sample *sample) {
  return sample ? sample->status : "GPU: sample runtime unavailable";
}

bool
GPUSampleWin32Failed(const GPUWin32Sample *sample) {
  return !sample || sample->failed;
}

void
set_status(const char *message, int failed) {
  if (!activeSample) {
    return;
  }
  snprintf(activeSample->status,
           sizeof(activeSample->status),
           "%s",
           message ? message : "GPU sample status");
  activeSample->failed |= failed != 0;
  fprintf(failed ? stderr : stdout, "%s\n", activeSample->status);
}

void
set_status_notice(const char *message) {
  set_status(message, 0);
}

int
read_file(const char *path, void **outData, uint64_t *outSize) {
  char resolved[MAX_PATH];

  return asset_path(path, resolved) &&
         read_path(resolved, outData, outSize);
}

GPUResult
request_webgpu_device(GPUInstance        *instance,
                      WebGPURequest      *request,
                      WebGPUReadyCallback callback,
                      void               *userData) {
  return request_webgpu_device_features(instance,
                                        request,
                                        callback,
                                        userData,
                                        NULL,
                                        0u);
}

GPUResult
request_webgpu_device_features(GPUInstance        *instance,
                               WebGPURequest      *request,
                               WebGPUReadyCallback callback,
                               void               *userData,
                               const GPUFeature   *optionalFeatures,
                               uint32_t            optionalFeatureCount) {
  (void)optionalFeatures;
  (void)optionalFeatureCount;
  if (!activeSample || !instance || !request || !callback) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  request->callback  = callback;
  request->adapter   = activeSample->adapter;
  request->userData  = userData;
  request->result    = GPU_OK;
  request->completed = true;
  callback(GPU_OK,
           activeSample->adapter,
           activeSample->device,
           userData);
  return activeSample->failed ? GPU_ERROR_BACKEND_FAILURE : GPU_OK;
}

int
resize_webgpu_canvas(GPUSwapchain *swapchain,
                     uint32_t     *width,
                     uint32_t     *height) {
  return resize_surface(activeSample, swapchain, width, height);
}

void
gpu_win32_set_main_loop(void (*callback)(void *),
                        void  *userData,
                        int    fps,
                        bool   simulateInfiniteLoop) {
  (void)fps;
  (void)simulateInfiniteLoop;
  if (!activeSample) {
    return;
  }
  activeSample->render     = callback;
  activeSample->renderData = userData;
}

void
gpu_win32_cancel_main_loop(void) {
  if (activeSample) {
    activeSample->canceled = true;
  }
}

double
gpu_win32_get_now(void) {
  LARGE_INTEGER frequency, now;

  if (!QueryPerformanceFrequency(&frequency) ||
      !QueryPerformanceCounter(&now) ||
      frequency.QuadPart == 0) {
    return (double)GetTickCount64();
  }
  return (double)now.QuadPart * 1000.0 / (double)frequency.QuadPart;
}

void*
gpu_win32_load_image(const char *path, int *width, int *height) {
  char     resolved[MAX_PATH];
  void    *bytes;
  uint8_t *pixels;
  uint64_t byteCount;
  uint32_t imageWidth, imageHeight;

  if (!path || !width || !height ||
      !asset_path(path, resolved) ||
      !read_path(resolved, &bytes, &byteCount) ||
      byteCount > SIZE_MAX) {
    return NULL;
  }
  imageWidth  = 0u;
  imageHeight = 0u;
  pixels      = GPUSampleWin32DecodeImage(bytes,
                                         (size_t)byteCount,
                                         &imageWidth,
                                         &imageHeight);
  free(bytes);
  if (!pixels) {
    return NULL;
  }
  *width  = (int)imageWidth;
  *height = (int)imageHeight;
  return pixels;
}

GPUResult
gpu_win32_sample_create_instance(const GPUInstanceCreateInfo *info,
                                 GPUInstance                **outInstance) {
  (void)info;
  if (!activeSample || !outInstance) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outInstance = activeSample->instance;
  return GPU_OK;
}

GPUSurface*
gpu_win32_sample_create_surface(GPUInstance   *instance,
                                GPUAdapter    *adapter,
                                void          *nativeHandle,
                                GPUSurfaceType nativeType,
                                float          contentScale) {
  (void)instance;
  (void)adapter;
  (void)nativeHandle;
  (void)nativeType;
  (void)contentScale;
  return activeSample ? activeSample->surface : NULL;
}

GPUSwapchain*
gpu_win32_sample_create_swapchain(GPUDevice  *device,
                                  GPUSurface *surface,
                                  uint32_t    width,
                                  uint32_t    height) {
  (void)device;
  (void)surface;
  (void)width;
  (void)height;
  return activeSample ? activeSample->swapchain : NULL;
}

int
sample_fetch_url(const char         *url,
                 SampleFetchCallback callback,
                 void               *userData) {
  GPUWin32Fetch *fetch;
  HANDLE         thread;
  int            length;

  if (!activeSample || !url || !callback) {
    return 0;
  }
  length = MultiByteToWideChar(CP_UTF8, 0u, url, -1, NULL, 0);
  fetch  = length > 0 ? calloc(1, sizeof(*fetch)) : NULL;
  if (!fetch) {
    return 0;
  }
  fetch->url = calloc((size_t)length, sizeof(*fetch->url));
  if (!fetch->url ||
      MultiByteToWideChar(CP_UTF8,
                          0u,
                          url,
                          -1,
                          fetch->url,
                          length) != length) {
    free(fetch->url);
    free(fetch);
    return 0;
  }
  fetch->callback = callback;
  fetch->userData = userData;
  thread = CreateThread(NULL, 0u, fetch_worker, fetch, 0u, NULL);
  if (!thread) {
    free(fetch->url);
    free(fetch);
    return 0;
  }
  CloseHandle(thread);
  return 1;
}

int
sample_decode_image(const void         *bytes,
                    uint64_t            byteCount,
                    SampleImageCallback callback,
                    void               *userData) {
  uint8_t *pixels;
  uint32_t width, height;

  if (!bytes || byteCount == 0u || byteCount > SIZE_MAX || !callback) {
    return 0;
  }
  width  = 0u;
  height = 0u;
  pixels = GPUSampleWin32DecodeImage(bytes,
                                    (size_t)byteCount,
                                    &width,
                                    &height);
  callback(pixels,
           width,
           height,
           pixels ? NULL : "sample: Windows image decode failed",
           userData);
  return pixels != NULL;
}

int
sample_temporary_path(const char *name, char *path, size_t capacity) {
  char  directory[MAX_PATH];
  DWORD length;
  int   written;

  if (!name || !name[0] || !path || capacity == 0u) {
    return 0;
  }
  length = GetTempPathA(MAX_PATH, directory);
  if (length == 0u || length >= MAX_PATH ||
      (size_t)length + strlen("gpu-samples\\") >= MAX_PATH) {
    return 0;
  }
  memcpy(directory + length, "gpu-samples", sizeof("gpu-samples"));
  if (!CreateDirectoryA(directory, NULL) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    return 0;
  }
  written = snprintf(path, capacity, "%s\\%s", directory, name);
  return written > 0 && (size_t)written < capacity;
}
