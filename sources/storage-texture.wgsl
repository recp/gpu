// Generated WGSL

requires readonly_and_readwrite_storage_textures;

struct StorageVertexIn {
    @location(0) position: vec4f,
    @location(1) uv: vec2f,
}

struct StorageVertexOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var usl_g0_b0: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(1) var usl_g0_b1: texture_storage_2d<rgba8unorm, write>;

@group(1) @binding(0) var usl_g1_b0: texture_storage_2d<rgba8unorm, read>;
@group(1) @binding(1) var usl_g1_b1: texture_storage_2d<rgba8unorm, read>;

@group(2) @binding(0) var usl_g2_b0: texture_storage_2d<rgba8unorm, write>;
@group(2) @binding(1) var usl_g2_b1: texture_storage_2d<rgba8unorm, write>;

@group(3) @binding(0) var usl_g3_b0: texture_2d<f32>;
@group(3) @binding(1) var usl_g3_b1: texture_2d<f32>;

@group(3) @binding(2) var usl_g3_b2: sampler;

@compute @workgroup_size(8, 8, 1)
fn paint_cs(@builtin(global_invocation_id) gid: vec3u) {
    let r5: f32 = (0.003921569 * f32(gid.x));
    let r9: f32 = (0.003921569 * f32(gid.y));
    textureStore(usl_g0_b0, vec2i(gid.xy), vec4f(r5, 0.08, r9, 1.0));
    textureStore(usl_g0_b1, vec2i(gid.xy), vec4f(0.08, r5, r9, 1.0));
}

@compute @workgroup_size(8, 8, 1)
fn filter_cs(@builtin(global_invocation_id) gid: vec3u) {
    let r26 = textureLoad(usl_g1_b0, vec2i(gid.xy));
    let r30 = textureLoad(usl_g1_b1, vec2i(gid.xy));
    let r41 = vec4f(r26[2], r26[0], r26[1], r26[3]);
    textureStore(usl_g2_b0, vec2i(gid.xy), r41);
    let r52 = vec4f(r30[1], r30[2], r30[0], r30[3]);
    textureStore(usl_g2_b1, vec2i(gid.xy), r52);
}

@vertex
fn storage_vs(input: StorageVertexIn) -> StorageVertexOut {
    return StorageVertexOut(input.position, input.uv);
}

@fragment
fn storage_fs(input: StorageVertexOut) -> @location(0) vec4f {
    let r63: f32 = (2.0 * input.uv.x);
    let r66 = vec2f(r63, input.uv.y);
    if ((input.uv.x < 0.5)) {
        return textureSampleLevel(usl_g3_b0, usl_g3_b2, r66, 0.0);
    } else {
        return textureSampleLevel(usl_g3_b1, usl_g3_b2, vec2f((r63 - 1.0), input.uv.y), 0.0);
    }
}
