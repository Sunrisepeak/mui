// Ordinary UI drawing resources are independent of the material-field entry
// module. Geometry, sampling and lighting come from one resource-free module.
import vector_paint;

[[vk::binding(0, 1)]] ConstantBuffer<Frame> vframe : register(b0, space1);
[[vk::binding(0, 2)]] Sampler2D ramps : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 2)]] Sampler2D atlas : register(t1, space2) : register(s1, space2);
[[vk::binding(2, 2)]] Sampler2D image : register(t2, space2) : register(s2, space2);
[[vk::binding(3, 2)]] Sampler2D clip_mask : register(t3, space2) : register(s3, space2);
[[vk::binding(4, 2)]] Sampler2D normals : register(t4, space2) : register(s4, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Frame> fframe : register(b0, space3);
[[vk::binding(1, 3)]] ConstantBuffer<Lit> lit : register(b1, space3);

[shader("vertex")]
VOut ui_vs(Prim i, uint vertex : SV_VertexID) { return vertex_at(i, vertex, vframe); }

[shader("fragment")]
float4 ui_fs(VOut i) : SV_Target {
    const float2 p = i.pos.xy;
    const int flags = int(i.m1.w);
    const uint k = coverage_at(atlas, clip_mask, fframe, i, p, int2(p), int(i.m1.z), flags);
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
    const uint k = coverage_at(atlas, clip_mask, fframe, i, p, int2(p), int(i.m1.z), flags);
    if (k == 0) discard;
    uint4 s = lit_paint_from(ramps, image, normals, fframe.ramps.x, lit, i, p, flags);
    s = mul255(s, k);
    s = mul255(s, uint(i.shape.y));
    if (all(s == 0)) discard;
    return float4(s) / 255.0;
}
