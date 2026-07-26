// Generated WGSL

struct PBRUniforms {
    mvp: mat4x4f,
    model: mat4x4f,
    cameraPosition: vec4f,
    lightDirection: vec4f,
    baseColorFactor: vec4f,
    emissiveFactor: vec4f,
    materialFactors: vec4f,
}

struct AssetVertex {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
}

struct AssetOut {
    @builtin(position) position: vec4f,
    @location(0) worldPosition: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
}

struct usl_buffer_g0_b0 {
    value: PBRUniforms,
}
@group(0) @binding(0) var<uniform> usl_g0_b0: usl_buffer_g0_b0;

@group(1) @binding(0) var usl_g1_b0: texture_2d<f32>;

@group(1) @binding(1) var usl_g1_b1: texture_2d<f32>;

@group(1) @binding(2) var usl_g1_b2: texture_2d<f32>;

@group(1) @binding(3) var usl_g1_b3: texture_2d<f32>;

@group(1) @binding(4) var usl_g1_b4: texture_2d<f32>;

@group(1) @binding(5) var usl_g1_b5: texture_cube<f32>;

@group(1) @binding(6) var usl_g1_b6: texture_cube<f32>;

@group(1) @binding(7) var usl_g1_b7: texture_2d<f32>;

@group(1) @binding(8) var usl_g1_b8: sampler;

@vertex
fn asset_vs(v: AssetVertex) -> AssetOut {
    let r1391 = vec4f(v.position, 1.0);
    return AssetOut((usl_g0_b0.value.mvp * r1391), (usl_g0_b0.value.model * r1391).xyz, normalize((usl_g0_b0.value.model * vec4f(v.normal, 0.0)).xyz), v.uv);
}

