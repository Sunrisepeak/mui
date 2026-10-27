// A layer composited over its backdrop under a blend mode other than normal
// (openmui.render:gpu): the CPU reference's composite_pixel, step for step.
// The layer's pixel is scaled by the layer's opacity and the clip mask in
// 8-bit integers (mul255), then blended with the backdrop — a copy of the
// target under the layer — by W3C Compositing and Blending §10 on
// unpremultiplied channels, and written in place of the target's pixel.
//
// Device space: pixels, y down; one quad over the composited box.

struct Params {
    float4 size;  // target width, height, -, -
    float4 box;   // the composited box, device pixels: x0, y0, x1, y1
    float4 mode;  // blend mode (render::BlendMode), opacity (0…255), clip mask (0 or 1), -
    float4 clip;  // the clip rectangle, device: x0, y0, x1, y1 (its fractional edges cover by area)
};
[[vk::binding(0, 1)]] ConstantBuffer<Params> vparams : register(b0, space1);

struct VOut {
    float4 pos : SV_Position;
};

[shader("vertex")]
VOut blend_vs(uint vertex : SV_VertexID) {
    const uint corners[6] = { 0, 1, 2, 1, 3, 2 };
    const uint c = corners[vertex % 6];
    const float2 device = lerp(vparams.box.xy, vparams.box.zw, float2(float(c & 1), float(c >> 1)));
    VOut o;
    o.pos = float4(device.x / vparams.size.x * 2 - 1, 1 - device.y / vparams.size.y * 2, 0, 1);
    return o;
}

// The layer, the backdrop (both target-sized) and the clip mask, read at
// pixel centres through a nearest sampler.
[[vk::binding(0, 2)]] Sampler2D layer : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 2)]] Sampler2D backdrop : register(t1, space2) : register(s1, space2);
[[vk::binding(2, 2)]] Sampler2D clip_mask : register(t2, space2) : register(s2, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Params> fparams : register(b0, space3);

// render::BlendMode
static const int MULTIPLY = 1;
static const int SCREEN = 2;
static const int OVERLAY = 3;
static const int DARKEN = 4;
static const int LIGHTEN = 5;
static const int COLOR_DODGE = 6;
static const int COLOR_BURN = 7;
static const int HARD_LIGHT = 8;
static const int SOFT_LIGHT = 9;
static const int DIFFERENCE = 10;
static const int EXCLUSION = 11;
static const int PLUS = 12;
static const int ERASE = 13;

uint mul255(uint x, uint y) {
    const uint t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

uint4 bytes(float4 v) { return uint4(round(saturate(v) * 255.0)); }

uint to8(float v) { return uint(int(clamp(v, 0.0, 1.0) * 255.0 + 0.5)); }

float screen(float cb, float cs) { return cb + cs - cb * cs; }

float hard_light(float cb, float cs) { return cs <= 0.5 ? cb * 2 * cs : screen(cb, 2 * cs - 1); }

float blend_channel(int mode, float cb, float cs) {
    switch (mode) {
        case MULTIPLY: return cb * cs;
        case SCREEN: return screen(cb, cs);
        case OVERLAY: return hard_light(cs, cb);
        case DARKEN: return min(cb, cs);
        case LIGHTEN: return max(cb, cs);
        case COLOR_DODGE:
            if (cb == 0) return 0;
            if (cs == 1) return 1;
            return min(1.0, cb / (1 - cs));
        case COLOR_BURN:
            if (cb == 1) return 1;
            if (cs == 0) return 0;
            return 1 - min(1.0, (1 - cb) / cs);
        case HARD_LIGHT: return hard_light(cb, cs);
        case SOFT_LIGHT: {
            if (cs <= 0.5) return cb - (1 - 2 * cs) * cb * (1 - cb);
            const float d = cb <= 0.25 ? ((16 * cb - 12) * cb + 4) * cb : sqrt(cb);
            return cb + (2 * cs - 1) * (d - cb);
        }
        case DIFFERENCE: return abs(cb - cs);
        case EXCLUSION: return cb + cs - 2 * cb * cs;
        default: return cs;
    }
}

[shader("fragment")]
float4 blend_fs(VOut i) : SV_Target {
    const float2 uv = i.pos.xy / fparams.size.xy;
    const uint4 d = bytes(backdrop.SampleLevel(uv, 0));
    uint4 s = bytes(layer.SampleLevel(uv, 0));
    if (all(s == 0)) return float4(d) / 255.0;
    uint k = uint(fparams.mode.y);
    const float2 p = i.pos.xy;
    const float2 a = clamp(min(p + 0.5, fparams.clip.zw) - max(p - 0.5, fparams.clip.xy), 0.0, 1.0);
    if (a.x * a.y < 1) k = mul255(k, uint(round(a.x * a.y * 255.0)));
    if (fparams.mode.z != 0) k = mul255(k, bytes(clip_mask.SampleLevel(uv, 0)).r);
    if (k != 255) s = uint4(mul255(s.r, k), mul255(s.g, k), mul255(s.b, k), mul255(s.a, k));
    const int mode = int(fparams.mode.x);
    uint4 o;
    if (mode == PLUS) {
        o = min(d + s, 255);
    } else if (mode == ERASE) {
        const uint inv = 255 - s.a;  // destination-out
        o = uint4(mul255(d.r, inv), mul255(d.g, inv), mul255(d.b, inv), mul255(d.a, inv));
    } else if (s.a == 0) {
        o = d;
    } else {
        const float sa = s.a / 255.0, da = d.a / 255.0;
        for (int c = 0; c < 3; ++c) {
            const float cs = s[c] / 255.0 / sa;
            const float cb = da > 0 ? d[c] / 255.0 / da : 0.0;
            const float b = blend_channel(mode, clamp(cb, 0.0, 1.0), clamp(cs, 0.0, 1.0));
            // co = cs·αs·(1 − αb) + cb·αb·(1 − αs) + αs·αb·B(cb, cs), premultiplied
            o[c] = to8((s[c] / 255.0) * (1 - da) + (d[c] / 255.0) * (1 - sa) + sa * da * b);
        }
        o.a = to8(sa + da - sa * da);
    }
    return float4(o) / 255.0;
}
