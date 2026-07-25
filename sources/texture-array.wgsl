// Generated WGSL

struct VSOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var usl_g0_b0: texture_2d_array<f32>;

@group(0) @binding(1) var usl_g0_b1: texture_storage_2d_array<rgba8unorm, write>;

@group(1) @binding(0) var usl_g1_b0: texture_2d_array<f32>;

@group(1) @binding(1) var usl_g1_b1: texture_2d_array<f32>;

@group(1) @binding(2) var usl_g1_b2: sampler;

@compute @workgroup_size(4, 1, 1)
fn line_copy_cs(@builtin(global_invocation_id) gid: vec3u) {
    textureStore(usl_g0_b1, vec2i(i32(gid.x), 0), i32(gid.y), (textureLoad(usl_g0_b0, vec2i(i32(gid.x), 0), i32(gid.y), 0) * vec4f(1.0, (0.125 * f32(textureDimensions(usl_g0_b0, 0u).x)), (0.5 * f32(textureNumLayers(usl_g0_b0))), 1.0)));
}

@vertex
fn array_vs(@builtin(vertex_index) vertexId: u32) -> VSOut {
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
fn array_fs(input: VSOut) -> @location(0) vec4f {
    let r63 = step(0.5, input.uv.x);
    let r69: f32 = ((2.0 * input.uv.x) - r63);
    if ((input.uv.y < 0.5)) {
        return textureSampleLevel(usl_g1_b0, usl_g1_b2, vec2f(r69, fract((input.uv.y * 2.0))), i32(r63), 0.0);
    } else {
        return textureLoad(usl_g1_b1, vec2i(i32((r69 * f32(textureDimensions(usl_g1_b1, 0u).x))), 0), i32(r63), 0);
    }
}
