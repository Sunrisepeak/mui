// The GPU renderer's shader for drawing (openmui.render:gpu; layers under
// blend modes are composited by blend.slang): every primitive of a
// display list is an instance of a quad, its kind saying how coverage is
// found; the paint says the colour. What the CPU reference computes exactly
// the GPU computes the same way (an axis-aligned rectangle's coverage is the
// area of the pixel inside it; a gradient is looked up in the same 256-entry
// table at the same index; paths, strokes, glyphs and shadows are coverage
// the CPU rasteriser produced, in an atlas). A rounded rectangle covers a
// pixel by the exact area of its circles; rotated shapes use signed
// distances, within the renderer's tolerance.
//
// Device space: pixels, y down, (0, 0) the target's top-left corner.

// What an instance is (one per primitive; slot 0, per instance).
struct Prim {
    float4 rect : TEXCOORD0;     // kind rect/rrect/image: local x, y, w, h; mask: device x, y, w, h
    float4 m0 : TEXCOORD1;       // local → device: a, b, c, d
    float4 m1 : TEXCOORD2;       // e, f, kind, flags
    float4 radii : TEXCOORD3;    // rrect: top-left, top-right, bottom-right, bottom-left; axis-aligned rect, image: the device rectangle's edges left, top, right, bottom
    float4 shape : TEXCOORD4;    // rring: the stroke's width (local); a byte the result is scaled by (a layered glyph's opacity), a layer's opacity byte, -
    float4 color : TEXCOORD5;    // solid: premultiplied bytes / 255; image paint: w the byte its texels are scaled by
    float4 paint : TEXCOORD6;    // paint kind (0 solid, 1–3 gradients, 4 image, 5 image mask), ramp row, spread, conic start angle
    float4 pgeo : TEXCOORD7;     // gradient start.xy, end.xy (radial: end.x is the radius); a lit image: its rotation into device space (r00, r01, r10, r11)
    float4 pinv0 : TEXCOORD8;    // device → paint: a, b, c, d
    float4 pinv1 : TEXCOORD9;    // e, f, image width, image height
    float4 clip : TEXCOORD10;    // device clip rectangle: x0, y0, x1, y1
    float4 uv : TEXCOORD11;      // mask: atlas x, y; image: source x, y, w, h
};

struct VOut {
    float4 pos : SV_Position;
    float2 local : TEXCOORD0;
    nointerpolation float4 rect : TEXCOORD1;
    nointerpolation float4 m0 : TEXCOORD2;
    nointerpolation float4 m1 : TEXCOORD3;
    nointerpolation float4 radii : TEXCOORD4;
    nointerpolation float4 shape : TEXCOORD5;
    nointerpolation float4 color : TEXCOORD6;
    nointerpolation float4 paint : TEXCOORD7;
    nointerpolation float4 pgeo : TEXCOORD8;
    nointerpolation float4 pinv0 : TEXCOORD9;
    nointerpolation float4 pinv1 : TEXCOORD10;
    nointerpolation float4 clip : TEXCOORD11;
    nointerpolation float4 uv : TEXCOORD12;
};

static const int KIND_RECT = 0;
static const int KIND_RRECT = 1;
static const int KIND_MASK = 2;
static const int KIND_IMAGE = 3;
static const int KIND_RRING = 4;  // a rounded rectangle's stroke: the rrect (the stroke's outer edge) less the one shape.x inside it

static const int FLAG_AXIS_ALIGNED = 1;   // the transform maps the rectangle to an axis-aligned one
static const int FLAG_CLIP_MASK = 2;      // multiply by the clip mask
static const int FLAG_LINEAR = 4;         // image paint / image: bilinear
static const int FLAG_LAYER = 8;          // a layer composited: its pixel at the layer's opacity (shape.z), as the CPU pops it

struct Frame {
    float4 size;   // target width, height; atlas width, height
    float4 ramps;  // the ramp texture's rows, -, -, -
};
[[vk::binding(0, 1)]] ConstantBuffer<Frame> vframe : register(b0, space1);

