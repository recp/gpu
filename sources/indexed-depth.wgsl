// Generated WGSL

struct VSIn {
    @location(0) position: vec4<f32>,
    @location(1) color: vec4<f32>,
}

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
}

struct PreviewOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@group(0) @binding(0) var usl_g0_b0: texture_depth_2d;

@vertex
fn cube_vs(v: VSIn) -> VSOut {
    return VSOut(v.position, v.color);
}

@fragment
fn cube_fs(i: VSOut) -> @location(0) vec4<f32> {
    return i.color;
}

@vertex
fn depth_preview_vs(@builtin(vertex_index) vertexId: u32) -> PreviewOut {
    switch (vertexId) {
        case 0u: {
            return PreviewOut(vec4<f32>(-1.0, -1.0, 0.0, 1.0), vec2<f32>(0.0, 1.0));
        }
        case 1u: {
            return PreviewOut(vec4<f32>(3.0, -1.0, 0.0, 1.0), vec2<f32>(2.0, 1.0));
        }
        default: {
            return PreviewOut(vec4<f32>(-1.0, 3.0, 0.0, 1.0), vec2<f32>(0.0, -1.0));
        }
    }
}

@fragment
fn depth_preview_fs(input: PreviewOut) -> @location(0) vec4<f32> {
    let usl_dims_r27 = textureDimensions(usl_g0_b0, 0u);
    let r39: f32 = textureLoad(usl_g0_b0, vec2<i32>((input.uv * (vec2<f32>(f32(usl_dims_r27.x), f32(usl_dims_r27.y)) - vec2<f32>(1.0)))), 0);
    return vec4<f32>(r39, r39, r39, 1.0);
}
