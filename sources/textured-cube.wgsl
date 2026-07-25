// Generated WGSL

struct CubeUniforms {
    mvp: mat4x4f,
    model: mat4x4f,
}

struct CubeIn {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
}

struct CubeOut {
    @builtin(position) position: vec4f,
    @location(0) normal: vec3f,
    @location(1) uv: vec2f,
}

struct usl_buffer_g0_b0 {
    value: CubeUniforms,
}
@group(0) @binding(0) var<uniform> usl_g0_b0: usl_buffer_g0_b0;

@group(0) @binding(1) var usl_g0_b1: texture_2d<f32>;

@group(1) @binding(0) var usl_g1_b0: sampler;

@vertex
fn cube_vs(v: CubeIn) -> CubeOut {
    return CubeOut((usl_g0_b0.value.mvp * vec4f(v.position, 1.0)), normalize((usl_g0_b0.value.model * vec4f(v.normal, 0.0)).xyz), v.uv);
}

@fragment
fn cube_fs(input: CubeOut) -> @location(0) vec4f {
    let r24 = dot(normalize(input.normal), vec3f(0.43554053, 0.7259009, 0.5323273));
    let r27: f32 = fma(0.72, max(r24, 0.0), 0.28);
    return (textureSample(usl_g0_b1, usl_g1_b0, input.uv) * vec4f(r27, r27, r27, 1.0));
}
