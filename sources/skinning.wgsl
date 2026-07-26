// Generated WGSL

struct SkinUniforms {
    bones: array<mat4x4f, 4>,
}

struct SkinVertex {
    @location(0) position: vec2f,
    @location(1) weights: vec2f,
    @location(2) color: vec4f,
    @location(3) joints: vec2u,
}

struct SkinOut {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
}

struct usl_buffer_g0_b0 {
    value: SkinUniforms,
}
@group(0) @binding(0) var<uniform> usl_g0_b0: usl_buffer_g0_b0;

@vertex
fn skin_vs(v: SkinVertex) -> SkinOut {
    let r4 = vec4f(v.position, 0.0, 1.0);
    let r23 = ((usl_g0_b0.value.bones[v.joints.y] * r4) * vec4f(v.weights.y));
    return SkinOut(fma((usl_g0_b0.value.bones[v.joints.x] * r4), vec4f(v.weights.x), r23), v.color);
}

@fragment
fn skin_fs(input: SkinOut) -> @location(0) vec4f {
    return input.color;
}
