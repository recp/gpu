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

struct usl_buffer_g3_b0 {
    value: ComputeConstants,
}
@group(3) @binding(0) var<uniform> usl_g3_b0: usl_buffer_g3_b0;

@compute @workgroup_size(1, 1, 1)
fn fill_vertices(@builtin(global_invocation_id) gid: vec3<u32>) {
    switch (gid.x) {
        case 0u: {
            usl_g0_b0.values[gid.x].position = vec4<f32>(-0.68, -0.62, 0.0, 1.0);
            usl_g0_b0.values[gid.x].color = (vec4<f32>(1.0, 0.18, 0.08, 1.0) * usl_g3_b0.value.tint);
        }
        case 1u: {
            usl_g0_b0.values[gid.x].position = vec4<f32>(0.68, -0.62, 0.0, 1.0);
            usl_g0_b0.values[gid.x].color = (vec4<f32>(0.12, 1.0, 0.32, 1.0) * usl_g3_b0.value.tint);
        }
        default: {
            usl_g0_b0.values[gid.x].position = vec4<f32>(0.0, 0.7, 0.0, 1.0);
            usl_g0_b0.values[gid.x].color = (vec4<f32>(0.12, 0.38, 1.0, 1.0) * usl_g3_b0.value.tint);
        }
    }
    return;
}

@vertex
fn tri_vs(v: VSIn) -> VSOut {
    return VSOut(v.position, v.color);
}

@fragment
fn tri_fs(i: VSOut) -> @location(0) vec4<f32> {
    return i.color;
}
