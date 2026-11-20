struct VOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
[[vk::binding(0, 2)]] Sampler2D a : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 2)]] Sampler2D b : register(t5, space2) : register(s5, space2);
[shader("fragment")]
float4 paint(VOut i) : SV_Target { return a.Sample(i.uv) + b.Sample(i.uv); }
