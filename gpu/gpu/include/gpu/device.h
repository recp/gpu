/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#ifndef gpu_device_h
#define gpu_device_h

#include "common.h"

typedef struct GPUDevice {
  void *priv;
} GPUDevice;

GPUDevice*
gpu_device_create(void);

#endif /* gpu_device_h */