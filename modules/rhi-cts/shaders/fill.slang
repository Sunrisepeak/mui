// Flat colour per vertex, moved by a per-instance offset: positions,
// orientation, instancing, indices, blending, scissor and viewport.
struct VIn {
    float2 pos : TEXCOORD0;     // NDC
    float4 color : TEXCOORD1;   // premultiplied
    float2 offset : TEXCOORD2;  // per instance
};
struct VOut {
    float4 pos : SV_Position;
    float4 color : TEXCOORD0;
};

[shader("vertex")]
VOut fill_vs(VIn i) {
    VOut o;
    o.pos = float4(i.pos + i.offset, 0, 1);
    o.color = i.color;
    return o;
}

[shader("fragment")]
float4 fill_fs(VOut i) : SV_Target { return i.color; }
