// Frames gathered over time (xrender.mesh's `Temporal`, the reference): a pixel's light is the last result's
// where its surface was a frame ago, mixed with this frame's — unless neither there nor beside it was the same
// thing: then this frame's, and it begins again. What is carried is held to what the frame shows about the
// pixel. The result's fourth part is how many frames are gathered there.
struct VIn {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
};
struct VOut {
    float4 clip : SV_Position;
};

[shader("vertex")]
VOut resolve_vs(VIn corner) {
    VOut made;
    made.clip = float4(corner.position.xy, 0.5, 1);
    return made;
}

struct Resolve {
    float4 size;  // xy: the picture's width and height; zw: one over them
    float4 how;   // x: over 0.5 — there is a last result; y: the newest frame's least share
};
[[vk::binding(0, 3)]] ConstantBuffer<Resolve> resolve : register(b0, space3);
[[vk::binding(0, 2)]] Sampler2D light : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 2)]] Sampler2D velocity : register(t1, space2) : register(s1, space2);
[[vk::binding(2, 2)]] Sampler2D who : register(t2, space2) : register(s2, space2);
[[vk::binding(3, 2)]] Sampler2D who_before : register(t3, space2) : register(s3, space2);
[[vk::binding(4, 2)]] Sampler2D gathered : register(t4, space2) : register(s4, space2);

float2 middle(float2 texel) { return (texel + 0.5) * resolve.size.zw; }

[shader("fragment")]
float4 resolve_fs(VOut point) : SV_Target {
    const float2 here = floor(point.clip.xy);
    const float2 last = resolve.size.xy - 1;
    const float3 now = light.SampleLevel(middle(here), 0).rgb;
    if (resolve.how.x < 0.5) return float4(now, 0);
    const float id = who.SampleLevel(middle(here), 0).r;
    // Where this surface was in the last result.
    const float2 was = here + 0.5 - velocity.SampleLevel(middle(here), 0).xy;
    const float2 near = floor(was);
    if (near.x < 0 || near.y < 0 || near.x > last.x || near.y > last.y) return float4(now, 0);
    // Whether the last result had this thing there or beside it.
    bool about = false;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const float2 beside = near + float2(dx, dy);
            if (beside.x < 0 || beside.y < 0 || beside.x > last.x || beside.y > last.y) continue;
            if (who_before.SampleLevel(middle(beside), 0).r == id) about = true;
        }
    if (!about) return float4(now, 0);
    // Its light there: between the four results about the place.
    const float2 from = was - 0.5;
    const float2 first = clamp(floor(from), float2(0, 0), last), second = min(first + 1, last);
    const float2 share = clamp(from - first, float2(0, 0), float2(1, 1));
    const float3 top = lerp(gathered.SampleLevel(middle(first), 0).rgb, gathered.SampleLevel(middle(float2(second.x, first.y)), 0).rgb, share.x);
    const float3 bottom = lerp(gathered.SampleLevel(middle(float2(first.x, second.y)), 0).rgb, gathered.SampleLevel(middle(second), 0).rgb, share.x);
    float3 carried = lerp(top, bottom, share.y);
    // Held to what the frame shows about this pixel: what has changed does not linger.
    float3 low = now, high = now;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const float3 beside = light.SampleLevel(middle(clamp(here + float2(dx, dy), float2(0, 0), last)), 0).rgb;
            low = min(low, beside), high = max(high, beside);
        }
    carried = clamp(carried, low, high);
    const float age = min(gathered.SampleLevel(middle(near), 0).a + 1, 1000.0);
    return float4(lerp(carried, now, max(1.0 / (age + 1), resolve.how.y)), age);
}
