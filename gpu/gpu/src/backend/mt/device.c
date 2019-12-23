/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#include "../../../include/gpu/device.h"
#include <cmt/cmt.h>

GPUDevice*
gpu_device_new(void) {
  GPUDevice *device;
  MtDevice  *mtDevice;
  
  mtDevice = mtCreateDevice();
  device   = calloc(1, sizeof(*device));

  device->priv = mtDevice;

  return device;
}

void
gpu_device_release(GPUDevice * __restrict device) {
  
}
