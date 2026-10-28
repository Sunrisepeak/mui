// reveal: a circle grows from a point and shows the layer as progress goes
// from 0 to 1. derived(0…2): the centre and the distance to the farthest
// corner, device pixels.
import openmui_effect;

[shader("fragment")]
float4 reveal_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const uint4 s = source_texel(p);
    if (s.a == 0) return out8(s);
    const float progress = param(0), soft = param(3);
    precise const float dx = float(p.x) + 0.5 - derived(0), dy = float(p.y) + 0.5 - derived(1);
    precise const float d2 = dx * dx + dy * dy;
    const float d = sqrt(d2) / derived(2);
    precise const float edge = progress * (1 + soft);
    return out8(scaled(s, clamp01((edge - d) / max(soft, 1e-4))));
}
