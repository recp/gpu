#include <metal_stdlib>
using namespace metal;

struct StorageVertexIn {
    float4 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
};

struct StorageVertexOut {
    float4 position [[position]];
    float2 uv [[attribute(0)]];
};

kernel void paint_cs(array<texture2d<float, access::write>, 2> outputImages [[texture(0)]], uint3 gid [[thread_position_in_grid]]) {
    float r2 = 0.003921569 * float(gid.x);
    float r5 = 0.003921569 * float(gid.y);
    outputImages[0].write(float4(r2, 0.08, r5, 1.0), gid.xy);
    outputImages[1].write(float4(0.08, r2, r5, 1.0), gid.xy);
}

kernel void filter_cs(array<texture2d<float, access::read>, 2> inputImages [[texture(2)]], array<texture2d<float, access::write>, 2> outputImages [[texture(4)]], uint3 gid [[thread_position_in_grid]]) {
    float4 r2 = inputImages[0].read(gid.xy);
    float4 r8 = inputImages[1].read(gid.xy);
    outputImages[0].write(float4(r2[2], r2[0], r2[1], r2[3]), gid.xy);
    outputImages[1].write(float4(r8[1], r8[2], r8[0], r8[3]), gid.xy);
}

vertex StorageVertexOut storage_vs(StorageVertexIn input [[stage_in]]) {
    return StorageVertexOut{input.position, input.uv};
}

fragment float4 storage_fs(StorageVertexOut input [[stage_in]], array<texture2d<float>, 2> colorTex [[texture(0)]], sampler colorSampler [[sampler(0)]]) {
    if (input.uv.x < 0.5) {
        return colorTex[0].sample(colorSampler, float2(2.0 * input.uv.x, input.uv.y), level(0.0));
    } else {
        return colorTex[1].sample(colorSampler, float2(fma(2.0, input.uv.x, -1.0), input.uv.y), level(0.0));
    }
}
