// A compute shader with every buffer kind and a storage texture: what SDL_GPU
// binds on Metal is [[buffer]] uniforms, then read-only, then read-write
// storage buffers; [[texture]] read-only, then read-write storage textures.
[[vk::binding(0, 0)]] StructuredBuffer<uint> before : register(t0, space0);
[[vk::binding(1, 0)]] StructuredBuffer<uint> rules : register(t1, space0);
[[vk::binding(0, 1)]] [format("r32ui")] RWTexture2D<uint> marks : register(u0, space1);
[[vk::binding(1, 1)]] RWStructuredBuffer<uint> after : register(u1, space1);
struct Params { uint width; uint height; uint pad0; uint pad1; };
[[vk::binding(0, 2)]] ConstantBuffer<Params> params : register(b0, space2);

[shader("compute")]
[numthreads(8, 8, 1)]
void evolve(uint3 id : SV_DispatchThreadID) {
    if (id.x >= params.width || id.y >= params.height) return;
    const uint cell = id.y * params.width + id.x;
    after[cell] = before[cell] + rules[0];
    marks[id.xy] = after[cell];
}
