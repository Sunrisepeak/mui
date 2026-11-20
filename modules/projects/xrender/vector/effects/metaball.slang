// metaball: the layer's shapes merge like drops; the bridges between them
// are filled with a colour, the content over them. effects.cpp's metaball.
// params: smoothness, colour rgba.
import openmui_effect;

[shader("fragment")]
float4 metaball_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const uint4 s = source_texel(p);
    precise const float k = param(0) * device_scale();
    const float cr = clamp01(param(1)), cg = clamp01(param(2)), cb = clamp01(param(3)), ca = clamp01(param(4));
    const float d = shapes_distance(float2(float(p.x) + 0.5, float(p.y) + 0.5), k);
    const float cov = clamp01(0.5 - d);
    if (cov <= 0) return out8(s);
    const float inv = 1 - float(s.a) / 255.0;
    precise const float a = 255.0 * ca * cov * inv;
    precise const float4 o = float4(float(s.r) + cr * a, float(s.g) + cg * a, float(s.b) + cb * a, float(s.a) + a);
    return out8(q8(o));
}
