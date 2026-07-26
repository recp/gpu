// Generated WGSL

struct usl_cube_coord {
    uv: vec2f,
    face: i32,
}

fn usl_cube_project(d: vec3f, face: i32) -> vec2f {
    if (face == 0) {
        return vec2f(-d.z, -d.y) / max(abs(d.x), 1e-20) * 0.5 + 0.5;
    }
    if (face == 1) {
        return vec2f(d.z, -d.y) / max(abs(d.x), 1e-20) * 0.5 + 0.5;
    }
    if (face == 2) {
        return vec2f(d.x, d.z) / max(abs(d.y), 1e-20) * 0.5 + 0.5;
    }
    if (face == 3) {
        return vec2f(d.x, -d.z) / max(abs(d.y), 1e-20) * 0.5 + 0.5;
    }
    if (face == 4) {
        return vec2f(d.x, -d.y) / max(abs(d.z), 1e-20) * 0.5 + 0.5;
    }
    return vec2f(-d.x, -d.y) / max(abs(d.z), 1e-20) * 0.5 + 0.5;
}

fn usl_cube_map(d: vec3f) -> usl_cube_coord {
    let a = abs(d);
    if (a.x >= a.y && a.x >= a.z) {
        if (d.x >= 0.0) {
            return usl_cube_coord(usl_cube_project(d, 0), 0);
        }
        return usl_cube_coord(usl_cube_project(d, 1), 1);
    }
    if (a.y >= a.z) {
        if (d.y >= 0.0) {
            return usl_cube_coord(usl_cube_project(d, 2), 2);
        }
        return usl_cube_coord(usl_cube_project(d, 3), 3);
    }
    if (d.z >= 0.0) {
        return usl_cube_coord(usl_cube_project(d, 4), 4);
    }
    return usl_cube_coord(usl_cube_project(d, 5), 5);
}

struct VSOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var usl_g0_b0: texture_2d_array<u32>;

@vertex
fn integer_cube_vs(@builtin(vertex_index) vertexId: u32) -> VSOut {
    switch (vertexId) {
        case 0u: {
            return VSOut(vec4f(-0.92, -0.78, 0.0, 1.0), vec2f(0.0, 1.0));
        }
        case 1u: {
            return VSOut(vec4f(0.92, -0.78, 0.0, 1.0), vec2f(1.0));
        }
        case 2u: {
            return VSOut(vec4f(-0.92, 0.78, 0.0, 1.0), vec2f(0.0));
        }
        case 3u: {
            return VSOut(vec4f(-0.92, 0.78, 0.0, 1.0), vec2f(0.0));
        }
        case 4u: {
            return VSOut(vec4f(0.92, -0.78, 0.0, 1.0), vec2f(1.0));
        }
        default: {
            return VSOut(vec4f(0.92, 0.78, 0.0, 1.0), vec2f(1.0, 0.0));
        }
    }
}

@fragment
fn integer_cube_nearest_fs(input: VSOut) -> @location(0) vec4f {
    let r92: f32 = fma(6.2831855, input.uv.x, -3.1415927);
    let r95: f32 = fma(-3.1415927, input.uv.y, 1.5707964);
    let r96: f32 = cos(r95);
    let r99: f32 = sin(r95);
    let r101: f32 = (cos(r92) * r96);
    let r102 = vec3f((sin(r92) * r96), r99, r101);
    let usl_cube_r59 = usl_cube_map(r102);
    let usl_idim_r59 = textureDimensions(usl_g0_b0);
    let usl_ic_r59 = clamp(vec2i(floor(usl_cube_r59.uv * vec2f(usl_idim_r59))), vec2i(0), vec2i(usl_idim_r59) - vec2i(1));
    let r59 = textureLoad(usl_g0_b0, usl_ic_r59, usl_cube_r59.face, 0);
    return (vec4f(0.003921569) * vec4f(r59));
}

@fragment
fn integer_cube_level_fs(input: VSOut) -> @location(0) vec4f {
    let r106: f32 = fma(6.2831855, input.uv.x, -3.1415927);
    let r109: f32 = fma(-3.1415927, input.uv.y, 1.5707964);
    let r110: f32 = cos(r109);
    let r113: f32 = sin(r109);
    let r115: f32 = (cos(r106) * r110);
    let r116 = vec3f((sin(r106) * r110), r113, r115);
    let usl_cube_r66 = usl_cube_map(r116);
    let usl_ilevels_r66 = max(textureNumLevels(usl_g0_b0), 1u);
    let usl_imip_r66 = min(u32(floor(max(1.0, 0.0) + 0.5)), usl_ilevels_r66 - 1u);
    let usl_idim_r66 = textureDimensions(usl_g0_b0, usl_imip_r66);
    let usl_ic_r66 = clamp(vec2i(floor(usl_cube_r66.uv * vec2f(usl_idim_r66))), vec2i(0), vec2i(usl_idim_r66) - vec2i(1));
    let r66 = textureLoad(usl_g0_b0, usl_ic_r66, usl_cube_r66.face, usl_imip_r66);
    return (vec4f(0.003921569) * vec4f(r66));
}

