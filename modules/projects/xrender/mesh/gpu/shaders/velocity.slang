// How far each pixel's point of the surface moved on the screen since the last frame (xrender.mesh's
// `Targets::velocity`): the corner through this frame's view and through the last frame's, and at each pixel
// the difference of the two places — the frame's jitter taken out, since it is not a movement.
struct VIn {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
};
struct VOut {
    float4 clip : SV_Position;
    float3 place : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 before : TEXCOORD2;  // where the corner was a frame ago, in that frame's clip space
};
struct Draw {
    float4 to_clip[4];
    float4 to_world[3];
    float4 normals[3];
    float4 before[4];  // rows: the corner to the last frame's clip space (no jitter)
};
[[vk::binding(0, 1)]] ConstantBuffer<Draw> draw : register(b0, space1);

[shader("vertex")]
VOut velocity_vs(VIn corner) {
    const float4 at = float4(corner.position, 1);
    VOut made;
    made.clip = float4(dot(draw.to_clip[0], at), dot(draw.to_clip[1], at), dot(draw.to_clip[2], at), dot(draw.to_clip[3], at));
    made.place = float3(dot(draw.to_world[0], at), dot(draw.to_world[1], at), dot(draw.to_world[2], at));
    made.normal = float3(dot(draw.normals[0].xyz, corner.normal), dot(draw.normals[1].xyz, corner.normal), dot(draw.normals[2].xyz, corner.normal));
    made.before = float4(dot(draw.before[0], at), dot(draw.before[1], at), dot(draw.before[2], at), dot(draw.before[3], at));
    return made;
}

struct Moved {
    float4 eye;
    float4 size_jitter;  // xy: the picture's width and height; zw: this frame's jitter, in pixels
    float4 still;        // x: over 0.5 — this thing and the view are where they were: exactly 0
};
[[vk::binding(0, 3)]] ConstantBuffer<Moved> moved : register(b0, space3);

[shader("fragment")]
float4 velocity_fs(VOut point) : SV_Target {
    if (dot(point.normal, moved.eye.xyz - point.place) < 0) discard;
    if (moved.still.x > 0.5 || !(point.before.w > 0)) return float4(0, 0, 0, 1);
    const float2 was = float2(point.before.x / point.before.w * 0.5 + 0.5, 0.5 - point.before.y / point.before.w * 0.5) * moved.size_jitter.xy;
    return float4(point.clip.xy - moved.size_jitter.zw - was, 0, 1);
}
