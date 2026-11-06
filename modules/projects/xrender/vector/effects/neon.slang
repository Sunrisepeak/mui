// neon: light around the layer's shapes — a glow outside fading over its
// radius, a bright tube just inside the edge — under the content, and a band
// of light sweeping across it. effects.cpp's neon, operation for operation.
// params: colour rgb, radius, intensity, sweep, sweep width.
import openmui_effect;

[shader("fragment")]
float4 neon_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const uint4 s = source_texel(p);
    const float scale = device_scale();
    const float cr = clamp01(param(0)), cg = clamp01(param(1)), cb = clamp01(param(2));
    const float radius = max(param(3) * scale, 1.0), intensity = clamp(param(4), 0.0, 2.0);
    const float sweep = param(5), width = max(param(6) * scale, 1.0);
    precise const float bx = effect.layer.x + effect.layer.z * sweep;
    precise const float tube = 1.5 * scale;
    const float qx = float(p.x) + 0.5, qy = float(p.y) + 0.5;
    const float d = shapes_distance(float2(qx, qy), 0.0);
    float g = 0;
    if (d > 0) {
        const float t = 1 - d / radius;
        if (t > 0) {
            precise const float tt = intensity * t * t;
            g = tt;
        }
    } else {
        g = intensity * clamp01(1 + d / tube);
    }
    g = clamp01(g);
    float4 c = float4(s);
    if (sweep >= 0 && d <= 0 && s.a > 0) {
        const float f = clamp01(1 - abs(qx - bx) / width) * 0.6;
        precise const float3 lit = c.rgb + (c.a - c.rgb) * f;
        c.rgb = lit;
    }
    const float inv = 1 - c.a / 255.0;
    precise const float a = 255.0 * g * inv;
    precise const float4 o = float4(c.r + cr * a, c.g + cg * a, c.b + cb * a, c.a + a);
    return out8(q8(o));
}
