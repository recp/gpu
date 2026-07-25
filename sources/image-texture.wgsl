// Generated WGSL

struct ImageVertex {
    @location(0) position: vec2<f32>,
    @location(1) uv: vec2<f32>,
}

struct ImageOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

struct ImageUniforms {
    scale: vec2<f32>,
}

struct usl_buffer_g0_b0 {
    value: ImageUniforms,
}
@group(0) @binding(0) var<uniform> usl_g0_b0: usl_buffer_g0_b0;

@group(0) @binding(1) var usl_g0_b1: texture_2d<f32>;

@group(0) @binding(2) var usl_g0_b2: sampler;

@vertex
fn image_vs(v: ImageVertex) -> ImageOut {
    return ImageOut(vec4<f32>((v.position * usl_g0_b0.value.scale), 0.0, 1.0), v.uv);
}

@fragment
fn image_fs(input: ImageOut) -> @location(0) vec4<f32> {
    return textureSample(usl_g0_b1, usl_g0_b2, input.uv);
}
