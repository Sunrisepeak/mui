// blur: one box pass of radius derived(0) along derived(1, 2) (1, 0 or 0, 1)
// in integers; the renderer runs three each way, as the reference does.
import openmui_effect;

[shader("fragment")]
float4 blur_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const int r = int(derived(0));
    const int2 step = int2(int(derived(1)), int(derived(2)));
    const int n = 2 * r + 1;
    uint4 sum = 0;
    for (int i = -r; i <= r; ++i) sum += source_texel(p + step * i);  // outside: 0
    return out8((sum + uint(n / 2)) / uint(n));
}
