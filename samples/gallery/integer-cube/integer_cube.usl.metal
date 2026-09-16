#include <metal_stdlib>
using namespace metal;

struct VSOut {
    float4 position [[position]];
    float2 uv [[attribute(0)]];
};


constexpr sampler c0(min_filter::nearest, mag_filter::nearest, mip_filter::nearest, address::clamp_to_edge);

vertex VSOut integer_cube_vs(uint vertexId [[vertex_id]]) {
    switch (vertexId) {
        case 0u: {
            return VSOut{float4(-0.92, -0.78, 0.0, 1.0), float2(0.0, 1.0)};
        }
        case 1u: {
            return VSOut{float4(0.92, -0.78, 0.0, 1.0), float2(1.0)};
        }
        case 2u: {
            return VSOut{float4(-0.92, 0.78, 0.0, 1.0), float2(0.0)};
        }
        case 3u: {
            return VSOut{float4(-0.92, 0.78, 0.0, 1.0), float2(0.0)};
        }
        case 4u: {
            return VSOut{float4(0.92, -0.78, 0.0, 1.0), float2(1.0)};
        }
        default: {
            return VSOut{float4(0.92, 0.78, 0.0, 1.0), float2(1.0, 0.0)};
        }
    }
}

fragment float4 integer_cube_nearest_fs(VSOut input [[stage_in]], texturecube<uint> environment [[texture(0)]]) {
    float r2 = fma(6.2831855, input.uv.x, -3.1415927);
    float r4 = fma(-3.1415927, input.uv.y, 1.5707964);
    float r5;
    float r8 = sincos(r4, r5);
    float r9;
    float r6 = sincos(r2, r9);
    return 0.003921569 * float4(environment.sample(c0, float3(r6 * r5, r8, r9 * r5)));
}

fragment float4 integer_cube_level_fs(VSOut input [[stage_in]], texturecube<uint> environment [[texture(0)]]) {
    float r2 = fma(6.2831855, input.uv.x, -3.1415927);
    float r4 = fma(-3.1415927, input.uv.y, 1.5707964);
    float r5;
    float r8 = sincos(r4, r5);
    float r9;
    float r6 = sincos(r2, r9);
    return 0.003921569 * float4(environment.sample(c0, float3(r6 * r5, r8, r9 * r5), level(1.0)));
}

fragment float4 integer_cube_gradient_fs(VSOut input [[stage_in]], texturecube<uint> environment [[texture(0)]]) {
    float r2 = fma(6.2831855, input.uv.x, -3.1415927);
    float r4 = fma(-3.1415927, input.uv.y, 1.5707964);
    float r5;
    float r8 = sincos(r4, r5);
    float r9;
    float r6 = sincos(r2, r9);
    float3 r11 = float3(r6 * r5, r8, r9 * r5);
    float3 r12 = dfdx(r11);
    float3 r13 = dfdy(r11);
    return 0.003921569 * float4(environment.sample(c0, r11, gradientcube(r12, r13)));
}

fragment float4 integer_cube_bias_fs(VSOut input [[stage_in]], texturecube<uint> environment [[texture(0)]]) {
    float r2 = fma(6.2831855, input.uv.x, -3.1415927);
    float r4 = fma(-3.1415927, input.uv.y, 1.5707964);
    float r5;
    float r8 = sincos(r4, r5);
    float r9;
    float r6 = sincos(r2, r9);
    return 0.003921569 * float4(environment.sample(c0, float3(r6 * r5, r8, r9 * r5), bias(6.0)));
}
