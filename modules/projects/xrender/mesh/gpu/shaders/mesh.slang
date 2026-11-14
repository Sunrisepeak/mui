// The mesh pass: xrender.mesh's reference shading, on a device. A matt surface under suns, lamps and spots, a
// constant light from everywhere, what the surface gives off, fog worked out along the ray; the display's
// curve at the end. Matrices are given a row at a time and applied by dot products: no convention of rows and
// columns to agree on.
struct VIn {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;  // the way a picture's first coordinate grows over the surface; w: the side its second is on (all 0: none)
};
struct VOut {
    float4 clip : SV_Position;
    float3 place : TEXCOORD0;   // in the world
    float3 normal : TEXCOORD1;  // in the world, not of length 1
    float2 uv : TEXCOORD2;      // the mesh's place in a picture
    float4 tangent : TEXCOORD3; // in the world, not of length 1; w as given
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
    made.tangent = float4(dot(draw.to_world[0].xyz, corner.tangent.xyz), dot(draw.to_world[1].xyz, corner.tangent.xyz), dot(draw.to_world[2].xyz, corner.tangent.xyz), corner.tangent.w);
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
    float4 picture;      // the draw's texture: xy its width and height; z how many sizes (levels) it has (0: none)
    float4 open;         // x: over 0.5 — there is a picture of how open each pixel is; zw: one over its width and height
    float4 cut;          // x: 1 or −1 — a pixel whose depth is past the half that way is beyond the view's far cut;
                         // y: where the surface hides less than this it is not there (0: it is whole); z: how much it
                         // hides, before its picture's own say; w: over 0.5 — it is seen from both sides
    // The surface's other pictures: xy a picture's width and height, z how many sizes it has (0: none).
    float4 surface_picture;  // its green multiplies the roughness, its blue the metal
    float4 normal_picture;   // which way the surface faces about its own normal; w: its red and green by this much
    float4 glow_picture;     // multiplies what the surface gives off
    float4 shade_picture;    // its red: the share of the light from everywhere that reaches the surface; w: how much of that is taken
    float4 own;              // x: the roughness as the material says it; y: the metal (what the surface picture multiplies)
    float4 through;          // x: over 0.5 — a layer of the surfaces seen through is drawn; y: a layer was laid before this one;
                             // zw: one over the frame's width and height
    Lamp   lamps[8];
};
[[vk::binding(0, 2)]] Sampler2D shadow_map : register(t0, space2) : register(s0, space2);
[[vk::binding(1, 2)]] Sampler2D lamp_map : register(t1, space2) : register(s1, space2);
[[vk::binding(2, 2)]] Sampler2D picture_map : register(t2, space2) : register(s2, space2);
[[vk::binding(3, 2)]] Sampler2D open_map : register(t3, space2) : register(s3, space2);
[[vk::binding(4, 2)]] Sampler2D surface_map : register(t4, space2) : register(s4, space2);
[[vk::binding(5, 2)]] Sampler2D normal_map : register(t5, space2) : register(s5, space2);
[[vk::binding(6, 2)]] Sampler2D glow_map : register(t6, space2) : register(s6, space2);
[[vk::binding(7, 2)]] Sampler2D shade_map : register(t7, space2) : register(s7, space2);
[[vk::binding(8, 2)]] Sampler2D behind_map : register(t8, space2) : register(s8, space2);  // how deep what stops the eye is
[[vk::binding(9, 2)]] Sampler2D before_map : register(t9, space2) : register(s9, space2);  // how deep the layer laid before is
[[vk::binding(0, 3)]] ConstantBuffer<Surface> surface : register(b0, space3);

// The surface at this pixel: the material's own, times its pictures where it has them.
static float3 base_colour;
static float3 given_off;
static float  rough_here;
static float  metal_here;

