// A compute pass writing a storage texture and a storage buffer, its size
// from uniforms: a pattern every texel and element can be checked against.
// rgba8: the target's format, which WGSL declares.
[[vk::binding(0, 1)]] [format("rgba8")] RWTexture2D<float4> target : register(u0, space1);
[[vk::binding(1, 1)]] RWStructuredBuffer<uint> values : register(u1, space1);
struct Params { uint width; uint height; uint pad0; uint pad1; };
[[vk::binding(0, 2)]] ConstantBuffer<Params> params : register(b0, space2);

[shader("compute")]
[numthreads(8, 8, 1)]
void pattern(uint3 id : SV_DispatchThreadID) {
    if (id.x >= params.width || id.y >= params.height) return;
    target[id.xy] = float4(float(id.x * 16) / 255.0, float(id.y * 16) / 255.0, 0, 1);
    values[id.y * params.width + id.x] = id.x + id.y * 100;
}
