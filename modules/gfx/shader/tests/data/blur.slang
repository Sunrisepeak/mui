// A compute pass: a sampled input, a written output, uniforms.
[[vk::binding(0, 0)]] Sampler2D source : register(t0, space0) : register(s0, space0);
[[vk::binding(0, 1)]] RWTexture2D<float4> target : register(u0, space1);
struct Params { float2 texel; float radius; float pad; };
[[vk::binding(0, 2)]] ConstantBuffer<Params> params : register(b0, space2);

[shader("compute")]
[numthreads(8, 8, 1)]
void blur(uint3 id : SV_DispatchThreadID) {
    float2 uv = (float2(id.xy) + 0.5) * params.texel;
    float4 sum = 0;
    for (int d = -2; d <= 2; ++d) sum += source.SampleLevel(uv + float2(d, 0) * params.texel * params.radius, 0);
    target[id.xy] = sum / 5;
}
