// Generated WGSL

struct FullscreenOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

struct MRTOut {
    @location(0) alphaBlend: vec4<f32>,
    @location(1) additive: vec4<f32>,
}

@group(0) @binding(0) var usl_g0_b0: texture_2d<f32>;

@group(0) @binding(1) var usl_g0_b1: sampler;

@vertex
fn fullscreen_vs(@builtin(vertex_index) vertexId: u32) -> FullscreenOut {
    switch (vertexId) {
        case 0u: {
            return FullscreenOut(vec4<f32>(-1.0, -1.0, 0.0, 1.0), vec2<f32>(0.0, 1.0));
        }
        case 1u: {
            return FullscreenOut(vec4<f32>(3.0, -1.0, 0.0, 1.0), vec2<f32>(2.0, 1.0));
        }
        default: {
            return FullscreenOut(vec4<f32>(-1.0, 3.0, 0.0, 1.0), vec2<f32>(0.0, -1.0));
        }
    }
}

@fragment
fn mrt_fs(input: FullscreenOut) -> MRTOut {
    let r28 = saturate(fma(-2.4, length((input.uv - vec2<f32>(0.5))), 1.2));
    return MRTOut(vec4<f32>(1.0, 0.18, 0.03, (0.88 * r28)), vec4<f32>(0.02, 0.42, 1.0, (0.72 * r28)));
}

@fragment
fn composite_fs(input: FullscreenOut) -> @location(0) vec4<f32> {
    return textureSample(usl_g0_b0, usl_g0_b1, input.uv);
}
