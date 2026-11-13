// Ordinary UI drawing resources are independent of the material-field entry
// module. Geometry, sampling and lighting come from one resource-free module.
import vector_paint;

[[vk::binding(0, 1)]] ConstantBuffer<Frame> vframe : register(b0, space1);
[[vk::binding(0, 2)]] Sampler2D ramps : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 2)]] Sampler2D atlas : register(t1, space2) : register(s1, space2);
[[vk::binding(2, 2)]] Sampler2D image : register(t2, space2) : register(s2, space2);
[[vk::binding(3, 2)]] Sampler2D clip_mask : register(t3, space2) : register(s3, space2);
[[vk::binding(4, 2)]] Sampler2D outlines : register(t4, space2) : register(s4, space2);
[[vk::binding(5, 2)]] Sampler2D normals : register(t5, space2) : register(s5, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Frame> fframe : register(b0, space3);
[[vk::binding(1, 3)]] ConstantBuffer<Lit> lit : register(b1, space3);

// A primitive's coverage at a pixel: a glyph from its outline, the rest as vector_paint has them.
uint covered(VOut i, float2 p, int flags) {
    const int kind = int(i.m1.z);
    if (kind == KIND_GLYPH) return clipped(clip_mask, outlines, fframe, i, p, int2(p), flags, glyph_coverage(outlines, fframe, i, p));
    if (kind == KIND_SHADOW) return clipped(clip_mask, outlines, fframe, i, p, int2(p), flags, shadow_coverage(outlines, fframe, i, p));
    return coverage_at(atlas, clip_mask, outlines, fframe, i, p, int2(p), kind, flags);
}

[shader("vertex")]
VOut ui_vs(Prim i, uint vertex : SV_VertexID) { return vertex_at(i, vertex, vframe); }

// Glyphs kept on the device (D-188, a run of a display list its content's revision keeps): an instance is a
// glyph in the space its run was drawn in — its transform there, its outline, its colour — made once; a frame
// sends only where that space lies now (Kept) and draws them with the fragment stage every glyph has.
struct KeptGlyph {
    float4 across : TEXCOORD0;   // glyph → the run's space: a, b, c, d
    float4 origin : TEXCOORD1;   // e, f; the outline's first point, points
    float4 bounds : TEXCOORD2;   // the outline's bounds: x, y, w, h
    float4 color : TEXCOORD3;    // premultiplied bytes / 255
};
struct Kept {
    float4 across;  // the run's space → device: a, b, c, d
    float4 origin;  // e, f; flags (the clip's mask, its record above them); -
    float4 clip;    // the device clip rectangle
};
[[vk::binding(1, 1)]] ConstantBuffer<Kept> vkept : register(b1, space1);

[shader("vertex")]
VOut ui_kept_vs(KeptGlyph i, uint vertex : SV_VertexID) {
    const uint corners[6] = { 0, 1, 2, 1, 3, 2 };
    const uint c = corners[vertex % 6];
    const float2 t = float2(float(c & 1), float(c >> 1));
    // The glyph's transform to the device: the run's after the glyph's own.
    const float4 m = vkept.across;
    const float4 to = float4(m.x * i.across.x + m.z * i.across.y, m.y * i.across.x + m.w * i.across.y,
                             m.x * i.across.z + m.z * i.across.w, m.y * i.across.z + m.w * i.across.w);
    const float2 at = float2(m.x * i.origin.x + m.z * i.origin.y + vkept.origin.x, m.y * i.origin.x + m.w * i.origin.y + vkept.origin.y);
    // Its outline's bounds there, a pixel around them (as a glyph encoded a frame has them).
    const float2 p0 = apply(to, at, i.bounds.xy), p1 = apply(to, at, i.bounds.xy + float2(i.bounds.z, 0));
    const float2 p2 = apply(to, at, i.bounds.xy + float2(0, i.bounds.w)), p3 = apply(to, at, i.bounds.xy + i.bounds.zw);
    const float2 lo = floor(min(min(p0, p1), min(p2, p3))) - 1, hi = ceil(max(max(p0, p1), max(p2, p3))) + 1;
    VOut o;
    o.local = lo + t * (hi - lo);
    o.pos = float4(o.local.x / vframe.size.x * 2 - 1, 1 - o.local.y / vframe.size.y * 2, 0, 1);
    o.rect = float4(lo, hi - lo);
    o.m0 = float4(0, 0, 0, 0);
    o.m1 = float4(0, 0, KIND_GLYPH, vkept.origin.z);
    o.radii = to;
    o.shape = float4(0, 255, 0, 0);
    o.color = i.color;
    o.paint = float4(0, 0, 0, 0);
    o.pgeo = float4(0, 0, 0, 0);
    o.pinv0 = float4(0, 0, 0, 0);
    o.pinv1 = float4(0, 0, 0, 0);
    o.clip = vkept.clip;
    o.uv = float4(at, i.origin.zw);
    return o;
}

[shader("fragment")]
float4 ui_fs(VOut i) : SV_Target {
    const float2 p = i.pos.xy;
    const int flags = int(i.m1.w);
    const uint k = covered(i, p, flags);
    if (k == 0) discard;
    uint4 s = (flags & FLAG_LAYER) != 0 ? texel_from(image, int2(p), i.pinv1.zw) : paint_from(ramps, image, fframe.ramps.x, i, p, flags);
    s = mul255(s, k);
    s = mul255(s, uint(i.shape.y));
    if (all(s == 0)) discard;
    return float4(s) / 255.0;
}

[shader("fragment")]
float4 ui_lit_fs(VOut i) : SV_Target {
    const float2 p = i.pos.xy;
    const int flags = int(i.m1.w);
    const uint k = covered(i, p, flags);
    if (k == 0) discard;
    uint4 s = lit_paint_from(ramps, image, normals, fframe.ramps.x, lit, i, p, flags);
    s = mul255(s, k);
    s = mul255(s, uint(i.shape.y));
    if (all(s == 0)) discard;
    return float4(s) / 255.0;
}
