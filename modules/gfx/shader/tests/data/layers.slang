// Two sampled textures and a storage buffer in the fragment stage, a
// written storage texture in compute: what Slang's WGSL lays out over one
// another, and the bindings rhi.webgpu reads instead.
struct VOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
[[vk::binding(0, 2)]] Sampler2D base : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 2)]] Sampler2D over : register(t1, space2) : register(s1, space2);
[[vk::binding(2, 2)]] StructuredBuffer<float4> weights : register(t2, space2);
struct Mix { float4 tint; };
[[vk::binding(0, 3)]] ConstantBuffer<Mix> mix : register(b0, space3);

[shader("fragment")]
float4 layers_fs(VOut i) : SV_Target { return (base.Sample(i.uv) * weights[0] + over.Sample(i.uv) * weights[1]) * mix.tint; }

[[vk::binding(0, 1)]] [format("rgba8")] RWTexture2D<float4> written : register(u0, space1);
[[vk::binding(1, 1)]] [format("r32f")] RWTexture2D<float> summed : register(u1, space1);

[shader("compute")]
[numthreads(8, 8, 1)]
void layers_cs(uint3 id : SV_DispatchThreadID) {
    written[id.xy] = float4(1, 0, 0, 1);
    summed[id.xy] = summed[id.xy] + 1;
}
