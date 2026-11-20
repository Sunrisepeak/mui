// An effect that also reads a buffer of its own: more than the template offers.
import openmui_effect;

[[vk::binding(2, 2)]] StructuredBuffer<float4> palette : register(t2, space2);

[shader("fragment")]
float4 greedy_fs(EffectPixel px) : SV_Target {
    const float4 aux = effect_aux.SampleLevel(float2(0.5, 0.5), 0);
    return out8(source_texel(at(px))) * palette[0] + aux;
}
