// liquid_glass (D-64): a thick transparent material over what lies under the
// layer — the backdrop (the aux texture, frosted by the renderer) bent at
// the shape's bevel, split a little by colour, tinted (darker over a bright
// backdrop, lighter over a dark one), lit at the rim facing the light and
// along a fine inner edge; the layer's own content over it. effects.cpp's
// liquid_glass, operation for operation.
// params: thickness, ior, blur, dispersion, tint rgba, light xy, smoothness,
// adaptive, rim.
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
    // Refraction: the backdrop pulled in from beyond the edge.
    precise const float bend = (1 - t) * (1 - t);
    precise const float off = (ior - 1) * thickness * bend;
    const float spread = dispersion * 0.5;
    precise const float ro = off * (1 + spread), bo = off * (1 - spread);
    precise const float rx = qx - nx * ro, ry = qy - ny * ro, gxq = qx - nx * off, gyq = qy - ny * off, bx = qx - nx * bo, by = qy - ny * bo;
    const float4 red = backdrop_bilinear(float2(rx, ry)), green = backdrop_bilinear(float2(gxq, gyq)), blue = backdrop_bilinear(float2(bx, by));
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
    // Light: the rim facing it, and a fine line inside the edge.
    const float facing = max(0.0, -(nx * lx + ny * ly));
    precise const float rim = rim_strength * (1 - t) * (1 - t) * (1 - t) * facing;
    const float line = rim_strength * 0.5 * clamp01(1 - e / (1.5 * scale));
    const float light = min(rim + line, 1.0);
    precise const float3 lit = c.rgb + (c.a - c.rgb) * light;
    c.rgb = min(lit, c.a);
    // The glass where it covers; the layer's content over it.
    const float inv = 1 - float(s.a) / 255.0;
    precise const float4 o = float4(s) + c * cov * inv;
    return out8(q8(o));
}
