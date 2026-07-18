#include <gpu/gpu.h>

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUInstance          *instance;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  if (GPUCreateInstance(&info, &instance) != GPU_OK || !instance) {
    return 1;
  }
  GPUDestroyInstance(instance);
  return 0;
}
