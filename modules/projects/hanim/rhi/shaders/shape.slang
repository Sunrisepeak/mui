// hanim's prepared shape morphs, every control point of every segment at
// once: the CPU sampler's arithmetic (PreparedShapeMorph::sample and
// PointPath::interpolate's chord-frame curve) in 32-bit floats.
// Each segment is 8 floats per end: start, control1, control2, end.
[[vk::binding(0, 0)]] StructuredBuffer<float> from_points : register(t0, space0);
[[vk::binding(1, 0)]] StructuredBuffer<float> to_points : register(t1, space0);
[[vk::binding(0, 1)]] RWStructuredBuffer<float> sampled : register(u0, space1);
struct Params {
    uint  points;      // segments × 4
    float progress;    // clamped to [0, 1] by the host
    float control1_x, control1_y, control2_x, control2_y;  // the point path's (chord frame)
    uint  curved;      // 0: straight
    uint  pad;
};
[[vk::binding(0, 2)]] ConstantBuffer<Params> params : register(b0, space2);

[shader("compute")]
[numthreads(64, 1, 1)]
void interpolate(uint3 id : SV_DispatchThreadID) {
    if (id.x >= params.points) return;
    const uint at = id.x * 2;
    const float2 a = float2(from_points[at], from_points[at + 1]);
    const float2 b = float2(to_points[at], to_points[at + 1]);
    const float t = params.progress;
    float2 point = a + (b - a) * t;
    if (params.curved != 0 && t > 0 && t < 1 && any(a != b)) {
        const float early = 3 * (1 - t) * (1 - t) * t, late = 3 * (1 - t) * t * t;
        const float along = early * params.control1_x + late * params.control2_x;
        const float normal = early * params.control1_y + late * params.control2_y;
        const float2 chord = b - a;
        point += float2(chord.x * along - chord.y * normal, chord.y * along + chord.x * normal);
    }
    sampled[at] = point.x;
    sampled[at + 1] = point.y;
}
