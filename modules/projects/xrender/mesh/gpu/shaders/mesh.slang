// The mesh pass: xrender.mesh's reference shading, on a device. A matt surface under suns, lamps and spots, a
// constant light from everywhere, what the surface gives off, fog worked out along the ray; the display's
// curve at the end. Matrices are given a row at a time and applied by dot products: no convention of rows and
// columns to agree on.
struct VIn {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};
struct VOut {
    float4 clip : SV_Position;
    float3 place : TEXCOORD0;   // in the world
    float3 normal : TEXCOORD1;  // in the world, not of length 1
    float2 uv : TEXCOORD2;      // the mesh's place in a picture
};
struct Draw {
    float4 to_clip[4];   // rows: the corner, in the mesh's own space, to clip space
    float4 to_world[3];  // rows
    float4 normals[3];   // rows: the inverse transposed
};
[[vk::binding(0, 1)]] ConstantBuffer<Draw> draw : register(b0, space1);

[shader("vertex")]
VOut mesh_vs(VIn corner) {
    const float4 at = float4(corner.position, 1);
    VOut made;
    made.clip = float4(dot(draw.to_clip[0], at), dot(draw.to_clip[1], at), dot(draw.to_clip[2], at), dot(draw.to_clip[3], at));
    made.place = float3(dot(draw.to_world[0], at), dot(draw.to_world[1], at), dot(draw.to_world[2], at));
    made.normal = float3(dot(draw.normals[0].xyz, corner.normal), dot(draw.normals[1].xyz, corner.normal), dot(draw.normals[2].xyz, corner.normal));
    made.uv = corner.uv;
    return made;
}

struct Lamp {
    float4 colour_kind;  // rgb times its intensity; w: 0 a sun, 1 a lamp, 2 a spot
    float4 at_range;     // where; w: how far it reaches
    float4 toward_cone;  // the way it shines (of length 1); w: the cone's cosine
    float4 shadow;       // x: its row of the lamps' map (−1: it has none)
};
struct Surface {
    float4 base;       // linear
    float4 emissive;
    float4 ambient;
    float4 eye;        // w: how many lamps
    float4 fog;        // rgb; w: its density (0: none)
    float4 fog_shape;  // x: the height the density is given at; y: how fast it thins above it
    float4 shine;        // x: roughness (0.04 … 1); y: metallic; z: 1 where the surface is matt (the plain path)
    float4 above;        // what is about the scene, above; w: 1 where there is an environment
    float4 level;
    float4 below;
    float4 to_light[3];  // rows: the world to the sun's map — x, y across it (−1 … 1), z how far (0 … 1)
    float4 shadow;       // x: which lamp the map is of (−1: none); y: how far along its normal a point is lifted
                         // before it is looked up; z: how much nearer the map must say for a shadow
    float4 lamp_shadow;  // x: a face's side of the lamps' map, in texels; zw: one over the map's width and height
    float4 picture;      // the draw's texture: xy its width and height; z how many sizes it is kept at (0: none)
    float4 picture_sheet; // xy: one over the width and height of the sheet its sizes are laid on
    Lamp   lamps[8];
};
[[vk::binding(0, 2)]] Sampler2D shadow_map : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 2)]] Sampler2D lamp_map : register(t1, space2) : register(s1, space2);
[[vk::binding(2, 2)]] Sampler2D picture_map : register(t2, space2) : register(s2, space2);
[[vk::binding(0, 3)]] ConstantBuffer<Surface> surface : register(b0, space3);

// The surface's own colour at this pixel: the material's, times its picture's light where it has one.
static float3 base_colour;

// A texture's light at `uv` in one of its sizes (xrender.mesh's `Texture::at`): between the four texels about
// the place, repeating. The sizes lie on one sheet: the finest at its corner, the others in a column beside it.
float3 picture_within(int size, float2 uv) {
    float2 extent = surface.picture.xy;
    float2 corner = float2(0, 0);
    for (int smaller = 0; smaller < size; ++smaller) {
        corner = smaller == 0 ? float2(surface.picture.x, 0) : corner + float2(0, extent.y);
        extent = max(floor(extent * 0.5), float2(1, 1));
    }
    const float2 at = (uv - floor(uv)) * extent - 0.5;
    const float2 first = floor(at), share = at - first;
    const float2 low = first - floor(first / extent) * extent, high = (first + 1) - floor((first + 1) / extent) * extent;
    const float2 sheet = surface.picture_sheet.xy;
    const float3 top = lerp(picture_map.SampleLevel((corner + float2(low.x, low.y) + 0.5) * sheet, 0).rgb, picture_map.SampleLevel((corner + float2(high.x, low.y) + 0.5) * sheet, 0).rgb, share.x);
    const float3 bottom = lerp(picture_map.SampleLevel((corner + float2(low.x, high.y) + 0.5) * sheet, 0).rgb, picture_map.SampleLevel((corner + float2(high.x, high.y) + 0.5) * sheet, 0).rgb, share.x);
    return lerp(top, bottom, share.y);
}
// The size whose texels are about a pixel across, mixed with the next.
float3 picture_at(float2 uv, float footprint) {
    const int sizes = int(surface.picture.z);
    int size = 0;
    float covers = max(footprint, 1.0);
    while (covers >= 2 && size + 1 < sizes) {
        covers *= 0.5;
        ++size;
    }
    const float toward_next = size + 1 < sizes ? covers - 1.0 : 0.0;
    const float3 here = picture_within(size, uv);
    return toward_next > 0 ? lerp(here, picture_within(size + 1, uv), toward_next) : here;
}

