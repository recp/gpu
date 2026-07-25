// Generated WGSL

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
}

@vertex
fn multi_vs(@builtin(vertex_index) vertexId: u32) -> VSOut {
    let r2: u32 = (vertexId % 3u);
    let r4: bool = (vertexId < 3u);
    let r9: bool = (r2 == 0u);
    let r17: f32 = select(select(0.78, -0.099999994, r4), select(0.099999994, -0.78, r4), (r2 == 1u));
    return VSOut(vec4<f32>(select(r17, select(0.44, -0.44, r4), r9), select(-0.5, 0.62, r9), 0.0, 1.0), select(vec4<f32>(0.08, 0.72, 1.0, 1.0), vec4<f32>(1.0, 0.3, 0.06, 1.0), r4));
}

@fragment
fn multi_fs(input: VSOut) -> @location(0) vec4<f32> {
    return input.color;
}
