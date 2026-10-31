// A storage texture with no format: fine for SPIR-V, not for WGSL.
[[vk::binding(0, 1)]] RWTexture2D<float4> target : register(u0, space1);

[shader("compute")]
[numthreads(8, 8, 1)]
void fill_cs(uint3 id : SV_DispatchThreadID) { target[id.xy] = float4(1, 0, 0, 1); }
