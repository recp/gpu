// Generated WGSL

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

struct usl_buffer_g0_b1 {
    values: array<u32>,
}
@group(0) @binding(1) var<storage, read> usl_g0_b1: usl_buffer_g0_b1;

struct usl_buffer_g3_b0 {
    value: ComputeConstants,
}
@group(3) @binding(0) var<uniform> usl_g3_b0: usl_buffer_g3_b0;

@compute @workgroup_size(1, 1, 1)
fn fill_timestamp_vertices(@builtin(global_invocation_id) gid: vec3<u32>) {
    let r22: bool = ((usl_g0_b1.values[2] != 4294967295u) || (usl_g0_b1.values[3] != 4294967295u));
    let r30: bool = ((usl_g0_b1.values[4] != 4294967295u) || (usl_g0_b1.values[5] != 4294967295u));
    let r46: bool = ((usl_g0_b1.values[8] != 4294967295u) || (usl_g0_b1.values[9] != 4294967295u));
    let r52: bool = ((((usl_g0_b1.values[0] == 4294967295u) && (usl_g0_b1.values[1] == 4294967295u)) && r22) && r30);
    let r56: bool = ((r52 && ((usl_g0_b1.values[6] != 4294967295u) || (usl_g0_b1.values[7] != 4294967295u))) && r46);
    let r59: bool = (gid.x == 0u);
    let r62: bool = (gid.x == 1u);
    let r80: vec4<f32> = select(vec4<f32>(0.74, 1.0, 0.1, 1.0), vec4<f32>(0.1, 0.72, 1.0, 1.0), r62);
    usl_g0_b0.values[gid.x].position = vec4<f32>(select(select(0.0, 0.68, r62), -0.68, r59), select(-0.62, 0.7, (gid.x == 2u)), 0.0, 1.0);
    let r97: vec4<f32> = (select(vec4<f32>(1.0, 0.06, 0.02, 1.0), select(r80, vec4<f32>(0.06, 1.0, 0.42, 1.0), r59), r56) * usl_g3_b0.value.tint);
    usl_g0_b0.values[gid.x].color = r97;
}

@vertex
fn tri_vs(v: VSIn) -> VSOut {
    return VSOut(v.position, v.color);
}

@fragment
fn tri_fs(i: VSOut) -> @location(0) vec4<f32> {
    return i.color;
}
