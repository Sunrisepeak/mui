// How far each pixel's point of the surface moved on the screen since the last frame (xrender.mesh's
// `Targets::velocity`): the corner through this frame's view and through the last frame's, and at each pixel
// the difference of the two places — the frame's jitter taken out, since it is not a movement.
struct VIn {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;  // not read here; declared so the inputs after it keep their places (Slang numbers them in order)
    float2 second : TEXCOORD4;  // the mesh's second set of texture coordinates
    float4 colour : TEXCOORD5;  // the mesh's own at the corner: its fourth is what it hides
    float3 was : TEXCOORD6;     // where the corner was a frame ago in the mesh (a bent mesh's last; else `position`)
};
struct VOut {
    float4 clip : SV_Position;
    float3 place : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 before : TEXCOORD2;  // where the corner was a frame ago, in that frame's clip space
    float4 uv : TEXCOORD3;  // by the mesh's first set of texture coordinates, and by its second
    float  hides : TEXCOORD4;  // the mesh's own colour's fourth
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
    const float4 was = float4(corner.was, 1);
    made.before = float4(dot(draw.before[0], was), dot(draw.before[1], was), dot(draw.before[2], was), dot(draw.before[3], was));
    made.uv = float4(corner.uv, corner.second), made.hides = corner.colour.a;
    return made;
}

// What these passes ask of the draw's material (xrender.mesh's `Material::cutout`, `opacity`, `two_sided`), the
// colour pass's own questions asked the same way: whether the surface is there at this pixel — its opacity, by
// its picture's cover over the strip the pixel covers of it, against the cut — and whether it is seen from behind.
struct Whole {
    float4 picture;  // the draw's base picture: xy its width and height; z how many sizes it has (0: none)
    float4 cut;      // x: where the surface hides less than this it is not there (0: whole); y: how much it hides;
                     // z: over 0.5 — it is seen from both sides
    float4 placing[2];  // how the picture is laid over the surface (xrender.mesh's `Placing`): its place across, and down, each of (u, v, 1)
                        // of a set; the first's w: 1 the mesh's second set
};
float cover_over(Sampler2D map, float4 picture, float2 uv, float2 right, float2 down) {
    const float across = length(right * picture.xy), along = length(down * picture.xy);
    const float2 lengthwise = across >= along ? right : down;
    const float longer = max(across, along), shorter = max(min(across, along), 1e-6);
    const float places = clamp(ceil(longer / shorter), 1.0, 8.0);
    const float footprint = longer / places;
    const int sizes = int(picture.z);
    int size = 0;
    float covers = max(footprint, 1.0);
    while (covers >= 2 && size + 1 < sizes) {
        covers *= 0.5;
        ++size;
    }
    const float level = float(size) + (size + 1 < sizes ? covers - 1.0 : 0.0);
    float sum = 0;
    for (float place = 0; place < places; place += 1) sum += map.SampleLevel(uv + lengthwise * ((place + 0.5) / places - 0.5), level).a;
    return sum / places;
}

struct Moved {
    float4 eye;          // w: 1 or −1 — a pixel whose depth is past the half that way is beyond the view's far cut
    float4 size_jitter;  // xy: the picture's width and height; zw: this frame's jitter, in pixels
    float4 still;        // x: over 0.5 — this thing and the view are where they were: exactly 0
    Whole  whole;
};
[[vk::binding(0, 2)]] Sampler2D picture_map : register(t0, space2) : register(s0, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Moved> moved : register(b0, space3);

// The mesh's texture coordinates as the material lays its pictures (xrender.mesh's `Placing`).
float2 placed(float4 both) {
    const float2 uv = moved.whole.placing[0].w > 0.5 ? both.zw : both.xy;
    return float2(dot(moved.whole.placing[0].xyz, float3(uv, 1)), dot(moved.whole.placing[1].xyz, float3(uv, 1)));
}

// True where nothing of the surface is drawn at this pixel: turned away (and of one side), beyond the view's far
// cut, or cut out.
bool left_out(VOut point, float2 right, float2 down) {
    if (moved.whole.cut.z < 0.5 && dot(point.normal, moved.eye.xyz - point.place) < 0) return true;
    if ((point.clip.z - 0.5) * moved.eye.w > 0) return true;
    if (moved.whole.cut.x > 0) {
        float hides = moved.whole.cut.y * point.hides;
        if (moved.whole.picture.z > 0.5) hides *= cover_over(picture_map, moved.whole.picture, placed(point.uv), right, down);
        if (hides < moved.whole.cut.x) return true;
    }
    return false;
}

[shader("fragment")]
float4 velocity_fs(VOut point) : SV_Target {
    const float2 right = ddx(placed(point.uv)), down = ddy(placed(point.uv));
    if (left_out(point, right, down)) discard;
    if (moved.still.x > 0.5 || !(point.before.w > 0)) return float4(0, 0, 0, 1);
    const float2 was = float2(point.before.x / point.before.w * 0.5 + 0.5, 0.5 - point.before.y / point.before.w * 0.5) * moved.size_jitter.xy;
    return float4(point.clip.xy - moved.size_jitter.zw - was, 0, 1);
}

// Both pictures of one drawing of the scene, where a pass has two colour targets: how far the pixel's point
// moved, and who drew it (`still.y`: the draw's instance and one more, a number a float holds exactly) —
// what velocity_fs and ids.slang's who_fs each write.
struct MovedAndWho {
    float4 velocity : SV_Target0;
    float4 who : SV_Target1;
};
[shader("fragment")]
MovedAndWho velocity_who_fs(VOut point) {
    const float2 right = ddx(placed(point.uv)), down = ddy(placed(point.uv));
    if (left_out(point, right, down)) discard;
    MovedAndWho made;
    made.who = float4(moved.still.y, 0, 0, 1);
    made.velocity = float4(0, 0, 0, 1);
    if (!(moved.still.x > 0.5 || !(point.before.w > 0))) {
        const float2 was = float2(point.before.x / point.before.w * 0.5 + 0.5, 0.5 - point.before.y / point.before.w * 0.5) * moved.size_jitter.xy;
        made.velocity = float4(point.clip.xy - moved.size_jitter.zw - was, 0, 1);
    }
    return made;
}
