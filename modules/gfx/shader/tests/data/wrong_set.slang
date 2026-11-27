struct VOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
[[vk::binding(0, 0)]] Sampler2D image : register(t0, space0) : register(s0, space0);
[shader("fragment")]
float4 paint(VOut i) : SV_Target { return image.Sample(i.uv); }
