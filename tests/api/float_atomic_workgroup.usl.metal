#include <metal_stdlib>
using namespace metal;

kernel void float_atomic(device float* values [[buffer(0)]], device float* output [[buffer(1)]], uint3 group [[threadgroup_position_in_grid]], uint local [[thread_index_in_threadgroup]], uint3 gid [[thread_position_in_grid]]) {
    threadgroup float wg0[1];
    if (local == 0u) {
        wg0[0] = 0.0;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    float r9 = atomic_fetch_add_explicit(reinterpret_cast<threadgroup atomic_float*>(&wg0[0u]), 1.0, memory_order_relaxed);
    output[gid.x] = r9;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (local == 0u) {
        values[group.x] = wg0[0];
    }
}
