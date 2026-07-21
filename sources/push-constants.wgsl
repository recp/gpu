// Generated WGSL

struct DrawConstants {
    tint: vec4<f32>,
}

struct usl_buffer_g3_b0 {
    value: DrawConstants,
}
@group(3) @binding(0) var<uniform> usl_g3_b0: usl_buffer_g3_b0;

@vertex
fn push_vs(@builtin(vertex_index) vertexId: u32) -> @builtin(position) vec4<f32> {
    switch (vertexId) {
        case 0u: {
            return vec4<f32>(0.0, 0.68, 0.0, 1.0);
        }
        case 1u: {
            return vec4<f32>(-0.72, -0.62, 0.0, 1.0);
        }
        default: {
            return vec4<f32>(0.72, -0.62, 0.0, 1.0);
        }
    }
}

@fragment
fn push_fs() -> @location(0) vec4<f32> {
    return usl_g3_b0.value.tint;
}