@fragment
fn integer_cube_gradient_fs(input: VSOut) -> @location(0) vec4f {
    let r120: f32 = fma(6.2831855, input.uv.x, -3.1415927);
    let r123: f32 = fma(-3.1415927, input.uv.y, 1.5707964);
    let r124: f32 = cos(r123);
    let r127: f32 = sin(r123);
    let r129: f32 = (cos(r120) * r124);
    let r130 = vec3f((sin(r120) * r124), r127, r129);
    let r76 = dpdx(r130);
    let r78 = dpdy(r130);
    let usl_cd_r79 = r130;
    let usl_cube_r79 = usl_cube_map(usl_cd_r79);
    let usl_cdx_r79 = usl_cube_project(usl_cd_r79 + r76, usl_cube_r79.face);
    let usl_cdy_r79 = usl_cube_project(usl_cd_r79 + r78, usl_cube_r79.face);
    let usl_ilevels_r79 = max(textureNumLevels(usl_g0_b0), 1u);
    let usl_ibase_r79 = textureDimensions(usl_g0_b0);
    let usl_irho_r79 = max(length((usl_cdx_r79 - usl_cube_r79.uv) * vec2f(usl_ibase_r79)), length((usl_cdy_r79 - usl_cube_r79.uv) * vec2f(usl_ibase_r79)));
    let usl_imip_r79 = min(u32(floor(max(log2(max(usl_irho_r79, 1e-20)), 0.0) + 0.5)), usl_ilevels_r79 - 1u);
    let usl_idim_r79 = textureDimensions(usl_g0_b0, usl_imip_r79);
    let usl_ic_r79 = clamp(vec2i(floor(usl_cube_r79.uv * vec2f(usl_idim_r79))), vec2i(0), vec2i(usl_idim_r79) - vec2i(1));
    let r79 = textureLoad(usl_g0_b0, usl_ic_r79, usl_cube_r79.face, usl_imip_r79);
    return (vec4f(0.003921569) * vec4f(r79));
}

@fragment
fn integer_cube_bias_fs(input: VSOut) -> @location(0) vec4f {
    let r134: f32 = fma(6.2831855, input.uv.x, -3.1415927);
    let r137: f32 = fma(-3.1415927, input.uv.y, 1.5707964);
    let r138: f32 = cos(r137);
    let r141: f32 = sin(r137);
    let r143: f32 = (cos(r134) * r138);
    let r144 = vec3f((sin(r134) * r138), r141, r143);
    let usl_cd_r86 = r144;
    let usl_cube_r86 = usl_cube_map(usl_cd_r86);
    let usl_cdx_r86 = usl_cube_project(usl_cd_r86 + dpdx(r144), usl_cube_r86.face);
    let usl_cdy_r86 = usl_cube_project(usl_cd_r86 + dpdy(r144), usl_cube_r86.face);
    let usl_ilevels_r86 = max(textureNumLevels(usl_g0_b0), 1u);
    let usl_ibase_r86 = textureDimensions(usl_g0_b0);
    let usl_irho_r86 = max(length((usl_cdx_r86 - usl_cube_r86.uv) * vec2f(usl_ibase_r86)), length((usl_cdy_r86 - usl_cube_r86.uv) * vec2f(usl_ibase_r86)));
    let usl_imip_r86 = min(u32(floor(max(log2(max(usl_irho_r86, 1e-20)) + 6.0, 0.0) + 0.5)), usl_ilevels_r86 - 1u);
    let usl_idim_r86 = textureDimensions(usl_g0_b0, usl_imip_r86);
    let usl_ic_r86 = clamp(vec2i(floor(usl_cube_r86.uv * vec2f(usl_idim_r86))), vec2i(0), vec2i(usl_idim_r86) - vec2i(1));
    let r86 = textureLoad(usl_g0_b0, usl_ic_r86, usl_cube_r86.face, usl_imip_r86);
    return (vec4f(0.003921569) * vec4f(r86));
}
