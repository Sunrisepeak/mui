// The light of what is whole at every size, for the surfaces that let light through (xrender.mesh's
// `Texture::of_light`), drawn by three corners that cover a square of the sheet.
struct VIn {
    float3 position : TEXCOORD0;  // already in clip space
    float3 normal : TEXCOORD1;    // not used: the pass's one layout of corners
};
struct VOut {
    float4 clip : SV_Position;
};

[shader("vertex")]
VOut behind_vs(VIn corner) {
    VOut made;
    made.clip = float4(corner.position.xy, 0.5, 1);
    return made;
}

// The frame's light times how much is drawn at each pixel — its fourth — and that; then each
// next size half as wide and as high (one at least), a texel the mean of the four it covers, those past an odd
// edge the edge's own.
struct Halved {
    float4 from;  // xy: where the finer size's first texel is on the texture read; zw: its width and height
    float4 size;  // zw: one over the texture read's width and height
};
[[vk::binding(0, 2)]] Sampler2D finer_map : register(t0, space2) : register(s0, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Halved> halved : register(b0, space3);

// A texel by its place, read at its middle (between none: the sampler is the nearest's).
float4 texel(float2 at) { return finer_map.SampleLevel((at + 0.5) * halved.size.zw, 0); }

[shader("fragment")]
float4 drawn_fs(VOut point) : SV_Target {
    const float4 light = texel(floor(point.clip.xy));
    const float there = clamp(light.a, 0.0, 1.0);
    return float4(light.rgb * there, there);
}

[shader("fragment")]
float4 halved_fs(VOut point) : SV_Target {
    const float2 at = floor(point.clip.xy) * 2, last = halved.from.zw - 1;
    const float2 first = min(at, last), second = min(at + 1, last);
    const float4 sum = texel(halved.from.xy + first) + texel(halved.from.xy + float2(second.x, first.y)) + texel(halved.from.xy + float2(first.x, second.y)) + texel(halved.from.xy + second);
    return sum * 0.25;
}

// A picture's size onto its square of the sheet of the materials' other pictures, with a texel about it: each
// texel the picture there, read by the picture's own sampler (repeating, mirrored or stopped past its edges, as
// the texel about the square then is). `halved.from`: xy where the square's first texel (the one about it) is on
// the sheet, zw the size's width and height; `halved.size.x`: which size.
[shader("fragment")]
float4 tiled_fs(VOut point) : SV_Target {
    const float2 within = floor(point.clip.xy) - halved.from.xy - 1.0;
    return finer_map.SampleLevel((within + 0.5) / halved.from.zw, halved.size.x);
}