@fragment
fn asset_fs(input: AssetOut) -> @location(0) vec4f {
    var r2991: vec3f;
    let r1426 = textureSample(usl_g1_b2, usl_g1_b8, input.uv);
    let r1437 = normalize(input.normal);
    let r1440 = dpdx(input.uv);
    let r1443 = dpdy(input.uv);
    let r1446 = dpdx(input.worldPosition);
    let r1449 = dpdy(input.worldPosition);
    let r1470: f32 = ((r1440[0] * r1443[1]) - (r1443[0] * r1440[1]));
    let r1471 = (((vec3f(r1443[1]) * r1446) - (vec3f(r1440[1]) * r1449)) / vec3f(r1470));
    let r1480 = normalize((r1471 - (r1437 * vec3f(dot(r1437, r1471)))));
    let r2603 = ((textureSample(usl_g1_b1, usl_g1_b8, input.uv).xyz * vec3f(2.0)) - vec3f(1.0));
    let r2609 = vec3f((r2603[0] * usl_g0_b0.value.materialFactors.z), (r2603[1] * usl_g0_b0.value.materialFactors.z), r2603[2]);
    let r2610 = normalize(r2609);
    let r2615 = fma(r1480, vec3f(r2610[0]), (cross(r1437, r1480) * vec3f(r2610[1])));
    let r2619 = normalize((r2615 + (r1437 * vec3f(r2610[2]))));
    let r2625 = saturate((usl_g0_b0.value.materialFactors.y * r1426[1]));
    let r2627 = (usl_g0_b0.value.baseColorFactor * textureSample(usl_g1_b0, usl_g1_b8, input.uv));
    let r2630 = ((textureSample(usl_g1_b4, usl_g1_b8, input.uv).rgb * usl_g0_b0.value.emissiveFactor.xyz) * vec3f(usl_g0_b0.value.emissiveFactor.w));
    let r2633: f32 = fma(usl_g0_b0.value.materialFactors.w, (textureSample(usl_g1_b3, usl_g1_b8, input.uv).r + -1.0), 1.0);
    let r1523 = normalize((usl_g0_b0.value.cameraPosition.xyz - input.worldPosition));
    let r1527 = normalize(usl_g0_b0.value.lightDirection.xyz);
    let r1532 = saturate(dot(r2619, r1523));
    let r1538 = normalize(reflect(-(r1523), r2619));
    let r1549 = textureSampleLevel(usl_g1_b6, usl_g1_b8, r1538, (r2625 * 6.0)).rgb;
    let r1555 = textureSampleLevel(usl_g1_b7, usl_g1_b8, vec2f(r1532, r2625), 0.0);
    let r1559 = r2627.rgb;
    let r2638: f32 = (r2625 * r2625);
    let r2639 = vec3f(0.04);
    let r2641 = saturate((usl_g0_b0.value.materialFactors.x * r1426[2]));
    let r2649: f32 = (1.0 - r2625);
    let r2653: f32 = (1.0 - r1532);
    let r2654: f32 = (r2653 * r2653);
    let r2656: f32 = ((r2654 * r2654) * r2653);
    let r2659 = r1555[0];
    let r2661 = r1555[1];
    let r2662 = fma(mix(r1559, max(vec3f(r2649), r1559), r2656), vec3f(r2659), vec3f(r2661));
    let r2667: f32 = (1.0 - (r2659 + r2661));
    let r2671 = mix(r1559, vec3f(1.0), 0.04761905);
    let r2698 = fma(mix(vec3f(0.04), max(vec3f(r2649), r2639), r2656), vec3f(r2659), vec3f(r2661));
    let r2715 = (r2698 + (((vec3f(r2667) * r2698) * vec3f(0.08571429)) / (vec3f(1.0) - (vec3f(0.08571429) * vec3f(r2667)))));
    let r2716 = ((r2662 + (((vec3f(r2667) * r2662) * r2671) / (vec3f(1.0) - (r2671 * vec3f(r2667))))) * r1549);
    let r2719 = mix((textureSampleLevel(usl_g1_b5, usl_g1_b8, r2619, 0.0).rgb * r1559), r1549, r2715);
    let r2725 = normalize((r1523 + r1527));
    let r2727 = saturate(dot(r2619, r1527));
    let r2731 = saturate(dot(r2619, r2725));
    let r2740: f32 = (1.0 - saturate(dot(r1523, r2725)));
    let r2742: f32 = (r2740 * r2740);
    let r2744: f32 = ((r2742 * r2742) * r2740);
    let r2758 = (vec3f(4.6, 4.1, 3.7) * vec3f(r2727));
    let r2765: f32 = (r2638 * r2638);
    let r2767: f32 = (1.0 - r2765);
    let r2777: f32 = (r1532 * sqrt(fma((r2727 * r2727), r2767, r2765)));
    let r2778: f32 = fma(r2727, sqrt(fma((r1532 * r1532), r2767, r2765)), r2777);
    let r2785: f32 = ((r2731 * r2731) * (r2765 + -1.0));
    let r2789: f32 = (r2765 / (fma(3.1415927, r2785, 3.1415927) * (r2785 + 1.0)));
    let r2792 = (r2758 * vec3f((select(0.0, (0.5 / r2778), (r2778 > 0.0)) * r2789)));
    let r2793 = (mix(r1559, vec3f(1.0), r2744) * r2792);
    let r2796 = mix(mix((r2758 * (r1559 * vec3f(0.31830987))), r2792, mix(r2639, vec3f(1.0), r2744)), r2793, r2641);
    let r2798 = (fma(mix(r2719, r2716, r2641), vec3f(r2633), r2796) + r2630);
    let r2965 = min(min(r2798.x, r2798.y), r2798.z);
    let r2971 = (r2798 - vec3f(select(0.04, (r2965 - ((6.25 * r2965) * r2965)), (r2965 < 0.08))));
    let r2976 = max(max(r2971.x, r2971.y), r2971.z);
    if ((r2976 < 0.76)) {
        r2991 = r2971;
    } else {
        let r2981: f32 = (1.0 - (0.057600003 / (r2976 - 0.52)));
        let r2990 = mix((r2971 * vec3f((r2981 / r2976))), vec3f(r2981), (1.0 - (1.0 / fma(0.15, (r2976 - r2981), 1.0))));
        r2991 = r2990;
    }
    return vec4f(pow(r2991, vec3f(0.45454544)), r2627.a);
}
