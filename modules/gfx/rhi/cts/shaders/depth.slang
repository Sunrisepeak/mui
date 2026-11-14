// Flat colour per vertex at a depth of its own: the depth test, depths kept
// from one pass for the next.
struct VIn {
    float3 pos : TEXCOORD0;     // NDC, z the depth (0 near, 1 far)
    float4 color : TEXCOORD1;   // premultiplied
};
struct VOut {
    float4 pos : SV_Position;
    float4 color : TEXCOORD0;
};

[shader("vertex")]
VOut depth_vs(VIn i) {
    VOut o;
    o.pos = float4(i.pos, 1);
    o.color = i.color;
    return o;
}

[shader("fragment")]
float4 depth_fs(VOut i) : SV_Target { return i.color; }
