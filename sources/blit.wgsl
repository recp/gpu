// Generated WGSL

struct BlitVertex {
    @location(0) position: vec2f,
    @location(1) uv: vec2f,
}

struct BlitOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var usl_g0_b0: texture_2d<f32>;

@group(0) @binding(1) var usl_g0_b1: sampler;

@vertex
fn blit_panel_vs(v: BlitVertex) -> BlitOut {
    return BlitOut(vec4f(v.position, 0.0, 1.0), v.uv);
}

@fragment
fn blit_panel_fs(input: BlitOut) -> @location(0) vec4f {
    return textureSample(usl_g0_b0, usl_g0_b1, input.uv);
}
