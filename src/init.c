/*
 * Copyright (C) 2020 Recep Aslantas
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

#include "common.h"

/* LEGACY / CONVENIENCE BOOTSTRAP:
 * Automatic global backend selection belongs to the older convenience path.
 * The canonical core direction is explicit instance/device creation.
 * A simplified bootstrap helper may still exist in final form, but it should
 * be layered on top of the canonical core instead of replacing it.
 */

void
GPU_CONSTRUCTOR
gpu__init(void) {
  gpuSwitchGPUApiAuto();
}

void
GPU_DESTRUCTOR
gpu__cleanup(void) {
  
}
