// A textured, tinted quad: one vertex buffer, a sampled texture, uniforms in
// both stages — rhi's convention for each.
struct VIn { float2 pos : TEXCOORD0; float2 uv : TEXCOORD1; };
struct VOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

struct Transform { float4 scale_offset; };
[[vk::binding(0, 1)]] ConstantBuffer<Transform> transform : register(b0, space1);

[shader("vertex")]
VOut quad_vs(VIn i) {
    VOut o;
    o.pos = float4(i.pos * transform.scale_offset.xy + transform.scale_offset.zw, 0, 1);
    o.uv = i.uv;
    return o;
}

struct Tint { float4 color; };
[[vk::binding(0, 2)]] Sampler2D image : register(t0, space2) : register(s0, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Tint> tint : register(b0, space3);

[shader("fragment")]
float4 quad_fs(VOut i) : SV_Target { return image.Sample(i.uv) * tint.color; }
