// scanlines: the rows at multiples of `period` (device pixels from the
// surface's top) darkened by `strength`.
import openmui_effect;

[shader("fragment")]
float4 scanlines_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const uint4 s = source_texel(p);
    const int period = max(1, int(round(param(1) * device_scale())));
    return out8(p.y % period == 0 ? scaled(s, 1 - param(0)) : s);
}
