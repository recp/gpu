// Generated WGSL

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@group(0) @binding(0) var usl_g0_b0: texture_2d<f32>;
@group(0) @binding(1) var usl_g0_b1: texture_2d<f32>;
@group(0) @binding(2) var usl_g0_b2: texture_2d<f32>;
@group(0) @binding(3) var usl_g0_b3: texture_2d<f32>;

@group(0) @binding(4) var usl_g0_b4: sampler;
@group(0) @binding(5) var usl_g0_b5: sampler;
@group(0) @binding(6) var usl_g0_b6: sampler;
@group(0) @binding(7) var usl_g0_b7: sampler;

@vertex
fn descriptor_array_vs(@builtin(vertex_index) vertexId: u32) -> VSOut {
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
fn descriptor_array_fs(input: VSOut) -> @location(0) vec4<f32> {
    let r41: vec2<f32> = (vec2<f32>(2.0) * input.uv);
    let r46 = textureSample(usl_g0_b0, usl_g0_b4, r41);
    let r51 = textureSample(usl_g0_b1, usl_g0_b5, r41);
    let r56 = textureSample(usl_g0_b2, usl_g0_b6, r41);
    let r61 = textureSample(usl_g0_b3, usl_g0_b7, r41);
    let r77: bool = (input.uv.y < 0.5);
    if ((input.uv.x < 0.5)) {
        return select(r56, r46, r77);
    } else {
        return select(r61, r51, r77);
    }
}
