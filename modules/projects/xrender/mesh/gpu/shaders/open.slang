// How open each pixel's place is to the light from everywhere, on a device (xrender.mesh's reference sends rays
// through the scene; a device has what the eye sees): first the surface at each pixel — which way it faces and
// how far from the eye it is — then, a pixel at a time, rays about the surface's normal stepped through that
// picture (shaders/ids.slang's `surface_fs`). A ray is shut where a step lies behind what the eye sees there, by no more than a thing is thick.
// What the eye does not see (behind things, off the picture) shuts nothing: said in the README.
struct VIn {
    float3 position : TEXCOORD0;  // already in clip space
    float3 normal : TEXCOORD1;    // not used: the pass's one layout of corners
};
struct CoverOut {
    float4 clip : SV_Position;
};
[shader("vertex")]
CoverOut open_vs(VIn corner) {
    CoverOut made;
    made.clip = float4(corner.position.xy, 0.5, 1);
    return made;
}

struct Open {
    float4 to_clip[4];    // rows: the world to clip space, the frame's jitter in it
    float4 from_clip[4];  // rows: back
    float4 eye;
    float4 size;          // xy: the picture's width and height; zw: one over them
    float4 how;           // x: rays a pixel; y: how far a ray goes; z: the seed; w: steps a ray
};
[[vk::binding(0, 3)]] ConstantBuffer<Open> open : register(b0, space3);
[[vk::binding(0, 2)]] Sampler2D surface_map : register(t0, space2) : register(s0, space2);

uint mixed(uint value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

[shader("fragment")]
float4 open_fs(CoverOut point) : SV_Target {
    const float4 here = surface_map.SampleLevel(point.clip.xy * open.size.zw, 0);
    const float far = here.z + here.w;
    if (!(far > 0) || far >= 250.0) return float4(1, 0, 0, 1);
    float3 normal = float3(here.xy, 1.0 - abs(here.x) - abs(here.y));
    if (normal.z < 0) normal.xy = (1.0 - abs(normal.yx)) * float2(normal.x >= 0 ? 1 : -1, normal.y >= 0 ? 1 : -1);
    normal = normalize(normal);
    // Where the pixel's surface is: along the pixel's own ray, as far as the picture says.
    const float4 through_pixel = float4(point.clip.x * open.size.z * 2.0 - 1.0, 1.0 - point.clip.y * open.size.w * 2.0, 0.5, 1);
    const float4 there = float4(dot(open.from_clip[0], through_pixel), dot(open.from_clip[1], through_pixel), dot(open.from_clip[2], through_pixel), dot(open.from_clip[3], through_pixel));
    const float3 toward = normalize(there.xyz / there.w - open.eye.xyz);
    const float3 place = open.eye.xyz + toward * far;
    // A step is as wide as a pixel is there, at the least: nearer than that the picture cannot tell.
    const float4 beside_pixel = through_pixel + float4(open.size.z * 2.0, 0, 0, 0);
    const float4 beside = float4(dot(open.from_clip[0], beside_pixel), dot(open.from_clip[1], beside_pixel), dot(open.from_clip[2], beside_pixel), dot(open.from_clip[3], beside_pixel));
    const float texel = far * length(normalize(beside.xyz / beside.w - open.eye.xyz) - toward);
    const float3 lifted = place + normal * max(2e-3, texel);
    const float3 aside = normalize(cross(abs(normal.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0), normal)), other = cross(normal, aside);
    const int rays = int(open.how.x), steps = int(open.how.w);
    const float reach = open.how.y;
    uint drawn = mixed(mixed(uint(open.how.z)) + uint(point.clip.y) * uint(open.size.x) + uint(point.clip.x));
    float free = 0;
    for (int ray = 0; ray < rays; ++ray) {
        drawn = mixed(drawn + 0x9e3779b9u);
        const float first = float(drawn >> 8) / 16777216.0;
        drawn = mixed(drawn + 0x9e3779b9u);
        const float turn = float(drawn >> 8) / 16777216.0 * 6.2831853;
        drawn = mixed(drawn + 0x9e3779b9u);
        const float start = float(drawn >> 8) / 16777216.0;  // where in its step a ray is looked at: no bands
        const float out = sqrt(first);
        const float3 way = normalize(aside * (out * cos(turn)) + other * (out * sin(turn)) + normal * sqrt(max(1.0 - first, 0.0)));
        bool met = false;
        for (int step = 0; step < steps && !met; ++step) {
            // Steps grow with the square: most of them near, where most of what shuts a ray is.
            const float share = (float(step) + start) / float(steps);
            const float along = reach * share * share;
            const float4 at = float4(lifted + way * along, 1);
            const float4 clip = float4(dot(open.to_clip[0], at), dot(open.to_clip[1], at), dot(open.to_clip[2], at), dot(open.to_clip[3], at));
            if (!(clip.w > 0)) break;
            const float2 on_picture = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
            if (on_picture.x < 0 || on_picture.x >= 1 || on_picture.y < 0 || on_picture.y >= 1) break;
            const float4 seen = surface_map.SampleLevel((floor(on_picture * open.size.xy) + 0.5) * open.size.zw, 0);
            const float seen_far = seen.z + seen.w;
            if (!(seen_far > 0)) continue;
            const float behind = length(at.xyz - open.eye.xyz) - seen_far;
            // Behind what the eye sees there — by more than the picture's own grain, by less than a thing is thick.
            const float grain = max(4e-3, 2.0 * texel) + along * 0.02;
            if (behind > grain && behind < max(0.5, along)) met = true;
        }
        if (!met) free += 1.0;
    }
    return float4(rays > 0 ? free / float(rays) : 1.0, 0, 0, 1);
}