// Whether something stands between a point and a lamp, by the lamp's six views about itself (a row of the
// lamps' map, a face a tile): the point, lifted off its surface by a texel and a half as wide as texels are
// that far from the lamp, is further than what the face it lies in saw there.
bool lamp_hidden(Lamp lamp, float3 place, float3 normal, float apart) {
    const float side = surface.lamp_shadow.x;
    const float3 from = place + normal * (apart * 3.0 / side) - lamp.at_range.xyz;
    const float3 size = abs(from);
    float3 axis, right, up;
    float face;
    if (size.x >= size.y && size.x >= size.z) {
        face = from.x > 0 ? 0 : 1, axis = float3(from.x > 0 ? 1 : -1, 0, 0), right = float3(0, 0, 1), up = float3(0, 1, 0);
    } else if (size.y >= size.z) {
        face = from.y > 0 ? 2 : 3, axis = float3(0, from.y > 0 ? 1 : -1, 0), right = float3(1, 0, 0), up = float3(0, 0, 1);
    } else {
        face = from.z > 0 ? 4 : 5, axis = float3(0, 0, from.z > 0 ? 1 : -1), right = float3(1, 0, 0), up = float3(0, 1, 0);
    }
    const float deep = dot(from, axis);
    if (!(deep > 0)) return false;
    const float2 across = float2(dot(from, right), dot(from, up)) / deep;
    // Held inside the face's own tile: the texel beside it is another face's.
    const float2 in_tile = clamp(float2(across.x * 0.5 + 0.5, 0.5 - across.y * 0.5) * side, float2(0.5, 0.5), float2(side - 0.5, side - 0.5));
    const float seen = lamp_map.SampleLevel((float2(face, lamp.shadow.x) * side + in_tile) * surface.lamp_shadow.zw, 0).r;
    return seen < (length(from) - apart * 2.0 / side) / lamp.at_range.w;
}

// Whether something stands between a point and the sun, by the sun's map: the point, lifted a little off its
// surface, is further from the light than what the map saw there. Off the map: lit.
bool in_shadow(float3 place, float3 normal) {
    const float4 lifted = float4(place + normal * surface.shadow.y, 1);
    const float3 there = float3(dot(surface.to_light[0], lifted), dot(surface.to_light[1], lifted), dot(surface.to_light[2], lifted));
    if (abs(there.x) >= 1 || abs(there.y) >= 1 || there.z >= 1) return false;
    const float seen = shadow_map.SampleLevel(float2(there.x * 0.5 + 0.5, 0.5 - there.y * 0.5), 0).r;
    return seen < there.z - surface.shadow.z;
}

// The share of a surface's light that comes through the fog (xrender.mesh's `through`).
float through(float3 toward, float length) {
    const float falloff = surface.fog_shape.y;
    const float here = surface.fog.w * (falloff > 0 ? exp(clamp(-falloff * (surface.eye.y - surface.fog_shape.x), -60.0, 60.0)) : 1.0);
    const float thins = falloff * toward.y;
    float depth = here * length;
    if (abs(thins * length) >= 1e-5) depth = here * (1.0 - exp(clamp(-thins * length, -60.0, 60.0))) / thins;
    return depth > 60 ? 0.0 : exp(-depth);
}
// What the surface sends toward the eye of light arriving along `toward_light` (xrender.mesh's `reflected`).
float3 reflected(float3 normal, float3 toward_eye, float3 toward_light, float facing) {
    const float rough = surface.shine.x, metal = surface.shine.y;
    const float3 matt = base_colour * ((1.0 - metal) * facing);
    const float seen = dot(normal, toward_eye);
    if (!(seen > 0)) return matt;
    const float3 halfway = normalize(toward_eye + toward_light);
    const float along_half = max(dot(normal, halfway), 0.0), eye_half = max(dot(toward_eye, halfway), 0.0);
    const float spread = rough * rough * rough * rough;
    const float under = along_half * along_half * (spread - 1.0) + 1.0;
    const float facets = spread / (under * under);
    const float k = (rough + 1.0) * (rough + 1.0) * 0.125;
    const float unhidden = 1.0 / ((facing * (1.0 - k) + k) * (seen * (1.0 - k) + k));
    const float glance = 1.0 - eye_half, glance5 = glance * glance * glance * glance * glance;
    const float3 head_on = float3(0.04, 0.04, 0.04) * (1.0 - metal) + base_colour * metal;
    const float3 mirrored = head_on + (float3(1, 1, 1) - head_on) * glance5;
    return matt + mirrored * (facets * unhidden * 0.25 * facing);
}
// What the surface mirrors of what is about the scene (xrender.mesh's `mirrored`).
float3 mirrored(float3 normal, float3 toward_eye) {
    const float rough = surface.shine.x, metal = surface.shine.y;
    const float seen = max(dot(normal, toward_eye), 0.0);
    const float3 way = normal * (2.0 * seen) - toward_eye;
    const float size = length(way);
    const float3 along = size > 0 ? way / size : normal;
    const float3 far = along.y > 0 ? surface.above.rgb : surface.below.rgb;
    const float3 there = surface.level.rgb + (far - surface.level.rgb) * abs(along.y);
    const float3 blurred = there + (surface.level.rgb - there) * rough;
    const float glance = 1.0 - seen, glance5 = glance * glance * glance * glance * glance;
    const float3 head_on = float3(0.04, 0.04, 0.04) * (1.0 - metal) + base_colour * metal;
    const float3 share = head_on + (float3(1, 1, 1) - head_on) * (glance5 * (1.0 - rough));
    return share * blurred;
}
float encoded(float linear) {
    const float held = clamp(linear, 0.0, 1.0);
    return held <= 0.0031308 ? held * 12.92 : 1.055 * pow(held, 1.0 / 2.4) - 0.055;
}

