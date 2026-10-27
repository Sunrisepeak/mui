struct VOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
[[vk::binding(0, 2)]] Texture2D image : register(t0, space2);
[[vk::binding(1, 2)]] SamplerState smp : register(s0, space2);
[shader("fragment")]
float4 paint(VOut i) : SV_Target { return image.Sample(smp, i.uv); }
