// dissolve: pixels (or cells of `grain` pixels) vanish in a seeded random
// order as progress goes from 0 to 1. derived(0): the cell in device pixels.
import openmui_effect;

[shader("fragment")]
float4 dissolve_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const uint4 s = source_texel(p);
    if (s.a == 0) return out8(s);
    const int cell = int(derived(0));
    const float n = hash01(param(1), p.x / cell, p.y / cell, 0);
    return out8(scaled(s, ramp(n, param(0), param(2))));
}
