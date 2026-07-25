// Generated WGSL

struct BloomVertexOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var usl_g0_b0: texture_2d<f32>;

@group(0) @binding(1) var usl_g0_b1: texture_storage_2d<rgba8unorm, write>;

@group(1) @binding(0) var usl_g1_b0: texture_2d<f32>;

@group(1) @binding(1) var usl_g1_b1: texture_2d<f32>;

@group(1) @binding(2) var usl_g1_b2: sampler;

@compute @workgroup_size(8, 8, 1)
fn blur_horizontal(@builtin(global_invocation_id) usl_builtin_2: vec3u) {
    let gid: vec2u = usl_builtin_2.xy;
    let r94: vec2i = vec2i(gid);
    let r95: vec2i = vec2i(1, 0);
    let usl_dims_r138 = textureDimensions(usl_g0_b0, 0u);
    let r144: vec2i = vec2i((i32(usl_dims_r138.x) + -1), (i32(usl_dims_r138.y) + -1));
    let r145: vec2i = vec2i(0);
    let r152: vec4f = (vec4f(0.1945946) * textureLoad(usl_g0_b0, clamp((r94 + r95), r145, r144), 0));
    let r157: vec4f = (vec4f(0.1945946) * textureLoad(usl_g0_b0, clamp((r94 - r95), r145, r144), 0));
    let r159: vec2i = (vec2i(2) * r95);
    let r163: vec4f = (vec4f(0.1216216) * textureLoad(usl_g0_b0, clamp((r94 + r159), r145, r144), 0));
    let r164: vec4f = ((fma(vec4f(0.227027), textureLoad(usl_g0_b0, clamp(r94, r145, r144), 0), r152) + r157) + r163);
    let r170: vec4f = (r164 + (vec4f(0.1216216) * textureLoad(usl_g0_b0, clamp((r94 - r159), r145, r144), 0)));
    let r171: vec2i = (vec2i(3) * r95);
    let r176: vec4f = (r170 + (vec4f(0.054054) * textureLoad(usl_g0_b0, clamp((r94 + r171), r145, r144), 0)));
    let r182: vec4f = (r176 + (vec4f(0.054054) * textureLoad(usl_g0_b0, clamp((r94 - r171), r145, r144), 0)));
    let r183: vec2i = (vec2i(4) * r95);
    let r188: vec4f = (r182 + (vec4f(0.016216) * textureLoad(usl_g0_b0, clamp((r94 + r183), r145, r144), 0)));
    let r194: vec4f = (r188 + (vec4f(0.016216) * textureLoad(usl_g0_b0, clamp((r94 - r183), r145, r144), 0)));
    textureStore(usl_g0_b1, vec2i(gid), r194);
}

@compute @workgroup_size(8, 8, 1)
fn blur_vertical(@builtin(global_invocation_id) usl_builtin_2: vec3u) {
    let gid: vec2u = usl_builtin_2.xy;
    let r100: vec2i = vec2i(gid);
    let r101: vec2i = vec2i(0, 1);
    let usl_dims_r195 = textureDimensions(usl_g0_b0, 0u);
    let r201: vec2i = vec2i((i32(usl_dims_r195.x) + -1), (i32(usl_dims_r195.y) + -1));
    let r202: vec2i = vec2i(0);
    let r209: vec4f = (vec4f(0.1945946) * textureLoad(usl_g0_b0, clamp((r100 + r101), r202, r201), 0));
    let r214: vec4f = (vec4f(0.1945946) * textureLoad(usl_g0_b0, clamp((r100 - r101), r202, r201), 0));
    let r216: vec2i = (vec2i(2) * r101);
    let r220: vec4f = (vec4f(0.1216216) * textureLoad(usl_g0_b0, clamp((r100 + r216), r202, r201), 0));
    let r221: vec4f = ((fma(vec4f(0.227027), textureLoad(usl_g0_b0, clamp(r100, r202, r201), 0), r209) + r214) + r220);
    let r227: vec4f = (r221 + (vec4f(0.1216216) * textureLoad(usl_g0_b0, clamp((r100 - r216), r202, r201), 0)));
    let r228: vec2i = (vec2i(3) * r101);
    let r233: vec4f = (r227 + (vec4f(0.054054) * textureLoad(usl_g0_b0, clamp((r100 + r228), r202, r201), 0)));
    let r239: vec4f = (r233 + (vec4f(0.054054) * textureLoad(usl_g0_b0, clamp((r100 - r228), r202, r201), 0)));
    let r240: vec2i = (vec2i(4) * r101);
    let r245: vec4f = (r239 + (vec4f(0.016216) * textureLoad(usl_g0_b0, clamp((r100 + r240), r202, r201), 0)));
    let r251: vec4f = (r245 + (vec4f(0.016216) * textureLoad(usl_g0_b0, clamp((r100 - r240), r202, r201), 0)));
    textureStore(usl_g0_b1, vec2i(gid), r251);
}

@vertex
fn bloom_vs(@builtin(vertex_index) vertexId: u32) -> BloomVertexOut {
    switch (vertexId) {
        case 0u: {
            return BloomVertexOut(vec4f(-1.0, -1.0, 0.0, 1.0), vec2f(0.0, 1.0));
        }
        case 1u: {
            return BloomVertexOut(vec4f(3.0, -1.0, 0.0, 1.0), vec2f(2.0, 1.0));
        }
        default: {
            return BloomVertexOut(vec4f(-1.0, 3.0, 0.0, 1.0), vec2f(0.0, -1.0));
        }
    }
}

@fragment
fn bloom_fs(input: BloomVertexOut) -> @location(0) vec4f {
    return vec4f(saturate(fma(vec3f(1.65), textureSampleLevel(usl_g1_b1, usl_g1_b2, input.uv, 0.0).rgb, textureSampleLevel(usl_g1_b0, usl_g1_b2, input.uv, 0.0).rgb)), 1.0);
}
