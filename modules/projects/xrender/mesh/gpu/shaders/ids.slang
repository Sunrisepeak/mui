// Who drew a pixel, on a device: the mesh pass's corners again, and for each pixel the number of its draw (its
// place in the frame's draws, and one more: 0 is "nothing"). The same faces as the colour pass — one turned
// away is not drawn.
struct VIn {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
};
struct VOut {
    float4 clip : SV_Position;
    float3 place : TEXCOORD0;
    float3 normal : TEXCOORD1;
};
struct Draw {
    float4 to_clip[4];
    float4 to_world[3];
    float4 normals[3];
};
[[vk::binding(0, 1)]] ConstantBuffer<Draw> draw : register(b0, space1);

[shader("vertex")]
VOut ids_vs(VIn corner) {
    const float4 at = float4(corner.position, 1);
    VOut made;
    made.clip = float4(dot(draw.to_clip[0], at), dot(draw.to_clip[1], at), dot(draw.to_clip[2], at), dot(draw.to_clip[3], at));
    made.place = float3(dot(draw.to_world[0], at), dot(draw.to_world[1], at), dot(draw.to_world[2], at));
    made.normal = float3(dot(draw.normals[0].xyz, corner.normal), dot(draw.normals[1].xyz, corner.normal), dot(draw.normals[2].xyz, corner.normal));
    return made;
}

struct Who {
    float4 eye;     // w: 1 or −1 — a pixel whose depth is past the half that way is beyond the view's far cut
    uint   number;  // the draw's place and one more
    uint   spare0, spare1, spare2;
};
[[vk::binding(0, 3)]] ConstantBuffer<Who> who : register(b0, space3);

[shader("fragment")]
uint ids_fs(VOut point) : SV_Target {
    if (dot(point.normal, who.eye.xyz - point.place) < 0) discard;
    if ((point.clip.z - 0.5) * who.eye.w > 0) discard;  // beyond the view's far cut
    return who.number;
}

// The same as a number a float holds exactly (below 2²⁴): what the resolve over time compares between frames,
// read as any other picture is.
[shader("fragment")]
float4 who_fs(VOut point) : SV_Target {
    if (dot(point.normal, who.eye.xyz - point.place) < 0) discard;
    if ((point.clip.z - 0.5) * who.eye.w > 0) discard;  // beyond the view's far cut
    return float4(float(who.number), 0, 0, 1);
}

// The surface at a pixel, in a picture of half floats: the normal folded onto a square (two numbers), and how
// far from the eye in two parts — eighths, which a half float holds exactly to 256, and what is left over,
// which it then holds to a few hundred-thousandths. Nothing there: 0 far.
[shader("fragment")]
float4 surface_fs(VOut point) : SV_Target {
    const float3 to_eye = who.eye.xyz - point.place;
    if (dot(point.normal, to_eye) < 0) discard;
    if ((point.clip.z - 0.5) * who.eye.w > 0) discard;  // beyond the view's far cut
    const float3 normal = normalize(point.normal);
    const float3 folded = normal / (abs(normal.x) + abs(normal.y) + abs(normal.z));
    float2 on_square = folded.xy;
    if (folded.z < 0) on_square = (1.0 - abs(folded.yx)) * float2(folded.x >= 0 ? 1 : -1, folded.y >= 0 ? 1 : -1);
    const float far = min(length(to_eye), 250.0);
    const float eighths = floor(far * 8.0) * 0.125;
    return float4(on_square, eighths, far - eighths);
}
