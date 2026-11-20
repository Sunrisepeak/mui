// liquid_glass (D-64): a thick transparent material over what lies under the
// layer — a height field over the bevel whose 3D normal refracts the view
// ray (Snell) onto the backdrop (the aux texture, frosted by the renderer),
// split a little by colour, tinted (darker over a bright backdrop, lighter
// over a dark one), shaded inside the edge turned away from the light, lit by
// a specular highlight, a Fresnel rim and a fine inner line; the layer's own
// content over it. effects.cpp's liquid_glass, operation for operation.
// params: thickness, ior, blur, dispersion, tint rgba, light xy, smoothness,
// adaptive, rim, height, specular, shade.
import openmui_effect;

[shader("fragment")]
float4 liquid_glass_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const uint4 s = source_texel(p);
    const float scale = device_scale();
    const float thickness = max(param(0) * scale, 1.0), ior = param(1), dispersion = param(3);
    const float4 tint = float4(clamp01(param(4)), clamp01(param(5)), clamp01(param(6)), clamp01(param(7)));
    precise const float ll2 = param(8) * param(8) + param(9) * param(9);
    const float ll = sqrt(ll2);
    const float lx = ll > 0 ? param(8) / ll : 0.0, ly = ll > 0 ? param(9) / ll : 0.0;
    const float k = param(10) * scale;
    const bool adaptive = param(11) >= 0.5;
    const float rim_strength = clamp01(param(12));
    const float height = clamp(param(13), 0.0, 4.0), specular = clamp01(param(14)), shade = clamp01(param(15));

    const float qx = float(p.x) + 0.5, qy = float(p.y) + 0.5;
    const float d = shapes_distance(float2(qx, qy), k);
    const float cov = clamp01(0.5 - d);
    if (cov <= 0) return out8(s);
    // The bevel: 0 at the edge, 1 where the glass is flat.
    const float e = max(-d, 0.0);
    const float t = clamp01(e / thickness);
    // The surface's normal, outward: the distance's gradient.
    precise const float gx = (shapes_distance(float2(qx + 1, qy), k) - shapes_distance(float2(qx - 1, qy), k)) * 0.5;
    precise const float gy = (shapes_distance(float2(qx, qy + 1), k) - shapes_distance(float2(qx, qy - 1), k)) * 0.5;
    precise const float g2 = gx * gx + gy * gy;
    const float gl = sqrt(g2);
    const float nx = gl > 0 ? gx / gl : 0.0, ny = gl > 0 ? gy / gl : 0.0;
    // The surface: its slope over the bevel, and its normal in 3D.
    const float w = 1 - t;
    precise const float slope = 3 * w * w * height;
    precise const float sl = sqrt(slope * slope + 1);
    precise const float sx = nx * slope / sl, sy = ny * slope / sl, sz = 1 / sl;
    // Refraction (Snell): the view ray, straight down, bent into the
    // glass; the backdrop where it lands, the glass's depth below.
    precise const float eta = 1 / ior;
    precise const float under_root = 1 - eta * eta * (1 - sz * sz);
    precise const float f = eta * sz - sqrt(max(under_root, 0.0));
    precise const float tz = f * sz - eta;
    precise const float reach = f * thickness * height / max(-tz, 0.05);
    precise const float dx = sx * reach, dy = sy * reach;
    const float spread = dispersion * 0.5;
    precise const float rs = 1 + spread, bs = 1 - spread;
    const float4 red = backdrop_bilinear(float2(qx + dx * rs, qy + dy * rs)), green = backdrop_bilinear(float2(qx + dx, qy + dy)),
                 blue = backdrop_bilinear(float2(qx + dx * bs, qy + dy * bs));
    float4 c = float4(min(red.r, green.a), green.g, min(blue.b, green.a), green.a);
    // Tint, over (premultiplied).
    const float ta = tint.a;
    precise const float3 tinted = tint.rgb * ta * 255.0 + c.rgb * (1 - ta);
    precise const float ta2 = ta * 255.0 + c.a * (1 - ta);
    c = float4(tinted, ta2);
    // Adaptive: what is on the glass stays legible.
    if (adaptive && c.a > 0) {
        precise const float lum = (0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b) / c.a;
        const float darken = 0.08 * clamp01((lum - 0.55) / 0.35), lighten = 0.08 * clamp01((0.45 - lum) / 0.35);
        precise const float3 adapted = c.rgb * (1 - darken) + c.a * lighten;
        c.rgb = adapted;
    }
    // Shade: inside the edge turned away from the light.
    precise const float away = max(0.0, nx * lx + ny * ly);
    precise const float dim = shade * w * w * away;
    precise const float3 shaded = c.rgb * (1 - dim);
    c.rgb = shaded;
    // Light: the specular highlight, the Fresnel rim facing it, and a fine
    // line inside the edge.
    precise const float hx = -lx * 0.70710678, hy = -ly * 0.70710678, hz = 1.70710678;
    precise const float hl = sqrt(hx * hx + hy * hy + hz * hz);
    precise const float nh = max(0.0, (sx * hx + sy * hy + sz * hz) / hl);
    precise float spec = nh * nh;
    spec = spec * spec;
    spec = spec * spec;
    spec = spec * spec;
    spec = spec * spec;
    const float edge = clamp01((1 - sz) * 1.5);
    const float facing = max(0.0, -(nx * lx + ny * ly));
    precise const float rim = rim_strength * edge * edge * (0.35 + 0.65 * facing);
    const float line = rim_strength * 0.5 * clamp01(1 - e / (1.5 * scale));
    precise const float light = min(rim + line + specular * spec, 1.0);
    precise const float3 lit = c.rgb + (c.a - c.rgb) * light;
    c.rgb = min(lit, c.a);
    // The glass where it covers; the layer's content over it.
    const float inv = 1 - float(s.a) / 255.0;
    precise const float4 o = float4(s) + c * cov * inv;
    return out8(q8(o));
}
