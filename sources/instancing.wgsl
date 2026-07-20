// Generated WGSL

struct DrawUniforms {
    transform: vec4<f32>,
    tint: vec4<f32>,
}

struct VSIn {
    @location(0) position: vec2<f32>,
    @location(1) instanceOffset: vec2<f32>,
    @location(2) instanceColor: vec4<f32>,
}

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
}

struct usl_buffer_g0_b0 {
    value: DrawUniforms,
}
@group(0) @binding(0) var<uniform> usl_g0_b0: usl_buffer_g0_b0;

@vertex
fn instanced_vs(v: VSIn) -> VSOut {
    return VSOut(vec4<f32>((usl_g0_b0.value.transform.xy + fma(v.position, vec2<f32>(usl_g0_b0.value.transform.z), v.instanceOffset)), 0.0, 1.0), (v.instanceColor * usl_g0_b0.value.tint));
}

@fragment
fn instanced_fs(i: VSOut) -> @location(0) vec4<f32> {
    return i.color;
}
