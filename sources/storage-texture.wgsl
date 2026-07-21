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

@group(1) @binding(0) var usl_g1_b0: texture_2d<f32>;

@group(1) @binding(1) var usl_g1_b1: sampler;

@compute @workgroup_size(8, 8, 1)
fn paint_cs(@builtin(global_invocation_id) gid: vec3<u32>) {
    textureStore(usl_g0_b0, vec2<i32>(gid.xy), vec4<f32>((0.003921569 * f32(gid.x)), (0.003921569 * f32(gid.y)), fma(0.0009765625, f32((gid.x + gid.y)), 0.25), 1.0));
}

@vertex
fn storage_vs(input: StorageVertexIn) -> StorageVertexOut {
    return StorageVertexOut(input.position, input.uv);
}

@fragment
fn storage_fs(input: StorageVertexOut) -> @location(0) vec4<f32> {
    return textureSample(usl_g1_b0, usl_g1_b1, input.uv);
}
