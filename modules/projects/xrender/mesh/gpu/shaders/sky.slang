// What is behind everything, through the fog: the background as a surface infinitely far along each pixel's
// own ray (xrender.mesh's reference: looked at upward through thinning fog it shows, level it does not).
// Three corners that cover the picture, drawn before anything with no depth written.
struct VIn {
    float3 position : TEXCOORD0;  // already in clip space
    float3 normal : TEXCOORD1;    // not used: the pass's one layout of corners
};
struct VOut {
    float4 clip : SV_Position;
    float2 at : TEXCOORD0;  // −1 … 1 across the picture, y up
};

[shader("vertex")]
VOut sky_vs(VIn corner) {
    VOut made;
    made.clip = float4(corner.position.xy, 0.5, 1);
    made.at = corner.position.xy;
    return made;
}

struct Sky {
    float4 from_clip[4];  // rows: clip space to the world
    float4 eye;
    float4 background;    // linear
    float4 fog;           // rgb; w: its density
    float4 fog_shape;     // x: the height the density is given at; y: how fast it thins above it
};
[[vk::binding(0, 3)]] ConstantBuffer<Sky> sky : register(b0, space3);

float encoded(float linear) {
    const float held = clamp(linear, 0.0, 1.0);
    return held <= 0.0031308 ? held * 12.92 : 1.055 * pow(held, 1.0 / 2.4) - 0.055;
}

// The way through a pixel, from the eye.
float3 way_through(VOut point) {
    const float4 through_pixel = float4(point.at, 0.5, 1);
    const float4 there = float4(dot(sky.from_clip[0], through_pixel), dot(sky.from_clip[1], through_pixel), dot(sky.from_clip[2], through_pixel), dot(sky.from_clip[3], through_pixel));
    return normalize(there.xyz / there.w - sky.eye.xyz);
}
// What is infinitely far along `toward`, `far`, as it shows through the fog (none: as it is).
float4 through_fog(float3 toward, float3 far) {
    if (!(sky.fog.w > 0)) {
        if (sky.background.w > 0.5) return float4(far, 1);
        return float4(encoded(far.r), encoded(far.g), encoded(far.b), 1);
    }
    const float falloff = sky.fog_shape.y;
    const float here = sky.fog.w * (falloff > 0 ? exp(clamp(-falloff * (sky.eye.y - sky.fog_shape.x), -60.0, 60.0)) : 1.0);
    const float thins = falloff * toward.y;
    // For ever along the ray: level or downward nothing comes through; upward, what the thinning leaves.
    float share = 0;
    if (thins > 1e-9) {
        const float depth = here / thins;
        share = depth > 60 ? 0.0 : exp(-depth);
    }
    const float3 colour = sky.fog.rgb + (far - sky.fog.rgb) * share;
    if (sky.background.w > 0.5) return float4(colour, 1);  // the target holds the light itself
    return float4(encoded(colour.r), encoded(colour.g), encoded(colour.b), 1);
}

[shader("fragment")]
float4 sky_fs(VOut point) : SV_Target { return through_fog(way_through(point), sky.background.rgb); }

// Behind everything the picture of the surroundings, as the eye sees it (xrender.mesh's `Environment::behind`):
// the pass's sheet of squares (mesh.slang's `About`), its first square the picture itself.
struct About {
    float4 shape;
    float4 squares[18];
};
[[vk::binding(0, 2)]] Sampler2D about_map : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 3)]] ConstantBuffer<About> about : register(b1, space3);
float2 square_of(float3 way) {
    const float3 size = abs(way);
    float2 at = way.xz / (size.x + size.y + size.z);
    if (way.y < 0) at = (float2(1, 1) - abs(at.yx)) * float2(at.x < 0 ? -1.0 : 1.0, at.y < 0 ? -1.0 : 1.0);
    return at * 0.5 + 0.5;
}
[shader("fragment")]
float4 sky_about_fs(VOut point) : SV_Target {
    const float3 toward = way_through(point);
    const float4 square = about.squares[0];
    return through_fog(toward, about_map.SampleLevel((square.xy + square_of(toward) * square.z) * about.shape.xy, 0).rgb);
}
