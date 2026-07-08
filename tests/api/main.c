#include "test.h"

int
main(void) {
  GPUPhysicalDevice *physicalDevice;
  GPUDevice *device;
  int ok;

  physicalDevice = GPUGetAutoSelectedPhysicalDevice(NULL);
  if (!physicalDevice) {
    fprintf(stderr, "failed to get physical device\n");
    return 1;
  }

  device = GPUCreateDeviceWithDefaultQueues(physicalDevice);
  if (!device) {
    fprintf(stderr, "failed to create device\n");
    return 1;
  }

  ok = gpu_test_queue(physicalDevice, device) &&
       gpu_test_sampler(device) &&
       gpu_test_bindgroup(device) &&
       gpu_test_resources(device) &&
       gpu_test_copy(device) &&
       gpu_test_render() &&
       gpu_test_compute();

  GPUDestroyDevice(device);
  if (!ok) {
    return 1;
  }

  printf("GPU API validation passed\n");
  return 0;
}
