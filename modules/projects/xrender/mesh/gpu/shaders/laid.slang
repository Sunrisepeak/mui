// The surfaces seen through, laid over the frame (xrender.mesh's `Material::see_through`): the layers' light
// summed — each already times what it hides and what the nearer ones let through — with what they hide
// together, drawn over the frame's own light by three corners that cover the picture: the target keeps of its own
// what is left over.
struct VIn {
    float3 position : TEXCOORD0;  // already in clip space
    float3 normal : TEXCOORD1;    // not used: the pass's one layout of corners
};
struct VOut {
    float4 clip : SV_Position;
};

[shader("vertex")]
VOut laid_vs(VIn corner) {
    VOut made;
    made.clip = float4(corner.position.xy, 0.5, 1);
    return made;
}

struct Laid {
    float4 size;  // zw: one over the frame's width and height
};
[[vk::binding(0, 2)]] Sampler2D gathered_map : register(t0, space2) : register(s0, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Laid> laid : register(b0, space3);

[shader("fragment")]
float4 laid_fs(VOut point) : SV_Target { return gathered_map.SampleLevel(point.clip.xy * laid.size.zw, 0); }
