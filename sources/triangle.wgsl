// Generated WGSL

@vertex
fn tri_vs(@builtin(vertex_index) vertexId: u32) -> @builtin(position) vec4<f32> {
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
fn tri_fs() -> @location(0) vec4<f32> {
    return vec4<f32>(1.0, 0.38, 0.08, 1.0);
}
