// pixelate: square blocks of their average colour. One invocation a block
// (derived(0): its size; derived(1, 2): the grid's origin, the layer's
// floored corner) sums its pixels in integers and writes the average to each.
[[vk::binding(0, 0)]] Sampler2D source : register(t0, space0) : register(s0, space0);
[[vk::binding(0, 1)]] RWTexture2D<float4> result : register(u0, space1);
struct Grid {
    int4 size;   // surface width, height; block size; -
    int4 first;  // the first block's corner (≤ 0): x, y; blocks across, down
};
[[vk::binding(0, 2)]] ConstantBuffer<Grid> grid : register(b0, space2);

[shader("compute")]
[numthreads(8, 8, 1)]
void pixelate_cs(uint3 id : SV_DispatchThreadID) {
    if (int(id.x) >= grid.first.z || int(id.y) >= grid.first.w) return;
    const int size = grid.size.z;
    const int bx = grid.first.x + int(id.x) * size, by = grid.first.y + int(id.y) * size;
    const int x0 = max(bx, 0), y0 = max(by, 0), x1 = min(bx + size, grid.size.x), y1 = min(by + size, grid.size.y);
    if (x1 <= x0 || y1 <= y0) return;
    uint4 sum = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            sum += uint4(round(saturate(source.SampleLevel((float2(x, y) + 0.5) / float2(grid.size.xy), 0)) * 255.0));
    const uint count = uint((x1 - x0) * (y1 - y0));
    const float4 avg = float4((sum + count / 2) / count) / 255.0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) result[int2(x, y)] = avg;
}
