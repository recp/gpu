#include <metal_stdlib>
using namespace metal;

struct VSIn {
    float2 position [[attribute(0)]];
};

struct FragmentUniforms {
    float4 tint;
};

vertex float4 tri_vs(VSIn v [[stage_in]]) {
    return float4(v.position, 0.0, 1.0);
}

fragment float4 tri_fs(constant FragmentUniforms& uniforms [[buffer(0)]]) {
    return uniforms.tint;
}

