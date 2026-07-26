// Generated WGSL

struct CompressedOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

struct CompressedUniforms {
    scale: vec2f,
}

struct usl_buffer_g0_b0 {
    value: CompressedUniforms,
}
@group(0) @binding(0) var<uniform> usl_g0_b0: usl_buffer_g0_b0;

@group(0) @binding(1) var usl_g0_b1: texture_2d<f32>;

@group(0) @binding(2) var usl_g0_b2: sampler;

@vertex
fn compressed_vs(@builtin(vertex_index) vertexId: u32) -> CompressedOut {
    let r4 = f32((vertexId & 1u));
    let r6 = f32((vertexId >> 1u));
    return CompressedOut(vec4f((((vec2f(2.0) * vec2f(r4, r6)) - vec2f(1.0)) * usl_g0_b0.value.scale), 0.0, 1.0), vec2f(r4, (1.0 - r6)));
}

@fragment
fn compressed_fs(input: CompressedOut) -> @location(0) vec4f {
    return textureSample(usl_g0_b1, usl_g0_b2, input.uv);
}
