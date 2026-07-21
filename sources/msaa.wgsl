// Generated WGSL

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
}

@vertex
fn msaa_vs(@builtin(vertex_index) vertexId: u32) -> VSOut {
    switch (vertexId) {
        case 0u: {
            return VSOut(vec4<f32>(0.0, 0.76, 0.0, 1.0), vec3<f32>(1.0, 0.28, 0.04));
        }
        case 1u: {
            return VSOut(vec4<f32>(-0.78, -0.68, 0.0, 1.0), vec3<f32>(1.0, 0.76, 0.08));
        }
        default: {
            return VSOut(vec4<f32>(0.78, -0.68, 0.0, 1.0), vec3<f32>(0.08, 0.64, 1.0));
        }
    }
}

@fragment
fn msaa_fs(input: VSOut) -> @location(0) vec4<f32> {
    return vec4<f32>(input.color, 1.0);
}
