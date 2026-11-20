// The sun's view of the scene, for shadows: each caster's corners through the light's own projection, and for
// each texel how far from the light the nearest thing is (0 … 1 across the map's depth), in a float target the
// mesh pass then reads.
struct VIn {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;  // not read here; declared so the inputs after it keep their places (Slang numbers them in order)
    float2 second : TEXCOORD4;  // the mesh's second set of texture coordinates
    float4 colour : TEXCOORD5;  // the mesh's own at the corner: its fourth is what it hides
};
// What a corner carries to its texel, the same for both views (a file's vertex entries link with its fragment
// entries on the Web: one shape between the stages): xyz where it is in the world (a lamp's view asks), w how
// far from the light's plane (the sun's).
struct VOut {
    float4 clip : SV_Position;
    float4 seen : TEXCOORD0;
    float4 uv : TEXCOORD1;  // by the mesh's first set of texture coordinates, and by its second
    float  hides : TEXCOORD2;  // the mesh's own colour's fourth
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
    made.uv = float4(corner.uv, corner.second), made.hides = corner.colour.a;
    return made;
}

// What casts: where the caster's material cuts its surface out (xrender.mesh's `Material::cutout`) it casts
// nothing — its opacity, by its picture's cover at the place between the four texels of its finest size, as the
// reference's ray asks it (`Texture::cover_at`).
struct Casting {
    float4 at_range;  // a lamp's view: where the lamp is; w: how far it reaches
    float4 picture;   // the caster's base picture: z over 0.5 — it has one
    float4 cut;       // x: where the surface hides less than this it is not there (0: whole); y: how much it hides
    float4 placing[2];  // how the picture is laid over the surface (xrender.mesh's `Placing`); the first's w: 1 the mesh's second set
};
[[vk::binding(0, 2)]] Sampler2D picture_map : register(t0, space2) : register(s0, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Casting> casting : register(b0, space3);

// The mesh's texture coordinates as the material lays its pictures (xrender.mesh's `Placing`).
float2 placed(float4 both) {
    const float2 uv = casting.placing[0].w > 0.5 ? both.zw : both.xy;
    return float2(dot(casting.placing[0].xyz, float3(uv, 1)), dot(casting.placing[1].xyz, float3(uv, 1)));
}

bool cut_out(float2 uv, float own) {
    if (!(casting.cut.x > 0)) return false;
    float hides = casting.cut.y * own;
    if (casting.picture.z > 0.5) hides *= picture_map.SampleLevel(uv, 0).a;
    return hides < casting.cut.x;
}

[shader("fragment")]
float4 shadow_fs(VOut point) : SV_Target {
    if (cut_out(placed(point.uv), point.hides)) discard;
    return float4(point.seen.w, 0, 0, 1);
}

// A lamp's or a spot's view, one of six faces about it: the same corners through the face's projection, and for
// each texel how far from the lamp the nearest thing is, as a share of the lamp's reach.
[shader("vertex")]
VOut lamp_vs(VIn corner) {
    const float4 at = float4(corner.position, 1);
    VOut made;
    made.clip = float4(dot(draw.to_clip[0], at), dot(draw.to_clip[1], at), dot(draw.to_clip[2], at), dot(draw.to_clip[3], at));
    made.seen = float4(dot(draw.to_world[0], at), dot(draw.to_world[1], at), dot(draw.to_world[2], at), 0);
    made.uv = float4(corner.uv, corner.second), made.hides = corner.colour.a;
    return made;
}

[shader("fragment")]
float4 lamp_fs(VOut point) : SV_Target {
    if (cut_out(placed(point.uv), point.hides)) discard;
    return float4(length(point.seen.xyz - casting.at_range.xyz) / casting.at_range.w, 0, 0, 1);
}
