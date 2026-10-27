// noise: seeded grain over the colours.
import openmui_effect;

[shader("fragment")]
float4 noise_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const uint4 s = source_texel(p);
    if (s.a == 0) return out8(s);
    precise const float k = (hash01(param(1), p.x, p.y, 0) - 0.5) * 2 * param(0) * float(s.a);
    const float top = float(s.a);
    return out8(uint4(q8(clamp(float(s.r) + k, 0.0, top)), q8(clamp(float(s.g) + k, 0.0, top)), q8(clamp(float(s.b) + k, 0.0, top)), s.a));
}
