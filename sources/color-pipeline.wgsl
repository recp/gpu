// Generated WGSL

struct ColorVertexOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var usl_g0_b0: texture_2d<f32>;

@group(0) @binding(1) var usl_g0_b1: texture_2d<f32>;

@group(0) @binding(2) var usl_g0_b2: sampler;

@group(1) @binding(0) var usl_g1_b0: texture_2d<f32>;

@group(1) @binding(1) var usl_g1_b1: sampler;

@vertex
fn color_vs(@builtin(vertex_index) vertexId: u32) -> ColorVertexOut {
    switch (vertexId) {
        case 0u: {
            return ColorVertexOut(vec4f(-1.0, -1.0, 0.0, 1.0), vec2f(0.0, 1.0));
        }
        case 1u: {
            return ColorVertexOut(vec4f(3.0, -1.0, 0.0, 1.0), vec2f(2.0, 1.0));
        }
        default: {
            return ColorVertexOut(vec4f(-1.0, 3.0, 0.0, 1.0), vec2f(0.0, -1.0));
        }
    }
}

@fragment
fn hdr_fs(input: ColorVertexOut) -> @location(0) vec4f {
    var source_0: vec4f;
    let r28 = vec2f(fract((input.uv.x * 2.0)), input.uv.y);
    if ((input.uv.x < 0.5)) {
        source_0 = textureSampleLevel(usl_g0_b0, usl_g0_b2, r28, 0.0);
    } else {
        source_0 = textureSampleLevel(usl_g0_b1, usl_g0_b2, r28, 0.0);
    }
    if ((abs((input.uv.x - 0.5)) < 0.003)) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    } else {
        return vec4f((source_0.rgb * vec3f(3.2)), 1.0);
    }
}

@fragment
fn tonemap_fs(input: ColorVertexOut) -> @location(0) vec4f {
    var r108: f32;
    var r115: f32;
    var r122: f32;
    let r72 = textureSampleLevel(usl_g1_b0, usl_g1_b1, input.uv, 0.0).rgb;
    let r101 = (r72 / (vec3f(1.0) + r72));
    let r102 = r101.r;
    if ((r102 <= 0.0031308)) {
        r108 = (r102 * 12.92);
    } else {
        r108 = fma(1.055, pow(r102, 0.41666666), -0.055);
    }
    let r109 = r101.g;
    if ((r109 <= 0.0031308)) {
        r115 = (r109 * 12.92);
    } else {
        r115 = fma(1.055, pow(r109, 0.41666666), -0.055);
    }
    let r116 = r101.b;
    if ((r116 <= 0.0031308)) {
        r122 = (r116 * 12.92);
    } else {
        r122 = fma(1.055, pow(r116, 0.41666666), -0.055);
    }
    return vec4f(vec3f(r108, r115, r122), 1.0);
}
