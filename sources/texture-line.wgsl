// Generated WGSL

struct LineOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var usl_g0_b0: texture_1d<f32>;

@group(0) @binding(1) var usl_g0_b1: texture_storage_1d<rgba8unorm, write>;

@group(1) @binding(0) var usl_g1_b0: texture_1d<f32>;

@group(1) @binding(1) var usl_g1_b1: texture_1d<f32>;

@compute @workgroup_size(8, 1, 1)
fn line_transform_cs(@builtin(global_invocation_id) usl_builtin_2: vec3u) {
    let gid: u32 = usl_builtin_2.x;
    let r12: f32 = (((0.5 * f32((1u + gid))) / f32(textureDimensions(usl_g0_b0, 0u))) + 0.5);
    let r14 = textureLoad(usl_g0_b0, i32(gid), 0);
    let r26: vec4f = vec4f(r12, r12, r12, 1.0);
    let r27: vec4f = (vec4f(r14[2], r14[0], r14[1], 1.0) * r26);
    textureStore(usl_g0_b1, i32(gid), r27);
}

@vertex
fn line_vs(@builtin(vertex_index) vertexId: u32) -> LineOut {
    switch (vertexId) {
        case 0u: {
            return LineOut(vec4f(-0.88, -0.76, 0.0, 1.0), vec2f(0.0, 1.0));
        }
        case 1u: {
            return LineOut(vec4f(0.88, -0.76, 0.0, 1.0), vec2f(1.0));
        }
        case 2u: {
            return LineOut(vec4f(-0.88, 0.76, 0.0, 1.0), vec2f(0.0));
        }
        case 3u: {
            return LineOut(vec4f(-0.88, 0.76, 0.0, 1.0), vec2f(0.0));
        }
        case 4u: {
            return LineOut(vec4f(0.88, -0.76, 0.0, 1.0), vec2f(1.0));
        }
        default: {
            return LineOut(vec4f(0.88, 0.76, 0.0, 1.0), vec2f(1.0, 0.0));
        }
    }
}

@fragment
fn line_fs(input: LineOut) -> @location(0) vec4f {
    let r61 = textureDimensions(usl_g1_b0, 0u);
    let r72 = min(i32((input.uv.x * f32(r61))), (i32(r61) + -1));
    if ((input.uv.y < 0.5)) {
        return textureLoad(usl_g1_b0, r72, 0);
    } else {
        return textureLoad(usl_g1_b1, r72, 0);
    }
}
