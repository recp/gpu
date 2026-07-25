// Generated WGSL

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@group(0) @binding(0) var usl_g0_b0: texture_cube<f32>;

@group(0) @binding(1) var usl_g0_b1: texture_3d<f32>;

@group(0) @binding(2) var usl_g0_b2: sampler;

@vertex
fn shapes_vs(@builtin(vertex_index) vertexId: u32) -> VSOut {
    switch (vertexId) {
        case 0u: {
            return VSOut(vec4<f32>(-0.88, -0.76, 0.0, 1.0), vec2<f32>(0.0, 1.0));
        }
        case 1u: {
            return VSOut(vec4<f32>(0.88, -0.76, 0.0, 1.0), vec2<f32>(1.0));
        }
        case 2u: {
            return VSOut(vec4<f32>(-0.88, 0.76, 0.0, 1.0), vec2<f32>(0.0));
        }
        case 3u: {
            return VSOut(vec4<f32>(-0.88, 0.76, 0.0, 1.0), vec2<f32>(0.0));
        }
        case 4u: {
            return VSOut(vec4<f32>(0.88, -0.76, 0.0, 1.0), vec2<f32>(1.0));
        }
        default: {
            return VSOut(vec4<f32>(0.88, 0.76, 0.0, 1.0), vec2<f32>(1.0, 0.0));
        }
    }
}

@fragment
fn shapes_fs(input: VSOut) -> @location(0) vec4<f32> {
    let r38 = fract((2.0 * input.uv.x));
    let r43: f32 = fma(6.2831855, r38, -3.1415927);
    let r48: f32 = fma(-3.1415927, input.uv.y, 1.5707964);
    let r51: f32 = cos(r48);
    let r58: f32 = sin(r48);
    let r62: f32 = (cos(r43) * r51);
    let r63: vec3<f32> = vec3<f32>((sin(r43) * r51), r58, r62);
    let r73 = textureSample(usl_g0_b1, usl_g0_b2, vec3<f32>(r38, input.uv.y, r38));
    return mix(textureSample(usl_g0_b0, usl_g0_b2, r63), r73, step(0.5, input.uv.x));
}
