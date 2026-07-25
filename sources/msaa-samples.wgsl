// Generated WGSL

struct SourceOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
}

struct PreviewOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@group(0) @binding(1) var usl_g0_b1: texture_2d<f32>;

@group(0) @binding(0) var usl_g0_b0: texture_multisampled_2d<f32>;

@vertex
fn sample_source_vs(@builtin(vertex_index) vertexId: u32) -> SourceOut {
    switch (vertexId) {
        case 0u: {
            return SourceOut(vec4<f32>(0.0, 0.82, 0.0, 1.0), vec3<f32>(1.0, 0.24, 0.04));
        }
        case 1u: {
            return SourceOut(vec4<f32>(-0.84, -0.72, 0.0, 1.0), vec3<f32>(0.08, 0.68, 1.0));
        }
        default: {
            return SourceOut(vec4<f32>(0.84, -0.72, 0.0, 1.0), vec3<f32>(0.24, 1.0, 0.38));
        }
    }
}

@fragment
fn sample_source_fs(input: SourceOut) -> @location(0) vec4<f32> {
    return vec4<f32>(input.color, 1.0);
}

@vertex
fn sample_preview_vs(@builtin(vertex_index) vertexId: u32) -> PreviewOut {
    switch (vertexId) {
        case 0u: {
            return PreviewOut(vec4<f32>(-1.0, -1.0, 0.0, 1.0), vec2<f32>(0.0, 1.0));
        }
        case 1u: {
            return PreviewOut(vec4<f32>(1.0, -1.0, 0.0, 1.0), vec2<f32>(1.0));
        }
        case 2u: {
            return PreviewOut(vec4<f32>(-1.0, 1.0, 0.0, 1.0), vec2<f32>(0.0));
        }
        case 3u: {
            return PreviewOut(vec4<f32>(-1.0, 1.0, 0.0, 1.0), vec2<f32>(0.0));
        }
        case 4u: {
            return PreviewOut(vec4<f32>(1.0, -1.0, 0.0, 1.0), vec2<f32>(1.0));
        }
        default: {
            return PreviewOut(vec4<f32>(1.0, 1.0, 0.0, 1.0), vec2<f32>(1.0, 0.0));
        }
    }
}

@fragment
fn resolve_preview_fs(input: PreviewOut) -> @location(0) vec4<f32> {
    let usl_dims_r51 = textureDimensions(usl_g0_b1, 0u);
    return textureLoad(usl_g0_b1, vec2<i32>((input.uv * (vec2<f32>(usl_dims_r51) - vec2<f32>(1.0)))), 0);
}

@fragment
fn sample_preview_fs(input: PreviewOut) -> @location(0) vec4<f32> {
    let usl_dims_r68 = textureDimensions(usl_g0_b0);
    let r74 = textureNumSamples(usl_g0_b0);
    let r80: f32 = (input.uv.x * f32(r74));
    let r86 = min(u32(r80), (r74 - 1u));
    let r99: vec2<f32> = (vec2<f32>(usl_dims_r68) - vec2<f32>(1.0));
    let r101: vec2<i32> = vec2<i32>((vec2<f32>((r80 - f32(r86)), input.uv.y) * r99));
    let r109: f32 = f32((r86 & 1u));
    let r114: f32 = f32(((r86 >> 1u) & 1u));
    let r126: f32 = (1.0 - ((0.6 * r109) * r114));
    let r127: vec3<f32> = vec3<f32>(fma(0.75, r109, 0.25), fma(0.75, r114, 0.25), r126);
    let r135: vec4<f32> = vec4<f32>((vec3<f32>(0.035) * r127), 0.0);
    return fma(textureLoad(usl_g0_b0, r101, i32(r86)), vec4<f32>(r127, 1.0), r135);
}
