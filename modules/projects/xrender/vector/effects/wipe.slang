// wipe: the layer is wiped away from one edge as progress goes from 0 to 1.
// derived(0…3): the direction's cosine and sine, and the layer's extent along
// it (low end, span).
import openmui_effect;

[shader("fragment")]
float4 wipe_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const uint4 s = source_texel(p);
    if (s.a == 0) return out8(s);
    precise const float along = (float(p.x) + 0.5) * derived(0) + (float(p.y) + 0.5) * derived(1);
    const float t = (along - derived(2)) / derived(3);
    return out8(scaled(s, ramp(t, param(0), param(2))));
}
