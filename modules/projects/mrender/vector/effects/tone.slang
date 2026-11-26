// tone: the layer in one colour — each pixel's brightness kept, its hue the
// tint's, mixed by amount. effects.cpp's tone, operation for operation.
// params: the tint (r, g, b), amount.
import openmui_effect;

[shader("fragment")]
float4 tone_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const uint4 s = source_texel(p);
    if (s.a == 0) return out8(s);
    const float tr = clamp01(param(0)), tg = clamp01(param(1)), tb = clamp01(param(2)), amount = clamp01(param(3));
    const float top = max(max(tr, tg), max(tb, 0.001));
    const float kr = tr / top, kg = tg / top, kb = tb / top;
    const float a = float(s.a);
    precise const float y = 0.2126 * float(s.r) + 0.7152 * float(s.g) + 0.0722 * float(s.b);
    precise const float r = float(s.r) + (min(y * kr, a) - float(s.r)) * amount;
    precise const float g = float(s.g) + (min(y * kg, a) - float(s.g)) * amount;
    precise const float b = float(s.b) + (min(y * kb, a) - float(s.b)) * amount;
    return out8(uint4(q8(r), q8(g), q8(b), s.a));
}
