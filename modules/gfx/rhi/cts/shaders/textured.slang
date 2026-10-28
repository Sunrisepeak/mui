// A sampled texture times a uniform tint: uploads, samplers, uniforms.
struct VIn {
    float2 pos : TEXCOORD0;
    float2 uv : TEXCOORD1;
};
struct VOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VOut textured_vs(VIn i) {
    VOut o;
    o.pos = float4(i.pos, 0, 1);
    o.uv = i.uv;
    return o;
}

struct Tint { float4 color; };
[[vk::binding(0, 2)]] Sampler2D image : register(t0, space2) : register(s0, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Tint> tint : register(b0, space3);

[shader("fragment")]
float4 textured_fs(VOut i) : SV_Target { return image.Sample(i.uv) * tint.color; }
