// The effect template (D-60 pixel tier, D-68; architecture §8: "a third
// party's shader uses the template only"). An effect is a fragment shader run
// once per pixel of the layer's surface — the layer drawn offscreen, larger
// than the layer by the effect's reach — that writes the result's pixel:
//
//     import openmui_effect;
//
//     [shader("fragment")]
//     float4 tint_fs(EffectPixel px) : SV_Target {
//         const uint4 s = source_texel(at(px));
//         return out8(scaled(s, param(0)));
//     }
//
// What it reads is fixed: the source surface (premultiplied RGBA, 8 bits a
// channel), an auxiliary texture the renderer fills for some built-ins, and
// `effect` — the surface's size, the layer's rectangle in it, the device
// scale, the parameters in the effect's order and values derived from them on
// the CPU. It cannot change pipeline state, and the renderer checks the
// shader binds nothing else.
//
// The helpers are the CPU reference implementations' (render/src/effects.cpp)
// operation for operation: an effect written with them gives the CPU's bytes
// wherever the arithmetic is exact (integer hashing, sums, comparisons) and
// within a level where the GPU rounds a division or a square root otherwise.

struct EffectFrame {
    float4 size;        // the surface's width, height (device pixels); the device scale; -
    float4 layer;       // the layer's rectangle in the surface: x, y, width, height
    float4 params[4];   // parameters 0…15, in the effect's order
    float4 derived[2];  // 0…7: values the renderer computed for the effect on the CPU
};

[[vk::binding(0, 2)]] Sampler2D effect_source : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 2)]] Sampler2D effect_aux : register(t1, space2) : register(s1, space2);
[[vk::binding(0, 3)]] ConstantBuffer<EffectFrame> effect : register(b0, space3);

// What a fragment of an effect gets: the pixel it writes.
struct EffectPixel {
    float4 pos : SV_Position;
};
int2 at(EffectPixel px) { return int2(px.pos.xy); }

float param(int i) { return effect.params[i >> 2][i & 3]; }
float derived(int i) { return effect.derived[i >> 2][i & 3]; }
int2 surface_size() { return int2(effect.size.xy); }
float device_scale() { return effect.size.z; }

// ── pixels ───────────────────────────────────────────────────────────────

// The source's pixel at p as bytes; outside the surface, transparent.
uint4 source_texel(int2 p) {
    const int2 n = surface_size();
    if (p.x < 0 || p.y < 0 || p.x >= n.x || p.y >= n.y) return uint4(0);
    return uint4(round(saturate(effect_source.SampleLevel((float2(p) + 0.5) / float2(n), 0)) * 255.0));
}

// Bilinear sample in byte units at a point of the surface (pixel centres at
// .5), outside transparent: effects.cpp's sample().
float4 source_bilinear(float2 q) {
    q -= 0.5;
    const float2 f = floor(q);
    const int2 i = int2(f);
    const float2 t = q - f;
    float4 out = 0;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
            const int2 p = i + int2(dx, dy);
            const int2 n = surface_size();
            if (p.x < 0 || p.y < 0 || p.x >= n.x || p.y >= n.y) continue;
            precise const float w = (dx != 0 ? t.x : 1 - t.x) * (dy != 0 ? t.y : 1 - t.y);
            precise const float4 v = float4(source_texel(p)) * w;
            out += v;
        }
    return out;
}

// A channel value 0…255 to a byte: effects.cpp's q8.
uint q8(float v) { return uint(clamp(v, 0.0, 255.0) + 0.5); }
uint4 q8(float4 v) { return uint4(q8(v.x), q8(v.y), q8(v.z), q8(v.w)); }

uint4 scaled(uint4 p, float k) {
    if (k >= 1) return p;
    if (k <= 0) return uint4(0);
    precise const float4 v = float4(p) * k;
    return q8(v);
}

// What a fragment returns for bytes.
float4 out8(uint4 p) { return float4(p) / 255.0; }

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// 1 for a value well past the threshold, 0 before it, `soft` wide; progress
// 0 shows everything, 1 nothing.
float ramp(float value, float progress, float soft) {
    soft = max(soft, 1e-4);
    precise const float threshold = progress * (1 + soft) - soft;
    return clamp01((value - threshold) / soft);
}

// ── hashing: effects.cpp's 64-bit mix, in pairs of 32-bit words ─────────

struct u64 {
    uint lo, hi;
};

u64 u64of(uint lo, uint hi) { u64 r; r.lo = lo; r.hi = hi; return r; }
u64 xor64(u64 a, u64 b) { return u64of(a.lo ^ b.lo, a.hi ^ b.hi); }
u64 add64(u64 a, u64 b) {
    const uint lo = a.lo + b.lo;
    return u64of(lo, a.hi + b.hi + (lo < a.lo ? 1u : 0u));
}
u64 shr64(u64 a, uint n) {  // 0 < n < 32
    return u64of((a.lo >> n) | (a.hi << (32 - n)), a.hi >> n);
}
// The low 32 bits and the high 32 bits of a 32 × 32 product.
u64 mul32(uint a, uint b) {
    const uint a0 = a & 0xffffu, a1 = a >> 16, b0 = b & 0xffffu, b1 = b >> 16;
    const uint p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const uint mid = (p00 >> 16) + (p01 & 0xffffu) + (p10 & 0xffffu);
    return u64of((p00 & 0xffffu) | (mid << 16), p11 + (p01 >> 16) + (p10 >> 16) + (mid >> 16));
}
u64 mul64(u64 a, u64 b) {
    u64 r = mul32(a.lo, b.lo);
    r.hi += a.lo * b.hi + a.hi * b.lo;
    return r;
}

u64 mix64(u64 x) {
    x = add64(x, u64of(0x7f4a7c15u, 0x9e3779b9u));
    x = mul64(xor64(x, shr64(x, 30)), u64of(0x1ce4e5b9u, 0xbf58476du));
    x = mul64(xor64(x, shr64(x, 27)), u64of(0x133111ebu, 0x94d049bbu));
    return xor64(x, shr64(x, 31));
}

// A signed integer, two's complement in 64 bits.
u64 i64of(int v) { return u64of(uint(v), v < 0 ? 0xffffffffu : 0u); }

// A number in [0, 1) for (seed, x, y, salt), the CPU's hash01: seeds 0…1e6
// (seed · 4096 fits 32 bits; negative seeds as small as −524288).
float hash01(float seed, int x, int y, uint salt) {
    const float s4096 = seed * 4096.0;
    const u64 s = s4096 >= 0 ? u64of(uint(s4096), 0) : i64of(int(s4096));
    const u64 sy = mix64(add64(i64of(y), mul64(u64of(salt, 0), u64of(0xd9b4e019u, 0x632be59bu))));
    const u64 sx = xor64(mul64(i64of(x), u64of(0x9e3779b1u, 0)), sy);
    const u64 h = mix64(xor64(s, mix64(sx)));
    return float(h.hi >> 8) * (1.0 / 16777216.0);
}

// ── geometry ─────────────────────────────────────────────────────────────

// lround: to the nearest integer, halves away from zero, exactly.
int lround_exact(float v) {
    const float a = abs(v), f = floor(a);
    const int r = int(a - f >= 0.5 ? f + 1 : f);
    return v < 0 ? -r : r;
}
