// Generated WGSL

struct StorageVertexIn {
    @location(0) position: vec4<f32>,
    @location(1) uv: vec2<f32>,
}

struct StorageVertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@group(0) @binding(0) var usl_g0_b0: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(1) var usl_g0_b1: texture_storage_2d<rgba8unorm, write>;

@group(1) @binding(0) var usl_g1_b0: texture_2d<f32>;
@group(1) @binding(1) var usl_g1_b1: texture_2d<f32>;

@group(1) @binding(2) var usl_g1_b2: sampler;

@compute @workgroup_size(8, 8, 1)
fn paint_cs(@builtin(global_invocation_id) gid: vec3<u32>) {
    let r5: f32 = (0.003921569 * f32(gid.x));
    let r9: f32 = (0.003921569 * f32(gid.y));
    textureStore(usl_g0_b0, vec2<i32>(gid.xy), vec4<f32>(r5, 0.08, r9, 1.0));
    textureStore(usl_g0_b1, vec2<i32>(gid.xy), vec4<f32>(0.08, r5, r9, 1.0));
}

@vertex
fn storage_vs(input: StorageVertexIn) -> StorageVertexOut {
    return StorageVertexOut(input.position, input.uv);
}

@fragment
fn storage_fs(input: StorageVertexOut) -> @location(0) vec4<f32> {
    let r30: f32 = (2.0 * input.uv.x);
    let r33: vec2<f32> = vec2<f32>(r30, input.uv.y);
    if ((input.uv.x < 0.5)) {
        return textureSampleLevel(usl_g1_b0, usl_g1_b2, r33, 0.0);
    } else {
        return textureSampleLevel(usl_g1_b1, usl_g1_b2, vec2<f32>((r30 - 1.0), input.uv.y), 0.0);
    }
}
