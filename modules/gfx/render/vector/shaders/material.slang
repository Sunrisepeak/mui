// Materials use the same pure paint implementation as UI drawing, but no
// atlas/clip resources. Its own dense layout is ramps0/image1/normal2.
import vector_paint;

[[vk::binding(0, 2)]] Sampler2D ramps : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 2)]] Sampler2D image : register(t1, space2) : register(s1, space2);
[[vk::binding(2, 2)]] Sampler2D normals : register(t2, space2) : register(s2, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Frame> fframe : register(b0, space3);
[[vk::binding(1, 3)]] ConstantBuffer<Lit> lit : register(b1, space3);

[shader("fragment")]
float4 ui_material_fs(VOut i) : SV_Target {
    return float4(paint_from(ramps, image, fframe.ramps.x, i, i.pos.xy, int(i.m1.w))) * (i.shape.w / 255.0);
}
[shader("fragment")]
float4 ui_material_lit_fs(VOut i) : SV_Target {
    return float4(lit_paint_from(ramps, image, normals, fframe.ramps.x, lit, i, i.pos.xy, int(i.m1.w))) * (i.shape.w / 255.0);
}
