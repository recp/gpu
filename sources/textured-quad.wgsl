// Generated WGSL

struct VSIn {
    @location(0) position: vec4<f32>,
    @location(1) uv: vec2<f32>,
}

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

struct FragmentUniforms {
    tint: vec4<f32>,
}

@group(1) @binding(0) var usl_g1_b0: texture_2d<f32>;

struct usl_buffer_g1_b1 {
    value: FragmentUniforms,
}
@group(1) @binding(1) var<uniform> usl_g1_b1: usl_buffer_g1_b1;

@group(0) @binding(0) var usl_static_sampler_b0: sampler;

@vertex
fn quad_vs(v: VSIn) -> VSOut {
    return VSOut(v.position, v.uv);
}

@fragment
fn quad_fs(input: VSOut) -> @location(0) vec4<f32> {
    return (textureSample(usl_g1_b0, usl_static_sampler_b0, input.uv) * usl_g1_b1.value.tint);
}
