// Generated WGSL

struct Particle {
    position: vec2<f32>,
    velocity: vec2<f32>,
    color: vec4<f32>,
}

struct Simulation {
    deltaTime: f32,
    time: f32,
    aspect: f32,
    count: u32,
}

struct ParticleOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) local: vec2<f32>,
}

struct usl_buffer_g0_b0 {
    values: array<Particle>,
}
@group(0) @binding(0) var<storage, read_write> usl_g0_b0: usl_buffer_g0_b0;

struct usl_buffer_g3_b0 {
    value: Simulation,
}
@group(3) @binding(0) var<uniform> usl_g3_b0: usl_buffer_g3_b0;

struct usl_buffer_g1_b0 {
    values: array<Particle>,
}
@group(1) @binding(0) var<storage, read> usl_g1_b0: usl_buffer_g1_b0;

fn particle_corner(vertexId: u32) -> vec2<f32> {
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

@compute @workgroup_size(64, 1, 1)
fn simulate_particles(@builtin(global_invocation_id) gid: vec3<u32>) {
    var velocity: vec2<f32> = usl_g0_b0.values[gid.x].velocity;
    let r34: vec2<f32> = fma(usl_g0_b0.values[gid.x].velocity, vec2<f32>(usl_g3_b0.value.deltaTime), usl_g0_b0.values[gid.x].position);
    velocity.x = select(usl_g0_b0.values[gid.x].velocity.x, -(usl_g0_b0.values[gid.x].velocity.x), ((r34.x < -0.96) || (r34.x > 0.96)));
    velocity.y = select(velocity.y, -(velocity.y), ((r34.y < -0.92) || (r34.y > 0.92)));
    usl_g0_b0.values[gid.x].position = clamp(r34, vec2<f32>(-0.96, -0.92), vec2<f32>(0.96, 0.92));
    usl_g0_b0.values[gid.x].velocity = velocity;
}

@vertex
fn particle_vs(@builtin(vertex_index) vertexId: u32, @builtin(instance_index) instanceId: u32) -> ParticleOut {
    let r87: vec2<f32> = particle_corner(vertexId);
    let r95: f32 = fma(0.009, usl_g1_b0.values[instanceId].color.a, 0.012);
    let r107: vec2<f32> = vec2<f32>(((r87.x * r95) / usl_g3_b0.value.aspect), (r87.y * r95));
    return ParticleOut(vec4<f32>((r107 + usl_g1_b0.values[instanceId].position), 0.0, 1.0), vec4<f32>(usl_g1_b0.values[instanceId].color.rgb, 1.0), r87);
}

@fragment
fn particle_fs(input: ParticleOut) -> @location(0) vec4<f32> {
    let r124 = saturate((1.0 - length(input.local)));
    return vec4<f32>((input.color.rgb * vec3<f32>(fma(0.55, r124, 0.45))), r124);
}
