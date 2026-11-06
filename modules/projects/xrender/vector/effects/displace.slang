// displace: rows shift sideways along a sine wave. The aux texture holds each
// row's shift (r32_float, one texel a row, computed on the CPU as the
// reference computes it).
import openmui_effect;

[shader("fragment")]
float4 displace_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const float dx = effect_aux.SampleLevel(float2((float(p.y) + 0.5) / float(surface_size().y), 0.5), 0).r;
    return out8(q8(source_bilinear(float2(float(p.x) + 0.5 - dx, float(p.y) + 0.5))));
}
