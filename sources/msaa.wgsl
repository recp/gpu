// Generated WGSL

struct VSOut {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
}

@vertex
fn msaa_vs(@builtin(vertex_index) vertexId: u32) -> VSOut {
    switch (vertexId) {
        case 0u: {
            return VSOut(vec4f(0.0, 0.76, 0.0, 1.0), vec3f(1.0, 0.28, 0.04));
        }
        case 1u: {
            return VSOut(vec4f(-0.78, -0.68, 0.0, 1.0), vec3f(1.0, 0.76, 0.08));
        }
        default: {
            return VSOut(vec4f(0.78, -0.68, 0.0, 1.0), vec3f(0.08, 0.64, 1.0));
        }
    }
}

@fragment
fn msaa_fs(input: VSOut) -> @location(0) vec4f {
    return vec4f(input.color, 1.0);
}
