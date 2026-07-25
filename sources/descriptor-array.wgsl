// Generated WGSL

struct VSOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var usl_g0_b0: texture_2d<f32>;
@group(0) @binding(1) var usl_g0_b1: texture_2d<f32>;
@group(0) @binding(2) var usl_g0_b2: texture_2d<f32>;
@group(0) @binding(3) var usl_g0_b3: texture_2d<f32>;

@group(0) @binding(4) var usl_g0_b4: sampler;
@group(0) @binding(5) var usl_g0_b5: sampler;
@group(0) @binding(6) var usl_g0_b6: sampler;
@group(0) @binding(7) var usl_g0_b7: sampler;

struct usl_buffer_g0_b2 {
    values: array<vec4f>,
}
@group(0) @binding(8) var<storage, read> usl_g0_b8: usl_buffer_g0_b2;
@group(0) @binding(9) var<storage, read> usl_g0_b9: usl_buffer_g0_b2;
@group(0) @binding(10) var<storage, read> usl_g0_b10: usl_buffer_g0_b2;
@group(0) @binding(11) var<storage, read> usl_g0_b11: usl_buffer_g0_b2;

@vertex
fn descriptor_array_vs(@builtin(vertex_index) vertexId: u32) -> VSOut {
    switch (vertexId) {
        case 0u: {
            return VSOut(vec4f(-0.88, -0.76, 0.0, 1.0), vec2f(0.0, 1.0));
        }
        case 1u: {
            return VSOut(vec4f(0.88, -0.76, 0.0, 1.0), vec2f(1.0));
        }
        case 2u: {
            return VSOut(vec4f(-0.88, 0.76, 0.0, 1.0), vec2f(0.0));
        }
        case 3u: {
            return VSOut(vec4f(-0.88, 0.76, 0.0, 1.0), vec2f(0.0));
        }
        case 4u: {
            return VSOut(vec4f(0.88, -0.76, 0.0, 1.0), vec2f(1.0));
        }
        default: {
            return VSOut(vec4f(0.88, 0.76, 0.0, 1.0), vec2f(1.0, 0.0));
        }
    }
}

@fragment
fn descriptor_array_fs(input: VSOut) -> @location(0) vec4f {
    let r41: vec2f = (vec2f(2.0) * input.uv);
    let r49: vec4f = (textureSample(usl_g0_b0, usl_g0_b4, r41) * usl_g0_b8.values[0]);
    let r57: vec4f = (textureSample(usl_g0_b1, usl_g0_b5, r41) * usl_g0_b9.values[0]);
    let r65: vec4f = (textureSample(usl_g0_b2, usl_g0_b6, r41) * usl_g0_b10.values[0]);
    let r73: vec4f = (textureSample(usl_g0_b3, usl_g0_b7, r41) * usl_g0_b11.values[0]);
    let r90: bool = (input.uv.y < 0.5);
    if ((input.uv.x < 0.5)) {
        return select(r65, r49, r90);
    } else {
        return select(r73, r57, r90);
    }
}
