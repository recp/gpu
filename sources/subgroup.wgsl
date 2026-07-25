// Generated WGSL

enable subgroups;

struct GeneratedVertex {
    position: vec4<f32>,
    color: vec4<f32>,
}

struct ComputeConstants {
    tint: vec4<f32>,
}

struct VSIn {
    @location(0) position: vec4<f32>,
    @location(1) color: vec4<f32>,
}

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
}

struct usl_buffer_g0_b0 {
    values: array<GeneratedVertex>,
}
@group(0) @binding(0) var<storage, read_write> usl_g0_b0: usl_buffer_g0_b0;

struct usl_buffer_g3_b0 {
    value: ComputeConstants,
}
@group(3) @binding(0) var<uniform> usl_g3_b0: usl_buffer_g3_b0;

@compute @workgroup_size(32, 1, 1)
fn fill_subgroup_vertices(@builtin(global_invocation_id) gid: vec3<u32>) {
    let r10: f32 = fma(0.18, f32((gid.x & 3u)), 0.28);
    let r13: f32 = subgroupShuffleXor(r10, 1u);
    let r16: bool = (gid.x == 0u);
    let r19: bool = (gid.x == 1u);
    usl_g0_b0.values[gid.x].position = vec4<f32>(select(select(0.0, 0.68, r19), -0.68, r16), select(-0.62, 0.7, (gid.x == 2u)), 0.0, 1.0);
    usl_g0_b0.values[gid.x].color = (select(select(vec4<f32>(0.12, r13, 1.0, 1.0), vec4<f32>(r13, 1.0, 0.3, 1.0), r19), vec4<f32>(1.0, r13, 0.08, 1.0), r16) * usl_g3_b0.value.tint);
}

@vertex
fn tri_vs(v: VSIn) -> VSOut {
    return VSOut(v.position, v.color);
}

@fragment
fn tri_fs(i: VSOut) -> @location(0) vec4<f32> {
    return i.color;
}
