#include <metal_stdlib>
using namespace metal;

kernel void float_atomic(device float* values [[buffer(0)]], device float* output [[buffer(1)]], uint3 gid [[thread_position_in_grid]]) {
    float r2 = atomic_fetch_add_explicit(reinterpret_cast<device atomic_float*>(&values[0u]), 1.0, memory_order_relaxed);
    output[gid.x] = r2;
    (void)atomic_fetch_add_explicit(reinterpret_cast<device atomic_float*>(&values[1u]), 0.5, memory_order_relaxed);
}
