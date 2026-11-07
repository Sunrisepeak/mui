// hanim's prepared shape morphs, every control point of every segment at
// once: the CPU sampler's arithmetic (PreparedShapeMorph::sample and
// PointPath::interpolate's chord-frame curve and path noise) in 32-bit floats.
// Each segment is 8 floats per end: start, control1, control2, end.
[[vk::binding(0, 0)]] StructuredBuffer<float> from_points : register(t0, space0);
[[vk::binding(1, 0)]] StructuredBuffer<float> to_points : register(t1, space0);
[[vk::binding(0, 1)]] RWStructuredBuffer<float> sampled : register(u0, space1);
struct Params {
    uint  points;      // segments × 4
    float progress;    // clamped to [0, 1] by the host
    float control1_x, control1_y, control2_x, control2_y;  // the point path's (chord frame)
    uint  curved;      // 0: straight
    uint  field;       // path noise: 0 none, 1 gradient, 2 curl
    float noise_amplitude, noise_scale, noise_speed;
    uint  seed_a, seed_b;  // mlibs.math's fold32 of the seed, and of the seed ^ golden
    uint  pad0, pad1, pad2;
};
[[vk::binding(0, 2)]] ConstantBuffer<Params> params : register(b0, space2);

// mlibs.math's gradient noise (noise.cppm), the same lattice: the 32-bit key
// (noise_key, mix32 of lowbias32), sixteen exact directions, Perlin's quintic fade.
static const float2 directions[16] = {
    float2(1, 0), float2(0.9238795325112867, 0.3826834323650898), float2(0.7071067811865476, 0.7071067811865476),
    float2(0.3826834323650898, 0.9238795325112867), float2(0, 1), float2(-0.3826834323650898, 0.9238795325112867),
    float2(-0.7071067811865476, 0.7071067811865476), float2(-0.9238795325112867, 0.3826834323650898), float2(-1, 0),
    float2(-0.9238795325112867, -0.3826834323650898), float2(-0.7071067811865476, -0.7071067811865476),
    float2(-0.3826834323650898, -0.9238795325112867), float2(0, -1), float2(0.3826834323650898, -0.9238795325112867),
    float2(0.7071067811865476, -0.7071067811865476), float2(0.9238795325112867, -0.3826834323650898)
};
uint finalize32(uint z) {
    z = (z ^ (z >> 16)) * 0x7feb352du;
    z = (z ^ (z >> 15)) * 0x846ca68bu;
    return z ^ (z >> 16);
}
uint mix32(uint x) { return finalize32(x + 0x9e3779b9u); }
float2 lattice(int cell_x, int cell_y, uint seed) {
    return directions[mix32(uint(cell_x) * 0x9e3779b1u ^ mix32(uint(cell_y) ^ seed)) >> 28];
}
// (value, ∂/∂x, ∂/∂y)
float3 gradient_noise(float x, float y, uint seed) {
    const float floor_x = floor(x), floor_y = floor(y);
    const float fx = x - floor_x, fy = y - floor_y;
    const int cx = int(floor_x), cy = int(floor_y);
    const float2 g00 = lattice(cx, cy, seed), g10 = lattice(cx + 1, cy, seed), g01 = lattice(cx, cy + 1, seed), g11 = lattice(cx + 1, cy + 1, seed);
    const float a = g00.x * fx + g00.y * fy, b = g10.x * (fx - 1) + g10.y * fy;
    const float c = g01.x * fx + g01.y * (fy - 1), d = g11.x * (fx - 1) + g11.y * (fy - 1);
    const float u = fx * fx * fx * (fx * (fx * 6 - 15) + 10), v = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
    const float du = 30 * fx * fx * (fx - 1) * (fx - 1), dv = 30 * fy * fy * (fy - 1) * (fy - 1);
    const float mixed = a - b - c + d;
    const float range = 1.4142135623730951;
    return range * float3(a + u * (b - a) + v * (c - a) + u * v * mixed,
                          g00.x + u * (g10.x - g00.x) + v * (g01.x - g00.x) + u * v * (g00.x - g10.x - g01.x + g11.x) + du * ((b - a) + v * mixed),
                          g00.y + u * (g10.y - g00.y) + v * (g01.y - g00.y) + u * v * (g00.y - g10.y - g01.y + g11.y) + dv * ((c - a) + u * mixed));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void interpolate(uint3 id : SV_DispatchThreadID) {
    if (id.x >= params.points) return;
    const uint at = id.x * 2;
    const float2 a = float2(from_points[at], from_points[at + 1]);
    const float2 b = float2(to_points[at], to_points[at + 1]);
    const float t = params.progress;
    float2 point = a + (b - a) * t;
    if (t > 0 && t < 1) {
        if (params.field != 0) {
            const float envelope = 16 * t * t * (1 - t) * (1 - t);
            const float drift = params.noise_speed * t;
            const float x = a.x / params.noise_scale + drift, y = a.y / params.noise_scale + drift * 0.6180339887498949;
            if (params.field == 2) {
                // Curl noise reaches about ±2.5; halved towards ±amplitude.
                const float3 stream = gradient_noise(x, y, params.seed_a);
                point += params.noise_amplitude * envelope * 0.5 * float2(stream.z, -stream.y);
            } else {
                point += params.noise_amplitude * envelope * float2(gradient_noise(x, y, params.seed_a).x, gradient_noise(x, y, params.seed_b).x);
            }
        }
        if (params.curved != 0 && any(a != b)) {
            const float early = 3 * (1 - t) * (1 - t) * t, late = 3 * (1 - t) * t * t;
            const float along = early * params.control1_x + late * params.control2_x;
            const float normal = early * params.control1_y + late * params.control2_y;
            const float2 chord = b - a;
            point += float2(chord.x * along - chord.y * normal, chord.y * along + chord.x * normal);
        }
    }
    sampled[at] = point.x;
    sampled[at + 1] = point.y;
}