float2 apply(float4 m0, float2 e, float2 p) { return float2(m0.x * p.x + m0.z * p.y + e.x, m0.y * p.x + m0.w * p.y + e.y); }

[shader("vertex")]
VOut ui_vs(Prim i, uint vertex : SV_VertexID) {
    // The quad's corner: 0 1 2, 1 3 2 over (0,0) (1,0) (0,1) (1,1).
    const uint corners[6] = { 0, 1, 2, 1, 3, 2 };
    const uint c = corners[vertex % 6];
    const float2 t = float2(float(c & 1), float(c >> 1));
    const int kind = int(i.m1.z);
    VOut o;
    float2 device;
    if (kind == KIND_MASK) {
        o.local = i.rect.xy + t * i.rect.zw;  // device space
        device = o.local;
    } else {
        // A pixel's reach around the shape, in local units, for its antialiased edge.
        const float sx = length(float2(i.m0.x, i.m0.y)), sy = length(float2(i.m0.z, i.m0.w));
        const float2 margin = float2(1.5 / max(sx, 1e-6), 1.5 / max(sy, 1e-6));
        const float2 lo = i.rect.xy - margin, hi = i.rect.xy + i.rect.zw + margin;
        o.local = lerp(lo, hi, t);
        device = apply(i.m0, i.m1.xy, o.local);
    }
    o.pos = float4(device.x / vframe.size.x * 2 - 1, 1 - device.y / vframe.size.y * 2, 0, 1);
    o.rect = i.rect;
    o.m0 = i.m0;
    o.m1 = i.m1;
    o.radii = i.radii;
    o.shape = i.shape;
    o.color = i.color;
    o.paint = i.paint;
    o.pgeo = i.pgeo;
    o.pinv0 = i.pinv0;
    o.pinv1 = i.pinv1;
    o.clip = i.clip;
    o.uv = i.uv;
    return o;
}

