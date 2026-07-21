// Generated WGSL

struct CubeUniforms {
    mvp: mat4x4<f32>,
    model: mat4x4<f32>,
}

struct CubeIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
}

struct CubeOut {
    @builtin(position) position: vec4<f32>,
    @location(0) normal: vec3<f32>,
    @location(1) uv: vec2<f32>,
}

struct usl_buffer_g0_b0 {
    value: CubeUniforms,
}
@group(0) @binding(0) var<uniform> usl_g0_b0: usl_buffer_g0_b0;

@group(0) @binding(1) var usl_g0_b1: texture_2d<f32>;

@group(1) @binding(0) var usl_g1_b0: sampler;

@vertex
fn cube_vs(v: CubeIn) -> CubeOut {
    return CubeOut((usl_g0_b0.value.mvp * vec4<f32>(v.position, 1.0)), normalize((usl_g0_b0.value.model * vec4<f32>(v.normal, 0.0)).xyz), v.uv);
}

@fragment
fn cube_fs(input: CubeOut) -> @location(0) vec4<f32> {
    return (textureSample(usl_g0_b1, usl_g1_b0, input.uv) * vec4<f32>(vec3<f32>(fma(0.72, max(dot(normalize(input.normal), vec3<f32>(0.43554053, 0.7259009, 0.5323273)), 0.0), 0.28)), 1.0));
}
