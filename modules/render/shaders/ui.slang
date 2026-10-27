// The GPU renderer's one shader (openmui.render:gpu): every primitive of a
// display list is an instance of a quad, its kind saying how coverage is
// found; the paint says the colour. What the CPU reference computes exactly
// the GPU computes the same way (an axis-aligned rectangle's coverage is the
// area of the pixel inside it; a gradient is looked up in the same 256-entry
// table at the same index; paths, strokes, glyphs and shadows are coverage
// the CPU rasteriser produced, in an atlas). Rounded rectangles and
// transformed rectangles use signed distances, within the renderer's
// tolerance.
//
// Device space: pixels, y down, (0, 0) the target's top-left corner.

// What an instance is (one per primitive; slot 0, per instance).
struct Prim {
    float4 rect : TEXCOORD0;     // kind rect/rrect/image: local x, y, w, h; mask: device x, y, w, h
    float4 m0 : TEXCOORD1;       // local → device: a, b, c, d
    float4 m1 : TEXCOORD2;       // e, f, kind, flags
    float4 radii : TEXCOORD3;    // top-left, top-right, bottom-right, bottom-left
    float4 shape : TEXCOORD4;    // -, opacity (an image's; a mask's coverage scale), -, -
    float4 color : TEXCOORD5;    // premultiplied solid colour (the tint of an image paint)
    float4 paint : TEXCOORD6;    // paint kind, ramp row, spread, conic start angle
    float4 pgeo : TEXCOORD7;     // gradient start.xy, end.xy (radial: end.x is the radius)
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

static const int FLAG_AXIS_ALIGNED = 1;   // the transform maps the rectangle to an axis-aligned one
static const int FLAG_CLIP_MASK = 2;      // multiply by the clip mask
static const int FLAG_LINEAR = 4;         // image paint / image: bilinear

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

float4 paint_at(VOut i, float2 p) {
    const int kind = int(i.paint.x);
    if (kind == 0) return i.color;
    const float2 q = apply(i.pinv0, i.pinv1.xy, p);
    if (kind == 4) {  // image: q in image pixels
        const float2 size = i.pinv1.zw;
        const float4 c = image.SampleLevel((q) / size, 0);
        return c * i.color.a;
    }
    float t = 0;
    if (kind == 1) {
        const float2 d = i.pgeo.zw - i.pgeo.xy;
        const float len2 = dot(d, d);
        t = len2 > 0 ? dot(q - i.pgeo.xy, d) / len2 : 0;
    } else if (kind == 2) {
        t = i.pgeo.z > 0 ? length(q - i.pgeo.xy) / i.pgeo.z : 1;
    } else {
        const float turn = (atan2(q.y - i.pgeo.y, q.x - i.pgeo.x) - i.paint.w) / 6.283185307179586;
        t = turn - floor(turn);
    }
    t = spread_t(t, int(i.paint.z));
    const float index = floor(t * 255.0 + 0.5);
    return ramps.SampleLevel(float2((index + 0.5) / 256.0, (i.paint.y + 0.5) / fframe.ramps.x), 0);
}

[shader("fragment")]
float4 ui_fs(VOut i) : SV_Target {
    const float2 p = i.pos.xy;  // the pixel's centre, device space
    const int kind = int(i.m1.z);
    const int flags = int(i.m1.w);
    float coverage;
    float4 colour;
    if (kind == KIND_MASK) {
        const float2 at = i.uv.xy + (p - i.rect.xy);
        coverage = atlas.SampleLevel(at / fframe.size.zw, 0).r * i.shape.y;
        colour = paint_at(i, p);
    } else {
        const float2 half = i.rect.zw * 0.5;
        const float2 centred = i.local - (i.rect.xy + half);
        if ((flags & FLAG_AXIS_ALIGNED) != 0 && (kind == KIND_RECT || kind == KIND_IMAGE)) {
            const float2 a = apply(i.m0, i.m1.xy, i.rect.xy), b = apply(i.m0, i.m1.xy, i.rect.xy + i.rect.zw);
            coverage = box(p, min(a, b), max(a, b));
        } else if ((flags & FLAG_AXIS_ALIGNED) != 0) {
            // The pixel into local space (an axis-aligned transform keeps it a rectangle).
            const float2 a = (p - 0.5 - i.m1.xy) / float2(i.m0.x, i.m0.w), b = (p + 0.5 - i.m1.xy) / float2(i.m0.x, i.m0.w);
            coverage = exact_rounded(min(a, b), max(a, b), i.rect.xy, i.rect.zw, i.radii);
        } else {
            coverage = rounded_coverage(centred, half, kind == KIND_RRECT ? i.radii : float4(0, 0, 0, 0), i.m0);
        }
        if (kind == KIND_IMAGE) {
            // The local position over the destination maps to the source's pixels.
            const float2 s = i.uv.xy + (i.local - i.rect.xy) / i.rect.zw * i.uv.zw;
            colour = image.SampleLevel(s / i.pinv1.zw, 0) * i.shape.y;
        } else {
            colour = paint_at(i, p);
        }
    }
    coverage *= box(p, i.clip.xy, i.clip.zw);
    if ((flags & FLAG_CLIP_MASK) != 0) coverage *= clip_mask.SampleLevel(p / fframe.size.xy, 0).r;
    return colour * coverage;
}