// The textures: gradient tables (256 wide, a row each), the coverage atlas,
// the image of an image paint or draw, the clip mask (device-sized).
[[vk::binding(0, 2)]] Sampler2D ramps : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 2)]] Sampler2D atlas : register(t1, space2) : register(s1, space2);
[[vk::binding(2, 2)]] Sampler2D image : register(t2, space2) : register(s2, space2);
[[vk::binding(3, 2)]] Sampler2D clip_mask : register(t3, space2) : register(s3, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Frame> fframe : register(b0, space3);

// The area of the pixel [p − ½, p + ½]² inside the rectangle [lo, hi].
float box(float2 p, float2 lo, float2 hi) {
    const float2 a = clamp(min(p + 0.5, hi) - max(p - 0.5, lo), 0.0, 1.0);
    return a.x * a.y;
}

// The signed distance from p (centred on the rectangle) to a rounded
// rectangle of half size h, a radius per corner (y down: top is −y), and its
// gradient (unit, local space).
float rounded(float2 p, float2 h, float4 radii, out float2 g) {
    const float r = p.x < 0 ? (p.y < 0 ? radii.x : radii.w) : (p.y < 0 ? radii.y : radii.z);
    const float2 q = abs(p) - h + r;
    const float2 s = float2(p.x < 0 ? -1.0 : 1.0, p.y < 0 ? -1.0 : 1.0);
    if (q.x > 0 && q.y > 0) {
        const float l = length(q);
        g = s * q / l;
        return l - r;
    }
    if (q.x > q.y) {
        g = float2(s.x, 0);
        return q.x - r;
    }
    g = float2(0, s.y);
    return q.y - r;
}

// The area of the pixel [−½, ½]² where n·x < D, n a unit normal: exact for a
// straight edge at any angle (a linear ramp in the middle, quadratic where
// the edge crosses a corner of the pixel).
float halfplane(float D, float2 n) {
    const float a = min(abs(n.x), abs(n.y)), b = max(abs(n.x), abs(n.y));
    const float h = (a + b) * 0.5;
    if (D >= h) return 1.0;
    if (D <= -h) return 0.0;
    if (a < 1e-6) return saturate(0.5 + D / b);
    if (abs(D) <= (b - a) * 0.5) return 0.5 + D / b;
    const float t = h - abs(D);
    const float corner = t * t / (2.0 * a * b);
    return D > 0 ? 1.0 - corner : corner;
}

// A straight edge's coverage: the field d (local space, unit gradient g)
// mapped to device pixels through the inverse transpose of the transform —
// the distance in pixels is d over the mapped gradient's length — then the
// half-plane's exact area.
float edge(float d, float2 g, float4 m0) {
    const float det = m0.x * m0.w - m0.z * m0.y;
    if (abs(det) < 1e-12) return 0.0;
    const float2 gd = float2(m0.w * g.x - m0.y * g.y, -m0.z * g.x + m0.x * g.y) / det;
    const float len = length(gd);
    if (len < 1e-12) return 0.0;
    return halfplane(-d / len, gd / len);
}

// A rounded rectangle's coverage (p centred, half size h). Away from the
// arcs it is the product of the two slabs' coverage — exact for an
// axis-aligned rectangle, sharp corners included — and on an arc the
// arc's edge.
float rounded_coverage(float2 p, float2 h, float4 radii, float4 m0) {
    const float r = p.x < 0 ? (p.y < 0 ? radii.x : radii.w) : (p.y < 0 ? radii.y : radii.z);
    const float2 q = abs(p) - h + r;
    if (r > 0 && q.x > 0 && q.y > 0) {
        float2 g;
        const float d = rounded(p, h, radii, g);
        return edge(d, g, m0);
    }
    const float x = edge(p.x - h.x, float2(1, 0), m0) + edge(-p.x - h.x, float2(-1, 0), m0) - 1.0;
    const float y = edge(p.y - h.y, float2(0, 1), m0) + edge(-p.y - h.y, float2(0, -1), m0) - 1.0;
    return saturate(x) * saturate(y);
}

// The area of [0, x] × [0, y] (x, y ≥ 0) inside the disk of radius r about
// the origin, exactly: x·y where the corner is inside, else the rectangle up
// to where the circle crosses y, and the circle's integral after it.
float quarter(float x, float y, float r) {
    x = min(x, r);
    y = min(y, r);
    if (x <= 0 || y <= 0) return 0.0;
    if (x * x + y * y <= r * r) return x * y;
    const float x0 = sqrt(max(r * r - y * y, 0.0));
    // ∫ √(r² − u²) du = ½ (u √(r² − u²) + r² asin(u / r))
    const float gx = 0.5 * (x * sqrt(max(r * r - x * x, 0.0)) + r * r * asin(saturate(x / r)));
    const float g0 = 0.5 * (x0 * sqrt(max(r * r - x0 * x0, 0.0)) + r * r * asin(saturate(x0 / r)));
    return x0 * y + gx - g0;
}

// The area of [u1, u2] × [v1, v2] (in a corner's quadrant, ≥ 0) inside its disk.
float quarter_area(float u1, float u2, float v1, float v2, float r) {
    return quarter(u2, v2, r) - quarter(u1, v2, r) - quarter(u2, v1, r) + quarter(u1, v1, r);
}

float overlap(float2 lo, float2 hi, float2 a, float2 b) {
    const float2 o = max(min(hi, b) - max(lo, a), 0.0);
    return o.x * o.y;
}

// The exact coverage of the pixel rectangle [lo, hi] (local space) by the
// rounded rectangle at r0 of size sz: the box, less each corner square's
// part outside its quarter disk.
float exact_rounded(float2 lo, float2 hi, float2 r0, float2 sz, float4 radii) {
    const float2 r1 = r0 + sz;
    float area = overlap(lo, hi, r0, r1);
    if (area <= 0) return 0.0;
    // top-left, top-right, bottom-right, bottom-left
    const float rs[4] = { radii.x, radii.y, radii.z, radii.w };
    for (int k = 0; k < 4; ++k) {
        const float r = rs[k];
        if (r <= 0) continue;
        const bool right = k == 1 || k == 2, bottom = k == 2 || k == 3;
        const float2 c = float2(right ? r1.x - r : r0.x + r, bottom ? r1.y - r : r0.y + r);
        const float2 a = float2(right ? c.x : r0.x, bottom ? c.y : r0.y);
        const float2 b = a + r;
        const float2 qlo = max(lo, a), qhi = min(hi, b);
        if (qhi.x <= qlo.x || qhi.y <= qlo.y) continue;
        // Into the quadrant's coordinates (distance from the centre, outward ≥ 0).
        const float u1 = right ? qlo.x - c.x : c.x - qhi.x, u2 = right ? qhi.x - c.x : c.x - qlo.x;
        const float v1 = bottom ? qlo.y - c.y : c.y - qhi.y, v2 = bottom ? qhi.y - c.y : c.y - qlo.y;
        area -= (qhi.x - qlo.x) * (qhi.y - qlo.y) - quarter_area(u1, u2, v1, v2, r);
    }
    return saturate(area / ((hi.x - lo.x) * (hi.y - lo.y)));
}

float spread_t(float t, int spread) {
    if (spread == 1) return t - floor(t);
    if (spread == 2) {
        const float m = t - 2.0 * floor(t / 2.0);
        return m > 1.0 ? 2.0 - m : m;
    }
    return saturate(t);
}

// ── bytes: the CPU reference's arithmetic (openmui.raster) ─────────────

// x·y/255 rounded to nearest, exactly, for 8-bit values.
uint mul255(uint x, uint y) {
    const uint t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}
uint4 mul255(uint4 c, uint k) { return k == 255 ? c : uint4(mul255(c.r, k), mul255(c.g, k), mul255(c.b, k), mul255(c.a, k)); }

uint4 bytes(float4 v) { return uint4(round(saturate(v) * 255.0)); }

// A fraction of a pixel covered as the rasteriser makes it a byte.
uint coverage_byte(float f) { return min(255u, uint(max(f, 0.0) * 256.0)); }

// A coordinate in 1/256 pixel, as rect_coverage rounds it.
int sub(float v) { return int(floor(v * 256.0 + 0.5)); }

// The coverage of pixel `px` by the device rectangle with edges left, top,
// right, bottom (as Rect::right() and bottom() compute them): raster's
// rect_coverage — sub-pixel edges, ((column · row + 128) >> 8) per pixel.
uint rect_coverage(int2 px, float4 edges) {
    const int l = sub(edges.x), t = sub(edges.y), rr = sub(edges.z), b = sub(edges.w);
    if (rr <= l || b <= t) return 0;
    const int col = max(0, min(rr, (px.x + 1) * 256) - max(l, px.x * 256));
    const int row = max(0, min(b, (px.y + 1) * 256) - max(t, px.y * 256));
    return min(255u, (uint(col) * uint(row) + 128) >> 8);
}

// An image's texel as bytes; coordinates clamped to its edges.
uint4 texel(int2 t, float2 size) {
    const int2 n = int2(size);
    t = clamp(t, int2(0, 0), n - 1);
    return bytes(image.SampleLevel((float2(t) + 0.5) / size, 0));
}

// raster's Shader::image_at: nearest, or bilinear in 1/256 weights.
uint4 image_at(float2 q, float2 size, bool bilinear) {
    if (!bilinear) return texel(int2(floor(q)), size);
    const float2 f = q - 0.5;
    const float2 f0 = floor(f);
    const int2 i0 = int2(f0);
    const uint wx = min(256u, uint(int((f.x - f0.x) * 256.0 + 0.5))), wy = min(256u, uint(int((f.y - f0.y) * 256.0 + 0.5)));
    const uint4 a = texel(i0, size), b = texel(i0 + int2(1, 0), size), c = texel(i0 + int2(0, 1), size), d = texel(i0 + int2(1, 1), size);
    const uint4 top = a * (256 - wx) + b * wx, bottom = c * (256 - wx) + d * wx;
    return (top * (256 - wy) + bottom * wy + 32768) >> 16;
}

float2 inverse_at(VOut i, float2 p) {
    precise const float qx = i.pinv0.x * p.x + i.pinv0.z * p.y + i.pinv1.x;
    precise const float qy = i.pinv0.y * p.x + i.pinv0.w * p.y + i.pinv1.y;
    return float2(qx, qy);
}

// The paint's pixel at p (the pixel's centre) as bytes.
uint4 paint_at(VOut i, float2 p, int flags) {
    const int kind = int(i.paint.x);
    if (kind == 0) return bytes(i.color);
    const float2 q = inverse_at(i, p);
    if (kind == 4) return mul255(image_at(q, i.pinv1.zw, (flags & FLAG_LINEAR) != 0), uint(i.color.w));
    if (kind == 5) return mul255(bytes(i.color), image_at(q, i.pinv1.zw, (flags & FLAG_LINEAR) != 0).a);  // an image as a mask of the colour
    float t = 0;
    if (kind == 1) {
        precise const float dx = i.pgeo.z - i.pgeo.x, dy = i.pgeo.w - i.pgeo.y;
        precise const float len2 = dx * dx + dy * dy;
        precise const float along = (q.x - i.pgeo.x) * dx + (q.y - i.pgeo.y) * dy;
        t = len2 > 0 ? along / len2 : 0;
    } else if (kind == 2) {
        precise const float dx = q.x - i.pgeo.x, dy = q.y - i.pgeo.y;
        precise const float d2 = dx * dx + dy * dy;
        t = i.pgeo.z > 0 ? sqrt(d2) / i.pgeo.z : 1;
    } else {
        const float turn = (atan2(q.y - i.pgeo.y, q.x - i.pgeo.x) - i.paint.w) / 6.283185307179586;
        t = turn - floor(turn);
    }
    t = spread_t(t, int(i.paint.z));
    const float index = float(int(t * 255.0 + 0.5));
    return bytes(ramps.SampleLevel(float2((index + 0.5) / 256.0, (i.paint.y + 0.5) / fframe.ramps.x), 0));
}

// The coverage of the pixel as a byte, as the CPU has it (the clip's included).
uint coverage_at(VOut i, float2 p, int2 px, int kind, int flags) {
    uint k;
    if ((flags & FLAG_LAYER) != 0) {
        k = uint(i.shape.z);
    } else if (kind == KIND_MASK) {
        const float2 at = i.uv.xy + (p - i.rect.xy);
        k = bytes(atlas.SampleLevel(at / fframe.size.zw, 0)).r;
    } else if ((flags & FLAG_AXIS_ALIGNED) != 0 && (kind == KIND_RECT || kind == KIND_IMAGE)) {
        k = rect_coverage(px, i.radii);
    } else {
        const float2 half = i.rect.zw * 0.5;
        const float2 centred = i.local - (i.rect.xy + half);
        // A stroke is its outer rrect less its inner one: the outline moved in
        // by the width, each radius by as much (none below zero) — the area
        // between, exactly, where the CPU strokes the rrect's path.
        const bool ring = kind == KIND_RRING;
        const float w = i.shape.x;
        const float2 inner = i.rect.zw - 2 * w;
        const bool hollow = ring && inner.x > 0 && inner.y > 0;
        const float4 inner_radii = max(i.radii - w, 0.0);
        float f;
        if ((flags & FLAG_AXIS_ALIGNED) != 0) {
            // The pixel into local space (an axis-aligned transform keeps it a rectangle).
            const float2 a = (p - 0.5 - i.m1.xy) / float2(i.m0.x, i.m0.w), b = (p + 0.5 - i.m1.xy) / float2(i.m0.x, i.m0.w);
            f = exact_rounded(min(a, b), max(a, b), i.rect.xy, i.rect.zw, i.radii);
            if (hollow) f -= exact_rounded(min(a, b), max(a, b), i.rect.xy + w, inner, inner_radii);
        } else {
            f = rounded_coverage(centred, half, kind == KIND_RRECT || ring ? i.radii : float4(0, 0, 0, 0), i.m0);
            if (hollow) f -= rounded_coverage(centred, inner * 0.5, inner_radii, i.m0);
        }
        k = coverage_byte(f);
    }
    // The clip: its rectangle covers by rect_coverage (a pixel-aligned one is
    // all or nothing), its mask by its bytes.
    const uint clip = rect_coverage(px, i.clip);
    if (clip != 255) k = mul255(k, clip);
    if ((flags & FLAG_CLIP_MASK) != 0) k = mul255(k, bytes(clip_mask.SampleLevel(p / fframe.size.xy, 0)).r);
    return k;
}

[shader("fragment")]
float4 ui_fs(VOut i) : SV_Target {
    const float2 p = i.pos.xy;  // the pixel's centre, device space
    const int2 px = int2(p);
    const int kind = int(i.m1.z);
    const int flags = int(i.m1.w);
    const uint k = coverage_at(i, p, px, kind, flags);
    if (k == 0) discard;
    uint4 s = (flags & FLAG_LAYER) != 0 ? texel(px, i.pinv1.zw) : paint_at(i, p, flags);
    s = mul255(s, k);
    s = mul255(s, uint(i.shape.y));
    if (all(s == 0)) discard;
    return float4(s) / 255.0;
}

// ── a lit image (render's Material; raster's Shader::lit) ──────────────
//
// The same draw with the image's normal map beside it and the material's
// constants (raster's MaterialSetup, made by the CPU so both shade with the
// same numbers); the instance's pgeo is its rotation into device space.
// Only image and image-mask paints are drawn with it.

struct Lit {
    float4 light;   // toward the light, unit: x, y, z; bands
    float4 face;    // its direction in the screen's plane, unit: x, y; 1 / softness (0: hard), rim width
    float4 shadow;  // r, g, b, strength
    float4 rim;     // r, g, b (premultiplied by its alpha), strength
    float4 nsize;   // the normal map's width, height; 1 / bands, 1 / (bands − 1)
    float4 key;     // the light's colour: r, g, b (white: as painted)
};
[[vk::binding(4, 2)]] Sampler2D normals : register(t4, space2) : register(s4, space2);
[[vk::binding(1, 3)]] ConstantBuffer<Lit> lit : register(b1, space3);

uint4 normal_texel(int2 t, float2 size) {
    const int2 n = int2(size);
    t = clamp(t, int2(0, 0), n - 1);
    return bytes(normals.SampleLevel((float2(t) + 0.5) / size, 0));
}

uint4 normal_at(float2 q, float2 size, bool bilinear) {
    if (!bilinear) return normal_texel(int2(floor(q)), size);
    const float2 f = q - 0.5;
    const float2 f0 = floor(f);
    const int2 i0 = int2(f0);
    const uint wx = min(256u, uint(int((f.x - f0.x) * 256.0 + 0.5))), wy = min(256u, uint(int((f.y - f0.y) * 256.0 + 0.5)));
    const uint4 a = normal_texel(i0, size), b = normal_texel(i0 + int2(1, 0), size), c = normal_texel(i0 + int2(0, 1), size),
                d = normal_texel(i0 + int2(1, 1), size);
    const uint4 top = a * (256 - wx) + b * wx, bottom = c * (256 - wx) + d * wx;
    return (top * (256 - wy) + bottom * wy + 32768) >> 16;
}

uint lit_channel(uint v, float k, float rc, float rim, float a) {
    precise const float o = clamp(float(v) * k + rc * rim * a, 0.0, a);
    return uint(int(o + 0.5));
}

// raster's Shader::lit, step by step.
uint4 shade_lit(uint4 c, uint4 n, float4 r) {
    if (c.a == 0) return c;
    const float unit = 1.0 / 127.0;
    precise const float nx = (float(n.r) - 128.0) * unit;
    precise const float ny = (float(n.g) - 128.0) * unit;
    precise const float nz = (float(n.b) - 128.0) * unit;
    precise float tx = r.x * nx + r.y * ny;
    precise float ty = r.z * nx + r.w * ny;
    precise float tz = nz;
    precise const float len2 = tx * tx + ty * ty + tz * tz;
    if (len2 > 0) {
        precise const float inv = 1.0 / sqrt(len2);
        tx = tx * inv;
        ty = ty * inv;
        tz = tz * inv;
    } else {
        tz = 1;
    }
    precise const float lambert = max(tx * lit.light.x + ty * lit.light.y + tz * lit.light.z, 0.0);
    precise float light = lambert;
    const int bands = int(lit.light.w);
    const float inv_soft = lit.face.z;
    if (bands >= 2) {
        precise float sum = 0;
        for (int b = 1; b < bands; ++b) {
            precise const float t = float(b) * lit.nsize.z;
            sum += inv_soft > 0 ? saturate((lambert - t) * inv_soft + 0.5) : (lambert >= t ? 1.0 : 0.0);
        }
        light = sum * lit.nsize.w;
    }
    precise const float shade = (1.0 - light) * lit.shadow.w;
    precise const float kr = (1.0 - shade) * lit.key.x + shade * lit.shadow.x;
    precise const float kg = (1.0 - shade) * lit.key.y + shade * lit.shadow.y;
    precise const float kb = (1.0 - shade) * lit.key.z + shade * lit.shadow.z;
    precise float rim = 0;
    const float width = lit.face.w;
    if (lit.rim.w > 0 && width > 0) {
        precise const float past = (1.0 - max(tz, 0.0)) - (1.0 - width);
        precise const float e = inv_soft > 0 ? saturate(past * inv_soft + 0.5) : (past >= 0 ? 1.0 : 0.0);
        rim = lit.rim.w * e * max(tx * lit.face.x + ty * lit.face.y, 0.0);
    }
    const float a = float(c.a);
    return uint4(lit_channel(c.r, kr, lit.rim.x, rim, a), lit_channel(c.g, kg, lit.rim.y, rim, a), lit_channel(c.b, kb, lit.rim.z, rim, a), c.a);
}

// raster's Shader::shaded_at: the texel (as a mask, the colour by its
// alpha), lit by its normal, then the tint's byte.
uint4 lit_paint_at(VOut i, float2 p, int flags) {
    if (int(i.paint.x) < 4) return paint_at(i, p, flags);  // only images are lit (the renderer draws nothing else with this shader)
    const float2 q = inverse_at(i, p);
    const bool bilinear = (flags & FLAG_LINEAR) != 0;
    uint4 c = image_at(q, i.pinv1.zw, bilinear);
    uint k = uint(i.color.w);
    if (int(i.paint.x) == 5) {
        c = mul255(bytes(i.color), c.a);
        k = 255;
    }
    const float2 qn = all(lit.nsize.xy == i.pinv1.zw) ? q : float2(q.x * (lit.nsize.x / i.pinv1.z), q.y * (lit.nsize.y / i.pinv1.w));
    c = shade_lit(c, normal_at(qn, lit.nsize.xy, bilinear), i.pgeo);
    return mul255(c, k);
}

[shader("fragment")]
float4 ui_lit_fs(VOut i) : SV_Target {
    const float2 p = i.pos.xy;
    const int2 px = int2(p);
    const int kind = int(i.m1.z);
    const int flags = int(i.m1.w);
    const uint k = coverage_at(i, p, px, kind, flags);
    if (k == 0) discard;
    uint4 s = lit_paint_at(i, p, flags);
    s = mul255(s, k);
    s = mul255(s, uint(i.shape.y));
    if (all(s == 0)) discard;
    return float4(s) / 255.0;
}
