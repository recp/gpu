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

#include <metal_stdlib>

using namespace metal;

struct Shape {
  uint m;
  uint n;
  uint k;
  uint a_stride;
  uint b_stride;
  uint c_stride;
  uint round;
};

kernel void
prepare(device float *a [[buffer(0)]],
        device float *b [[buffer(1)]],
        device float *c [[buffer(2)]],
        device float *witness [[buffer(3)]],
        constant Shape &shape [[buffer(30)]],
        uint i [[thread_position_in_grid]]) {
  uint row;
  uint column;

  if (i < shape.m * shape.k) {
    row    = i / shape.k;
    column = i % shape.k;
    a[row * shape.a_stride + column] = float(int((row * 3u + column * 5u + shape.round * 2u) % 7u) - 3);
  }

  if (i < shape.k * shape.n) {
    row    = i / shape.n;
    column = i % shape.n;
    b[row * shape.b_stride + column] = float(int((row * 2u + column * 5u + shape.round * 3u) % 9u) - 4);
  }

  if (i < shape.m * shape.n) {
    row    = i / shape.n;
    column = i % shape.n;
    c[row * shape.c_stride + column] = -10000.0f;
    witness[shape.round * shape.m * shape.n + i] = -20000.0f;
  }
}

kernel void
consume(device float *c [[buffer(2)]],
        device float *witness [[buffer(3)]],
        constant Shape &shape [[buffer(30)]],
        uint i [[thread_position_in_grid]]) {
  uint row;
  uint column;

  if (i >= shape.m * shape.n) {
    return;
  }

  row    = i / shape.n;
  column = i % shape.n;
  witness[shape.round * shape.m * shape.n + i] = c[row * shape.c_stride + column] + float(shape.round * 10000u + 1u);
}
