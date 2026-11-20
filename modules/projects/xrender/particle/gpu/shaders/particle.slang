// Particles over a frame's light (xrender.particle's reference on a device): each a square facing the eye, its
// corners in the eye's space (laid out on the CPU in the reference's order, the farthest first), its shape a
// round spot — or the batch's picture — read at the pixel's place on the square, faded by how near it is — along the eye's ray — to the
// surface behind it (the mesh pass's `record_surface`: how far from the eye, 0 where nothing is).
struct VIn {
    float3 eye : TEXCOORD0;     // the corner in the eye's space
    float2 square : TEXCOORD1;  // its place on the square: −1 … 1 each way, u right, v up
    float4 colour : TEXCOORD2;  // the particle's light; w: how much it hides
    float footprint : TEXCOORD3;  // a picture's texels a pixel covers (the reference's, a particle's)
};
struct VOut {
    float4 clip : SV_Position;
    float3 eye : TEXCOORD0;
    float2 square : TEXCOORD1;
    float4 colour : TEXCOORD2;
    float footprint : TEXCOORD3;
};
struct View {
    float4 projection[4];  // rows
};
[[vk::binding(0, 1)]] ConstantBuffer<View> view : register(b0, space1);

[shader("vertex")]
VOut particle_vs(VIn corner) {
    const float4 at = float4(corner.eye, 1);
    VOut made;
    made.clip = float4(dot(view.projection[0], at), dot(view.projection[1], at), dot(view.projection[2], at), dot(view.projection[3], at));
    made.eye = corner.eye, made.square = corner.square, made.colour = corner.colour, made.footprint = corner.footprint;
    return made;
}

struct Batch {
    float4 frame;    // its width and height in pixels; how near a surface it fades from (`soft`); 1: laid over, 0: added
    float4 picture;  // 1: the batch has a picture; its sizes; —; —
};
[[vk::binding(0, 2)]] Sampler2D surface_map : register(t0, space2) : register(s0, space2);
// Light times cover, and cover; a level a size (`xrender.mesh.gpu`'s `picture_on`).
[[vk::binding(1, 2)]] Sampler2D picture_map : register(t1, space2) : register(s1, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Batch> batch : register(b0, space3);

// The reference's round spot: whole to half its radius, then smoothly down to nothing at its edge.
float spot(float2 at) {
    const float radius = sqrt(at.x * at.x + at.y * at.y);
    if (radius >= 1) return 0;
    if (radius <= 0.5) return 1;
    const float edge = (radius - 0.5) * 2;
    return 1 - edge * edge * (3 - 2 * edge);
}

// The reference's size for a footprint (`Texture::seen_at`): the size whose texels are a pixel across or just
// under, and toward the next by how far the footprint is past it — straight in the footprint, not in its log.
float level_of(float footprint, float sizes) {
    float covers = max(footprint, 1.0);
    float size = 0;
    while (covers >= 2 && size + 1 < sizes) covers *= 0.5, size += 1;
    return size + (size + 1 < sizes ? covers - 1 : 0);
}

[shader("fragment")]
float4 particle_fs(VOut point) : SV_Target {
    if (abs(point.square.x) > 1 || abs(point.square.y) > 1) discard;
    // What its shape gives (times its cover) and how much of it is there.
    float3 shape;
    float cover;
    if (batch.picture.x > 0.5) {
        const float4 seen = picture_map.SampleLevel(float2((point.square.x + 1) * 0.5, (1 - point.square.y) * 0.5), level_of(point.footprint, batch.picture.y));
        shape = seen.rgb, cover = seen.a;
    } else {
        cover = spot(point.square);
        shape = float3(cover, cover, cover);
    }
    if (!(cover > 0)) discard;
    // How far the surface behind is (`ids.slang`'s folded distance: eighths and what is left; 0: nothing there).
    const float4 surface = surface_map.SampleLevel(point.clip.xy / batch.frame.xy, 0);
    const float behind = surface.z + surface.w;
    float fade = 1;
    if (behind > 0) {
        const float gap = behind - length(point.eye);
        fade = batch.frame.z > 0 ? clamp(gap / batch.frame.z, 0.0, 1.0) : (gap > 0 ? 1.0 : 0.0);
    }
    if (!(fade > 0)) discard;
    const float3 given = point.colour.rgb * shape * fade;
    if (batch.frame.w > 0.5) {
        const float hides = clamp(point.colour.a * cover * fade, 0.0, 1.0);
        return float4(given * point.colour.a, hides);
    }
    return float4(given, 0);
}
