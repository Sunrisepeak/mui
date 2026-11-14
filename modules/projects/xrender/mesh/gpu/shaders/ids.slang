// Who drew a pixel, on a device: the mesh pass's corners again, and for each pixel the number of its draw (its
// place in the frame's draws, and one more: 0 is "nothing"). The same faces as the colour pass — one turned
// away is not drawn.
struct VIn {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};
struct VOut {
    float4 clip : SV_Position;
    float3 place : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
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
    made.uv = corner.uv;
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

struct Who {
    float4 eye;     // w: 1 or −1 — a pixel whose depth is past the half that way is beyond the view's far cut
    uint   number;  // the draw's place and one more
    uint   spare0, spare1, spare2;
    Whole  whole;
};
[[vk::binding(0, 2)]] Sampler2D picture_map : register(t0, space2) : register(s0, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Who> who : register(b0, space3);

// The mesh's texture coordinates as the material lays its pictures (xrender.mesh's `Placing`).
float2 placed(float2 uv) { return float2(dot(who.whole.placing[0].xyz, float3(uv, 1)), dot(who.whole.placing[1].xyz, float3(uv, 1))); }

// True where nothing of the surface is drawn at this pixel: turned away (and of one side), beyond the view's far
// cut, or cut out. `right` and `down`: how far its place in its picture moves to the next pixel across and below.
bool left_out(VOut point, float2 right, float2 down) {
    if (who.whole.cut.z < 0.5 && dot(point.normal, who.eye.xyz - point.place) < 0) return true;
    if ((point.clip.z - 0.5) * who.eye.w > 0) return true;
    if (who.whole.cut.x > 0) {
        float hides = who.whole.cut.y;
        if (who.whole.picture.z > 0.5) hides *= cover_over(picture_map, who.whole.picture, placed(point.uv), right, down);
        if (hides < who.whole.cut.x) return true;
    }
    return false;
}

[shader("fragment")]
uint ids_fs(VOut point) : SV_Target {
    const float2 right = ddx(placed(point.uv)), down = ddy(placed(point.uv));
    if (left_out(point, right, down)) discard;
    return who.number;
}

// The same as a number a float holds exactly (below 2²⁴): what the resolve over time compares between frames,
// read as any other picture is.
[shader("fragment")]
float4 who_fs(VOut point) : SV_Target {
    const float2 right = ddx(placed(point.uv)), down = ddy(placed(point.uv));
    if (left_out(point, right, down)) discard;
    return float4(float(who.number), 0, 0, 1);
}

// The surface at a pixel, in a picture of half floats: the normal folded onto a square (two numbers), and how
// far from the eye in two parts — eighths, which a half float holds exactly to 256, and what is left over,
// which it then holds to a few hundred-thousandths. Nothing there: 0 far.
[shader("fragment")]
float4 surface_fs(VOut point) : SV_Target {
    const float2 right = ddx(placed(point.uv)), down = ddy(placed(point.uv));
    const float3 to_eye = who.eye.xyz - point.place;
    if (left_out(point, right, down)) discard;
    float3 normal = normalize(point.normal);
    if (dot(normal, to_eye) < 0) normal = -normal;  // seen from behind (a surface of both sides): it faces the eye
    const float3 folded = normal / (abs(normal.x) + abs(normal.y) + abs(normal.z));
    float2 on_square = folded.xy;
    if (folded.z < 0) on_square = (1.0 - abs(folded.yx)) * float2(folded.x >= 0 ? 1 : -1, folded.y >= 0 ? 1 : -1);
    const float far = min(length(to_eye), 250.0);
    const float eighths = floor(far * 8.0) * 0.125;
    return float4(on_square, eighths, far - eighths);
}

// How deep what stops the eye is, as a picture a later pass reads (the depth target itself is not read): the
// pass's own depth at the pixel. Nothing there: what the picture was cleared to.
[shader("fragment")]
float4 deep_fs(VOut point) : SV_Target {
    const float2 right = ddx(placed(point.uv)), down = ddy(placed(point.uv));
    if (left_out(point, right, down)) discard;
    return float4(point.clip.z, 0, 0, 1);
}
