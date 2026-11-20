// chromatic: red and blue drift apart from green. derived(0, 1): the offset,
// device pixels.
import openmui_effect;

[shader("fragment")]
float4 chromatic_fs(EffectPixel px) : SV_Target {
    const float2 q = float2(at(px)) + 0.5;
    const float2 o = float2(derived(0), derived(1));
    const float4 r = source_bilinear(q - o), g = source_bilinear(q), b = source_bilinear(q + o);
    const float alpha = max(max(r.a, g.a), b.a);
    return out8(uint4(q8(min(r.r, alpha)), q8(min(g.g, alpha)), q8(min(b.b, alpha)), q8(alpha)));
}
