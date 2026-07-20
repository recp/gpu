// Generated WGSL

struct VSIn {
    @location(0) position: vec4<f32>,
    @location(1) color: vec4<f32>,
}

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
}

@vertex
fn cube_vs(v: VSIn) -> VSOut {
    return VSOut(v.position, v.color);
}

@fragment
fn cube_fs(i: VSOut) -> @location(0) vec4<f32> {
    return i.color;
}
