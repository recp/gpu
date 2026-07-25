// Generated WGSL

struct DrawConstants {
    tint: vec4f,
}

struct usl_buffer_g3_b0 {
    value: DrawConstants,
}
@group(3) @binding(0) var<uniform> usl_g3_b0: usl_buffer_g3_b0;

@vertex
fn push_vs(@builtin(vertex_index) vertexId: u32) -> @builtin(position) vec4f {
    switch (vertexId) {
        case 0u: {
            return vec4f(0.0, 0.68, 0.0, 1.0);
        }
        case 1u: {
            return vec4f(-0.72, -0.62, 0.0, 1.0);
        }
        default: {
            return vec4f(0.72, -0.62, 0.0, 1.0);
        }
    }
}

@fragment
fn push_fs() -> @location(0) vec4f {
    return usl_g3_b0.value.tint;
}
