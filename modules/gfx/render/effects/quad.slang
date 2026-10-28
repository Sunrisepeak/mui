// The vertex shader of every per-pixel effect (the effect template's): one
// triangle over the whole surface, so each of its pixels runs the effect's
// fragment shader once.
struct EffectPixel {
    float4 pos : SV_Position;
};

[shader("vertex")]
EffectPixel quad_vs(uint vertex : SV_VertexID) {
    const float2 corner = float2(float((vertex << 1) & 2), float(vertex & 2));  // (0,0) (2,0) (0,2)
    EffectPixel o;
    o.pos = float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
