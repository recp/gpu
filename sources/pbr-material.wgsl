// Generated WGSL

struct PBRUniforms {
    mvp: mat4x4f,
    model: mat4x4f,
    cameraPosition: vec4f,
    lightDirection: vec4f,
}

struct PBRVertex {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
}

struct PBROut {
    @builtin(position) position: vec4f,
    @location(0) worldPosition: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec3f,
    @location(3) bitangent: vec3f,
    @location(4) uv: vec2f,
}

struct usl_buffer_g0_b0 {
    value: PBRUniforms,
}
@group(0) @binding(0) var<uniform> usl_g0_b0: usl_buffer_g0_b0;

@group(1) @binding(0) var usl_g1_b0: texture_2d<f32>;

@group(1) @binding(1) var usl_g1_b1: texture_2d<f32>;

@group(1) @binding(2) var usl_g1_b2: texture_2d<f32>;

@group(1) @binding(3) var usl_g1_b3: texture_cube<f32>;

@group(1) @binding(4) var usl_g1_b4: texture_cube<f32>;

@group(1) @binding(5) var usl_g1_b5: texture_2d<f32>;

@group(1) @binding(6) var usl_g1_b6: sampler;

@vertex
fn pbr_vs(v: PBRVertex) -> PBROut {
    let r1391 = vec4f(v.position, 1.0);
    let r1399 = normalize((usl_g0_b0.value.model * vec4f(v.normal, 0.0)).xyz);
    let r1407 = normalize((usl_g0_b0.value.model * vec4f(v.tangent.xyz, 0.0)).xyz);
    return PBROut((usl_g0_b0.value.mvp * r1391), (usl_g0_b0.value.model * r1391).xyz, r1399, r1407, (normalize(cross(r1399, r1407)) * vec3f(v.tangent.w)), v.uv);
}

@fragment
fn pbr_fs(input: PBROut) -> @location(0) vec4f {
    var r2913: vec3f;
    let r1442 = textureSample(usl_g1_b2, usl_g1_b6, input.uv);
    let r2536 = saturate(r1442[1]);
    let r2539 = ((textureSample(usl_g1_b1, usl_g1_b6, input.uv).xyz * vec3f(2.0)) - vec3f(1.0));
    let r2546 = normalize(r2539);
    let r2550 = (normalize(input.bitangent) * vec3f(r2546[1]));
    let r2553 = (normalize(input.normal) * vec3f(r2546[2]));
    let r2554 = (fma(normalize(input.tangent), vec3f(r2546[0]), r2550) + r2553);
    let r2555 = normalize(r2554);
    let r1461 = normalize((usl_g0_b0.value.cameraPosition.xyz - input.worldPosition));
    let r1465 = normalize(usl_g0_b0.value.lightDirection.xyz);
    let r1470 = saturate(dot(r2555, r1461));
    let r1476 = normalize(reflect(-(r1461), r2555));
    let r1487 = textureSampleLevel(usl_g1_b4, usl_g1_b6, r1476, (r2536 * 6.0)).rgb;
    let r1493 = textureSampleLevel(usl_g1_b5, usl_g1_b6, vec2f(r1470, r2536), 0.0);
    let r1497 = textureSample(usl_g1_b0, usl_g1_b6, input.uv).rgb;
    let r2560: f32 = (r2536 * r2536);
    let r2561 = vec3f(0.04);
    let r2563 = saturate(r1442[2]);
    let r2571: f32 = (1.0 - r2536);
    let r2575: f32 = (1.0 - r1470);
    let r2576: f32 = (r2575 * r2575);
    let r2578: f32 = ((r2576 * r2576) * r2575);
    let r2581 = r1493[0];
    let r2583 = r1493[1];
    let r2584 = fma(mix(r1497, max(vec3f(r2571), r1497), r2578), vec3f(r2581), vec3f(r2583));
    let r2589: f32 = (1.0 - (r2581 + r2583));
    let r2593 = mix(r1497, vec3f(1.0), 0.04761905);
    let r2620 = fma(mix(vec3f(0.04), max(vec3f(r2571), r2561), r2578), vec3f(r2581), vec3f(r2583));
    let r2637 = (r2620 + (((vec3f(r2589) * r2620) * vec3f(0.08571429)) / (vec3f(1.0) - (vec3f(0.08571429) * vec3f(r2589)))));
    let r2638 = ((r2584 + (((vec3f(r2589) * r2584) * r2593) / (vec3f(1.0) - (r2593 * vec3f(r2589))))) * r1487);
    let r2641 = mix((textureSampleLevel(usl_g1_b3, usl_g1_b6, r2555, 0.0).rgb * r1497), r1487, r2637);
    let r2647 = normalize((r1461 + r1465));
    let r2649 = saturate(dot(r2555, r1465));
    let r2653 = saturate(dot(r2555, r2647));
    let r2662: f32 = (1.0 - saturate(dot(r1461, r2647)));
    let r2664: f32 = (r2662 * r2662);
    let r2666: f32 = ((r2664 * r2664) * r2662);
    let r2680 = (vec3f(4.6, 4.1, 3.7) * vec3f(r2649));
    let r2687: f32 = (r2560 * r2560);
    let r2689: f32 = (1.0 - r2687);
    let r2699: f32 = (r1470 * sqrt(fma((r2649 * r2649), r2689, r2687)));
    let r2700: f32 = fma(r2649, sqrt(fma((r1470 * r1470), r2689, r2687)), r2699);
    let r2707: f32 = ((r2653 * r2653) * (r2687 + -1.0));
    let r2711: f32 = (r2687 / (fma(3.1415927, r2707, 3.1415927) * (r2707 + 1.0)));
    let r2714 = (r2680 * vec3f((select(0.0, (0.5 / r2700), (r2700 > 0.0)) * r2711)));
    let r2715 = (mix(r1497, vec3f(1.0), r2666) * r2714);
    let r2718 = mix(mix((r2680 * (r1497 * vec3f(0.31830987))), r2714, mix(r2561, vec3f(1.0), r2666)), r2715, r2563);
    let r2719 = fma(mix(r2641, r2638, r2563), vec3f(r1442.r), r2718);
    let r2887 = min(min(r2719.x, r2719.y), r2719.z);
    let r2893 = (r2719 - vec3f(select(0.04, (r2887 - ((6.25 * r2887) * r2887)), (r2887 < 0.08))));
    let r2898 = max(max(r2893.x, r2893.y), r2893.z);
    if ((r2898 < 0.76)) {
        r2913 = r2893;
    } else {
        let r2903: f32 = (1.0 - (0.057600003 / (r2898 - 0.52)));
        let r2912 = mix((r2893 * vec3f((r2903 / r2898))), vec3f(r2903), (1.0 - (1.0 / fma(0.15, (r2898 - r2903), 1.0))));
        r2913 = r2912;
    }
    return vec4f(pow(r2913, vec3f(0.45454544)), 1.0);
}
