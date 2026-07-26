// Generated WGSL

struct VSOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var usl_g0_b0: texture_2d<f32>;
@group(0) @binding(1) var usl_g0_b1: texture_2d<f32>;
@group(0) @binding(2) var usl_g0_b2: texture_2d<f32>;
@group(0) @binding(3) var usl_g0_b3: texture_2d<f32>;

@group(0) @binding(4) var usl_g0_b4: sampler;
@group(0) @binding(5) var usl_g0_b5: sampler;
@group(0) @binding(6) var usl_g0_b6: sampler;
@group(0) @binding(7) var usl_g0_b7: sampler;

struct usl_buffer_g0_b2 {
    values: array<vec4f>,
}
@group(0) @binding(8) var<storage, read> usl_g0_b8: usl_buffer_g0_b2;
@group(0) @binding(9) var<storage, read> usl_g0_b9: usl_buffer_g0_b2;
@group(0) @binding(10) var<storage, read> usl_g0_b10: usl_buffer_g0_b2;
@group(0) @binding(11) var<storage, read> usl_g0_b11: usl_buffer_g0_b2;

fn usl_br_g0_b2(di: u32, ei: u32) -> vec4f {
    switch (di) {
        case 0u: { return usl_g0_b8.values[ei]; }
        case 1u: { return usl_g0_b9.values[ei]; }
        case 2u: { return usl_g0_b10.values[ei]; }
        case 3u: { return usl_g0_b11.values[ei]; }
        default: { return vec4f(); }
    }
}

@vertex
fn descriptor_array_vs(@builtin(vertex_index) vertexId: u32) -> VSOut {
    switch (vertexId) {
        case 0u: {
            return VSOut(vec4f(-0.88, -0.76, 0.0, 1.0), vec2f(0.0, 1.0));
        }
        case 1u: {
            return VSOut(vec4f(0.88, -0.76, 0.0, 1.0), vec2f(1.0));
        }
        case 2u: {
            return VSOut(vec4f(-0.88, 0.76, 0.0, 1.0), vec2f(0.0));
        }
        case 3u: {
            return VSOut(vec4f(-0.88, 0.76, 0.0, 1.0), vec2f(0.0));
        }
        case 4u: {
            return VSOut(vec4f(0.88, -0.76, 0.0, 1.0), vec2f(1.0));
        }
        default: {
            return VSOut(vec4f(0.88, 0.76, 0.0, 1.0), vec2f(1.0, 0.0));
        }
    }
}

@fragment
fn descriptor_array_fs(input: VSOut) -> @location(0) vec4f {
    let r48: u32 = (min(u32((2.0 * input.uv.y)), 1u) * 2u);
    let r50: u32 = (min(u32((2.0 * input.uv.x)), 1u) + r48);
    var r62: vec4f;
    switch (r50) {
        case 0u: {
            r62 = textureSampleLevel(usl_g0_b0, usl_g0_b4, fract((vec2f(2.0) * input.uv)), 0.0);
        }
        case 1u: {
            r62 = textureSampleLevel(usl_g0_b1, usl_g0_b5, fract((vec2f(2.0) * input.uv)), 0.0);
        }
        case 2u: {
            r62 = textureSampleLevel(usl_g0_b2, usl_g0_b6, fract((vec2f(2.0) * input.uv)), 0.0);
        }
        case 3u: {
            r62 = textureSampleLevel(usl_g0_b3, usl_g0_b7, fract((vec2f(2.0) * input.uv)), 0.0);
        }
        default: {}
    }
    return (r62 * usl_br_g0_b2(r50, 0u));
}
