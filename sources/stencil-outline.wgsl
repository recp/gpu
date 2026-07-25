// Generated WGSL

struct VSOut {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
}

@vertex
fn fill_vs(@builtin(vertex_index) vertexId: u32) -> VSOut {
    return VSOut(vec4f(select(0.48, -0.48, ((vertexId & 1u) == 0u)), select(0.48, -0.48, ((vertexId & 2u) == 0u)), 0.0, 1.0), vec4f(0.05, 0.68, 0.92, 1.0));
}

@vertex
fn outline_vs(@builtin(vertex_index) vertexId: u32) -> VSOut {
    return VSOut(vec4f(select(0.57, -0.57, ((vertexId & 1u) == 0u)), select(0.57, -0.57, ((vertexId & 2u) == 0u)), 0.0, 1.0), vec4f(1.0, 0.34, 0.06, 1.0));
}

@fragment
fn solid_fs(input: VSOut) -> @location(0) vec4f {
    return input.color;
}