[shader("fragment")]
float4 mesh_fs(VOut point) : SV_Target {
    const float3 normal = normalize(point.normal);
    const float3 to_eye = surface.eye.xyz - point.place;
    // A face turned away is not drawn (the reference leaves it out by its winding; a closed thing's far side).
    if (dot(normal, to_eye) < 0) discard;
    base_colour = surface.base.rgb;
    if (surface.picture.z > 0.5) {
        // How many texels a pixel covers: how far apart the surface's places in the picture are at this pixel and the next.
        // The strip a pixel covers of it (xrender.mesh's `Texture::over`): up to eight places along its length, each at
        // the size its width asks.
        const float2 right = ddx(point.uv), down = ddy(point.uv);
        const float across = length(right * surface.picture.xy), along = length(down * surface.picture.xy);
        const float2 lengthwise = across >= along ? right : down;
        const float longer = max(across, along), shorter = max(min(across, along), 1e-6);
        const float places = clamp(ceil(longer / shorter), 1.0, 8.0);
        const float footprint = longer / places;
        float3 sum = float3(0, 0, 0);
        for (float place = 0; place < places; place += 1) sum += picture_at(point.uv + lengthwise * ((place + 0.5) / places - 0.5), footprint);
        base_colour *= sum / places;
    }
    const bool matt = surface.shine.z > 0.5;
    const float3 toward_eye = normalize(to_eye);
    // Matt: the lights summed, then the surface's colour. Otherwise each light's share by `reflected`.
    float3 light = surface.ambient.rgb;
    float3 sent = float3(0, 0, 0);
    const int count = int(surface.eye.w);
    for (int index = 0; index < count; ++index) {
        const Lamp lamp = surface.lamps[index];
        float3 along = -lamp.toward_cone.xyz;
        float3 arriving = lamp.colour_kind.rgb;
        if (lamp.colour_kind.w < 0.5) {
            if (float(index) == surface.shadow.x && dot(normal, along) > 0 && in_shadow(point.place, normal)) continue;
        } else {
            const float3 to = lamp.at_range.xyz - point.place;
            const float apart = length(to);
            if (!(apart < lamp.at_range.w) || !(apart > 0)) continue;
            along = to / apart;
            if (lamp.colour_kind.w > 1.5 && -dot(along, lamp.toward_cone.xyz) < lamp.toward_cone.w) continue;
            const float near = 1.0 - (apart / lamp.at_range.w) * (apart / lamp.at_range.w);
            arriving *= near * near;
            if (lamp.shadow.x >= 0 && dot(normal, along) > 0 && lamp_hidden(lamp, point.place, normal, apart)) continue;
        }
        const float facing = dot(normal, along);
        if (!(facing > 0)) continue;
        if (matt) light += arriving * facing;
        else sent += reflected(normal, toward_eye, along, facing) * arriving;
    }
    float3 colour = base_colour * light + surface.emissive.rgb;
    if (!matt) {
        const float metal = surface.shine.y;
        const float3 everywhere = base_colour * (1.0 - metal) + (float3(0.04, 0.04, 0.04) * (1.0 - metal) + base_colour * metal);
        colour = everywhere * surface.ambient.rgb + surface.emissive.rgb + sent;
        if (surface.above.w > 0.5) colour += mirrored(normal, toward_eye);
    }
    if (surface.fog.w > 0) {
        const float apart = length(to_eye);
        const float share = apart > 0 ? through(-to_eye / apart, apart) : 1.0;
        colour = surface.fog.rgb + (colour - surface.fog.rgb) * share;
    }
    // The light itself where the target holds it (what `Glow` takes); else through the display's curve.
    if (surface.shine.w > 0.5) return float4(colour, 1);
    return float4(encoded(colour.r), encoded(colour.g), encoded(colour.b), 1);
}
