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

#ifndef metal_binding_limits_h
#define metal_binding_limits_h

enum {
  MT_ARGUMENT_BUFFER_COUNT   = 31u,
  MT_ARGUMENT_TEXTURE_COUNT  = 128u,
  MT_ARGUMENT_SAMPLER_COUNT  = 16u,
  MT_PUSH_CONSTANT_INDEX     = 30u,
  MT_BIND_GROUP_BUFFER_COUNT = MT_PUSH_CONSTANT_INDEX
};

#endif /* metal_binding_limits_h */
