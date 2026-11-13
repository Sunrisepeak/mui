// The sun's view of the scene, for shadows: each caster's corners through the light's own projection, and for
// each texel how far from the light the nearest thing is (0 … 1 across the map's depth), in a float target the
// mesh pass then reads.
struct VIn {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
};
// What a corner carries to its texel, the same for both views (a file's vertex entries link with its fragment
// entries on the Web: one shape between the stages): xyz where it is in the world (a lamp's view asks), w how
// far from the light's plane (the sun's).
struct VOut {
    float4 clip : SV_Position;
    float4 seen : TEXCOORD0;
};
struct Draw {
    float4 to_clip[4];   // rows: the corner to the light's clip space
    float4 to_world[3];  // rows (a lamp's view asks where a point is)
    float4 normals[3];
};
[[vk::binding(0, 1)]] ConstantBuffer<Draw> draw : register(b0, space1);

[shader("vertex")]
VOut shadow_vs(VIn corner) {
    const float4 at = float4(corner.position, 1);
    VOut made;
    made.clip = float4(dot(draw.to_clip[0], at), dot(draw.to_clip[1], at), dot(draw.to_clip[2], at), dot(draw.to_clip[3], at));
    made.seen = float4(0, 0, 0, made.clip.z);
    return made;
}

[shader("fragment")]
float4 shadow_fs(VOut point) : SV_Target { return float4(point.seen.w, 0, 0, 1); }

// A lamp's or a spot's view, one of six faces about it: the same corners through the face's projection, and for
// each texel how far from the lamp the nearest thing is, as a share of the lamp's reach.
struct LampAt {
    float4 at_range;  // where the lamp is; w: how far it reaches
};
[[vk::binding(0, 3)]] ConstantBuffer<LampAt> lamp : register(b0, space3);

[shader("vertex")]
VOut lamp_vs(VIn corner) {
    const float4 at = float4(corner.position, 1);
    VOut made;
    made.clip = float4(dot(draw.to_clip[0], at), dot(draw.to_clip[1], at), dot(draw.to_clip[2], at), dot(draw.to_clip[3], at));
    made.seen = float4(dot(draw.to_world[0], at), dot(draw.to_world[1], at), dot(draw.to_world[2], at), 0);
    return made;
}

[shader("fragment")]
float4 lamp_fs(VOut point) : SV_Target { return float4(length(point.seen.xyz - lamp.at_range.xyz) / lamp.at_range.w, 0, 0, 1); }
