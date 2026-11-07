// A field's colours on the device (D-132): each cell's pixel by the first
// swatch whose tests it passes (pixel_of in xengine.simulation.cellular),
// written into an rgba8 texture a renderer draws as an external texture —
// the field never read back.
//
// The palette, as cellular::gpu::Painter encodes it: [count, fallback, then
// each swatch: pixel, tests, (channel, low, high) a test]. A pixel is
// premultiplied RGBA8, red in its lowest byte.
[[vk::binding(0, 0)]] StructuredBuffer<uint> cells : register(t0, space0);
[[vk::binding(1, 0)]] StructuredBuffer<uint> palette : register(t1, space0);
[[vk::binding(0, 1)]] [format("rgba8")] RWTexture2D<float4> image : register(u0, space1);
struct Params { uint width; uint height; uint pad0; uint pad1; };
[[vk::binding(0, 2)]] ConstantBuffer<Params> params : register(b0, space2);

[shader("compute")]
[numthreads(8, 8, 1)]
void paint(uint3 id : SV_DispatchThreadID) {
    if (id.x >= params.width || id.y >= params.height) return;
    const uint area = params.width * params.height, cell = id.y * params.width + id.x;
    uint pixel = palette[1], at = 2;
    for (uint s = 0; s < palette[0]; ++s) {
        const uint swatch_pixel = palette[at], tests = palette[at + 1];
        bool all = true;
        for (uint t = 0; t < tests; ++t) {
            const uint value = cells[palette[at + 2 + 3 * t] * area + cell];
            if (value < palette[at + 3 + 3 * t] || value > palette[at + 4 + 3 * t]) { all = false; break; }
        }
        if (all) { pixel = swatch_pixel; break; }
        at += 2 + 3 * tests;
    }
    image[id.xy] = float4(float(pixel & 0xffu), float((pixel >> 8) & 0xffu), float((pixel >> 16) & 0xffu), float(pixel >> 24)) / 255.0;
}