// A texture's light at `uv` for a pixel that covers `footprint` texels of its finest size (xrender.mesh's
// `Texture::at`): the size whose texels are about a pixel across mixed with the next — its sizes are the
// texture's levels, read between four texels of each and between the two, repeating, in one read.
float4 picture_at(Sampler2D map, float sizes_said, float2 uv, float footprint) {
    const int sizes = int(sizes_said);
    int size = 0;
    float covers = max(footprint, 1.0);
    while (covers >= 2 && size + 1 < sizes) {
        covers *= 0.5;
        ++size;
    }
    const float toward_next = size + 1 < sizes ? covers - 1.0 : 0.0;
    return map.SampleLevel(uv, float(size) + toward_next);
}
// A picture over the strip a pixel covers of it (xrender.mesh's `Texture::over`): up to eight places along the
// strip's length, each at the size its width asks. `right` and `down`: how far the surface's place in the
// picture moves from this pixel to the next across and to the next below. The fourth number is how much the
// picture hides there (its cover; 1 for a picture that has none), and the first three are then the light times
// it: what `Texture::seen` divides.
float4 picture_over(Sampler2D map, float4 picture, float2 uv, float2 right, float2 down) {
    const float across = length(right * picture.xy), along = length(down * picture.xy);
    const float2 lengthwise = across >= along ? right : down;
    const float longer = max(across, along), shorter = max(min(across, along), 1e-6);
    const float places = clamp(ceil(longer / shorter), 1.0, 8.0);
    const float footprint = longer / places;
    float4 sum = float4(0, 0, 0, 0);
    for (float place = 0; place < places; place += 1) sum += picture_at(map, picture.z, uv + lengthwise * ((place + 0.5) / places - 0.5), footprint);
    return sum / places;
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
    const float rough = rough_here, metal = metal_here;
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
    const float rough = rough_here, metal = metal_here;
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

// The surface's light at a pixel, and how much it hides of what is behind it; `gone`: nothing of it is drawn there
// (turned away, beyond the view, cut out).
struct Lit {
    float3 light;
    float  hides;
    bool   gone;
};
Lit lit_at(VOut point) {
    Lit lit;
    lit.light = float3(0, 0, 0), lit.hides = 0, lit.gone = true;
    // How far the surface's place in its pictures moves from this pixel to the next across and to the next below:
    // how many texels a pixel covers. And how far the surface itself goes. Both taken here, before anything turns
    // on what a pixel is: a difference between pixels is one only where every pixel takes the same way through
    // the code.
    const float2 right = ddx(point.uv), down = ddy(point.uv);
    const float3 place_right = ddx(point.place), place_down = ddy(point.place);
    float3 normal = normalize(point.normal);
    const float3 to_eye = surface.eye.xyz - point.place;
    // A face turned away is not drawn (the reference leaves it out by its winding; a closed thing's far side) —
    // unless the surface is seen from both sides: then it faces the eye wherever it is seen.
    if (dot(normal, to_eye) < 0) {
        if (surface.cut.w > 0.5) normal = -normal;
        else return lit;
    }
    // Beyond the view: the far cut, taken at the pixel (the device's own far plane lies further off).
    if ((point.clip.z - 0.5) * surface.cut.x > 0) return lit;
    base_colour = surface.base.rgb, given_off = surface.emissive.rgb;
    rough_here = surface.shine.x, metal_here = surface.shine.y;
    bool matt = surface.shine.z > 0.5;
    float shaded = 1.0;
    float hides = surface.cut.z;
    if (surface.picture.z > 0.5) {
        const float4 there = picture_over(picture_map, surface.picture, point.uv, right, down);
        base_colour *= there.a > 0 ? there.rgb / there.a : there.rgb;
        hides *= there.a;
    }
    // Cut out: where it hides less than the cut, the surface is not there.
    if (surface.cut.y > 0 && hides < surface.cut.y) return lit;
    if (surface.surface_picture.z > 0.5) {
        // The material's own numbers ride in the draw's `shine` already held to what the model takes; the
        // picture's share is taken of what the material says, then held the same way (xrender.mesh's `reflected`).
        const float3 said = picture_over(surface_map, surface.surface_picture, point.uv, right, down).rgb;
        const float rough = surface.own.x * said.g, metal = surface.own.y * said.b;
        matt = rough >= 1 && metal <= 0;
        rough_here = clamp(rough, 0.04, 1.0), metal_here = clamp(metal, 0.0, 1.0);
    }
    if (surface.glow_picture.z > 0.5) given_off *= picture_over(glow_map, surface.glow_picture, point.uv, right, down).rgb;
    if (surface.shade_picture.z > 0.5) shaded = 1.0 + surface.shade_picture.w * (picture_over(shade_map, surface.shade_picture, point.uv, right, down).r - 1.0);
    if (surface.normal_picture.z > 0.5) {
        // The picture's three ways in the world here: along it (the mesh's tangent, or the way the surface goes as
        // the picture's first coordinate grows), up it, and out (the normal).
        float3 right_way = point.tangent.xyz;
        float3 up_way = cross(normal, right_way) * point.tangent.w;
        if (!(dot(right_way, right_way) > 0)) {
            const float3 one = place_right, two = place_down;
            const float turn = right.x * down.y - down.x * right.y;
            if (turn != 0) {
                right_way = (one * down.y - two * right.y) / turn;
                up_way = (one * down.x - two * right.x) / turn;
            }
        }
        right_way -= normal * dot(normal, right_way);
        if (dot(right_way, right_way) > 0) {
            right_way = normalize(right_way);
            const float3 other = cross(normal, right_way);
            up_way = dot(other, up_way) < 0 ? -other : other;
            const float3 said = picture_over(normal_map, surface.normal_picture, point.uv, right, down).rgb;
            const float3 about = float3((said.xy * 2 - 1) * surface.normal_picture.w, said.z * 2 - 1);
            const float3 turned = right_way * about.x + up_way * about.y + normal * about.z;
            if (dot(turned, turned) > 0) normal = normalize(turned);
        }
    }
    const float3 toward_eye = normalize(to_eye);
    // Matt: the lights summed, then the surface's colour. Otherwise each light's share by `reflected`.
    // The light from everywhere, by how open the place is (xrender.mesh's `reaching`): the open share, and by the
    // ways that are shut what a surface of this colour sends back.
    float3 everywhere_in = surface.ambient.rgb;
    if (surface.open.x > 0.5) {
        const float open = open_map.SampleLevel(point.clip.xy * surface.open.zw, 0).r;
        if (open < 1) everywhere_in *= open / (1.0 - clamp(surface.base.rgb, 0.0, 0.98) * (1.0 - open));
    }
    everywhere_in *= shaded;
    float3 light = everywhere_in;
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
    float3 colour = base_colour * light + given_off;
    if (!matt) {
        const float metal = metal_here;
        const float3 everywhere = base_colour * (1.0 - metal) + (float3(0.04, 0.04, 0.04) * (1.0 - metal) + base_colour * metal);
        colour = everywhere * everywhere_in + given_off + sent;
        if (surface.above.w > 0.5) colour += mirrored(normal, toward_eye);
    }
    if (surface.fog.w > 0) {
        const float apart = length(to_eye);
        const float share = apart > 0 ? through(-to_eye / apart, apart) : 1.0;
        colour = surface.fog.rgb + (colour - surface.fog.rgb) * share;
    }
    lit.light = colour, lit.hides = hides, lit.gone = false;
    return lit;
}

// What the pass writes. A frame's pass has the one target and `deep` goes nowhere; a layer of the surfaces seen
// through (xrender.mesh's `Material::see_through`) has both: of each pixel's, the nearest one that is nearer than
// what stops the eye and farther than the layer laid before — its light times what it hides, and how much it
// hides, to be put under the layers before it; and how deep it is, for the next layer to lie behind. One shader
// for both, told which by `surface.through.x`: the surface's light is worked out by the same code, once.
struct Layer {
    float4 light : SV_Target0;
    float4 deep : SV_Target1;
};
[shader("fragment")]
Layer mesh_fs(VOut point) {
    const Lit lit = lit_at(point);
    Layer made;
    made.deep = float4(point.clip.z, 0, 0, 1);
    if (surface.through.x > 0.5) {
        const float2 at = point.clip.xy * surface.through.zw;
        const float deep = point.clip.z;
        bool gone = lit.gone || !(lit.hides > 0);
        if ((deep - behind_map.SampleLevel(at, 0).r) * surface.cut.x >= 0) gone = true;
        if (surface.through.y > 0.5 && (deep - before_map.SampleLevel(at, 0).r) * surface.cut.x <= 0) gone = true;
        if (gone) discard;
        const float hides = min(lit.hides, 1.0);
        made.light = float4(lit.light * hides, hides);
        return made;
    }
    if (lit.gone) discard;
    // The light itself where the target holds it (what `Glow` takes); else through the display's curve.
    if (surface.shine.w > 0.5) made.light = float4(lit.light, 1);
    else made.light = float4(encoded(lit.light.r), encoded(lit.light.g), encoded(lit.light.b), 1);
    return made;
}
