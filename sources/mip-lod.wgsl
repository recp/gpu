// Generated WGSL

struct MipOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) lod: f32,
}

@group(0) @binding(0) var usl_g0_b0: texture_2d<f32>;

@group(0) @binding(1) var usl_g0_b1: sampler;

fn quad_corner(vertexId: u32) -> vec2<f32> {
    switch (vertexId) {
        case 0u: {
            return vec2<f32>(-1.0);
        }
        case 1u: {
            return vec2<f32>(1.0, -1.0);
        }
        case 2u: {
            return vec2<f32>(-1.0, 1.0);
        }
        case 3u: {
            return vec2<f32>(-1.0, 1.0);
        }
        case 4u: {
            return vec2<f32>(1.0, -1.0);
        }
        default: {
            return vec2<f32>(1.0);
        }
    }
}

@vertex
fn mip_vs(@builtin(vertex_index) vertexId: u32, @builtin(instance_index) instanceId: u32) -> MipOut {
    let r21: vec2<f32> = quad_corner(vertexId);
    let r23: f32 = f32(instanceId);
    return MipOut(vec4<f32>(fma(r21, vec2<f32>(0.17, 0.72), vec2<f32>(fma(0.39, r23, -0.78), 0.0)), 0.0, 1.0), fma(r21, vec2<f32>(1.5, 4.0), vec2<f32>(0.5)), r23);
}

@fragment
fn mip_fs(input: MipOut) -> @location(0) vec4<f32> {
    return textureSampleLevel(usl_g0_b0, usl_g0_b1, input.uv, input.lod);
}
