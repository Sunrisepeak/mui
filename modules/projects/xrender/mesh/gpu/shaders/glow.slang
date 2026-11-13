// Light brighter than the display shows, spread to the pixels about it (xrender.mesh's `Glow`, the reference):
// one box pass along a row or a column — the first of them taking only what is above the threshold — and at
// the end the light, what spread, and the display's curve. Texels are read at their middles, one for one.
struct VIn {
    float3 position : TEXCOORD0;  // already in clip space
    float3 normal : TEXCOORD1;    // not used: the pass's one layout of corners
};
struct VOut {
    float4 clip : SV_Position;
};

[shader("vertex")]
VOut glow_vs(VIn corner) {
    VOut made;
    made.clip = float4(corner.position.xy, 0.5, 1);
    return made;
}

struct Glow {
    float4 size;    // xy: the picture's width and height; zw: one over them
    float4 along;   // xy: one texel along the pass (1, 0) or (0, 1); z: the radius; w: over 0.5 — take only what is above the threshold
    float4 amount;  // x: the threshold; y: the strength
};
[[vk::binding(0, 3)]] ConstantBuffer<Glow> glow : register(b0, space3);
[[vk::binding(0, 2)]] Sampler2D first : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 2)]] Sampler2D second : register(t1, space2) : register(s1, space2);

float3 excess(float3 light) {
    const float brightest = max(light.r, max(light.g, light.b));
    return brightest > glow.amount.x ? light * ((brightest - glow.amount.x) / brightest) : float3(0, 0, 0);
}

[shader("fragment")]
float4 spread_fs(VOut point) : SV_Target {
    const float2 here = floor(point.clip.xy);
    const int radius = int(glow.along.z);
    float3 sum = float3(0, 0, 0);
    for (int offset = -radius; offset <= radius; ++offset) {
        const float2 there = here + glow.along.xy * float(offset);
        // Past the picture's edge there is nothing.
        if (there.x < 0 || there.y < 0 || there.x >= glow.size.x || there.y >= glow.size.y) continue;
        const float3 value = first.SampleLevel((there + 0.5) * glow.size.zw, 0).rgb;
        sum += glow.along.w > 0.5 ? excess(value) : value;
    }
    return float4(sum / float(radius * 2 + 1), 1);
}

float encoded(float linear) {
    const float held = clamp(linear, 0.0, 1.0);
    return held <= 0.0031308 ? held * 12.92 : 1.055 * pow(held, 1.0 / 2.4) - 0.055;
}

// `first`: the light; `second`: what spread.
[shader("fragment")]
float4 shown_fs(VOut point) : SV_Target {
    const float2 at = (floor(point.clip.xy) + 0.5) * glow.size.zw;
    const float3 colour = first.SampleLevel(at, 0).rgb + second.SampleLevel(at, 0).rgb * glow.amount.y;
    return float4(encoded(colour.r), encoded(colour.g), encoded(colour.b), 1);
}
