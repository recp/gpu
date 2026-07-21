// Generated WGSL

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@group(0) @binding(0) var usl_g0_b0: texture_2d_array<f32>;

@group(0) @binding(1) var usl_g0_b1: sampler;

@vertex
fn array_vs(@builtin(vertex_index) vertexId: u32) -> VSOut {
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
fn array_fs(input: VSOut) -> @location(0) vec4<f32> {
    let r37 = step(0.5, input.uv.x);
    return textureSample(usl_g0_b0, usl_g0_b1, vec2<f32>(((2.0 * input.uv.x) - r37), input.uv.y), i32(r37));
}
