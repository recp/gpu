// Generated WGSL

struct ShadowOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@group(0) @binding(0) var usl_g0_b0: texture_depth_2d;

@group(0) @binding(1) var usl_g0_b1: sampler_comparison;

@vertex
fn shadow_depth_vs(@builtin(vertex_index) vertexId: u32) -> @builtin(position) vec4<f32> {
    switch (vertexId) {
        case 0u: {
            return vec4<f32>(0.0, 0.72, 0.32, 1.0);
        }
        case 1u: {
            return vec4<f32>(-0.76, -0.62, 0.32, 1.0);
        }
        default: {
            return vec4<f32>(0.76, -0.62, 0.32, 1.0);
        }
    }
}

@fragment
fn shadow_depth_fs() -> @location(0) vec4<f32> {
    return vec4<f32>(0.08, 0.12, 0.2, 1.0);
}

@vertex
fn shadow_preview_vs(@builtin(vertex_index) vertexId: u32) -> ShadowOut {
    switch (vertexId) {
        case 0u: {
            return ShadowOut(vec4<f32>(-1.0, -1.0, 0.0, 1.0), vec2<f32>(0.0, 1.0));
        }
        case 1u: {
            return ShadowOut(vec4<f32>(3.0, -1.0, 0.0, 1.0), vec2<f32>(2.0, 1.0));
        }
        default: {
            return ShadowOut(vec4<f32>(-1.0, 3.0, 0.0, 1.0), vec2<f32>(0.0, -1.0));
        }
    }
}

@fragment
fn shadow_preview_fs(input: ShadowOut) -> @location(0) vec4<f32> {
    return fma(vec4<f32>(0.96, 0.45, 0.06, 0.0), vec4<f32>(textureSampleCompare(usl_g0_b0, usl_g0_b1, input.uv, 0.52)), vec4<f32>(0.04, 0.07, 0.12, 1.0));
}
