/*%%HEADER%%*/
/*%%DEFINES%%*/
#ifdef MJGS_SUBGROUP
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_ARB_shader_atomic_counter_ops : require
#endif

layout(local_size_x = 256) in;
uniform mat4 modelViewProj;
uniform vec2 nearFar;
uniform uint keyMax;
layout(binding = 4, offset = 0) uniform atomic_uint output_count;
layout(std430, binding = 0) readonly buffer PosBuffer { vec4 positions[]; };
layout(std430, binding = 1) writeonly buffer OutputBuffer { uint quantizedZs[]; };
layout(std430, binding = 2) writeonly buffer OutputBuffer2 { uint indices[]; };

void main() {
    uint idx = gl_GlobalInvocationID.x;
    bool valid = idx < uint(positions.length());
    vec4 p = valid ? modelViewProj * vec4(positions[idx].xyz, 1.0) : vec4(0.0);
    float depth = p.w;
    float xx = p.x / depth;
    float yy = p.y / depth;
    bool visible = valid && depth >= nearFar.x && depth <= nearFar.y &&
                   xx < 1.5 && xx > -1.5 && yy < 1.5 && yy > -1.5;
    uint count = 0u;
#ifdef MJGS_SUBGROUP
    // Every active lane, including padding in the last group, participates.
    // Reserve one contiguous range per subgroup instead of one atomic per point.
    uvec4 mask = subgroupBallot(visible);
    uint total = subgroupBallotBitCount(mask);
    if (subgroupElect() && total > 0u) count = atomicCounterAdd(output_count, total);
    count = subgroupBroadcastFirst(count) + subgroupBallotExclusiveBitCount(mask);
#else
    if (visible) count = atomicCounterIncrement(output_count);
#endif
    if (visible) {
        quantizedZs[count] = keyMax - uint((depth / nearFar.y) * keyMax);
        indices[count] = idx;
    }
}
