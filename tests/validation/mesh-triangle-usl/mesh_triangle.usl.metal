#include <metal_stdlib>
using namespace metal;

struct MeshVertex {
    float4 position [[position]];
    float4 color [[attribute(0)]];
};

struct MeshPrimitive {
    bool culled [[primitive_culled]];
};

struct TaskParams {
    uint4 meshGroups;
    float4 offset;
    float4 tint;
};

struct MeshPayload {
    array<float4, 2> offsets;
    array<float4, 2> tints;
};

[[object, max_total_threads_per_threadgroup(1)]]
void task_main(metal::mesh_grid_properties __usl_mesh_grid, object_data MeshPayload& payload [[payload]], constant TaskParams& params [[buffer(0)]]) {
    payload.offsets[0] = params.offset + float4(-0.42, 0.0, 0.0, 0.0);
    payload.offsets[1] = params.offset + float4(0.42, 0.0, 0.0, 0.0);
    payload.tints[0] = params.tint;
    payload.tints[1] = params.tint.wzyx;
    __usl_mesh_grid.set_threadgroups_per_grid(uint3(params.meshGroups.x, params.meshGroups.y, params.meshGroups.z));
}

[[mesh, max_total_threads_per_threadgroup(6)]]
void mesh_main(metal::mesh<MeshVertex, MeshPrimitive, 6, 2, metal::topology::triangle> __usl_mesh, const object_data MeshPayload& payload [[payload]], uint tid [[thread_index_in_threadgroup]]) {
    __usl_mesh.set_primitive_count(2);
    if (tid < 2u) {
        __usl_mesh.set_primitive(tid, MeshPrimitive{tid == 1u});
    }
    if (tid < 6u) {
        uint r9 = tid / 3u;
        uint r10 = tid % 3u;
        float r12 = float(r10 == 1u);
        float r15 = float(r10 == 0u);
        float r19 = float(r10 == 2u);
        float4 r25 = float4(fma(-0.34, r15, 0.34 * r12), fma(0.68, r19, -0.34), 0.0, 1.0) + payload.offsets[r9];
        float4 r28 = float4(r15, r12, r19, 1.0) * payload.tints[r9];
        __usl_mesh.set_vertex(tid, MeshVertex{r25, r28});
        __usl_mesh.set_index(tid, tid);
    }
}

fragment float4 fragment_main(MeshVertex input [[stage_in]]) {
    return input.color;
}
