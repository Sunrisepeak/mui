// paper: as if printed on paper — a warm tone, a fine grain, darker towards
// the edges. effects.cpp's paper, operation for operation.
// params: grain, warmth, seed, grain size, edge.
import openmui_effect;

[shader("fragment")]
float4 paper_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const uint4 s = source_texel(p);
    if (s.a == 0) return out8(s);
    const float grain = clamp01(param(0)), warmth = clamp01(param(1)), seed = param(2);
    const float size = max(1.0, float(lround_exact(param(3) * device_scale()))), edge = clamp01(param(4));
    const float hw = max(effect.layer.z * 0.5, 1.0), hh = max(effect.layer.w * 0.5, 1.0);
    precise const float cx = effect.layer.x + hw, cy = effect.layer.y + hh;
    const float n = hash01(seed, int(floor(float(p.x) / size)), int(floor(float(p.y) / size)), 3);
    const float a = float(s.a);
    precise const float k = (n - 0.5) * 2 * grain * 0.12 * a;
    const float u = (float(p.x) + 0.5 - cx) / hw, v = (float(p.y) + 0.5 - cy) / hh;
    precise const float dark = 1 - edge * 0.25 * (u * u + v * v);
    precise const float tg = 1 - warmth * 0.04, tb = 1 - warmth * 0.12;
    precise const float r = clamp((float(s.r) * 1.0 + k) * dark, 0.0, a);
    precise const float g = clamp((float(s.g) * tg + k) * dark, 0.0, a);
    precise const float b = clamp((float(s.b) * tb + k) * dark, 0.0, a);
    return out8(uint4(q8(r), q8(g), q8(b), s.a));
}
