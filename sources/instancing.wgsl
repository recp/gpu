// Generated WGSL

struct DrawUniforms {
    transform: vec4f,
    tint: vec4f,
}

struct VSIn {
    @location(0) position: vec2f,
    @location(1) instanceOffset: vec2f,
    @location(2) instanceColor: vec4f,
}

struct VSOut {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
}

struct usl_buffer_g0_b0 {
    value: DrawUniforms,
}
@group(0) @binding(0) var<uniform> usl_g0_b0: usl_buffer_g0_b0;

@vertex
fn instanced_vs(v: VSIn) -> VSOut {
    return VSOut(vec4f((usl_g0_b0.value.transform.xy + fma(v.position, vec2f(usl_g0_b0.value.transform.z), v.instanceOffset)), 0.0, 1.0), (v.instanceColor * usl_g0_b0.value.tint));
}

@fragment
fn instanced_fs(i: VSOut) -> @location(0) vec4f {
    return i.color;
}
