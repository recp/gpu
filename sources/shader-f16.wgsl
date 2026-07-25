// Generated WGSL

enable f16;

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

@compute @workgroup_size(1, 1, 1)
fn fill_f16_vertices(@builtin(global_invocation_id) gid: vec3<u32>) {
    let r16: f32 = f32(fma(0.27001953, f16(gid.x), 0.41992188));
    let r19: bool = (gid.x == 0u);
    let r22: bool = (gid.x == 1u);
    let r41: vec4<f32> = select(vec4<f32>(0.12, r16, 1.0, 1.0), vec4<f32>(r16, 1.0, 0.3, 1.0), r22);
    usl_g0_b0.values[gid.x].position = vec4<f32>(select(select(0.0, 0.68, r22), -0.68, r19), select(-0.62, 0.7, (gid.x == 2u)), 0.0, 1.0);
    let r55: vec4<f32> = (select(r41, vec4<f32>(1.0, r16, 0.08, 1.0), r19) * usl_g3_b0.value.tint);
    usl_g0_b0.values[gid.x].color = r55;
}

@vertex
fn tri_vs(v: VSIn) -> VSOut {
    return VSOut(v.position, v.color);
}

@fragment
fn tri_fs(i: VSOut) -> @location(0) vec4<f32> {
    return i.color;
}
