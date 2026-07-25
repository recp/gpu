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

struct usl_buffer_g0_b1 {
    values: array<u32>,
}
@group(0) @binding(1) var<storage, read> usl_g0_b1: usl_buffer_g0_b1;

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
    let r36: vec2<f32> = (vec2<f32>(usl_dims_r27) - vec2<f32>(1.0));
    let r50: vec3<f32> = select(vec3<f32>(1.0, 0.05, 0.02), vec3<f32>(0.18, 1.0, 0.38), ((usl_g0_b1.values[2] != 0u) || (usl_g0_b1.values[3] != 0u)));
    return vec4<f32>((vec3<f32>(textureLoad(usl_g0_b0, vec2<i32>((input.uv * r36)), 0)) * r50), 1.0);
}
